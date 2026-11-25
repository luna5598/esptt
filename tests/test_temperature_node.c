#undef NDEBUG
#include <assert.h>
#include "temperature_node.h"
#include "limb_reader.h"

static const uint8_t warm[] = {0x08, 0x80, 0, 6, 0, 0, 0xa6}; /* 25 C, 50 %RH */
static const uint8_t cold[] = {0x08, 0x80, 0, 2, 0, 0, 0x8f}; /* -25 C, 50 %RH */
static const uint8_t busy[] = {0x88, 0x80, 0, 6, 0, 0, 0x4a};
static const uint8_t extreme[] = {0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0x92};

static bool read_node(void *ctx, uint8_t reg, uint8_t *out, size_t n)
{
    return temperature_node_read(ctx, reg, out, n);
}

static bool write_node(void *ctx, const uint8_t *frame, size_t n)
{
    return temperature_node_write(ctx, frame, n);
}

static void check_binding_and_measurement(void)
{
    temperature_node node = {0};
    limb_reader reader = {0};
    limb_transport io = {.ctx = &node, .read = read_node, .write = write_node};
    uint8_t frame[LIMB_BIND_FRAME_BYTES], corrupt[7] = {0};
    uint16_t first_gen;
    limb_poll_result result;

    assert(limb_poll(&reader, &io, 10).result == LIMB_EMPTY_SLOT);
    temperature_node_set_tip(&node, LIMB_TEMP_HUM);
    first_gen = node.generation;
    assert(limb_poll(&reader, &io, 10).result == LIMB_NO_SAMPLE && node.session == 10);
    assert(limb_bind_frame(LIMB_BIND, first_gen, 10, frame));
    assert(write_node(&node, frame, sizeof(frame))); /* Replay of the same BIND. */
    assert(limb_bind_frame(LIMB_BIND, first_gen, 11, frame));
    assert(!write_node(&node, frame, sizeof(frame)) && node.session == 10);
    assert(limb_bind_frame(LIMB_RELEASE, first_gen, 10, frame));
    frame[7] ^= 1;
    assert(!write_node(&node, frame, sizeof(frame)) && node.session == 10);
    assert(!write_node(&node, frame, sizeof(frame) - 1));

    assert(temperature_node_receive_aht20(&node, first_gen, warm, sizeof(warm)));
    temperature_node_advance(&node, 170);
    result = limb_poll(&reader, &io, 11);
    assert(result.result == LIMB_NEW_SAMPLE);
    assert(result.sample.a == 2500 && result.sample.b == 5000 && result.sample.age_ms == 170);
    assert(!temperature_node_receive_aht20(&node, first_gen, busy, sizeof(busy)));
    assert(limb_poll(&reader, &io, 11).result == LIMB_UNCHANGED);
    temperature_node_advance(&node, UINT32_MAX);
    result = limb_poll(&reader, &io, 11);
    assert(result.result == LIMB_UNCHANGED && result.sample.age_ms == UINT16_MAX);
    corrupt[0] = 0x08;
    assert(!temperature_node_receive_aht20(&node, first_gen, corrupt, sizeof(corrupt)));
    assert(limb_poll(&reader, &io, 11).result == LIMB_NO_SAMPLE);
    assert(!temperature_node_receive_aht20(&node, first_gen, extreme, sizeof(extreme)));
    result = limb_poll(&reader, &io, 11);
    assert(result.result == LIMB_NO_SAMPLE && result.sample.flags == 2);
    assert(result.sample.b == 10000);

    temperature_node_set_tip(&node, 0);
    temperature_node_set_tip(&node, LIMB_TEMP_HUM); /* Host did not poll during the gap. */
    assert(node.session == 0 && node.generation != first_gen);
    assert(!temperature_node_receive_aht20(&node, first_gen, warm, sizeof(warm)));
    assert(limb_bind_frame(LIMB_BIND, first_gen, 10, frame));
    assert(!write_node(&node, frame, sizeof(frame)));
    assert(temperature_node_receive_aht20(&node, node.generation, cold, sizeof(cold)));
    result = limb_poll(&reader, &io, 12);
    assert(result.replaced && result.result == LIMB_NEW_SAMPLE && node.session == 12);
    assert(result.sample.a == -2500 && result.sample.b == 5000);

    assert(limb_bind_frame(LIMB_RELEASE, node.generation, 10, frame));
    assert(!write_node(&node, frame, sizeof(frame)) && node.session == 12);
    assert(limb_bind_frame(LIMB_RELEASE, node.generation, 12, frame));
    assert(write_node(&node, frame, sizeof(frame)) && node.session == 0);
    assert(write_node(&node, frame, sizeof(frame)));
}

static void check_two_slots(void)
{
    temperature_node nodes[2] = {{0}};
    limb_reader readers[2] = {{0}};
    limb_transport io = {.read = read_node, .write = write_node};
    unsigned i;
    for (i = 0; i < 2; i++) {
        temperature_node_set_tip(&nodes[i], LIMB_TEMP_HUM);
        io.ctx = &nodes[i];
        assert(limb_poll(&readers[i], &io, 100 + i).result == LIMB_NO_SAMPLE);
    }
    temperature_node_set_tip(&nodes[0], 0);
    io.ctx = &nodes[0];
    assert(limb_poll(&readers[0], &io, 200).result == LIMB_EMPTY_SLOT);
    io.ctx = &nodes[1];
    assert(temperature_node_receive_aht20(&nodes[1], nodes[1].generation, warm, sizeof(warm)));
    assert(limb_poll(&readers[1], &io, 201).result == LIMB_NEW_SAMPLE);
    assert(nodes[1].session == 101);
}

int main(void)
{
    check_binding_and_measurement();
    check_two_slots();
    return 0;
}
