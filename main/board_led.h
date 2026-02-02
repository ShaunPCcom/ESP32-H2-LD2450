#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_LED_OFF = 0,
    BOARD_LED_BOOT,
    BOARD_LED_PAIRING,
    BOARD_LED_JOINED,
    BOARD_LED_ERROR,
} board_led_state_t;

void board_led_init(void);
void board_led_set_state(board_led_state_t state);

#ifdef __cplusplus
}
#endif

