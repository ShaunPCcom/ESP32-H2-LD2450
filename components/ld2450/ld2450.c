#include "ld2450.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <inttypes.h>
#include "ld2450_parser.h"
#include "ld2450_zone.h"

#define LD2450_ZONE_COUNT 5
#define ZONE_ID_USER(z) ((z) + 1)

static ld2450_zone_t s_zones[LD2450_ZONE_COUNT] = {
    // Example placeholders (you will replace these later from HA/Zigbee config)
    { .enabled=true, .v={{0,500},{500,500},{500,1500},{0,1500}} },     // Zone0
    { .enabled=false, .v={{0,0},{0,0},{0,0},{0,0}} },                  // Zone1
    { .enabled=false, .v={{0,0},{0,0},{0,0},{0,0}} },                  // Zone2
    { .enabled=false, .v={{0,0},{0,0},{0,0},{0,0}} },                  // Zone3
    { .enabled=false, .v={{0,0},{0,0},{0,0},{0,0}} },                  // Zone4
};

static const char *TAG = "ld2450";

static TaskHandle_t s_uart_task = NULL;
static uart_port_t s_uart_num = UART_NUM_MAX;

static void ld2450_uart_task(void *arg)
{
    const int buf_len = 256;
    uint8_t buf[buf_len];

    ESP_LOGI(TAG, "UART task started on uart=%d", (int)s_uart_num);

    ld2450_parser_t *parser = ld2450_parser_create();
    if (!parser) {
        ESP_LOGE(TAG, "ld2450_parser_create failed");
        vTaskDelete(NULL);
        return;
    }

    ld2450_report_t last = {0};
    bool have_last = false;

    while (1) {
        // Block up to 1s waiting for data
        int n = uart_read_bytes(s_uart_num, buf, buf_len, pdMS_TO_TICKS(1000));
        if (n > 0) {
            if (ld2450_parser_feed(parser, buf, (size_t)n)) {
                const ld2450_report_t *r = ld2450_parser_get_report(parser);

                bool changed = !have_last || memcmp(&last, r, sizeof(*r)) != 0;
            if (changed) {
	        ESP_LOGI(TAG, "report: occupied=%d target_count=%u",
	            (int)r->occupied, (unsigned)r->target_count);
               
                for (unsigned i = 0; i < r->target_count && i < 3; i++) {
	            const ld2450_target_t *t = &r->targets[i];
	            ESP_LOGI(TAG,
                        "  T%u: present=%d x_mm=%d y_mm=%d speed=%d",
                        i, (int)t->present, (int)t->x_mm, (int)t->y_mm, (int)t->speed);
	        }		

	        // ---- Zone evaluation ----
                bool zone_occ[LD2450_ZONE_COUNT] = {0};
                
		for (unsigned zi = 0; zi < LD2450_ZONE_COUNT; zi++) {
                    if (!s_zones[zi].enabled)
                        continue;

        	    for (unsigned ti = 0; ti < r->target_count && ti < 3; ti++) {
            	        const ld2450_target_t *t = &r->targets[ti];
            		if (!t->present)
                	    continue;

            		ld2450_point_t p = { .x_mm = t->x_mm, .y_mm = t->y_mm };
                        if (ld2450_zone_contains_point(&s_zones[zi], p)) {
                	    zone_occ[zi] = true;
	                    break;
            		}
        	    }
    		}

    		// ---- Zone change logging ----
    		static bool last_zone_occ[LD2450_ZONE_COUNT] = {0};

  		for (unsigned zi = 0; zi < LD2450_ZONE_COUNT; zi++) {
		    if (zone_occ[zi] != last_zone_occ[zi]) {
            		ESP_LOGI(TAG, "zone%u: %s", ZONE_ID_USER(zi), zone_occ[zi] ? "occupied" : "clear");
            		last_zone_occ[zi] = zone_occ[zi];
        	    }
     		}

	        last = *r;        // struct copy
	        have_last = true;
		}
            }
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

