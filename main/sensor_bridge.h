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

#ifdef __cplusplus
}
#endif
