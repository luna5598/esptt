#include "slot_observation.h"
#include "limb_scan.h"
#include "r01/tip_profile.h"
#include "vl53l1x_fixture.h"
#include <string.h>

enum sensor_fault { NORMAL, STUCK_BUSY, NO_ACK, BAD_FRAME };

typedef struct {
    tip_acquisition tip;
    limb_reader reader;
    enum sensor_fault fault;
    bool seated, detached, powered, calibrated, triggered;
    uint32_t clock, trigger_ms;
    vl53_fixture range;
    uint16_t tip_adc, distance_mm;
    int temperature, humidity;
    uint8_t frame[7];
    unsigned lines_low, hold_after_poll;
} bench_slot;

static bench_slot slots[MO_SLOT_COUNT];
static enum limb_result status[MO_SLOT_COUNT];
static uint64_t bench_boot;
static uint32_t session_counter;
static limb_scan scan;
static int selected;
static unsigned upstream_lines_low;
static bool bus_fault;

static bool bus_blocked(void)
{
    return upstream_lines_low || (selected >= 0 && slots[selected].lines_low);
}

static bool select_slot(void *ctx, unsigned slot)
{
    (void)ctx;
    if (bus_blocked()) return false;
    selected = (int)slot;
    return true;
}

static bool recover_mux(void *ctx)
{
    (void)ctx;
    selected = -1;
    return !bus_blocked();
}

static void power(void *ctx, bool on)
{
    bench_slot *s = ctx;
    s->powered = on;
    vl53_fixture_reset(&s->range);
    s->calibrated = s->triggered = false;
}

static uint32_t raw20(int reading, int span)
{
    if (reading <= 0) return 0;
    if (reading >= span) return 0xfffff;
    return (uint32_t)(((uint64_t)reading << 20) / (unsigned)span);
}

static void capture(bench_slot *s)
{
    uint32_t h = raw20(s->humidity, 10000);
    uint32_t t = raw20(s->temperature + 5000, 20000);
    uint8_t *f = s->frame;
    f[0] = 8;
    f[1] = (uint8_t)(h >> 12);
    f[2] = (uint8_t)(h >> 4);
    f[3] = (uint8_t)((h << 4) | (t >> 16));
    f[4] = (uint8_t)(t >> 8);
    f[5] = (uint8_t)t;
    uint8_t crc = 0xff;
    for (unsigned i = 0; i < 6; i++) {
        crc ^= f[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1);
    }
    f[6] = crc;
}

static bool sensor_write(void *ctx, uint8_t addr, const uint8_t *p, size_t n)
{
    bench_slot *s = ctx;
    if (!s->powered || s->fault == NO_ACK) return false;
    if (addr == 0x29 && s->tip.node.type == LIMB_DISTANCE)
        return vl53_fixture_write(&s->range, s->clock, p, n);
    if (addr != 0x38 || s->tip.node.type != LIMB_TEMP_HUM || n != 3) return false;
    if (p[0] == 0xbe && p[1] == 8 && p[2] == 0) {
        s->calibrated = true;
        return true;
    }
    if (p[0] != 0xac || p[1] != 0x33 || p[2] || !s->calibrated) return false;
    s->trigger_ms = s->clock;
    s->triggered = true;
    capture(s);
    return true;
}

static bool sensor_read(void *ctx, uint8_t addr, uint8_t *p, size_t n)
{
    bench_slot *s = ctx;
    if (addr != 0x38 || !s->powered || s->fault == NO_ACK) return false;
    bool busy = s->fault == STUCK_BUSY || (s->triggered && s->clock - s->trigger_ms < 80);
    if (n == 1) {
        p[0] = (s->calibrated ? 8 : 0) | (busy ? 0x80 : 0);
        return true;
    }
    if (n != 7 || !s->triggered) return false;
    memcpy(p, s->frame, n);
    if (busy) p[0] |= 0x80;
    else s->triggered = false;
    if (s->fault == BAD_FRAME) p[6] ^= 1;
    return true;
}

