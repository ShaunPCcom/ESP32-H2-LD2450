// SPDX-License-Identifier: MIT
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "nvs.h"

/* Zigbee SDK */
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl/esp_zigbee_zcl_common.h"

/* Project */
#include "board_config.h"
#include "board_led.h"
#include "ld2450.h"
#include "ld2450_cmd.h"
#include "nvs_config.h"
#include "zigbee_defs.h"
#include "zigbee_ota.h"

static const char *TAG = "zigbee";

/* Sensor poll interval (ms) - LD2450 outputs at 10Hz (100ms) */
#define SENSOR_POLL_INTERVAL_MS  100

/* Reporting intervals (seconds) */
#define REPORT_MIN_INTERVAL   0
#define REPORT_MAX_INTERVAL   300

/* Scheduler alarm param */
#define ALARM_PARAM_POLL    0

/* ---- State tracking for change detection ---- */
static bool s_network_joined = false;
static bool s_last_occupied = false;
static bool s_last_zone_occ[5] = {false};
static uint8_t s_last_target_count = 0;
static char s_last_coords[64] = {0};

/* ---- Cooldown tracking (per endpoint: 0=main, 1-5=zones) ---- */
static uint32_t s_last_report_time[6] = {0};
static bool s_pending_clear[6] = {false};      /* tracking pending Clear reports */
static uint32_t s_clear_start_time[6] = {0};  /* when Clear was first detected */

/* ---- Forward declarations ---- */
static void sensor_poll_cb(uint8_t param);
static void bridge_start(void);
static void configure_all_reporting(void);
static esp_err_t action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message);

/* ================================================================== */
/*  Helper: create cluster lists for endpoints                         */
/* ================================================================== */

