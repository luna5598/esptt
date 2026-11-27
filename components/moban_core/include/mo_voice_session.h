#pragma once

#include "moban_core.h"

#define MO_VOICE_WINDOW_MS 5000u

typedef struct {
    uint32_t slot;
    uint32_t generation;
} mo_voice_target;

typedef struct {
    bool armed;
    uint64_t boot;
    uint64_t opened_ms;
    uint32_t run;
    mo_voice_target preferred;
    mo_voice_target course;
} mo_voice_session;

/* Single audio-task owner. Capture both targets under the device snapshot lock.
 * Dispatch still checks boot, generation, run and phase under the runtime lock. */
bool mo_voice_take(mo_voice_session *session, int command_id, uint64_t now_ms,
                   enum mo_action *action, mo_voice_target *target);
