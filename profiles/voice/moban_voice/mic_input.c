#include <stdbool.h>
#include <stdlib.h>
#include "mic_input.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

typedef struct {
    i2s_chan_handle_t rx;
    voice_callback callback;
    void *context;
} mic_input;

static bool valid_pins(int bclk, int ws, int din)
{
    if (bclk < 0 || ws < 0 || din < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(bclk) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(ws) || !GPIO_IS_VALID_GPIO(din) ||
        bclk == ws || bclk == din || ws == din) return false;
#if CONFIG_MOBAN_LIMB_BUS
    const int limb[] = {CONFIG_MOBAN_SDA, CONFIG_MOBAN_SCL, CONFIG_MOBAN_MUX_RESET,
        CONFIG_MOBAN_SLOT_0_EN, CONFIG_MOBAN_SLOT_1_EN, CONFIG_MOBAN_SLOT_2_EN, CONFIG_MOBAN_SLOT_3_EN};
    for (size_t i = 0; i < sizeof(limb) / sizeof(limb[0]); ++i) {
        if (limb[i] == bclk || limb[i] == ws || limb[i] == din) return false;
    }
#endif
    return true;
}

static void mic_task(void *opaque)
{
    mic_input *mic = opaque;
    voice_engine *engine = NULL;
    int16_t *pcm = NULL;
    bool enabled = false;
    esp_err_t err = voice_engine_open(&engine, mic->callback, mic->context);
    if (err != ESP_OK) goto done;
    size_t samples = voice_frame_samples(engine);
    size_t bytes = samples * sizeof(*pcm);
    pcm = malloc(bytes);
    if (!pcm) { err = ESP_ERR_NO_MEM; goto done; }
    err = i2s_channel_enable(mic->rx);
    if (err != ESP_OK) goto done;
    enabled = true;
    size_t filled = 0;
    while (err == ESP_OK) {
        size_t got = 0;
        err = i2s_channel_read(mic->rx, (uint8_t *)pcm + filled, bytes - filled, &got, 1000);
        if (err != ESP_OK) break;
        if (!got) { err = ESP_ERR_TIMEOUT; break; }
        filled += got;
        if (filled == bytes) {
            err = voice_feed(engine, pcm, samples);
            filled = 0;
        }
    }

done:
    ESP_LOGE("voice", "Microphone stopped: %s", esp_err_to_name(err));
    if (enabled) i2s_channel_disable(mic->rx);
    i2s_del_channel(mic->rx);
    free(pcm);
    voice_engine_close(engine);
    free(mic);
    vTaskDelete(NULL);
}

esp_err_t mic_input_start(voice_callback callback, void *context)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    int bclk = CONFIG_MOBAN_VOICE_BCLK, ws = CONFIG_MOBAN_VOICE_WS, din = CONFIG_MOBAN_VOICE_DIN;
    if (bclk == -1 && ws == -1 && din == -1) return ESP_ERR_NOT_FOUND;
    if (!valid_pins(bclk, ws, din)) return ESP_ERR_INVALID_ARG;
    mic_input *mic = calloc(1, sizeof(*mic));
    if (!mic) return ESP_ERR_NO_MEM;
    mic->callback = callback;
    mic->context = context;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan, NULL, &mic->rx);
    if (err != ESP_OK) goto fail;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = bclk, .ws = ws, .dout = I2S_GPIO_UNUSED, .din = din}
    };
    config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    err = i2s_channel_init_std_mode(mic->rx, &config);
    if (err != ESP_OK) goto fail;
    if (xTaskCreate(mic_task, "voice_mic", 8192, mic, 5, NULL) == pdPASS) return ESP_OK;
    err = ESP_ERR_NO_MEM;
fail:
    if (mic->rx) i2s_del_channel(mic->rx);
    free(mic);
    return err;
}