static esp_zb_cluster_list_t *create_main_ep_clusters(void)
{
    /* Basic cluster */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)ZB_MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)ZB_MODEL_IDENTIFIER);
    esp_zb_basic_cluster_add_attr(basic,
        ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, (void *)ZB_SW_BUILD_ID);

    /* Identify cluster */
    esp_zb_identify_cluster_cfg_t identify_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&identify_cfg);

    /* Occupancy Sensing cluster */
    esp_zb_occupancy_sensing_cluster_cfg_t occ_cfg = {
        .occupancy         = 0,
        .sensor_type       = ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_RESERVED,
        .sensor_type_bitmap = (1 << 2),
    };
    esp_zb_attribute_list_t *occ = esp_zb_occupancy_sensing_cluster_create(&occ_cfg);

    /* Custom cluster 0xFC00 - LD2450 config + sensor data */
    esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(ZB_CLUSTER_LD2450_CONFIG);

    /* Load current config for initial values */
    nvs_config_t cfg;
    nvs_config_get(&cfg);

    uint8_t zero_u8 = 0;
    uint16_t init_dist = cfg.max_distance_mm;
    uint8_t init_al = cfg.angle_left_deg;
    uint8_t init_ar = cfg.angle_right_deg;
    uint8_t init_mode = cfg.tracking_mode;
    uint8_t init_coords = cfg.publish_coords;
    uint16_t init_cooldown = cfg.occupancy_cooldown_sec[0];

    /* ZCL char-string: first byte = length, rest = chars. Empty string = "\x00" */
    char empty_str[2] = {0x00, 0x00};

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_TARGET_COUNT,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        &zero_u8);

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_TARGET_COORDS,
        ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        empty_str);

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_MAX_DISTANCE,
        ESP_ZB_ZCL_ATTR_TYPE_U16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &init_dist);

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_ANGLE_LEFT,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &init_al);

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_ANGLE_RIGHT,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &init_ar);

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_TRACKING_MODE,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &init_mode);

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_COORD_PUBLISHING,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &init_coords);

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_OCCUPANCY_COOLDOWN,
        ESP_ZB_ZCL_ATTR_TYPE_U16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &init_cooldown);

    esp_zb_custom_cluster_add_custom_attr(custom, ZB_ATTR_RESTART,
        ESP_ZB_ZCL_ATTR_TYPE_U8,
        ESP_ZB_ZCL_ATTR_ACCESS_WRITE_ONLY,
        &zero_u8);

    /* Assemble cluster list */
    esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cl, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_occupancy_sensing_cluster(cl, occ, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(cl, custom, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Add OTA cluster */
    zigbee_ota_config_t ota_cfg = ZIGBEE_OTA_CONFIG_DEFAULT();
    ota_cfg.manufacturer_code = 0x131B;  /* Espressif */
    ota_cfg.image_type = 0x0001;         /* LD2450 application */
    ota_cfg.current_file_version = 0x00010002;  /* v1.0.0.2 */
    ota_cfg.hw_version = 1;
    ota_cfg.query_interval_minutes = 1440;  /* Check every 24 hours */
    ESP_ERROR_CHECK(zigbee_ota_init(cl, ZB_EP_MAIN, &ota_cfg));

    return cl;
}

static esp_zb_cluster_list_t *create_zone_ep_clusters(uint8_t zone_idx)
{
    /* Basic cluster (minimal for zone endpoints) */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);

    /* Identify cluster */
    esp_zb_identify_cluster_cfg_t identify_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&identify_cfg);

    /* Occupancy Sensing cluster */
    esp_zb_occupancy_sensing_cluster_cfg_t occ_cfg = {
        .occupancy         = 0,
        .sensor_type       = ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_RESERVED,
        .sensor_type_bitmap = (1 << 2),
    };
    esp_zb_attribute_list_t *occ = esp_zb_occupancy_sensing_cluster_create(&occ_cfg);

    /* Custom cluster 0xFC01 - Zone vertex config */
    esp_zb_attribute_list_t *zone_custom = esp_zb_zcl_attr_list_create(ZB_CLUSTER_LD2450_ZONE);

    /* Load zone vertices from NVS */
    nvs_config_t cfg;
    nvs_config_get(&cfg);
    const ld2450_zone_t *z = &cfg.zones[zone_idx];

    for (int v = 0; v < ZB_ATTR_ZONE_VERTEX_COUNT; v++) {
        int16_t val;
        int vi = v / 2;  /* vertex index 0-3 */
        if (v % 2 == 0) {
            val = z->v[vi].x_mm;
        } else {
            val = z->v[vi].y_mm;
        }
        esp_zb_custom_cluster_add_custom_attr(zone_custom, (uint16_t)v,
            ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
            &val);
    }

    /* Add occupancy cooldown attribute for this zone */
    uint16_t zone_cooldown = cfg.occupancy_cooldown_sec[zone_idx + 1];
    esp_zb_custom_cluster_add_custom_attr(zone_custom, ZB_ATTR_OCCUPANCY_COOLDOWN,
        ESP_ZB_ZCL_ATTR_TYPE_U16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
        &zone_cooldown);

    /* Assemble cluster list */
    esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cl, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_occupancy_sensing_cluster(cl, occ, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(cl, zone_custom, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    return cl;
}

/* ================================================================== */
/*  Endpoint registration (6 endpoints)                                */
/* ================================================================== */

static void zigbee_register_endpoints(void)
{
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

    /* EP 1: Main device */
    esp_zb_endpoint_config_t main_ep_cfg = {
        .endpoint       = ZB_EP_MAIN,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id  = ZB_DEVICE_ID_OCCUPANCY_SENSOR,
        .app_device_version = 0,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, create_main_ep_clusters(), main_ep_cfg));

    /* EPs 2-6: Zone occupancy */
    for (int i = 0; i < ZB_EP_ZONE_COUNT; i++) {
        esp_zb_endpoint_config_t zone_ep_cfg = {
            .endpoint       = ZB_EP_ZONE(i),
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id  = ZB_DEVICE_ID_OCCUPANCY_SENSOR,
            .app_device_version = 0,
        };
        ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, create_zone_ep_clusters((uint8_t)i), zone_ep_cfg));
    }

    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));
    ESP_LOGI(TAG, "Registered %d endpoints (EP %d main + EP %d-%d zones)",
             1 + ZB_EP_ZONE_COUNT, ZB_EP_MAIN,
             ZB_EP_ZONE(0), ZB_EP_ZONE(ZB_EP_ZONE_COUNT - 1));
}

/* ================================================================== */
/*  Action handler (writable attribute callbacks)                      */
/* ================================================================== */

