#include "agent.h"
#include "claw_cap.h"
#include "claw_core.h"
#include "freertos/semphr.h"
#include "agent_temperature.h"

static claw_core_handle_t core;
static SemaphoreHandle_t question_lock;
static uint32_t next_id;
static uint32_t cancelled_id;

esp_err_t agent_init(const char *base_url, const char *model, const char *api_key)
{
    static const char *const groups[] = {"moban"};
    esp_err_t err = claw_cap_init();
    if (err == ESP_OK) err = agent_register_temperature();
    if (err == ESP_OK) err = claw_cap_set_llm_visible_groups(groups, 1);
    if (err == ESP_OK) err = claw_cap_start_all();
    if (err != ESP_OK) return err;
    question_lock = xSemaphoreCreateMutex();
    if (!question_lock) return ESP_ERR_NO_MEM;
    const claw_core_config_t config = {
        .backend_type = "openai_compatible", .base_url = base_url, .model = model,
        .api_key = api_key, .auth_type = "bearer", .supports_tools = true,
        .system_prompt = "You answer MO companion temperature and humidity questions. "
            "Call read_temperature on every question. Slots 0,1,2,3 mean left hand, "
            "right hand, left foot, right foot. Report the measured sample, divide centi-units "
            "by 100, and reply in the user's language. If unavailable or stale, report its "
            "status without a numeric reading. Never invent readings.",
        .call_cap = claw_cap_call_from_core, .timeout_ms = 15000, .max_tokens = 256,
        .task_core = tskNO_AFFINITY, .max_tool_iterations = 3,
        .max_context_providers = 1, .request_queue_len = 1, .response_queue_len = 1,
    };
    err = claw_core_create(&config, &core);
    if (err == ESP_OK) err = claw_core_add_context_provider(core, &claw_cap_root_agent_tools_provider);
    if (err == ESP_OK) err = claw_core_start(core);
    if (err != ESP_OK) {
        if (core) claw_core_destroy(core);
        core = NULL;
        vSemaphoreDelete(question_lock);
        question_lock = NULL;
    }
    return err;
}

esp_err_t agent_ask(const char *question, char **answer)
{
    if (!answer || !question || !question[0]) return ESP_ERR_INVALID_ARG;
    *answer = NULL;
    if (!core) return ESP_ERR_INVALID_STATE;
    if (!xSemaphoreTake(question_lock, 0)) return ESP_ERR_NOT_FINISHED;
    if (cancelled_id) {
        claw_core_response_t discarded = {0};
        if (claw_core_receive_for(core, cancelled_id, &discarded, 100) != ESP_OK) {
            xSemaphoreGive(question_lock);
            return ESP_ERR_NOT_FINISHED;
        }
        claw_core_response_free(&discarded);
        cancelled_id = 0;
    }
    if (++next_id == 0) ++next_id;
    agent_begin_observations(next_id);
    const claw_core_request_t request = {
        .request_id = next_id, .session_id = "temperature", .user_text = question,
    };
    claw_core_response_t response = {0};
    esp_err_t err = claw_core_start_run(core, &request, 0);
    if (err == ESP_OK) {
        /* Keep late responses out of subsequent callers' single-entry queue. */
        err = claw_core_receive_for(core, next_id, &response, 75000);
        if (err == ESP_ERR_TIMEOUT) {
            claw_core_cancel_request(core, next_id);
            cancelled_id = next_id;
        }
    }
    if (err == ESP_OK) {
        if (response.status == CLAW_CORE_RESPONSE_STATUS_OK && response.text &&
            agent_observations_current(next_id)) {
            *answer = response.text;
            response.text = NULL;
        } else {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    claw_core_response_free(&response);
    xSemaphoreGive(question_lock);
    return err;
}