static bool sensor_read_reg16(void *ctx, uint8_t addr, uint16_t reg, uint8_t *p, size_t n)
{
    bench_slot *s = ctx;
    if (addr != 0x29 || s->tip.node.type != LIMB_DISTANCE || !s->powered || s->fault == NO_ACK)
        return false;
    s->range.status = s->fault == BAD_FRAME ? 4 : 9;
    return vl53_fixture_read(&s->range, s->clock, reg, p, n, s->distance_mm, s->fault == STUCK_BUSY);
}

static bool node_read(void *ctx, uint8_t reg, uint8_t *p, size_t n)
{
    bench_slot *s = ctx;
    return temperature_node_read(&s->tip.node, reg, p, n);
}

static bool node_write(void *ctx, const uint8_t *p, size_t n)
{
    bench_slot *s = ctx;
    return temperature_node_write(&s->tip.node, p, n);
}

static bool poll_node(void *ctx, unsigned slot, limb_poll_result *out)
{
    (void)ctx;
    if (selected != (int)slot || bus_blocked()) return false;
    bench_slot *s = &slots[slot];
    limb_transport transport = {s, node_read, node_write};
    if (++session_counter == 0) ++session_counter;
    *out = limb_poll(&s->reader, &transport, session_counter);
    /* Hold the chosen line after the node has finished replying, so the
     * following switch sees the previous branch on the upstream wires. */
    if (s->hold_after_poll) {
        s->lines_low = s->hold_after_poll;
        s->hold_after_poll = 0;
    }
    return true;
}

bool bench_init(uint64_t boot)
{
    memset(slots, 0, sizeof(slots));
    memset(status, 0, sizeof(status));
    bench_boot = boot;
    session_counter = 0;
    selected = -1;
    upstream_lines_low = 0;
    bus_fault = false;
    scan = (limb_scan){0};
    mo_init(boot);
    for (unsigned i = 0; i < MO_SLOT_COUNT; i++) {
        bench_slot *s = &slots[i];
        tip_io io = {s, power, sensor_read, sensor_write, sensor_read_reg16};
        if (!tip_acquisition_init(&s->tip, r01_tip_profile, io)) return false;
        s->seated = i < 3;
        s->tip_adc = i == 2 ? 1230 : 871;
        s->distance_mm = 300;
    }
    return true;
}

void bench_contact(unsigned slot, bool present)
{
    bench_slot *s = &slots[slot];
    if (s->seated && !present) s->detached = true;
    s->seated = present;
}

void bench_attach(unsigned slot, bool distance)
{
    bench_slot *s = &slots[slot];
    uint16_t adc = distance ? 1230 : 871;
    if (s->seated && adc != s->tip_adc) s->detached = true;
    s->tip_adc = adc;
    s->seated = true;
}

void bench_distance(unsigned slot, unsigned mm)
{
    slots[slot].distance_mm = (uint16_t)mm;
}

void bench_fault(unsigned slot, unsigned fault)
{
    slots[slot].fault = (enum sensor_fault)fault;
}

void bench_branch_lines(unsigned slot, unsigned mask)
{
    bench_slot *s = &slots[slot];
    if (!mask || s->lines_low) s->lines_low = mask;
    s->hold_after_poll = s->lines_low ? 0 : mask;
}

void bench_upstream_lines(unsigned mask)
{
    upstream_lines_low = mask;
}

bool bench_bus_fault(void)
{
    return bus_fault;
}

void bench_tick(unsigned slot, uint64_t now, int temp, int humidity)
{
    bench_slot *s = &slots[slot];
    s->clock = (uint32_t)now;
    s->temperature = temp;
    s->humidity = humidity;
    tip_acquisition_tick(&s->tip, s->clock, s->seated, s->detached, s->seated ? s->tip_adc : 4095);
    s->detached = false;
    const limb_scan_io io = {.select=select_slot, .recover=recover_mux, .poll=poll_node};
    limb_poll_result poll;
    enum scan_event event = limb_scan_slot(&scan, &io, slot, now, &poll);
    if (event == SCAN_BUS_DOWN) {
        bus_fault = true;
        mo_observe_bus_fault(now, status);
    } else if (event == SCAN_OBSERVED) {
        bus_fault = false;
        status[slot] = poll.result;
        mo_observe_slot(bench_boot, slot, now, &poll);
    }
}

const char *bench_status(unsigned slot)
{
    return mo_link_name(status[slot]);
}
