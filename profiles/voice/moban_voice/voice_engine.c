#include <stdlib.h>
#include "voice_engine.h"
#include "mo_voice_session.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

struct voice_engine {
    srmodel_list_t *models;
    const esp_afe_sr_iface_t *afe;
    esp_afe_sr_data_t *frontend;
    esp_mn_iface_t *mn;
    model_iface_data_t *recognizer;
    size_t feed_samples;
    int detect_samples;
    bool owns_commands;
    bool listening;
    voice_callback callback;
    void *context;
};

void voice_engine_close(voice_engine *engine)
{
    if (!engine) return;
    if (engine->owns_commands) esp_mn_commands_free();
    if (engine->recognizer) engine->mn->destroy(engine->recognizer);
    if (engine->frontend) engine->afe->destroy(engine->frontend);
    if (engine->models) esp_srmodel_deinit(engine->models);
    free(engine);
}

static esp_err_t configure_commands(voice_engine *engine)
{
    esp_err_t err = esp_mn_commands_alloc(engine->mn, engine->recognizer);
    if (err != ESP_OK) return err;
    engine->owns_commands = true;
    size_t count;
    const mo_spoken_command *commands = mo_spoken_commands(&count);
    for (size_t i = 0; i < count; ++i) {
        err = esp_mn_commands_add(commands[i].action, commands[i].pinyin);
        if (err != ESP_OK) return err;
    }
    return esp_mn_commands_update() ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t voice_engine_open(voice_engine **out, voice_callback callback, void *context)
{
    if (!out || !callback) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    voice_engine *engine = calloc(1, sizeof(*engine));
    if (!engine) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    engine->callback = callback;
    engine->context = context;
    engine->models = esp_srmodel_init("model");
    if (!engine->models) goto fail;
    char *name = esp_srmodel_filter(engine->models, "mn", "_cn");
    if (!name || !esp_srmodel_filter(engine->models, "wn", NULL)) goto fail;
    engine->mn = esp_mn_handle_from_name(name);
    if (!engine->mn) goto fail;

    err = ESP_ERR_NO_MEM;
    afe_config_t *config = afe_config_init("M", engine->models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!config) goto fail;
    config->aec_init = false;
    config->wakenet_init = true;
    engine->afe = esp_afe_handle_from_config(config);
    if (engine->afe) engine->frontend = engine->afe->create_from_config(config);
    afe_config_free(config);
    if (!engine->frontend) goto fail;
    engine->recognizer = engine->mn->create(name, MO_VOICE_WINDOW_MS);
    if (!engine->recognizer) goto fail;
    int feed = engine->afe->get_feed_chunksize(engine->frontend);
    engine->detect_samples = engine->mn->get_samp_chunksize(engine->recognizer);
    err = ESP_ERR_NOT_SUPPORTED;
    if (feed <= 0 || engine->detect_samples <= 0 ||
        engine->afe->get_feed_channel_num(engine->frontend) != 1 ||
        engine->afe->get_samp_rate(engine->frontend) != 16000 ||
        engine->mn->get_samp_rate(engine->recognizer) != 16000 ||
        engine->afe->get_fetch_chunksize(engine->frontend) != engine->detect_samples) goto fail;
    engine->feed_samples = (size_t)feed;
    err = configure_commands(engine);
    if (err != ESP_OK) goto fail;
    *out = engine;
    return ESP_OK;
fail:
    voice_engine_close(engine);
    return err;
}

size_t voice_frame_samples(const voice_engine *engine)
{
    return engine->feed_samples;
}

static void recognize(voice_engine *engine, afe_fetch_result_t *frame)
{
    if (frame->wakeup_state == WAKENET_DETECTED) {
        engine->mn->clean(engine->recognizer);
        engine->listening = true;
        engine->callback(engine->context, VOICE_WAKE, 0);
        return;
    }
    if (!engine->listening) return;
    esp_mn_state_t state = engine->mn->detect(engine->recognizer, frame->data);
    if (state == ESP_MN_STATE_DETECTING) return;
    engine->listening = false;
    if (state == ESP_MN_STATE_DETECTED) {
        esp_mn_results_t *r = engine->mn->get_results(engine->recognizer);
        if (r && r->num > 0) {
            engine->callback(engine->context, VOICE_COMMAND, r->command_id[0]);
            return;
        }
    }
    engine->callback(engine->context, VOICE_TIMEOUT, 0);
}

esp_err_t voice_feed(voice_engine *engine, const int16_t *pcm, size_t samples)
{
    if (!engine || !pcm || samples != engine->feed_samples) return ESP_ERR_INVALID_ARG;
    if (engine->afe->feed(engine->frontend, pcm) <= 0) return ESP_FAIL;
    for (;;) {
        afe_fetch_result_t *frame = engine->afe->fetch_with_delay(engine->frontend, 0);
        if (!frame || frame->ret_value != ESP_OK) break;
        if (!frame->data || frame->data_size != engine->detect_samples * (int)sizeof(int16_t))
            return ESP_ERR_INVALID_SIZE;
        recognize(engine, frame);
    }
    return ESP_OK;
}
