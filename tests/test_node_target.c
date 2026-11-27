#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "node_target.h"
#include "limb_protocol.h"

static void send(node_target *t, const uint8_t *p, size_t n)
{
    node_target_start(t);
    while (n--) node_target_receive(t, *p++);
}

int main(void)
{
    node_target t = {0};
    temperature_node n = {0};
    uint8_t frame[LIMB_BIND_FRAME_BYTES], bytes[LIMB_SAMPLE_BYTES];
    temperature_node_set_tip(&n, LIMB_DISTANCE);
    limb_node_receive_range(&n, n.generation, 125, true, 3);
    node_target_publish(&t, &n);
    assert(limb_bind_frame(LIMB_BIND, n.generation, 0x10203040, frame));
    send(&t, frame, sizeof(frame)-1);
    node_target_stop(&t);
    assert(!t.published.session);
    send(&t, frame, sizeof(frame));
    node_target_receive(&t, 0);
    node_target_stop(&t);
    assert(!t.published.session);
    send(&t, frame, sizeof(frame));
    node_target_start(&t); /* Only STOP may commit a command. */
    node_target_stop(&t);
    assert(!t.published.session);
    send(&t, frame, sizeof(frame));
    node_target_stop(&t);
    node_target_publish(&t, &n);
    assert(t.published.session == 0x10203040);
    node_target_start(&t);
    bytes[0] = node_target_receive(&t, LIMB_SAMPLE);
    node_target_start(&t);
    limb_node_receive_range(&n, n.generation, 250, true, 1);
    node_target_publish(&t, &n);
    for (size_t i = 1; i < sizeof(bytes); i++) bytes[i] = node_target_next(&t);
    limb_sample sample;
    assert(limb_decode_sample(bytes, sizeof(bytes), &sample) && sample.a == 125);
    assert(node_target_next(&t) == 0xff);
    node_target_stop(&t);
    temperature_node_set_tip(&n, 0);
    temperature_node_set_tip(&n, LIMB_DISTANCE);
    node_target_publish(&t, &n);
    send(&t, frame, sizeof(frame));
    node_target_stop(&t);
    assert(!t.published.session);
    return 0;
}
