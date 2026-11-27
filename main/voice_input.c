#include "sdkconfig.h"
#if CONFIG_MOBAN_OFFLINE_VOICE
#include "mic_input.h"
#include "mo_voice_session.h"
#include "runtime.h"
#include "esp_log.h"
#include "esp_timer.h"

static mo_voice_session wake;

static void on_voice(void *ctx, voice_event event, int command_id)
{
    mo_voice_session *session = ctx;
    if (event == VOICE_WAKE) {
        device_snapshot snapshot;
        device_snapshot_read(&snapshot);
        unsigned preferred = CONFIG_MOBAN_VOICE_SLOT;
        unsigned active = snapshot.course.slot;
        *session = (mo_voice_session){
            .boot = snapshot.boot, .opened_ms = snapshot.now, .run = snapshot.course.run_id, .armed = true,
            .preferred = {preferred, snapshot.slots[preferred].generation},
            .course = {active, snapshot.slots[active].generation}
        };
    } else if (event == VOICE_COMMAND) {
        enum mo_action action;
        mo_voice_target target;
        uint64_t now = (uint64_t)esp_timer_get_time() / 1000u;
        if (mo_voice_take(session, command_id, now, &action, &target)) {
            int rc = device_course_action(action, session->boot, target.slot, target.generation, session->run);
            ESP_LOGI("voice", "Command %d, slot %u, result %d", command_id, (unsigned)target.slot, rc);
        }
    } else {
        session->armed = false;
    }
}

void device_voice_start(void)
{
    esp_err_t err = mic_input_start(on_voice, &wake);
    if (err == ESP_ERR_NOT_FOUND) ESP_LOGI("voice", "Microphone GPIOs are not configured");
    else if (err != ESP_OK) ESP_LOGE("voice", "Microphone unavailable: %s", esp_err_to_name(err));
}
#endif
