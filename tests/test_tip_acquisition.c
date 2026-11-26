#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "tip_acquisition.h"

typedef struct {
    bool power, calibrated, busy, corrupt, nack;
    unsigned status_reads, frame_reads, triggers, init_writes, power_ons;
} sensor;

static void power(void *ctx, bool on)
{
    sensor *f = ctx;
    f->power = on;
    f->power_ons += on;
}

static bool write_sensor(void *ctx, uint8_t addr, const uint8_t *p, size_t n)
{
    sensor *f = ctx;
    assert(f->power && addr == 0x38 && n == 3 && p[2] == 0);
    if (f->nack) return false;
    if (p[0] == 0xbe) {
        assert(p[1] == 0x08);
        f->init_writes++;
        f->calibrated = true;
    } else {
        assert(p[0] == 0xac && p[1] == 0x33 && f->calibrated);
        f->triggers++;
    }
    return true;
}

static bool read_sensor(void *ctx, uint8_t addr, uint8_t *p, size_t n)
{
    /* Known AHT20 frame: 50.00 %RH, 25.00 C. */
    static const uint8_t frame[] = {8, 0x80, 0, 6, 0, 0, 0xa6};
    sensor *f = ctx;
    assert(f->power && addr == 0x38);
    if (f->nack) return false;
    if (n == 1) {
        f->status_reads++;
        *p = f->calibrated ? 8 : 0;
    } else {
        assert(n == sizeof(frame));
        memcpy(p, frame, n);
        f->frame_reads++;
        if (f->busy) p[0] |= 0x80;
        if (f->corrupt) p[6] ^= 1;
    }
    return true;
}

static const tip_id_band bands[] = {{791, 951, LIMB_TEMP_HUM}, {1200, 1320, 4}};
static const tip_id_profile profile = {bands, 2, 3950, 4095};

static void tick(tip_acquisition *s, uint32_t ms, uint16_t adc)
{
    tip_acquisition_tick(s, ms, true, false, adc);
}

static void init(tip_acquisition *s, sensor *f)
{
    tip_io io = {f, power, read_sensor, write_sensor};
    assert(tip_acquisition_init(s, profile, io));
}

static void test_id_and_power(void)
{
    sensor f = {0};
    tip_acquisition s;
    init(&s, &f);
    tick(&s, 0, 791);
    tick(&s, 10, 951);
    tick(&s, 11, 3950); /* Intermittent open resets the vote. */
    tick(&s, 12, 791);
    tick(&s, 12, 791);
    tick(&s, 22, 951);
    tick(&s, 100, 791); /* Excessive gap starts a new vote. */
    tick(&s, 110, 951);
    assert(!s.node.type && !f.power);
    tick(&s, 120, 791);
    assert(s.node.type == LIMB_TEMP_HUM && s.node.probing && f.power);
    uint8_t bind[LIMB_BIND_FRAME_BYTES];
    assert(limb_bind_frame(LIMB_BIND, s.node.generation, 17, bind));
    assert(!temperature_node_write(&s.node, bind, sizeof(bind)));
    tick(&s, 121, 952);
    assert(!s.node.type && !f.power);
    tick(&s, 131, 952);
    tick(&s, 141, 952);
    assert(s.node.type == 255 && !f.power && f.status_reads == 0);
    tick(&s, 151, 1200);
    tick(&s, 161, 1320);
    tick(&s, 171, 1320);
    assert(s.node.type == 4 && !f.power && f.power_ons == 1);
    tick(&s, 181, 3950);
    assert(!s.node.type);
    static const tip_id_band overlap[] = {{1, 20, 2}, {20, 30, 3}};
    tip_id_profile invalid = {overlap, 2, 3950, 4095};
    tip_io io = {&f, power, read_sensor, write_sensor};
    assert(!tip_acquisition_init(&s, invalid, io));
    tick(&s, 200, 12);
    assert(!s.node.type && !f.power);
}

static void test_conversion_and_replacement(void)
{
    sensor f = {0};
    tip_acquisition s;
    init(&s, &f);
    uint32_t origin = UINT32_MAX - 150;
    for (uint32_t ms = 0; ms <= 210; ms += 10) tick(&s, origin + ms, 871);
    assert(!s.node.probing && f.init_writes == 1 && f.triggers == 1);
    assert(!f.frame_reads && !s.node.sample_flags);
    tick(&s, origin + 220, 871);
    assert(s.node.sample_flags == 1 && s.node.age_ms == 80);
    assert(s.node.temperature == 2500 && s.node.humidity == 5000);
    uint16_t generation = s.node.generation;
    uint8_t bind[LIMB_BIND_FRAME_BYTES];
    assert(limb_bind_frame(LIMB_BIND, generation, 17, bind));
    assert(temperature_node_write(&s.node, bind, sizeof(bind)));
    tick(&s, origin + 1220, 871);
    assert(s.phase == AHT_CONVERTING && s.node.session == 17);
    /* A removal edge caught between polls must invalidate a same-type replacement. */
    tip_acquisition_tick(&s, origin + 1221, true, true, 871);
    assert(!f.power && !s.node.type && !s.node.session && !s.node.sample_flags);
    tick(&s, origin + 1231, 871);
    tick(&s, origin + 1241, 871);
    assert(s.node.generation != generation && s.node.probing);
    assert(!temperature_node_write(&s.node, bind, sizeof(bind)));
    for (uint32_t ms = 1251; ms <= 1431; ms += 10) tick(&s, origin + ms, 871);
    assert(s.node.sample_flags == 1 && s.node.sequence == 1 && f.init_writes == 1);
    tip_acquisition_tick(&s, origin + 1441, false, false, 871);
    assert(!f.power && !s.node.type);
}

static void test_timeout_and_retry(void)
{
    sensor f = {.calibrated = true};
    tip_acquisition s;
    init(&s, &f);
    for (uint32_t ms = 0; ms <= 210; ms += 10) tick(&s, ms, 871);
    assert(s.node.sample_flags == 1);
    f.busy = true;
    for (uint32_t ms = 1210; ms <= 1460; ms += 10) tick(&s, ms, 871);
    assert(!s.node.sample_flags && s.phase == AHT_RETRY);
    unsigned reads = f.frame_reads;
    for (uint32_t ms = 1470; ms <= 2450; ms += 10) tick(&s, ms, 871);
    assert(f.frame_reads == reads);
    f.busy = false;
    f.nack = true;
    tick(&s, 2460, 871);
    tick(&s, 2470, 871);
    assert(s.phase == AHT_RETRY && !s.node.sample_flags);
    f.nack = false;
    f.corrupt = true;
    for (uint32_t ms = 3470; ms <= 3570; ms += 10) tick(&s, ms, 871);
    assert(s.phase == AHT_RETRY && !s.node.sample_flags && s.node.sequence == 1);
    f.corrupt = false;
    for (uint32_t ms = 4570; ms <= 4670; ms += 10) tick(&s, ms, 871);
    assert(s.node.sample_flags == 1 && s.node.sequence == 2 && f.power_ons == 1);
}

int main(void)
{
    test_id_and_power();
    test_conversion_and_replacement();
    test_timeout_and_retry();
    return 0;
}
