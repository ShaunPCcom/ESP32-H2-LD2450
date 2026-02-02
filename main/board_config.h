#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"

// ESP32-H2 + LD2450 wiring:
// uart rx_pin: GPIO6   (ESP32 RX)  <- LD2450 TX
// uart tx_pin: GPIO22  (ESP32 TX)  -> LD2450 RX
// (GPIO9 avoided for UART - shared with BOOT button)
#define LD2450_UART_NUM      UART_NUM_1
#define LD2450_UART_TX_GPIO  GPIO_NUM_6
#define LD2450_UART_RX_GPIO  GPIO_NUM_22
#define LD2450_UART_BAUD     256000

// BOOT button (active-low, internal pull-up)
#define BOARD_BUTTON_GPIO    GPIO_NUM_9
#define BOARD_BUTTON_HOLD_MS 3000

