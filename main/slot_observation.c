#include "slot_observation.h"

static const char *const link_names[] = {"offline","empty","pending","unsupported",
    "foreign_binding","bad_frame","link_fault","invalid_sample","unchanged","sample"};

const char *mo_link_name(enum limb_result result)
{
    return link_names[result];
}

void mo_observe_slot(uint64_t boot, unsigned slot, uint64_t now, const limb_poll_result *poll)
{
    if (poll->replaced) mo_set_tip(slot, MO_NO_TIP, now);
    bool present = poll->result == LIMB_NEW_SAMPLE || poll->result == LIMB_UNCHANGED ||
                   poll->result == LIMB_NO_SAMPLE;
    enum mo_tip tip = MO_NO_TIP;
    if (present) tip = poll->sample.type == LIMB_DISTANCE ? MO_RANGEFINDER : MO_THERMOMETER;
    mo_set_tip(slot, tip, now);
    if (poll->result == LIMB_NO_SAMPLE ||
        (poll->result == LIMB_NEW_SAMPLE && (now < poll->sample.age_ms || poll->sample.age_ms > MO_FRESH_MS))) {
        mo_invalidate(slot);
    } else if (poll->result == LIMB_NEW_SAMPLE) {
        uint32_t gen = mo_slot(slot)->generation;
        uint64_t sampled = now - poll->sample.age_ms;
        if (tip == MO_RANGEFINDER) {
            if (poll->sample.age_ms > MO_DISTANCE_FRESH_MS) mo_invalidate(slot);
            else mo_distance_sample(boot, slot, gen, sampled, poll->sample.a);
        } else {
            mo_sample(boot, slot, gen, sampled, poll->sample.a, (uint32_t)poll->sample.b);
        }
    }
}
