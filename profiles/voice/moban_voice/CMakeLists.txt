idf_component_register(SRCS "voice_engine.c" "mic_input.c" INCLUDE_DIRS "include"
                       PRIV_REQUIRES moban_core esp_driver_gpio esp_driver_i2s)
