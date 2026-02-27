// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* C wrappers for C++ BoardLed API (called from zigbee_app.c) */
void board_led_set_state_off(void);
void board_led_set_state_not_joined(void);
void board_led_set_state_pairing(void);
void board_led_set_state_joined(void);
void board_led_set_state_error(void);

#ifdef __cplusplus
}
#endif
