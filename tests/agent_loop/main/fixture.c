/* Execute the real agent loop and tool. Only the model's HTTP responses and the
 * device snapshot are fixtures. Runs on a test board or on ESP32-C3 QEMU. */
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "agent.h"
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
static bool bypass_tool, replace_during_reply;
static const char question[] = "左手现在多少度？";

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
    assert(cJSON_GetArraySize(tools) == 1);
    cJSON *function = cJSON_GetObjectItem(cJSON_GetArrayItem(tools, 0), "function");
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(function, "name")), "read_temperature"));
    cJSON *messages = cJSON_GetObjectItem(root, "messages");
    cJSON *last = cJSON_GetArrayItem(messages, cJSON_GetArraySize(messages) - 1);
    const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(last, "role"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(last, "content"));
    const char *response;
    if (!strcmp(role, "user") && !bypass_tool) {
        assert(content && strstr(content, question));
        response = "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{\"role\":\"assistant\","
            "\"tool_calls\":[{\"id\":\"call_one\",\"type\":\"function\",\"function\":{"
            "\"name\":\"read_temperature\",\"arguments\":\"{\\\"slot\\\":0}\"}}]}}]}";
    } else {
        if (!bypass_tool) {
            assert(!strcmp(role, "tool"));
            cJSON *reading = cJSON_Parse(content);
            assert(reading && cJSON_GetObjectItem(reading, "slot")->valueint == 0);
            assert(cJSON_GetObjectItem(reading, "temp_centi_c")->valueint == 2137);
            assert(cJSON_GetObjectItem(reading, "humidity_centi_rh")->valueint == 4830);
            cJSON_Delete(reading);
            if (replace_during_reply) state.slots[0].generation++;
        }
        response = "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\","
                   "\"content\":\"左手温度为 21.37 °C。\"}}]}";
    }
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static void check_read(const char *input, const char *status, bool numeric)
{
    char out[256];
    const claw_cap_call_context_t ctx = {.caller = CLAW_CAP_CALLER_AGENT};
    esp_err_t err = claw_cap_call("read_temperature", input, &ctx, out, sizeof(out));
    if (!status) {
        assert(err != ESP_OK);
        return;
    }
    assert(err == ESP_OK);
    cJSON *result = cJSON_Parse(out);
    assert(result && !strcmp(status, cJSON_GetStringValue(cJSON_GetObjectItem(result, "status"))));
    assert(cJSON_IsNumber(cJSON_GetObjectItem(result, "temp_centi_c")) == numeric);
    cJSON_Delete(result);
}

static void check_http_question(void)
{
    esp_http_client_config_t cfg = {
        .url = "http://127.0.0.1:18181/api/ask", .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    assert(client);
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "X-MOBan-UI", "1"));
    ESP_ERROR_CHECK(esp_http_client_open(client, sizeof(question) - 1));
    assert(esp_http_client_write(client, question, sizeof(question) - 1) == sizeof(question) - 1);
    assert(esp_http_client_fetch_headers(client) >= 0);
    assert(esp_http_client_get_status_code(client) == 200);
    char answer[128] = {0};
    assert(esp_http_client_read_response(client, answer, sizeof(answer) - 1) > 0);
    assert(strstr(answer, "21.37"));
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
    check_read("{\"slot\":0}", "bus_offline", false);
    check_read("{\"slot\":0.5}", NULL, false);
    check_read("{\"slot\":4}", NULL, false);
    check_read("{\"slot\":0}xxx", NULL, false);
    state.bus_ready = true;
    state.status[1] = LIMB_EMPTY_SLOT;
    check_read("{\"slot\":1}", "empty", false);
    state.slots[0] = (mo_slot_view){.tip=MO_THERMOMETER, .present=1, .generation=5, .has_sample=1,
        .temp_centi=2137, .humidity_centi=4830, .sampled_ms=15};
    state.now = 15 + MO_FRESH_MS + 1;
    check_read("{\"slot\":0}", "stale", false);
    state.now--;
    state.slots[0].tip = MO_RANGEFINDER;
    check_read("{\"slot\":0}", "unsupported", false);
    state.slots[0].tip = MO_THERMOMETER;
    check_read("{\"slot\":0}", "ok", true);
    char *answer = NULL;
    check_http_question();
    bypass_tool = true;
    assert(agent_ask(question, &answer) == ESP_ERR_INVALID_RESPONSE && !answer);
    bypass_tool = false;
    replace_during_reply = true;
    assert(agent_ask(question, &answer) == ESP_ERR_INVALID_RESPONSE && !answer);
    puts("MOBAN_AGENT_LOOP_PASS");
    fflush(stdout);
}
