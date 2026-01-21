#pragma once

// Board: ESP32-H2-1-N4 DevKit (exact pinout may vary by vendor)
//
// UART pins for LD2450:
// Adjust once hardware arrives (or based on vendor schematic).
#define LD2450_UART_NUM   UART_NUM_1
#define LD2450_UART_TX_GPIO 17
#define LD2450_UART_RX_GPIO 18

// LD2450 baud rate (confirm from your existing ESPHome config)
#define LD2450_UART_BAUD  256000
