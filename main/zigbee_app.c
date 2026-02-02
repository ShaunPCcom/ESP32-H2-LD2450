#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"

/* Zigbee SDK */
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

/* Project */
#include "board_led.h"
#include "ld2450.h"

static const char *TAG = "zigbee";

/* ---- Endpoint / cluster constants ---- */
#define OCCUPANCY_ENDPOINT          1
#define OCCUPANCY_SENSOR_DEVICE_ID  0x0107  /* HA Occupancy Sensor */

/* Reporting intervals (seconds) */
#define OCCUPANCY_REPORT_MIN_INTERVAL   0
#define OCCUPANCY_REPORT_MAX_INTERVAL   300

/* Basic cluster identity (ZCL char-string: length byte + chars) */
#define MANUFACTURER_NAME   "\x09" "LD2450-ZB"
#define MODEL_IDENTIFIER    "\x08" "LD2450-H2"
#define SW_BUILD_ID         "\x05" "1.0.0"

/* Scheduler alarm param values */
#define ALARM_PARAM_POLL    0

/* Sensor poll interval (ms) */
#define SENSOR_POLL_INTERVAL_MS  500

/* ---- State ---- */
static bool s_last_occupied = false;
static bool s_network_joined = false;

/* ---- Forward declarations ---- */
static void sensor_poll_cb(uint8_t param);
static void occupancy_bridge_start(void);
static void configure_reporting(void);

/* ------------------------------------------------------------------ */
/*  Sensor-to-Zigbee bridge                                           */
/* ------------------------------------------------------------------ */

static void occupancy_bridge_start(void)
{
    ESP_LOGI(TAG, "Starting occupancy bridge (poll every %d ms)", SENSOR_POLL_INTERVAL_MS);
    configure_reporting();
    esp_zb_scheduler_alarm(sensor_poll_cb, ALARM_PARAM_POLL, SENSOR_POLL_INTERVAL_MS);
}

static void sensor_poll_cb(uint8_t param)
{
    (void)param;

    /* Reschedule next poll */
    esp_zb_scheduler_alarm(sensor_poll_cb, ALARM_PARAM_POLL, SENSOR_POLL_INTERVAL_MS);

    if (!s_network_joined) return;

    ld2450_state_t state;
    if (ld2450_get_state(&state) != ESP_OK) return;

    bool occupied = state.occupied_global;

    if (occupied != s_last_occupied) {
        uint8_t val = occupied ? 1 : 0;
        esp_zb_zcl_set_attribute_val(
            OCCUPANCY_ENDPOINT,
            ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
            &val, false);

        s_last_occupied = occupied;
        ESP_LOGI(TAG, "Occupancy -> %s", occupied ? "OCCUPIED" : "UNOCCUPIED");
    }
}

static void configure_reporting(void)
{
    esp_zb_zcl_reporting_info_t rpt = {0};
    rpt.direction   = ESP_ZB_ZCL_REPORT_DIRECTION_SEND;
    rpt.ep          = OCCUPANCY_ENDPOINT;
    rpt.cluster_id  = ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING;
    rpt.cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
    rpt.attr_id     = ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID;
    rpt.u.send_info.min_interval     = OCCUPANCY_REPORT_MIN_INTERVAL;
    rpt.u.send_info.max_interval     = OCCUPANCY_REPORT_MAX_INTERVAL;
    rpt.u.send_info.def_min_interval = OCCUPANCY_REPORT_MIN_INTERVAL;
    rpt.u.send_info.def_max_interval = OCCUPANCY_REPORT_MAX_INTERVAL;
    rpt.u.send_info.delta.u8         = 0;
    rpt.dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    rpt.manuf_code     = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

    esp_err_t err = esp_zb_zcl_update_reporting_info(&rpt);
    ESP_LOGI(TAG, "Reporting config: %s (min=%d max=%d)",
             esp_err_to_name(err),
             OCCUPANCY_REPORT_MIN_INTERVAL, OCCUPANCY_REPORT_MAX_INTERVAL);
}

/* ------------------------------------------------------------------ */
/*  Signal handler                                                     */
/* ------------------------------------------------------------------ */