static esp_err_t handle_set_attr_value(const esp_zb_zcl_set_attr_value_message_t *msg)
{
    uint8_t ep = msg->info.dst_endpoint;
    uint16_t cluster = msg->info.cluster;
    uint16_t attr_id = msg->attribute.id;
    void *val = msg->attribute.data.value;

    ESP_LOGI(TAG, "Write: ep=%u cluster=0x%04X attr=0x%04X", ep, cluster, attr_id);

    /* EP 1 custom cluster */
    if (ep == ZB_EP_MAIN && cluster == ZB_CLUSTER_LD2450_CONFIG) {
        switch (attr_id) {
        case ZB_ATTR_MAX_DISTANCE: {
            uint16_t dist = *(uint16_t *)val;
            esp_err_t err = nvs_config_save_max_distance(dist);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save max_distance to NVS: %s", esp_err_to_name(err));
            }
            nvs_config_t cfg;
            nvs_config_get(&cfg);
            ld2450_cmd_apply_distance_angle(cfg.max_distance_mm, cfg.angle_left_deg, cfg.angle_right_deg);
            ESP_LOGI(TAG, "Max distance -> %u mm%s", dist, (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
            return ESP_OK;
        }
        case ZB_ATTR_ANGLE_LEFT: {
            uint8_t deg = *(uint8_t *)val;
            esp_err_t err = nvs_config_save_angle_left(deg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save angle_left to NVS: %s", esp_err_to_name(err));
            }
            nvs_config_t cfg;
            nvs_config_get(&cfg);
            ld2450_cmd_apply_distance_angle(cfg.max_distance_mm, cfg.angle_left_deg, cfg.angle_right_deg);
            ESP_LOGI(TAG, "Angle left -> %u%s", deg, (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
            return ESP_OK;
        }
        case ZB_ATTR_ANGLE_RIGHT: {
            uint8_t deg = *(uint8_t *)val;
            esp_err_t err = nvs_config_save_angle_right(deg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save angle_right to NVS: %s", esp_err_to_name(err));
            }
            nvs_config_t cfg;
            nvs_config_get(&cfg);
            ld2450_cmd_apply_distance_angle(cfg.max_distance_mm, cfg.angle_left_deg, cfg.angle_right_deg);
            ESP_LOGI(TAG, "Angle right -> %u%s", deg, (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
            return ESP_OK;
        }
        case ZB_ATTR_TRACKING_MODE: {
            uint8_t mode = *(uint8_t *)val;
            ld2450_set_tracking_mode(mode ? LD2450_TRACK_SINGLE : LD2450_TRACK_MULTI);
            esp_err_t err = nvs_config_save_tracking_mode(mode);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save tracking_mode to NVS: %s", esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "Tracking mode -> %s%s", mode ? "single" : "multi", (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
            return ESP_OK;
        }
        case ZB_ATTR_COORD_PUBLISHING: {
            uint8_t en = *(uint8_t *)val;
            ld2450_set_publish_coords(en != 0);
            esp_err_t err = nvs_config_save_publish_coords(en);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save publish_coords to NVS: %s", esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "Coord publishing -> %s%s", en ? "on" : "off", (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
            return ESP_OK;
        }
        case ZB_ATTR_OCCUPANCY_COOLDOWN: {
            uint16_t sec = *(uint16_t *)val;
            esp_err_t err = nvs_config_save_occupancy_cooldown(0, sec);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save main occupancy_cooldown to NVS: %s", esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "Main occupancy cooldown -> %u sec%s", sec, (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
            return ESP_OK;
        }
        case ZB_ATTR_RESTART:
            ESP_LOGI(TAG, "Restart requested via Zigbee, restarting in 1s...");
            /* Delay so the ZCL Write Attributes Response is sent before we reset.
             * Without this, Z2M retries the write after reconnect → double reboot. */
            esp_zb_scheduler_alarm((esp_zb_callback_t)esp_restart, 0, 1000);
            return ESP_OK;
        default:
            break;
        }
    }

    /* EPs 2-6: zone vertex writes */
    if (ep >= ZB_EP_ZONE_BASE && ep < ZB_EP_ZONE_BASE + ZB_EP_ZONE_COUNT
        && cluster == ZB_CLUSTER_LD2450_ZONE) {

        uint8_t zone_idx = ep - ZB_EP_ZONE_BASE;

        /* Zone occupancy cooldown writes */
        if (attr_id == ZB_ATTR_OCCUPANCY_COOLDOWN) {
            uint16_t sec = *(uint16_t *)val;
            esp_err_t err = nvs_config_save_occupancy_cooldown(zone_idx + 1, sec);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save zone %d occupancy_cooldown to NVS: %s", zone_idx + 1, esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "Zone %d occupancy cooldown -> %u sec%s", zone_idx + 1, sec, (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
            return ESP_OK;
        }

        /* Zone vertex writes */
        if (attr_id < ZB_ATTR_ZONE_VERTEX_COUNT) {
            int16_t coord_val = *(int16_t *)val;

            /* Read current zone, update one coordinate, write back */
            ld2450_zone_t zones[5];
            ld2450_get_zones(zones, 5);

            int vi = attr_id / 2;  /* vertex index */
            if (attr_id % 2 == 0) {
                zones[zone_idx].v[vi].x_mm = coord_val;
            } else {
                zones[zone_idx].v[vi].y_mm = coord_val;
            }
            zones[zone_idx].enabled = true;

            ld2450_set_zone(zone_idx, &zones[zone_idx]);
            esp_err_t err = nvs_config_save_zone(zone_idx, &zones[zone_idx]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save zone %d to NVS: %s", zone_idx + 1, esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "Zone %d vertex attr 0x%04X -> %d (saved to NVS)", zone_idx + 1, attr_id, coord_val);
            }
            return ESP_OK;
        }
    }

    return ESP_OK;
}

static esp_err_t action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    /* Route OTA callbacks to OTA component */
    esp_err_t ret = zigbee_ota_action_handler(callback_id, message);
    if (ret != ESP_ERR_NOT_SUPPORTED) {
        return ret;  /* OTA component handled it */
    }

    /* Handle application callbacks */
    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        return handle_set_attr_value((const esp_zb_zcl_set_attr_value_message_t *)message);
    }
    return ESP_OK;
}

/* ================================================================== */
/*  Sensor bridge: poll LD2450 and update Zigbee attributes            */
/* ================================================================== */

static void format_coords_string(const ld2450_state_t *state, char *buf, size_t buf_size)
{
    /* Format: "x1,y1;x2,y2;x3,y3" with ZCL char-string length prefix */
    char tmp[48];
    int pos = 0;

    for (int i = 0; i < 3; i++) {
        if (state->targets[i].present) {
            if (pos > 0) {
                pos += snprintf(tmp + pos, sizeof(tmp) - pos, ";");
            }
            pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%d,%d",
                          (int)state->targets[i].x_mm, (int)state->targets[i].y_mm);
        }
    }

    if (pos == 0) {
        buf[0] = 0;  /* ZCL empty string */
        buf[1] = 0;
    } else {
        buf[0] = (char)pos;  /* ZCL length prefix */
        memcpy(buf + 1, tmp, pos);
        buf[pos + 1] = 0;
    }
}

static void sensor_poll_cb(uint8_t param)
{
    (void)param;
    esp_zb_scheduler_alarm(sensor_poll_cb, ALARM_PARAM_POLL, SENSOR_POLL_INTERVAL_MS);

    if (!s_network_joined) return;

    ld2450_state_t state;
    if (ld2450_get_state(&state) != ESP_OK) return;

    ld2450_runtime_cfg_t rt_cfg;
    ld2450_get_runtime_cfg(&rt_cfg);

    /* Get current config and time */
    nvs_config_t cfg;
    nvs_config_get(&cfg);
    uint32_t current_ticks = xTaskGetTickCount();

    /* EP 1: Overall occupancy */
    bool occupied = state.occupied_global;
    uint32_t main_cooldown_ticks = pdMS_TO_TICKS(cfg.occupancy_cooldown_sec[0] * 1000);

    if (occupied != s_last_occupied) {
        if (!occupied) {
            /* State went Occupied → Clear: Start cooldown (don't report yet) */
            if (!s_pending_clear[0]) {
                s_pending_clear[0] = true;
                s_clear_start_time[0] = current_ticks;
            }
        } else {
            /* State went Clear → Occupied: Cancel pending clear, report immediately */
            s_pending_clear[0] = false;
            uint8_t val = 1;
            esp_zb_zcl_set_attribute_val(ZB_EP_MAIN,
                ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
                &val, false);
            s_last_occupied = true;
            s_last_report_time[0] = current_ticks;
        }
    }

    /* Check for pending Clear report that has completed cooldown */
    if (s_pending_clear[0] && !occupied) {
        if (main_cooldown_ticks == 0 || (current_ticks - s_clear_start_time[0]) >= main_cooldown_ticks) {
            /* Cooldown complete and still clear - report it */
            uint8_t val = 0;
            esp_zb_zcl_set_attribute_val(ZB_EP_MAIN,
                ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
                &val, false);
            s_last_occupied = false;
            s_last_report_time[0] = current_ticks;
            s_pending_clear[0] = false;
        }
    }

    /* EPs 2-6: Per-zone occupancy */
    for (int i = 0; i < 5; i++) {
        bool zone_occ = state.zone_occupied[i];
        uint32_t zone_cooldown_ticks = pdMS_TO_TICKS(cfg.occupancy_cooldown_sec[i + 1] * 1000);

        if (zone_occ != s_last_zone_occ[i]) {
            if (!zone_occ) {
                /* State went Occupied → Clear: Start cooldown (don't report yet) */
                if (!s_pending_clear[i + 1]) {
                    s_pending_clear[i + 1] = true;
                    s_clear_start_time[i + 1] = current_ticks;
                }
            } else {
                /* State went Clear → Occupied: Cancel pending clear, report immediately */
                s_pending_clear[i + 1] = false;
                uint8_t val = 1;
                esp_zb_zcl_set_attribute_val(ZB_EP_ZONE(i),
                    ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
                    &val, false);
                s_last_zone_occ[i] = true;
                s_last_report_time[i + 1] = current_ticks;
            }
        }

        /* Check for pending Clear report that has completed cooldown */
        if (s_pending_clear[i + 1] && !zone_occ) {
            if (zone_cooldown_ticks == 0 || (current_ticks - s_clear_start_time[i + 1]) >= zone_cooldown_ticks) {
                /* Cooldown complete and still clear - report it */
                uint8_t val = 0;
                esp_zb_zcl_set_attribute_val(ZB_EP_ZONE(i),
                    ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
                    &val, false);
                s_last_zone_occ[i] = false;
                s_last_report_time[i + 1] = current_ticks;
                s_pending_clear[i + 1] = false;
            }
        }
    }

    /* EP 1: Target count */
    uint8_t count = state.target_count_effective;
    if (count != s_last_target_count) {
        esp_zb_zcl_set_attribute_val(ZB_EP_MAIN,
            ZB_CLUSTER_LD2450_CONFIG,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ZB_ATTR_TARGET_COUNT,
            &count, false);
        s_last_target_count = count;
    }

    /* EP 1: Target coordinates (only if publishing enabled) */
    if (rt_cfg.publish_coords) {
        char coords[64];
        format_coords_string(&state, coords, sizeof(coords));
        if (memcmp(coords, s_last_coords, sizeof(coords)) != 0) {
            esp_zb_zcl_set_attribute_val(ZB_EP_MAIN,
                ZB_CLUSTER_LD2450_CONFIG,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ZB_ATTR_TARGET_COORDS,
                coords, false);
            memcpy(s_last_coords, coords, sizeof(s_last_coords));
        }
    }
}

static void configure_reporting_for_occ(uint8_t ep)
{
    esp_zb_zcl_reporting_info_t rpt = {0};
    rpt.direction   = ESP_ZB_ZCL_REPORT_DIRECTION_SEND;
    rpt.ep          = ep;
    rpt.cluster_id  = ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING;
    rpt.cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
    rpt.attr_id     = ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID;
    rpt.u.send_info.min_interval     = REPORT_MIN_INTERVAL;
    rpt.u.send_info.max_interval     = REPORT_MAX_INTERVAL;
    rpt.u.send_info.def_min_interval = REPORT_MIN_INTERVAL;
    rpt.u.send_info.def_max_interval = REPORT_MAX_INTERVAL;
    rpt.u.send_info.delta.u8         = 0;
    rpt.dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    rpt.manuf_code     = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
    esp_zb_zcl_update_reporting_info(&rpt);
}

static void configure_all_reporting(void)
{
    /* Occupancy reporting on all 6 endpoints */
    configure_reporting_for_occ(ZB_EP_MAIN);
    for (int i = 0; i < ZB_EP_ZONE_COUNT; i++) {
        configure_reporting_for_occ(ZB_EP_ZONE(i));
    }
    ESP_LOGI(TAG, "Reporting configured for all endpoints");
}

static void bridge_start(void)
{
    ESP_LOGI(TAG, "Starting sensor bridge (poll every %d ms)", SENSOR_POLL_INTERVAL_MS);
    configure_all_reporting();
    esp_zb_scheduler_alarm(sensor_poll_cb, ALARM_PARAM_POLL, SENSOR_POLL_INTERVAL_MS);
}

/* ================================================================== */
/*  Signal handler                                                     */
/* ================================================================== */

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
                bridge_start();
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
            bridge_start();
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

/* ================================================================== */
/*  Zigbee task                                                        */
/* ================================================================== */

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

    /* Register action handler before endpoint registration */
    esp_zb_core_action_handler_register(action_handler);

    zigbee_register_endpoints();
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

/* ================================================================== */
/*  Factory reset (callable from any context)                          */
/* ================================================================== */

void zigbee_factory_reset(void)
{
    ESP_LOGW(TAG, "Zigbee network reset - leaving network, keeping config");
    board_led_set_state(BOARD_LED_ERROR);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_zb_factory_reset();
    /* esp_zb_factory_reset() restarts, but just in case: */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void zigbee_full_factory_reset(void)
{
    ESP_LOGW(TAG, "FULL factory reset - erasing Zigbee network + NVS config");
    board_led_set_state(BOARD_LED_ERROR);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Erase application NVS namespace */
    nvs_handle_t h;
    if (nvs_open("ld2450_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "NVS config erased");
    }

    /* Then erase Zigbee network data and restart */
    esp_zb_factory_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/* ================================================================== */
/*  Boot button monitor: 3s = Zigbee reset, 10s = full factory reset  */
/* ================================================================== */

static void button_task(void *pv)
{
    (void)pv;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    uint32_t held_ms = 0;
    uint32_t blink_counter = 0;

    while (1) {
        if (gpio_get_level(BOARD_BUTTON_GPIO) == 0) {
            /* Button pressed */
            held_ms += 100;
            blink_counter++;

            if (held_ms >= 1000 && held_ms < BOARD_BUTTON_HOLD_ZIGBEE_MS) {
                /* 1-3s: Fast red blink (every 200ms) - building to Zigbee reset */
                if (blink_counter % 2 == 0) {
                    board_led_set_state(BOARD_LED_ERROR);
                } else {
                    board_led_set_state(BOARD_LED_NOT_JOINED);
                }
            } else if (held_ms >= BOARD_BUTTON_HOLD_ZIGBEE_MS && held_ms < BOARD_BUTTON_HOLD_FULL_MS) {
                /* 3-10s: Slow red blink (every 500ms) - Zigbee reset armed, holding for full */
                if ((blink_counter / 5) % 2 == 0) {
                    board_led_set_state(BOARD_LED_ERROR);
                } else {
                    board_led_set_state(BOARD_LED_NOT_JOINED);
                }
            } else if (held_ms >= BOARD_BUTTON_HOLD_FULL_MS) {
                /* >10s: Solid red - full reset armed */
                board_led_set_state(BOARD_LED_ERROR);
            }
        } else {
            /* Button released */
            if (held_ms >= BOARD_BUTTON_HOLD_FULL_MS) {
                /* >10s hold: Full factory reset (Zigbee + NVS) */
                zigbee_full_factory_reset();
            } else if (held_ms >= BOARD_BUTTON_HOLD_ZIGBEE_MS) {
                /* 3-10s hold: Zigbee network reset only */
                zigbee_factory_reset();
            } else if (held_ms >= 1000) {
                /* 1-3s hold: Cancelled - restore LED to current network state */
                board_led_set_state(s_network_joined
                    ? BOARD_LED_JOINED : BOARD_LED_NOT_JOINED);
            }
            held_ms = 0;
            blink_counter = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void zigbee_app_start(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "LD2450 Firmware Version: v1.0.0.0");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting Zigbee task...");
    xTaskCreate(zigbee_task, "zb_task", 8192, NULL, 5, NULL);
    xTaskCreate(button_task, "btn_task", 2048, NULL, 5, NULL);
}
