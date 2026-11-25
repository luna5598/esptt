#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "runtime.h"

#if !CONFIG_IDF_TARGET_ESP32S31
#error Select esp32s31 as the target.
#endif

esp_err_t limb_bus_start(void);
esp_err_t web_start(void);

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!ap) abort();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    /* Enable the radio before generating per-boot credentials and instance IDs. */
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    wifi_config_t cfg = {0};
    snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), "MOBan-%02X%02X%02X", mac[3], mac[4], mac[5]);
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.max_connection = 4;
    cfg.ap.channel = 1;
    /* Keep AP disabled until fresh credentials have been installed. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    snprintf((char *)cfg.ap.password, sizeof(cfg.ap.password), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    device_init();
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_netif_ip_info_t ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap, &ip));
    printf("Classroom Wi-Fi: %s  Password: %s  URL: http://" IPSTR "/\n",
           (char *)cfg.ap.ssid, (char *)cfg.ap.password, IP2STR(&ip.ip));
    esp_err_t bus = limb_bus_start();
    if (bus != ESP_OK) ESP_LOGE("board", "Limb bus unavailable: %s", esp_err_to_name(bus));
    ESP_ERROR_CHECK(web_start());
}