static void steering_retry_cb(uint8_t param)
{
    board_led_set_state(BOARD_LED_PAIRING);
    esp_zb_bdb_start_top_level_commissioning(param);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct ? signal_struct->p_app_signal : NULL;
    esp_zb_app_signal_type_t sig = p_sg_p ? *p_sg_p : 0;
    esp_err_t status = signal_struct ? signal_struct->esp_err_status : ESP_OK;

    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Stack initialized, starting steering");
        board_led_set_state(BOARD_LED_PAIRING);
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_NETWORK_STEERING);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (status == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Factory new device, starting steering");
                board_led_set_state(BOARD_LED_PAIRING);
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted, already commissioned");
                board_led_set_state(BOARD_LED_JOINED);
                s_network_joined = true;
                occupancy_bridge_start();
            }
        } else {
            ESP_LOGW(TAG, "Device start/reboot failed: %s", esp_err_to_name(status));
            board_led_set_state(BOARD_LED_ERROR);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (status == ESP_OK) {
            ESP_LOGI(TAG, "Joined network successfully");
            board_led_set_state(BOARD_LED_JOINED);
            s_network_joined = true;
            occupancy_bridge_start();
        } else {
            ESP_LOGW(TAG, "Steering failed (%s), retrying...", esp_err_to_name(status));
            board_led_set_state(BOARD_LED_NOT_JOINED);
            esp_zb_scheduler_alarm(steering_retry_cb,
                                   ESP_ZB_BDB_NETWORK_STEERING, 1000);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "Left network");
        s_network_joined = false;
        board_led_set_state(BOARD_LED_NOT_JOINED);
        esp_zb_scheduler_alarm(steering_retry_cb,
                               ESP_ZB_BDB_NETWORK_STEERING, 1000);
        break;

    case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
        break;

    default:
        ESP_LOGI(TAG, "ZB signal=0x%08" PRIx32 " status=%s",
                 (uint32_t)sig, esp_err_to_name(status));
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Endpoint registration                                              */
/* ------------------------------------------------------------------ */

static void zigbee_register_endpoints(void)
{
    /* --- Basic cluster (server) --- */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic_cluster,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic_cluster,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)MODEL_IDENTIFIER);
    esp_zb_basic_cluster_add_attr(basic_cluster,
        ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, (void *)SW_BUILD_ID);

    /* --- Identify cluster (server) --- */
    esp_zb_identify_cluster_cfg_t identify_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *identify_cluster = esp_zb_identify_cluster_create(&identify_cfg);

    /* --- Occupancy Sensing cluster (server) --- */
    esp_zb_occupancy_sensing_cluster_cfg_t occ_cfg = {
        .occupancy         = 0,
        .sensor_type       = ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_RESERVED,
        .sensor_type_bitmap = (1 << 2),
    };
    esp_zb_attribute_list_t *occ_cluster = esp_zb_occupancy_sensing_cluster_create(&occ_cfg);

    /* --- Assemble cluster list --- */
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(
        cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(
        cluster_list, identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_occupancy_sensing_cluster(
        cluster_list, occ_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* --- Register endpoint --- */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint       = OCCUPANCY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id  = OCCUPANCY_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg));
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    ESP_LOGI(TAG, "Endpoint %d registered: Occupancy Sensor (0x%04x)",
             OCCUPANCY_ENDPOINT, OCCUPANCY_SENSOR_DEVICE_ID);
}

/* ------------------------------------------------------------------ */
/*  Zigbee task                                                        */
/* ------------------------------------------------------------------ */

static void zigbee_task(void *pv)
{
    (void)pv;

    esp_zb_platform_config_t platform_cfg = {0};
    platform_cfg.radio_config.radio_mode = ZB_RADIO_MODE_NATIVE;
    platform_cfg.host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE;

    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));

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
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void zigbee_app_start(void)
{
    ESP_LOGI(TAG, "Starting Zigbee task...");
    xTaskCreate(zigbee_task, "zb_task", 8192, NULL, 5, NULL);
}

/* External API (callable from any task with lock) */
void zigbee_app_set_occupied(bool occupied)
{
    if (!s_network_joined) return;

    if (esp_zb_lock_acquire(pdMS_TO_TICKS(100))) {
        uint8_t val = occupied ? 1 : 0;
        esp_zb_zcl_set_attribute_val(
            OCCUPANCY_ENDPOINT,
            ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
            &val, false);
        esp_zb_lock_release();
    }
}

void zigbee_app_set_target_count(uint8_t count)
{
    /* Phase 2: expose via custom attribute or analog value */
    (void)count;
}
