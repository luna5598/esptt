#include "mo_voice_session.h"

bool mo_voice_take(mo_voice_session *session, int id, uint64_t now,
                   enum mo_action *action, mo_voice_target *target)
{
    if (!session->armed) return false;
    session->armed = false;
    if (now < session->opened_ms || now - session->opened_ms >= MO_VOICE_WINDOW_MS)
        return false;

    switch (id) {
    case MO_START_TEMP:
    case MO_START_DISTANCE:
        *target = session->preferred;
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
