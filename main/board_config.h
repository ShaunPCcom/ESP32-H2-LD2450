// SPDX-License-Identifier: MIT
#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"
#include "sdkconfig.h"

// LD2450 UART wiring - GPIO selection depends on target:
//   ESP32-H2: GPIO12 TX -> LD2450 RX, GPIO22 RX <- LD2450 TX
//   ESP32-C6: GPIO10 TX -> LD2450 RX, GPIO11 RX <- LD2450 TX
//   (C6: GPIO12=USB D-, GPIO22=SPI flash, so 10/11 used instead)
#define LD2450_UART_NUM      UART_NUM_1
#if CONFIG_IDF_TARGET_ESP32C6
#  define LD2450_UART_TX_GPIO  GPIO_NUM_10
#  define LD2450_UART_RX_GPIO  GPIO_NUM_11
#elif CONFIG_IDF_TARGET_ESP32H2
#  define LD2450_UART_TX_GPIO  GPIO_NUM_12
#  define LD2450_UART_RX_GPIO  GPIO_NUM_22
#else
#  error "Unsupported target - add GPIO definitions for this target"
#endif
#define LD2450_UART_BAUD     256000

// BOOT button (active-low, internal pull-up)
#define BOARD_BUTTON_GPIO              GPIO_NUM_9
#define BOARD_BUTTON_HOLD_ZIGBEE_MS    3000   /* Zigbee network reset */
#define BOARD_BUTTON_HOLD_FULL_MS      10000  /* Full factory reset (Zigbee + NVS) */

