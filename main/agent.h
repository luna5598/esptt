#pragma once

#include "esp_err.h"

/* Handles one bounded, independent question at a time. Free *answer on success. */
esp_err_t agent_init(const char *base_url, const char *model, const char *api_key);
esp_err_t agent_ask(const char *question, char **answer);
