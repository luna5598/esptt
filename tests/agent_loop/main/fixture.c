/* Execute the real agent loop and tool. Only the model's HTTP responses and the
 * device snapshot are fixtures. Runs on a test board or on ESP32-C3 QEMU. */
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "agent.h"
#include "agent_sensors.h"
#include <stdio.h>
#include "runtime.h"
#include "cJSON.h"
#include "claw_cap.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "agent_network.h"
#include "esp_netif.h"

esp_err_t agent_web_ask(httpd_req_t *req);

bool agent_network_online(void)
{
    return true;
}

static device_snapshot state;
typedef struct {
    const char *question, *tool, *status, *answer;
    unsigned slot;
} example;

static const example temperature = {"左手现在多少度？", "read_temperature", "ok", "左手采样温度为 21.37 °C。", 0};
static const example distance = {"左脚前面的物体有多远？", "read_distance", "ok", "左脚采样距离为 300 mm。", 2};
static const example empty = {"右脚前面的物体有多远？", "read_distance", "empty", "右脚未插入配件。", 3};
static const example no_echo = {"左脚前面的物体有多远？", "read_distance", "no_sample", "左脚没有有效距离读数。", 2};
static const example stale = {"左脚前面的物体有多远？", "read_distance", "stale", "左脚读数已过期。", 2};
static const example *current;
enum mutation { NONE, BYPASS, REPLACE, INVALIDATE, EXPIRE, RESAMPLE };
static enum mutation mutation;

void device_snapshot_read(device_snapshot *out)
{
    *out = state;
}

static esp_err_t model_reply(httpd_req_t *req)
{
    assert(req->content_len > 0 && req->content_len < 8192);
    char *body = calloc(1, req->content_len + 1);
    assert(body);
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        assert(n > 0);
        received += n;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    assert(root);
    cJSON *tools = cJSON_GetObjectItem(root, "tools");
    assert(cJSON_GetArraySize(tools) == 2);
    bool has_temp = false, has_distance = false;
    cJSON *item;
    cJSON_ArrayForEach(item, tools) {
        cJSON *fn = cJSON_GetObjectItem(item, "function");
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(fn, "name"));
        assert(name);
        if (!strcmp(name, "read_temperature")) has_temp = true;
        if (!strcmp(name, "read_distance")) has_distance = true;
    }
    assert(has_temp && has_distance);
    cJSON *messages = cJSON_GetObjectItem(root, "messages");
    cJSON *last = cJSON_GetArrayItem(messages, cJSON_GetArraySize(messages) - 1);
    const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(last, "role"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(last, "content"));
    char response[512];
    int written;
    if (!strcmp(role, "user") && mutation != BYPASS) {
        assert(content && strstr(content, current->question));
        written = snprintf(response, sizeof(response),
            "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{\"role\":\"assistant\","
            "\"tool_calls\":[{\"id\":\"call_one\",\"type\":\"function\",\"function\":{"
            "\"name\":\"%s\",\"arguments\":\"{\\\"slot\\\":%u}\"}}]}}]}", current->tool, current->slot);
    } else {
        if (mutation != BYPASS) {
            assert(!strcmp(role, "tool"));
            cJSON *reading = cJSON_Parse(content);
            assert(reading && cJSON_GetObjectItem(reading, "slot")->valueint == (int)current->slot);
            assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(reading, "status")), current->status));
            if (current == &temperature) {
                assert(cJSON_GetObjectItem(reading, "temp_centi_c")->valueint == 2137);
                assert(cJSON_GetObjectItem(reading, "humidity_centi_rh")->valueint == 4830);
                assert(!cJSON_GetObjectItem(reading, "distance_mm"));
            } else if (current == &distance) {
                assert(cJSON_GetObjectItem(reading, "distance_mm")->valueint == 300);
                assert(!cJSON_GetObjectItem(reading, "temp_centi_c"));
            } else {
                assert(!cJSON_GetObjectItem(reading, "distance_mm"));
                assert(!cJSON_GetObjectItem(reading, "sampled_ms"));
            }
            cJSON_Delete(reading);
            mo_slot_view *slot = &state.slots[current->slot];
            if (mutation == REPLACE) slot->generation++;
            if (mutation == INVALIDATE) slot->has_sample = 0;
            if (mutation == EXPIRE) state.now += MO_DISTANCE_FRESH_MS + 1;
            if (mutation == RESAMPLE) {
                state.now += 500;
                slot->sampled_ms = state.now;
                slot->distance_mm = 420;
            }
        }
        written = snprintf(response, sizeof(response),
            "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\","
            "\"content\":\"%s\"}}]}", current->answer);
    }
    assert(written > 0 && (size_t)written < sizeof(response));
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static void check_read(const char *tool, const char *input, const char *status, bool numeric)
{
    char out[256];
    const claw_cap_call_context_t ctx = {.caller = CLAW_CAP_CALLER_AGENT};
    esp_err_t err = claw_cap_call(tool, input, &ctx, out, sizeof(out));
    if (!status) {
        assert(err != ESP_OK);
        return;
    }
    assert(err == ESP_OK);
    cJSON *result = cJSON_Parse(out);
    assert(result && !strcmp(status, cJSON_GetStringValue(cJSON_GetObjectItem(result, "status"))));
    bool temp = !strcmp(tool, "read_temperature");
    assert(!!cJSON_IsNumber(cJSON_GetObjectItem(result, "temp_centi_c")) == (numeric && temp));
    assert(!!cJSON_IsNumber(cJSON_GetObjectItem(result, "humidity_centi_rh")) == (numeric && temp));
    assert(!!cJSON_IsNumber(cJSON_GetObjectItem(result, "distance_mm")) == (numeric && !temp));
    assert(!!cJSON_IsNumber(cJSON_GetObjectItem(result, "sampled_ms")) == numeric);
    cJSON_Delete(result);
}

