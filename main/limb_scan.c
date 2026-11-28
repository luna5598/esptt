#include "limb_scan.h"

enum { RETRY_MS = 500, FOREIGN_RETRY_MS = 1000 };

static enum scan_event bus_down(limb_scan *s, uint64_t now)
{
    bool notify = s->bus_retry_ms == 0;
    s->bus_retry_ms = now + RETRY_MS;
    return notify ? SCAN_BUS_DOWN : SCAN_SKIP;
}

enum scan_event limb_scan_slot(limb_scan *s, const limb_scan_io *io, unsigned slot,
                               uint64_t now, limb_poll_result *out)
{
    if (slot >= MO_SLOT_COUNT || now < s->slot_retry_ms[slot]) return SCAN_SKIP;
    if (s->bus_retry_ms) {
        if (now < s->bus_retry_ms) return SCAN_SKIP;
        if (!io->recover(io->ctx)) return bus_down(s, now);
    }

    /* A failed switch may still have the PREVIOUS branch connected. Retry the
     * requested channel from a verified all-off state before attributing a fault. */
    if (!io->select(io->ctx, slot)) {
        if (!io->recover(io->ctx)) return bus_down(s, now);
        if (!io->select(io->ctx, slot)) {
            if (!io->recover(io->ctx)) return bus_down(s, now);
            *out = (limb_poll_result){.result = LIMB_CHANNEL_FAULT};
            s->bus_retry_ms = 0;
            s->slot_retry_ms[slot] = now + RETRY_MS;
            return SCAN_OBSERVED;
        }
    }
    if (!io->poll(io->ctx, slot, out)) {
        if (!io->recover(io->ctx)) return bus_down(s, now);
        *out = (limb_poll_result){.result = LIMB_LINK_FAULT};
        s->slot_retry_ms[slot] = now + RETRY_MS;
    } else if (out->result == LIMB_FOREIGN_BINDING) {
        if (!io->recover(io->ctx)) return bus_down(s, now);
        s->slot_retry_ms[slot] = now + FOREIGN_RETRY_MS;
    }
    s->bus_retry_ms = 0;
    return SCAN_OBSERVED;
}
