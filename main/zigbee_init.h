// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start Zigbee stack
 *
 * Creates FreeRTOS task that initializes platform, registers endpoints,
 * and starts the Zigbee stack main loop.
 */
void zigbee_init(void);

#ifdef __cplusplus
}
#endif
