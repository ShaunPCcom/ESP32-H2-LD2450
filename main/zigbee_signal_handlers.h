// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include "esp_zigbee_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zigbee application signal handler
 *
 * Called by Zigbee stack for network lifecycle events.
 * Handles steering, join, leave, factory reset signals.
 *
 * @param signal_struct Signal data from stack
 */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct);

/**
 * @brief Check if device has joined a Zigbee network
 *
 * @return true if joined, false otherwise
 */
bool zigbee_is_network_joined(void);

/**
 * @brief Zigbee network reset only
 *
 * Leaves network and erases Zigbee network data, but keeps NVS config.
 * Device restarts after reset.
 */
void zigbee_factory_reset(void);

/**
 * @brief Full factory reset
 *
 * Erases both Zigbee network data AND NVS application config.
 * Device restarts with default settings.
 */
void zigbee_full_factory_reset(void);

#ifdef __cplusplus
}
#endif
