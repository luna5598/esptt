set(moban_main "${CMAKE_CURRENT_LIST_DIR}/../../../main")
idf_component_register(SRCS "fixture.c" "${moban_main}/agent_core.c" "${moban_main}/agent_sensors.c"
                       "${moban_main}/agent_web.c"
    INCLUDE_DIRS "${moban_main}"
    PRIV_REQUIRES claw_core claw_cap moban_core limb_reader esp_http_server esp_http_client esp_netif esp_event)
