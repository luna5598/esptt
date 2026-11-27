#pragma once

#include "voice_engine.h"

/* Call once from app_main after board setup. The callback runs on the audio task. */
esp_err_t mic_input_start(voice_callback callback, void *context);
