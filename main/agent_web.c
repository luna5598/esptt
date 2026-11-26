#include "agent.h"
#include "agent_network.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    httpd_req_t *request;
    char question[513];
} agent_query;

static atomic_flag pending = ATOMIC_FLAG_INIT;

static void respond(void *arg)
{
    agent_query *query = arg;
    char *answer = NULL;
    esp_err_t err = agent_ask(query->question, &answer);
    httpd_resp_set_type(query->request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(query->request, "Cache-Control", "no-store");
    if (err == ESP_OK) {
        httpd_resp_sendstr(query->request, answer);
    } else {
        httpd_resp_set_status(query->request, "502 Bad Gateway");
        httpd_resp_sendstr(query->request, "本次未获得经工具核对的传感器回答。");
    }
    free(answer);
    httpd_req_async_handler_complete(query->request);
    free(query);
    atomic_flag_clear(&pending);
    vTaskDelete(NULL);
}

esp_err_t agent_web_ask(httpd_req_t *req)
{
    char header[4];
    if (httpd_req_get_hdr_value_str(req, "X-MOBan-UI", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1")) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "UI header required");
    if (!agent_network_online()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "请检查设备联网和时间同步。");
    }
    if (!req->content_len || req->content_len >= sizeof(((agent_query *)0)->question))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected up to 512 bytes");
    if (atomic_flag_test_and_set(&pending)) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        return httpd_resp_sendstr(req, "上一条问答仍在进行。");
    }
    agent_query *query = calloc(1, sizeof(*query));
    if (!query) goto failed;
    for (size_t n = 0; n < req->content_len;) {
        int count = httpd_req_recv(req, query->question + n, req->content_len - n);
        if (count <= 0) goto failed;
        n += (size_t)count;
    }
    if (strlen(query->question) != req->content_len) {
        free(query);
        atomic_flag_clear(&pending);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid question");
    }
    if (httpd_req_async_handler_begin(req, &query->request) != ESP_OK) goto failed;
    if (xTaskCreate(respond, "agent_reply", 6144, query, 4, NULL) == pdPASS) return ESP_OK;
    httpd_resp_send_err(query->request, HTTPD_500_INTERNAL_SERVER_ERROR, "Agent busy");
    httpd_req_async_handler_complete(query->request);
    free(query);
    atomic_flag_clear(&pending);
    return ESP_OK;
failed:
    free(query);
    atomic_flag_clear(&pending);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not receive question");
}
