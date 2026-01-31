#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"

/* Zigbee SDK */
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

static const char *TAG = "zigbee";

/* Some releases don’t define this macro; keep build moving */
#ifndef ESP_ZB_HA_OCCUPANCY_SENSOR_DEVICE_ID
#define ESP_ZB_HA_OCCUPANCY_SENSOR_DEVICE_ID 0x0107
#endif

/* Newer esp-zigbee-lib uses p_app_signal rather than signal_struct->signal */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct ? signal_struct->p_app_signal : NULL;
    esp_zb_app_signal_type_t sig = p_sg_p ? *p_sg_p : 0;

    esp_err_t status = signal_struct ? signal_struct->esp_err_status : ESP_OK;

    ESP_LOGI(TAG, "ZB signal=0x%08" PRIx32 " status=%s",
             (uint32_t)sig, esp_err_to_name(status));
}

/* For now we do *no* endpoint/cluster work.
   Goal = stack boots + commissioning works. */
static void zigbee_register_endpoints(void)
{
    /* TODO: add endpoint + occupancy cluster once stack bring-up is stable */
}

static void zigbee_task(void *pv)
{
    (void)pv;

    /* Avoid ESP_ZB_DEFAULT_* macros to prevent Werror/missing-braces issues */
    esp_zb_platform_config_t platform_cfg = {0};
    platform_cfg.radio_config.radio_mode = ZB_RADIO_MODE_NATIVE;
    platform_cfg.host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE;

    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));

    /* Minimal init: end device for initial bring-up */
    esp_zb_cfg_t zb_cfg = {0};
    zb_cfg.esp_zb_role = (
    #if CONFIG_LD2450_ZB_ROUTER
        ESP_ZB_DEVICE_TYPE_ROUTER
    #else
        ESP_ZB_DEVICE_TYPE_ED
    #endif
    );

    esp_zb_init(&zb_cfg);

    zigbee_register_endpoints();

    /* Start Zigbee stack (commissioning handled via signal handler flow) */
    esp_zb_start(false);
    
    while (true) {
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            esp_zb_main_loop_iteration();
        #pragma GCC diagnostic pop

    vTaskDelay(pdMS_TO_TICKS(10));
}

}

void zigbee_app_start(void)
{
    ESP_LOGI(TAG, "Starting Zigbee task...");
    xTaskCreate(zigbee_task, "zb_task", 8192, NULL, 5, NULL);
}

/* Stubs for later integration */
void zigbee_app_set_occupied(bool occupied) { (void)occupied; }
void zigbee_app_set_target_count(uint8_t count) { (void)count; }
