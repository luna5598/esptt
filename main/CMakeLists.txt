idf_component_register(SRCS "app_main.c" "runtime.c" "limb_bus.c" "web.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES moban_core limb_reader esp_timer esp_hw_support esp_wifi
                  esp_event esp_netif nvs_flash esp_http_server esp_driver_gpio esp_driver_i2c
    EMBED_TXTFILES "index.html")
