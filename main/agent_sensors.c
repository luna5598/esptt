#include "agent_sensors.h"
#include "runtime.h"
#include "claw_cap.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum sensor_tool { TEMPERATURE, DISTANCE, TOOL_COUNT };
enum reading_status { UNREAD, OK, BUS_OFFLINE, EMPTY, UNSUPPORTED, UNAVAILABLE, NO_SAMPLE, STALE, BUS_FAULT };
static const char *const status_name[] = {
    [OK] = "ok", [BUS_OFFLINE] = "bus_offline", [EMPTY] = "empty",
    [UNSUPPORTED] = "unsupported", [UNAVAILABLE] = "unavailable",
    [NO_SAMPLE] = "no_sample", [STALE] = "stale", [BUS_FAULT] = "bus_fault"
};
static const enum mo_tip tool_tip[TOOL_COUNT] = {
    [TEMPERATURE] = MO_THERMOMETER, [DISTANCE] = MO_RANGEFINDER
};

static enum reading_status status_at(const device_snapshot *s, unsigned slot, enum sensor_tool tool)
{
    const mo_slot_view *v = &s->slots[slot];
    if (s->bus_fault) return BUS_FAULT;
    if (!s->bus_ready) return BUS_OFFLINE;
    switch (s->status[slot]) {
    case LIMB_EMPTY_SLOT: return EMPTY;
    case LIMB_UNSUPPORTED: return UNSUPPORTED;
    case LIMB_NEW_SAMPLE:
    case LIMB_UNCHANGED:
    case LIMB_NO_SAMPLE: break;
    default: return UNAVAILABLE;
    }
    if (!v->present) return UNAVAILABLE;
    if (v->tip != (uint32_t)tool_tip[tool]) return UNSUPPORTED;
    if (s->status[slot] == LIMB_NO_SAMPLE || !v->has_sample) return NO_SAMPLE;
    return mo_sample_fresh(v, s->now) ? OK : STALE;
}

/* A response can include several tool calls. Keep every observed slot/type
 * until delivery, including unavailable readings used to answer the user. */
typedef struct {
    uint32_t generation;
    enum reading_status status;
} observation;

static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
static uint32_t active_request;
static uint64_t observed_boot;
static bool used, changed;
static observation observations[MO_SLOT_COUNT][TOOL_COUNT];

void agent_begin_observations(uint32_t request_id)
{
    portENTER_CRITICAL(&guard);
    active_request = request_id;
    used = changed = false;
    memset(observations, 0, sizeof(observations));
    portEXIT_CRITICAL(&guard);
}

static void observe(uint32_t id, const device_snapshot *s, unsigned slot, enum sensor_tool tool,
                    enum reading_status status)
{
    portENTER_CRITICAL(&guard);
    if (id && id == active_request) {
        observation *o = &observations[slot][tool];
        if ((used && observed_boot != s->boot) ||
            (o->status != UNREAD && (o->generation != s->slots[slot].generation || o->status != status)))
            changed = true;
        observed_boot = s->boot;
        used = true;
        *o = (observation){.generation = s->slots[slot].generation, .status = status};
    }
    portEXIT_CRITICAL(&guard);
}

bool agent_observations_current(uint32_t id)
{
    device_snapshot s;
    device_snapshot_read(&s);
    portENTER_CRITICAL(&guard);
    bool valid = id == active_request && used && !changed && s.boot == observed_boot;
    for (unsigned slot = 0; valid && slot < MO_SLOT_COUNT; ++slot) {
        for (unsigned tool = 0; tool < TOOL_COUNT; ++tool) {
            const observation *o = &observations[slot][tool];
            if (o->status != UNREAD && (o->generation != s.slots[slot].generation ||
                o->status != status_at(&s, slot, (enum sensor_tool)tool))) {
                valid = false;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&guard);
    return valid;
}

static esp_err_t read_sensor(enum sensor_tool tool, const char *input, const claw_cap_call_context_t *ctx,
                             char *out, size_t capacity)
{
    cJSON *args = cJSON_ParseWithOpts(input, NULL, true);
    const cJSON *arg = cJSON_GetObjectItemCaseSensitive(args, "slot");
    if (!cJSON_IsObject(args) || cJSON_GetArraySize(args) != 1 || !cJSON_IsNumber(arg) ||
        arg->valueint < 0 || arg->valueint >= (int)MO_SLOT_COUNT || arg->valuedouble != arg->valueint) {
        cJSON_Delete(args);
        snprintf(out, capacity, "Expected an integer slot from 0 through 3.");
        return ESP_ERR_INVALID_ARG;
    }
    unsigned slot = (unsigned)arg->valueint;
    cJSON_Delete(args);

    device_snapshot s;
    device_snapshot_read(&s);
    const mo_slot_view *v = &s.slots[slot];
    enum reading_status status = status_at(&s, slot, tool);
    char reading[128] = "";
    if (status == OK) {
        if (tool == DISTANCE)
            snprintf(reading, sizeof(reading), ",\"distance_mm\":%" PRId32 ",\"sampled_ms\":%" PRIu64,
                     v->distance_mm, v->sampled_ms);
        else
            snprintf(reading, sizeof(reading), ",\"temp_centi_c\":%" PRId32
                     ",\"humidity_centi_rh\":%" PRIu32 ",\"sampled_ms\":%" PRIu64,
                     v->temp_centi, v->humidity_centi, v->sampled_ms);
    }
    int len = snprintf(out, capacity, "{\"slot\":%u,\"boot\":\"%016" PRIx64
                       "\",\"generation\":%" PRIu32 ",\"status\":\"%s\"%s}",
                       slot, s.boot, v->generation, status_name[status], reading);
    if (len < 0 || (size_t)len >= capacity) return ESP_ERR_INVALID_SIZE;
    if (ctx) observe(ctx->request_id, &s, slot, tool, status);
    return ESP_OK;
}

static esp_err_t read_temperature(const char *input, const claw_cap_call_context_t *ctx,
                                  char *out, size_t capacity)
{
    return read_sensor(TEMPERATURE, input, ctx, out, capacity);
}

static esp_err_t read_distance(const char *input, const claw_cap_call_context_t *ctx,
                               char *out, size_t capacity)
{
    return read_sensor(DISTANCE, input, ctx, out, capacity);
}

esp_err_t agent_register_sensors(void)
{
    static const char slot_schema[] = "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\","
        "\"minimum\":0,\"maximum\":3}},\"required\":[\"slot\"],\"additionalProperties\":false}";
    static const claw_cap_descriptor_t tools[] = {
        {.id = "read_temperature", .name = "read_temperature", .family = "moban",
         .description = "Read temperature and relative humidity in centi-units for one limb. "
                        "0=left hand, 1=right hand, 2=left foot, 3=right foot.",
         .kind = CLAW_CAP_KIND_CALLABLE, .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
         .input_schema_json = slot_schema, .execute = read_temperature},
        {.id = "read_distance", .name = "read_distance", .family = "moban",
         .description = "Read distance to the target in millimeters for one limb. "
                        "0=left hand, 1=right hand, 2=left foot, 3=right foot.",
         .kind = CLAW_CAP_KIND_CALLABLE, .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
         .input_schema_json = slot_schema, .execute = read_distance},
    };
    static const claw_cap_group_t group = {
        .group_id = "moban", .plugin_name = "MO companion", .version = "2",
        .descriptors = tools, .descriptor_count = sizeof(tools) / sizeof(tools[0]),
    };
    return claw_cap_register_group(&group);
}
