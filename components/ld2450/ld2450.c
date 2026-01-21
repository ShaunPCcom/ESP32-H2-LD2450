#include "ld2450.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "ld2450";

static TaskHandle_t s_uart_task = NULL;
static uart_port_t s_uart_num = UART_NUM_MAX;

static void ld2450_uart_task(void *arg)
{
    const int buf_len = 256;
    uint8_t buf[buf_len];

    ESP_LOGI(TAG, "UART task started on uart=%d", (int)s_uart_num);

    while (1) {
        // Block up to 1s waiting for data
        int n = uart_read_bytes(s_uart_num, buf, buf_len, pdMS_TO_TICKS(1000));
        if (n > 0) {
            // Print as hex so we can see framing later
            ESP_LOGI(TAG, "RX %d bytes", n);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, n, ESP_LOG_INFO);
        }
    }
}

esp_err_t ld2450_init(const ld2450_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (cfg->uart_num >= UART_NUM_MAX) return ESP_ERR_INVALID_ARG;
    if (cfg->rx_gpio < 0 || cfg->tx_gpio < 0) return ESP_ERR_INVALID_ARG;

    if (s_uart_task) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    s_uart_num = cfg->uart_num;

    uart_config_t uart_cfg = {
        .baud_rate = cfg->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(
        s_uart_num,
        cfg->rx_buf_size > 0 ? cfg->rx_buf_size : 2048,
        0,      // no TX buffer
        0, NULL,
        0
    ));
    ESP_ERROR_CHECK(uart_param_config(s_uart_num, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(s_uart_num, cfg->tx_gpio, cfg->rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Optional: reduce log spam if line noise exists
    ESP_LOGI(TAG, "Configured UART%d: baud=%d tx=%d rx=%d",
             (int)s_uart_num, cfg->baud_rate, cfg->tx_gpio, cfg->rx_gpio);

    BaseType_t ok = xTaskCreate(ld2450_uart_task, "ld2450_uart", 4096, NULL, 10, &s_uart_task);
    if (ok != pdPASS) {
        s_uart_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool ld2450_is_running(void)
{
    return s_uart_task != NULL;
}

