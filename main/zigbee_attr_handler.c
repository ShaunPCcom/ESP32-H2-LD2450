// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_zigbee_core.h"

/* Project */
#include "ld2450.h"
#include "ld2450_cmd.h"
#include "nvs_config.h"
#include "zigbee_attr_handler.h"
#include "zigbee_defs.h"
#include "zigbee_ota.h"

static const char *TAG = "zigbee_attr";

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
        case ZB_ATTR_OCCUPANCY_DELAY: {
            uint16_t ms = *(uint16_t *)val;
            esp_err_t err = nvs_config_save_occupancy_delay(0, ms);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save main occupancy_delay to NVS: %s", esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "Main occupancy delay -> %u ms%s", ms, (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
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

        /* Zone occupancy delay writes */
        if (attr_id == ZB_ATTR_OCCUPANCY_DELAY) {
            uint16_t ms = *(uint16_t *)val;
            esp_err_t err = nvs_config_save_occupancy_delay(zone_idx + 1, ms);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save zone %d occupancy_delay to NVS: %s", zone_idx + 1, esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "Zone %d occupancy delay -> %u ms%s", zone_idx + 1, ms, (err == ESP_OK) ? " (saved)" : " (NVS FAILED)");
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

esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
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
