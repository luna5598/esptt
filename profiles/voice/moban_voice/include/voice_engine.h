#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct voice_engine voice_engine;
typedef enum { VOICE_WAKE, VOICE_COMMAND, VOICE_TIMEOUT } voice_event;
typedef void (*voice_callback)(void *context, voice_event event, int command_id);

/* One owner task per device. Input is mono signed PCM, 16 kHz, in complete frames. */
esp_err_t voice_engine_open(voice_engine **out, voice_callback callback, void *context);
size_t voice_frame_samples(const voice_engine *engine);
esp_err_t voice_feed(voice_engine *engine, const int16_t *pcm, size_t samples);
void voice_engine_close(voice_engine *engine);
