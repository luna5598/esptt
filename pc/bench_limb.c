#include "slot_observation.h"
#include "tip_acquisition.h"
#include <string.h>

enum sensor_fault { NORMAL, STUCK_BUSY, NO_ACK, BAD_CRC };

typedef struct {
    tip_acquisition tip;
    limb_reader reader;
    enum limb_result status;
    enum sensor_fault fault;
    bool seated, detached, powered, calibrated, triggered;
    uint32_t clock, trigger_ms;
    int temperature, humidity;
    uint8_t frame[7];
} bench_slot;

static bench_slot slots[MO_SLOT_COUNT];
static uint64_t bench_boot;
static uint32_t session_counter;
static const tip_id_band temp_band = {791, 951, LIMB_TEMP_HUM}; /* PC fixture only. */

static void power(void *ctx, bool on)
{
    bench_slot *s = ctx;
    s->powered = on;
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
    if (addr != 0x38 || !s->powered || s->fault == NO_ACK || n != 3) return false;
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
    if (s->fault == BAD_CRC) p[6] ^= 1;
    return true;
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

bool bench_init(uint64_t boot)
{
    memset(slots, 0, sizeof(slots));
    bench_boot = boot;
    session_counter = 0;
    mo_init(boot);
    const tip_id_profile profile = {&temp_band, 1, 3950, 4095};
    for (unsigned i = 0; i < MO_SLOT_COUNT; i++) {
        bench_slot *s = &slots[i];
        tip_io io = {s, power, sensor_read, sensor_write};
        if (!tip_acquisition_init(&s->tip, profile, io)) return false;
        s->seated = i < 2;
    }
    return true;
}

void bench_contact(unsigned slot, bool present)
{
    bench_slot *s = &slots[slot];
    if (s->seated && !present) s->detached = true;
    s->seated = present;
}

void bench_fault(unsigned slot, unsigned fault)
{
    slots[slot].fault = (enum sensor_fault)fault;
}

void bench_tick(unsigned slot, uint64_t now, int temp, int humidity)
{
    bench_slot *s = &slots[slot];
    s->clock = (uint32_t)now;
    s->temperature = temp;
    s->humidity = humidity;
    tip_acquisition_tick(&s->tip, s->clock, s->seated, s->detached, s->seated ? 871 : 4095);
    s->detached = false;
    limb_transport transport = {s, node_read, node_write};
    session_counter++;
    if (!session_counter) session_counter = 1;
    limb_poll_result poll = limb_poll(&s->reader, &transport, session_counter);
    s->status = poll.result;
    mo_observe_slot(bench_boot, slot, now, &poll);
}

const char *bench_status(unsigned slot)
{
    return mo_link_name(slots[slot].status);
}
