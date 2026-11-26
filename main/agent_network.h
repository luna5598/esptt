#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Missing provisioning leaves the local classroom running in AP mode. */
esp_err_t agent_network_start(void);
bool agent_network_configured(void);
bool agent_network_online(void);
