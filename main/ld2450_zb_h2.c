#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "board_config.h"
#include "ld2450.h"
#include "ld2450_cli.h"

static const char *TAG = "ld2450_hwtest";

void app_main(void)
{
    // NVS is commonly required by IDF subsystems; keep it initialized even if
    // we don't use it directly yet.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    // Minimal LD2450 init; the component owns UART/task setup internally.
    ld2450_config_t cfg = {
        .uart_num     = LD2450_UART_NUM,
        .tx_gpio      = LD2450_UART_TX_GPIO,
        .rx_gpio      = LD2450_UART_RX_GPIO,
        .baud_rate    = LD2450_UART_BAUD,
        .rx_buf_size  = 2048,
    };

    ESP_ERROR_CHECK(ld2450_init(&cfg));
    ld2450_cli_start();


    ESP_LOGI(TAG, "LD2450 initialized. running=%d", ld2450_is_running());

// app_main has nothing else to do; sensor runs in its own task
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

