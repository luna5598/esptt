#include "runtime.h"
#include "agent_temperature.h"
#include "claw_cap.h"
#include "cJSON.h"
#include <inttypes.h>
#include <stdio.h>

/* The agent worker and the HTTP reply task run independently. Reject late
 * results after a same-slot replacement, even if several slots were queried. */
static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
static uint32_t active_run, used_slots;
static uint32_t observed_generation[MO_SLOT_COUNT];
static bool replaced;

void agent_begin_observations(uint32_t request_id)
{
    portENTER_CRITICAL(&guard);
    active_run = request_id;
    used_slots = 0;
    replaced = false;
    portEXIT_CRITICAL(&guard);
}

static void observe(uint32_t run, unsigned slot, uint32_t generation)
{
    uint32_t bit = 1u << slot;
    portENTER_CRITICAL(&guard);
    if (run && run == active_run) {
        if ((used_slots & bit) && observed_generation[slot] != generation) replaced = true;
        observed_generation[slot] = generation;
        used_slots |= bit;
    }
    portEXIT_CRITICAL(&guard);
}

bool agent_observations_current(uint32_t request_id)
{
    device_snapshot snapshot;
    device_snapshot_read(&snapshot);
    portENTER_CRITICAL(&guard);
    bool valid = request_id == active_run && used_slots && !replaced;
    for (unsigned i = 0; i < MO_SLOT_COUNT; i++) {
        if ((used_slots & (1u << i)) && observed_generation[i] != snapshot.slots[i].generation)
            valid = false;
    }
    portEXIT_CRITICAL(&guard);
    return valid;
}

static esp_err_t read_temperature(const char *input, const claw_cap_call_context_t *ctx,
                                  char *out, size_t capacity)
{
    cJSON *args = cJSON_ParseWithOpts(input, NULL, true);
    const cJSON *slot = cJSON_GetObjectItemCaseSensitive(args, "slot");
    if (!cJSON_IsObject(args) || cJSON_GetArraySize(args) != 1 || !cJSON_IsNumber(slot) ||
        slot->valueint < 0 || slot->valueint >= (int)MO_SLOT_COUNT ||
        slot->valuedouble != slot->valueint) {
        cJSON_Delete(args);
        snprintf(out, capacity, "Expected an integer slot from 0 through 3.");
        return ESP_ERR_INVALID_ARG;
    }
    const unsigned i = (unsigned)slot->valueint;
    cJSON_Delete(args);
    device_snapshot snapshot;
    device_snapshot_read(&snapshot);
    const mo_slot_view *v = &snapshot.slots[i];
    bool fresh = v->tip == MO_THERMOMETER && snapshot.bus_ready && mo_sample_fresh(v, snapshot.now);
    const char *status = "ok";
    if (!snapshot.bus_ready) status = "bus_offline";
    else if (snapshot.status[i] == LIMB_EMPTY_SLOT) status = "empty";
    else if (snapshot.status[i] == LIMB_UNSUPPORTED) status = "unsupported";
    else if (!v->present) status = "unavailable";
    else if (v->tip != MO_THERMOMETER) status = "unsupported";
    else if (!v->has_sample) status = "no_sample";
    else if (!fresh) status = "stale";

    char reading[128] = "";
    if (fresh) {
        snprintf(reading, sizeof(reading), ",\"temp_centi_c\":%" PRId32
                 ",\"humidity_centi_rh\":%" PRIu32 ",\"sampled_ms\":%" PRIu64,
                 v->temp_centi, v->humidity_centi, v->sampled_ms);
    }
    int len = snprintf(out, capacity, "{\"slot\":%u,\"boot\":\"%016" PRIx64
                       "\",\"generation\":%" PRIu32 ",\"status\":\"%s\"%s}",
                       i, snapshot.boot, v->generation, status, reading);
    if (len < 0 || (size_t)len >= capacity) return ESP_ERR_INVALID_SIZE;
    if (ctx) observe(ctx->request_id, i, v->generation);
    return ESP_OK;
}

esp_err_t agent_register_temperature(void)
{
    static const claw_cap_descriptor_t tool = {
        .id = "read_temperature", .name = "read_temperature", .family = "moban",
        .description = "Read a single MO companion limb's temperature and relative humidity. "
            "0=left hand, 1=right hand, 2=left foot, 3=right foot. Values are centi-units.",
        .kind = CLAW_CAP_KIND_CALLABLE, .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\","
            "\"minimum\":0,\"maximum\":3}},\"required\":[\"slot\"],\"additionalProperties\":false}",
        .execute = read_temperature,
    };
    static const claw_cap_group_t group = {
        .group_id = "moban", .plugin_name = "MO companion", .version = "1",
        .descriptors = &tool, .descriptor_count = 1,
    };
    return claw_cap_register_group(&group);
}
