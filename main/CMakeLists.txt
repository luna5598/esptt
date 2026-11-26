idf_component_register(SRCS "app_main.c" "runtime.c" "slot_observation.c" "limb_bus.c" "web.c" "agent_core.c" "agent_temperature.c"
                       "agent_network.c" "agent_web.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES claw_core claw_cap esp_timer esp-tls moban_core limb_reader lwip esp_hw_support esp_wifi
                  esp_event esp_netif nvs_flash esp_http_server esp_driver_gpio esp_driver_i2c
    EMBED_TXTFILES "index.html")
