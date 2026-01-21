#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "ld2450.h"

static const char *TAG = "app";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // TODO: Set these to match your ESP32-H2 board wiring.
    // For now, these are placeholders that compile.
    ld2450_config_t cfg = {
        .uart_num = UART_NUM_1,
        .tx_gpio = 17,
        .rx_gpio = 18,
        .baud_rate = 256000,   // confirm LD2450 baud when hardware arrives
        .rx_buf_size = 2048
    };

    ESP_ERROR_CHECK(ld2450_init(&cfg));
    ESP_LOGI(TAG, "LD2450 component initialized: running=%d", ld2450_is_running());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive");
    }
}

