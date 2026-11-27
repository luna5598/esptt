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
    if (present) {
        switch (poll->sample.type) {
        case LIMB_TEMP_HUM: tip = MO_THERMOMETER; break;
        case LIMB_DISTANCE: tip = MO_RANGEFINDER; break;
        }
    }
    if (mo_set_tip(slot, tip, now) != MO_OK || tip == MO_NO_TIP) return;
    if (poll->result == LIMB_UNCHANGED) return;
    uint16_t age = poll->sample.age_ms;
    if (poll->result == LIMB_NO_SAMPLE || age > now || age > mo_max_sample_age(tip)) {
        mo_invalidate(slot);
        return;
    }
    uint32_t gen = mo_slot(slot)->generation;
    if (tip == MO_RANGEFINDER) mo_distance_sample(boot, slot, gen, now - age, poll->sample.a);
    else mo_sample(boot, slot, gen, now - age, poll->sample.a, (uint32_t)poll->sample.b);
}
