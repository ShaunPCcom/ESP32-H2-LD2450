// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start sensor bridge polling and reporting
 *
 * Called by signal handler after network join.
 * Configures reporting and starts periodic sensor polling.
 */
void sensor_bridge_start(void);

/**
 * @brief Mark device config as dirty so the next poll cycle pushes all
 *        config ZCL attributes to the attribute table.
 *
 * Call this whenever config is written from a non-Zigbee source (e.g.
 * the web UI REST API) so Z2M and HA see the updated values.
 * Safe to call from any task/context.
 */
void sensor_bridge_mark_config_dirty(void);

#ifdef __cplusplus
}
#endif
