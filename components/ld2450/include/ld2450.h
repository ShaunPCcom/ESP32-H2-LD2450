#pragma once

#include <stdbool.h>
#include "driver/uart.h"

typedef struct {
    uart_port_t uart_num;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    int rx_buf_size;
} ld2450_config_t;

esp_err_t ld2450_init(const ld2450_config_t *cfg);

/**
 * Optional helper: returns true if UART task is running.
 */
bool ld2450_is_running(void);

