#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"

// WROOM32 + LD2450 wiring (matches your working ESPHome YAML):
// uart rx_pin: GPIO16  (ESP32 RX)  <- LD2450 TX
// uart tx_pin: GPIO17  (ESP32 TX)  -> LD2450 RX
#define LD2450_UART_NUM      UART_NUM_1
#define LD2450_UART_TX_GPIO  GPIO_NUM_17
#define LD2450_UART_RX_GPIO  GPIO_NUM_16
#define LD2450_UART_BAUD     256000

