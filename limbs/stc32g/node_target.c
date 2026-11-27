#include "node_target.h"

void node_target_publish(node_target *t, const temperature_node *sample)
{
    uint32_t binding = t->published.session;
    if (sample->generation != t->published.generation || sample->type != t->published.type)
        binding = 0;
    t->published = *sample;
    t->published.session = binding;
}

void node_target_start(node_target *t)
{
    /* A repeated START discards an uncommitted command, but a pointer/read
     * pair must keep the preloaded reply across its repeated START. */
    if (t->received != 1) t->length = 0;
    t->received = 0;
}

uint8_t node_target_next(node_target *t)
{
    return t->cursor < t->length ? t->reply[t->cursor++] : 0xff;
}

uint8_t node_target_receive(node_target *t, uint8_t octet)
{
    if (t->received < sizeof(t->rx)) t->rx[t->received] = octet;
    if (t->received <= sizeof(t->rx)) t->received++;
    if (t->received != 1) {
        t->length = 0;
        return 0xff;
    }
    t->cursor = 0;
    switch (octet) {
    case LIMB_INFO: t->length = LIMB_INFO_BYTES; break;
    case LIMB_ACK: t->length = LIMB_ACK_BYTES; break;
    case LIMB_SAMPLE: t->length = LIMB_SAMPLE_BYTES; break;
    default: t->length = 0; return 0xff;
    }
    temperature_node_read(&t->published, octet, t->reply, t->length);
    return node_target_next(t);
}

void node_target_stop(node_target *t)
{
    if (t->received == LIMB_BIND_FRAME_BYTES)
        temperature_node_write(&t->published, t->rx, t->received);
    t->received = t->length = t->cursor = 0;
}
