#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t agent_register_temperature(void);
void agent_begin_observations(uint32_t request_id);
bool agent_observations_current(uint32_t request_id);
