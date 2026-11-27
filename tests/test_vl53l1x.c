#undef NDEBUG
#include <assert.h>
#include "r01/tip_profile.h"
#include "vl53l1x_fixture.h"

typedef struct {
    tip_acquisition slot;
    vl53_fixture chip;
    uint32_t now;
    unsigned writes, result_reads;
    uint16_t fail_reg;
    bool powered, booting, stuck, freeze_stream;
} bench;

static void power(void *ctx, bool enabled)
{
    bench *b = ctx;
    b->powered = enabled;
    vl53_fixture_reset(&b->chip);
}

static bool raw_read(void *ctx, uint8_t addr, uint8_t *p, size_t n)
{
    (void)ctx; (void)addr; (void)p; (void)n;
    assert(0); /* A ToF read must send its 16-bit register with repeated START. */
    return false;
}

static bool read_reg(void *ctx, uint8_t addr, uint16_t reg, uint8_t *p, size_t n)
{
    bench *b = ctx;
    assert(b->powered && addr == 0x29);
    if (reg == b->fail_reg) return false;
    if (!vl53_fixture_read(&b->chip, b->now, reg, p, n, 0x0123, b->stuck)) return false;
    if (reg == 0x00e5 && b->booting) p[0] = 0;
    if (reg == 0x0089) {
        b->result_reads++;
        if (b->freeze_stream) p[2] = 0xff;
    }
    return true;
}

static bool write_reg(void *ctx, uint8_t addr, const uint8_t *p, size_t n)
{
    bench *b = ctx;
    assert(b->powered && addr == 0x29 && n >= 3 && n <= 10);
    uint16_t reg = (uint16_t)((unsigned)p[0] << 8 | p[1]);
    b->writes++;
    return reg != b->fail_reg && vl53_fixture_write(&b->chip, b->now, p, n);
}

static void tick(bench *b, unsigned elapsed)
{
    b->now += elapsed;
    tip_acquisition_tick(&b->slot, b->now, true, false, 1230);
}

static void run(bench *b, unsigned ms)
{
    for (unsigned i = 0; i < ms; ++i) tick(b, 1);
}

static void init(bench *b)
{
    *b = (bench){.now = UINT32_MAX - 120};
    tip_io io = {b, power, raw_read, write_reg, read_reg};
    assert(tip_acquisition_init(&b->slot, r01_tip_profile, io));
    run(b, 21);
    assert(b->powered && b->slot.node.probing && b->slot.node.type == LIMB_DISTANCE);
}

static void test_frames(void)
{
    bench b;
    init(&b);
    b.chip.stream = 254;
    run(&b, 200);
    assert(b.result_reads == 0 && b.slot.node.sequence == 0); /* Discard initial calibration. */
    run(&b, 30);
    temperature_node *node = &b.slot.node;
    assert(!node->probing && node->sequence == 1 && node->distance_mm == 291);
    assert(b.chip.registers[0x2e] == 1);
    assert(b.chip.period == 80 && b.chip.registers[0x5f] == 0xad && b.chip.registers[0x62] == 0xc6);
    assert(b.chip.registers[0x08] == 9 && b.chip.registers[0x0b] == 0);
    assert(node->age_ms >= 50 && node->age_ms < 200 && b.slot.sensor.range.stream == 0);
    b.chip.status = 4;
    run(&b, 80);
    assert(node->sample_flags == 2 && !node->distance_mm && node->sequence == 2);
    b.chip.status = 9;
    run(&b, 80);
    assert(node->sequence == 3 && node->sample_flags == 1);
    b.fail_reg = 0x0086; /* A failed interrupt acknowledgement must not publish the sample. */
    run(&b, 80);
    assert(node->sequence == 3 && !node->sample_flags);
    b.fail_reg = 0;
    run(&b, 800);
    assert(node->sequence > 3 && node->sample_flags == 1);
}

static void test_recovery_and_detach(void)
{
    bench b;
    init(&b);
    b.chip.model_id = 0xeaaa;
    run(&b, 100);
    assert(!b.writes && b.slot.node.probing);
    b.chip.model_id = 0xeacc;
    b.fail_reg = 0x0035; /* Partial config: restart from the beginning after backoff. */
    run(&b, 500);
    assert(b.slot.sensor.range.phase == VL_RETRY && !b.result_reads);
    b.fail_reg = 0;
    b.chip.pll = 0;
    run(&b, 650);
    assert(b.slot.sensor.range.phase == VL_RETRY && !b.result_reads);
    b.chip.pll = 512;
    run(&b, 800);
    assert(b.slot.node.sample_flags == 1);
    uint16_t gen = b.slot.node.generation;
    b.stuck = true;
    run(&b, 201);
    assert(!b.slot.node.sample_flags && b.slot.sensor.range.phase == VL_RETRY);
    b.stuck = false;
    run(&b, 800);
    assert(b.slot.node.generation == gen && b.slot.node.sample_flags == 1);
    unsigned reads = b.result_reads;
    tip_acquisition_tick(&b.slot, ++b.now, true, true, 1230);
    assert(!b.powered && !b.slot.node.type && !b.slot.node.sample_flags);
    run(&b, 20);
    assert(b.powered && b.slot.node.generation != gen && b.slot.node.probing);
    assert(!limb_node_receive_range(&b.slot.node, gen, 123, true, 0));
    assert(b.result_reads == reads);
}

static void test_stale_or_stalled(void)
{
    bench b;
    init(&b);
    b.booting = true;
    run(&b, 350);
    assert(b.slot.sensor.range.phase == VL_RETRY && !b.writes);
    b.booting = false;
    b.stuck = true;
    run(&b, 900);
    assert(b.slot.sensor.range.phase == VL_RETRY && !b.result_reads);
    b.stuck = false;
    b.freeze_stream = true;
    run(&b, 780);
    assert(b.slot.node.sequence == 1 && b.slot.node.sample_flags == 1);
    run(&b, 200);
    assert(b.slot.node.sequence == 1 && !b.slot.node.sample_flags);
    b.freeze_stream = false;
    run(&b, 800);
    uint16_t sequence = b.slot.node.sequence;
    assert(b.slot.node.sample_flags == 1);
    tick(&b, 1000); /* Scheduler stall must not give an old result a new timestamp. */
    assert(b.slot.node.sequence == sequence && !b.slot.node.sample_flags);
}

int main(void)
{
    test_frames();
    test_recovery_and_detach();
    test_stale_or_stalled();
    return 0;
}
