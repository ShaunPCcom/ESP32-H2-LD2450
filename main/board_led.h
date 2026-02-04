// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_LED_OFF = 0,
    BOARD_LED_NOT_JOINED,   /* blinking amber, indefinite */
    BOARD_LED_PAIRING,      /* blinking blue, indefinite */
    BOARD_LED_JOINED,       /* solid green 5s, then OFF */
    BOARD_LED_ERROR,        /* blinking red 5s, then NOT_JOINED */
} board_led_state_t;

void board_led_init(void);
void board_led_set_state(board_led_state_t state);

#ifdef __cplusplus
}
#endif
