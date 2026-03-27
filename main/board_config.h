// SPDX-License-Identifier: MIT
#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"

// nanoESP32-C6 + LD2450 wiring:
// GPIO10: ESP32 TX -> LD2450 RX (commands to sensor)
// GPIO11: ESP32 RX <- LD2450 TX (data from sensor)
// (GPIO12 unavailable on C6 - USB D-, GPIO22 - SPI flash)
#define LD2450_UART_NUM      UART_NUM_1
#define LD2450_UART_TX_GPIO  GPIO_NUM_10
#define LD2450_UART_RX_GPIO  GPIO_NUM_11
#define LD2450_UART_BAUD     256000

// BOOT button (active-low, internal pull-up)
#define BOARD_BUTTON_GPIO              GPIO_NUM_9
#define BOARD_BUTTON_HOLD_ZIGBEE_MS    3000   /* Zigbee network reset */
#define BOARD_BUTTON_HOLD_FULL_MS      10000  /* Full factory reset (Zigbee + NVS) */