static void check_http_question(const example *e)
{
    current = e;
    size_t length = strlen(e->question);
    esp_http_client_config_t cfg = {
        .url = "http://127.0.0.1:18181/api/ask", .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    assert(client);
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "X-MOBan-UI", "1"));
    ESP_ERROR_CHECK(esp_http_client_open(client, length));
    assert(esp_http_client_write(client, e->question, length) == (int)length);
    assert(esp_http_client_fetch_headers(client) >= 0);
    assert(esp_http_client_get_status_code(client) == 200);
    char answer[128] = {0};
    assert(esp_http_client_read_response(client, answer, sizeof(answer) - 1) > 0);
    assert(!strcmp(answer, e->answer));
    esp_http_client_cleanup(client);
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 18181;
    cfg.stack_size = 6144;
    httpd_handle_t server;
    const httpd_uri_t route = {.uri="/v1/chat/completions", .method=HTTP_POST, .handler=model_reply};
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    const httpd_uri_t ask = {.uri="/api/ask", .method=HTTP_POST, .handler=agent_web_ask};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ask));
    ESP_ERROR_CHECK(agent_init("http://127.0.0.1:18181/v1", "fixture", "fixture"));
    state.boot = 17;
    check_read("read_temperature", "{\"slot\":0}", "bus_offline", false);
    check_read("read_distance", "{\"slot\":2}", "bus_offline", false);
    check_read("read_temperature", "{\"slot\":0.5}", NULL, false);
    check_read("read_distance", "{\"slot\":4}", NULL, false);
    check_read("read_distance", "{\"slot\":0}xxx", NULL, false);
    check_read("read_distance", "{\"slot\":2,\"slot\":0}", NULL, false);
    state.bus_ready = true;
    state.status[0] = state.status[2] = LIMB_NEW_SAMPLE;
    state.status[1] = state.status[3] = LIMB_EMPTY_SLOT;
    check_read("read_temperature", "{\"slot\":1}", "empty", false);
    state.slots[0] = (mo_slot_view){.tip=MO_THERMOMETER, .present=1, .generation=5, .has_sample=1,
        .temp_centi=2137, .humidity_centi=4830, .sampled_ms=15};
    state.now = 15 + MO_FRESH_MS + 1;
    check_read("read_temperature", "{\"slot\":0}", "stale", false);
    state.now--;
    check_read("read_temperature", "{\"slot\":0}", "ok", true);
    check_read("read_distance", "{\"slot\":0}", "unsupported", false);
    check_http_question(&temperature);

    state.slots[2] = (mo_slot_view){.tip=MO_RANGEFINDER, .present=1, .generation=7, .has_sample=1,
        .distance_mm=300, .sampled_ms=state.now - MO_DISTANCE_FRESH_MS};
    state.bus_ready = false;
    state.bus_fault = true;
    check_read("read_distance", "{\"slot\":2}", "bus_fault", false);
    check_read("read_temperature", "{\"slot\":0}", "bus_fault", false);
    state.bus_fault = false;
    state.bus_ready = true;
    check_read("read_distance", "{\"slot\":2}", "ok", true);
    check_read("read_temperature", "{\"slot\":2}", "unsupported", false);
    check_http_question(&distance);
    check_http_question(&empty);
    ++state.now;
    check_http_question(&stale);
    state.status[2] = LIMB_NO_SAMPLE;
    check_http_question(&no_echo);

    /* A mixed question depends on all the observed slots; a different slot is immaterial. */
    state.slots[0].sampled_ms = state.slots[2].sampled_ms = state.now;
    state.status[2] = LIMB_UNCHANGED;
    const uint32_t id = 100;
    agent_begin_observations(id);
    const claw_cap_call_context_t ctx = {.caller = CLAW_CAP_CALLER_AGENT, .request_id = id};
    char out[256];
    ESP_ERROR_CHECK(claw_cap_call("read_temperature", "{\"slot\":0}", &ctx, out, sizeof(out)));
    ESP_ERROR_CHECK(claw_cap_call("read_distance", "{\"slot\":2}", &ctx, out, sizeof(out)));
    state.slots[1].generation++;
    assert(agent_observations_current(id));
    state.slots[0].generation++;
    assert(!agent_observations_current(id));

    char *answer = NULL;
    current = &distance;
    const enum mutation rejected[] = {BYPASS, REPLACE, INVALIDATE, EXPIRE};
    for (size_t i = 0; i < sizeof(rejected)/sizeof(rejected[0]); ++i) {
        mutation = rejected[i];
        state.slots[2].has_sample = 1;
        state.slots[2].sampled_ms = state.now;
        assert(agent_ask(current->question, &answer) == ESP_ERR_INVALID_RESPONSE && !answer);
    }
    /* The original reading is explicitly a sample; newer healthy readings may arrive
     * during model latency without invalidating the answer about that sample. */
    mutation = RESAMPLE;
    state.slots[2].sampled_ms = state.now;
    ESP_ERROR_CHECK(agent_ask(current->question, &answer));
    assert(answer && !strcmp(answer, current->answer) && state.slots[2].distance_mm == 420);
    free(answer);
    puts("MOBAN_AGENT_LOOP_PASS");
    fflush(stdout);
}
