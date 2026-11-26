#include "limb_reader.h"

limb_poll_result limb_poll(limb_reader *r, const limb_transport *io, uint32_t next_session)
{
    limb_poll_result out = {.result = LIMB_LINK_FAULT};
    uint8_t data[LIMB_SAMPLE_BYTES];
    limb_info info;
    if (!io->read(io->ctx, LIMB_INFO, data, LIMB_INFO_BYTES)) return out;
    if (!limb_decode_info(data, LIMB_INFO_BYTES, &info)) {
        out.result = LIMB_BAD_FRAME;
        return out;
    }
    uint16_t cap = limb_sensor_cap(info.type);
    if ((info.flags & (LIMB_PRESENT | LIMB_READY)) != (LIMB_PRESENT | LIMB_READY) ||
        info.fault || !(info.capabilities & cap)) {
        *r = (limb_reader){0};
        if (info.fault) out.result = LIMB_LINK_FAULT;
        else if (!(info.flags & LIMB_PRESENT)) out.result = LIMB_EMPTY_SLOT;
        else if (cap && !(info.flags & LIMB_READY)) out.result = LIMB_PENDING;
        else out.result = LIMB_UNSUPPORTED;
        return out;
    }

    bool bound = (info.flags & LIMB_BOUND) != 0;
    if (info.tip_gen != r->tip_gen || (r->identified && !bound)) {
        out.replaced = true;
        *r = (limb_reader){.tip_gen = info.tip_gen};
    }
    if (!bound) {
        if (!r->session) r->session = next_session;
        uint8_t frame[LIMB_BIND_FRAME_BYTES];
        if (!limb_bind_frame(LIMB_BIND, info.tip_gen, r->session, frame) ||
            !io->write(io->ctx, frame, sizeof(frame))) return out;
    }
    limb_ack ack;
    if (!io->read(io->ctx, LIMB_ACK, data, LIMB_ACK_BYTES)) return out;
    if (!limb_decode_ack(data, LIMB_ACK_BYTES, &ack)) {
        out.result = LIMB_BAD_FRAME;
        return out;
    }
    if (!r->session || ack.session != r->session || ack.tip_gen != info.tip_gen ||
        ack.status != 0 || ack.sequence != 0) {
        out.result = LIMB_FOREIGN_BINDING;
        return out;
    }
    r->identified = true;
    if (!io->read(io->ctx, LIMB_SAMPLE, data, LIMB_SAMPLE_BYTES)) return out;
    limb_sample *s = &out.sample;
    if (!limb_decode_sample(data, LIMB_SAMPLE_BYTES, s) || s->tip_gen != info.tip_gen ||
        s->type != info.type) {
        out.result = LIMB_BAD_FRAME;
        return out;
    }
    bool in_range = s->type == LIMB_DISTANCE ? (s->a > 0 && s->b == 0) :
                    (s->a >= -4000 && s->a <= 8500 && s->b >= 0 && s->b <= 10000);
    if ((s->flags & 3u) != 1 || !in_range) {
        out.result = LIMB_NO_SAMPLE;
        return out;
    }
    uint16_t delta = (uint16_t)(s->sequence - r->last_sample);
    if (r->has_sequence && (!delta || delta >= 0x8000u)) {
        out.result = LIMB_UNCHANGED;
        return out;
    }
    r->last_sample = s->sequence;
    r->has_sequence = true;
    out.result = LIMB_NEW_SAMPLE;
    return out;
}
