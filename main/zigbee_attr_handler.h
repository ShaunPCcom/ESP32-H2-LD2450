// SPDX-License-Identifier: MIT
#pragma once

#include "esp_err.h"
#include "esp_zigbee_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zigbee action handler callback
 *
 * Routes callbacks to appropriate handlers (OTA, attribute writes, etc.)
 * Registered with esp_zb_core_action_handler_register().
 *
 * @param callback_id Type of callback
 * @param message Callback-specific message data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message);

#ifdef __cplusplus
}
#endif
