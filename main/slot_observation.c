#include "slot_observation.h"

static const char *const link_names[] = {"offline","empty","pending","unsupported",
    "foreign_binding","bad_frame","link_fault","invalid_sample","unchanged","sample"};

const char *mo_link_name(enum limb_result result)
{
    return link_names[result];
}

void mo_observe_slot(uint64_t boot, unsigned slot, uint64_t now, const limb_poll_result *poll)
{
    if (poll->replaced) mo_presence(slot, 0);
    bool present = poll->result == LIMB_NEW_SAMPLE || poll->result == LIMB_UNCHANGED ||
                   poll->result == LIMB_NO_SAMPLE;
    mo_presence(slot, present);
    if (poll->result == LIMB_NO_SAMPLE ||
        (poll->result == LIMB_NEW_SAMPLE && (now < poll->sample.age_ms || poll->sample.age_ms > MO_FRESH_MS))) {
        mo_invalidate(slot);
    } else if (poll->result == LIMB_NEW_SAMPLE) {
        mo_sample(boot, slot, mo_slot(slot)->generation, now - poll->sample.age_ms,
                  poll->sample.a, (uint32_t)poll->sample.b);
    }
}
