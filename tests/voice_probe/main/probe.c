#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "voice_engine.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void log_event(void *ctx, voice_event event, int id)
{
    (void)ctx;
    ESP_LOGI("probe", "event=%d command=%d", (int)event, id);
}

void app_main(void)
{
    voice_engine *engine = NULL;
    ESP_ERROR_CHECK(voice_engine_open(&engine, log_event, NULL));
    size_t frame = voice_frame_samples(engine);
    int16_t *pcm = calloc(frame, sizeof(*pcm));
    if (!pcm) { voice_engine_close(engine); ESP_LOGE("probe", "Out of PCM memory"); return; }
    const uint8_t *input = NULL;
    size_t length = 0;
#ifdef MOBAN_PROBE_HAS_PCM
    extern const uint8_t start[] asm("_binary_probe_pcm_start");
    extern const uint8_t end[] asm("_binary_probe_pcm_end");
    input = start;
    length = (size_t)((uintptr_t)end - (uintptr_t)start);
#endif
    if (length % sizeof(*pcm)) {
        ESP_LOGE("probe", "Expected 16-bit little-endian mono samples");
    } else {
        size_t remaining = length / sizeof(*pcm);
        /* Real-time pacing, then silence to drain queued frames and recognize the tail. */
        size_t trailing = 6 * 16000u;
        TickType_t deadline = xTaskGetTickCount();
        uint32_t tick_remainder = 0;
        while (remaining || trailing) {
            size_t n = remaining < frame ? remaining : frame;
            memset(pcm, 0, frame * sizeof(*pcm));
            if (n) {
                memcpy(pcm, input, n * sizeof(*pcm));
                input += n * sizeof(*pcm);
                remaining -= n;
            } else {
                trailing -= trailing < frame ? trailing : frame;
            }
            esp_err_t err = voice_feed(engine, pcm, frame);
            if (err != ESP_OK) { ESP_LOGE("probe", "PCM feed: %s", esp_err_to_name(err)); break; }
            tick_remainder += (uint32_t)frame * configTICK_RATE_HZ;
            TickType_t ticks = tick_remainder / 16000;
            tick_remainder %= 16000;
            if (ticks) xTaskDelayUntil(&deadline, ticks);
        }
    }
    ESP_LOGI("probe", "PSRAM low-water=%u bytes, largest internal DMA block=%u bytes",
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    free(pcm);
    voice_engine_close(engine);
}
