#include "agent_network.h"
#include "agent.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <string.h>
#include <time.h>

static bool configured;
static atomic_bool online;

static void on_network_event(void *arg, esp_event_base_t base, int32_t id, void *event)
{
    (void)arg;
    (void)event;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&online, true);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&online, false);
        esp_wifi_connect();
    }
}

static esp_err_t load_and_start(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("agent_cfg", "agent", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    char ssid[33], password[65], url[256], model[96], key[512];
    struct { const char *name; char *value; size_t size; } fields[] = {
        {"ssid", ssid, sizeof(ssid)}, {"password", password, sizeof(password)},
        {"url", url, sizeof(url)}, {"model", model, sizeof(model)}, {"key", key, sizeof(key)}
    };
    for (unsigned i = 0; i < sizeof(fields)/sizeof(fields[0]); i++) {
        err = nvs_get_str(handle, fields[i].name, fields[i].value, &fields[i].size);
        if (err != ESP_OK) break;
    }
    nvs_close(handle);
    if (err != ESP_OK) return err;
    if (!ssid[0] || !model[0] || !key[0] || strncmp(url, "https://", 8) || !url[8])
        return ESP_ERR_INVALID_ARG;

    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi.sta.password, password, strlen(password));
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    err = agent_init(url, model, key);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_network_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_network_event, NULL);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (err == ESP_OK) {
        esp_sntp_config_t clock = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        err = esp_netif_sntp_init(&clock);
    }
    if (err == ESP_OK) err = esp_wifi_connect();
    configured = err == ESP_OK;
    return err;
}

esp_err_t agent_network_start(void)
{
    esp_err_t err = nvs_flash_init_partition("agent_cfg");
    return err == ESP_OK ? load_and_start() : err;
}

bool agent_network_configured(void)
{
    return configured;
}

bool agent_network_online(void)
{
    return configured && atomic_load(&online) && time(NULL) >= 1704067200;
}
