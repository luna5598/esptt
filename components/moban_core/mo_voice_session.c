#include "mo_voice_session.h"

static mo_voice_target find_sensor(const mo_slot_view slots[MO_SLOT_COUNT],
                                   enum mo_tip tip, unsigned preferred)
{
    mo_voice_target target = {.slot = MO_SLOT_COUNT};
    unsigned matches = 0;
    for (unsigned i = 0; i < MO_SLOT_COUNT; ++i) {
        if (!slots[i].present || slots[i].tip != (uint32_t)tip) continue;
        target = (mo_voice_target){i, slots[i].generation};
        if (i == preferred) return target;
        ++matches;
    }
    return matches == 1 ? target : (mo_voice_target){.slot = MO_SLOT_COUNT};
}

void mo_voice_arm(mo_voice_session *session, uint64_t boot, uint64_t now,
                  const mo_slot_view slots[MO_SLOT_COUNT], const mo_course_view *course,
                  unsigned preferred)
{
    unsigned active = course->slot;
    *session = (mo_voice_session){
        .boot = boot, .opened_ms = now, .run = course->run_id, .armed = true,
        .temperature = find_sensor(slots, MO_THERMOMETER, preferred),
        .distance = find_sensor(slots, MO_RANGEFINDER, preferred),
        .course = {.slot = active, .generation = active < MO_SLOT_COUNT ? slots[active].generation : 0}
    };
}

bool mo_voice_take(mo_voice_session *session, int id, uint64_t now,
                   enum mo_action *action, mo_voice_target *target)
{
    if (!session->armed) return false;
    session->armed = false;
    if (now < session->opened_ms || now - session->opened_ms >= MO_VOICE_WINDOW_MS)
        return false;

    switch (id) {
    case MO_START_TEMP:
        *target = session->temperature;
        break;
    case MO_START_DISTANCE:
        *target = session->distance;
        break;
    case MO_RESUME_COURSE:
    case MO_STOP_COURSE:
        *target = session->course;
        break;
    default:
        return false;
    }
    *action = (enum mo_action)id;
    return target->slot < MO_SLOT_COUNT;
}
