// SPDX-License-Identifier: MIT
#include "config_api.h"

#include "coordinator_fallback.h"
#include "ld2450.h"
#include "ld2450_cmd.h"
#include "ld2450_zone.h"
#include "ld2450_zone_csv.h"
#include "nvs_config.h"

#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

/* Max bytes for a zone coords CSV string (10 vertices × ~15 chars/pair + separators) */
#define ZONE_CSV_BUF_SIZE   160

static const char *TAG = "config_api";

/* ---- Sensor hardware config ---- */

esp_err_t config_api_set_max_distance(uint16_t mm)
{
    esp_err_t err = nvs_config_save_max_distance(mm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save max_distance: %s", esp_err_to_name(err));
    }
    nvs_config_t cfg;
    nvs_config_get(&cfg);
    ld2450_cmd_apply_distance_angle(cfg.max_distance_mm, cfg.angle_left_deg, cfg.angle_right_deg);
    return err;
}

esp_err_t config_api_set_angle_left(uint8_t deg)
{
    esp_err_t err = nvs_config_save_angle_left(deg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save angle_left: %s", esp_err_to_name(err));
    }
    nvs_config_t cfg;
    nvs_config_get(&cfg);
    ld2450_cmd_apply_distance_angle(cfg.max_distance_mm, cfg.angle_left_deg, cfg.angle_right_deg);
    return err;
}

esp_err_t config_api_set_angle_right(uint8_t deg)
{
    esp_err_t err = nvs_config_save_angle_right(deg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save angle_right: %s", esp_err_to_name(err));
    }
    nvs_config_t cfg;
    nvs_config_get(&cfg);
    ld2450_cmd_apply_distance_angle(cfg.max_distance_mm, cfg.angle_left_deg, cfg.angle_right_deg);
    return err;
}

esp_err_t config_api_set_tracking_mode(uint8_t mode)
{
    ld2450_set_tracking_mode(mode ? LD2450_TRACK_SINGLE : LD2450_TRACK_MULTI);
    esp_err_t err = nvs_config_save_tracking_mode(mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save tracking_mode: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_api_set_publish_coords(uint8_t enabled)
{
    ld2450_set_publish_coords(enabled != 0);
    esp_err_t err = nvs_config_save_publish_coords(enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save publish_coords: %s", esp_err_to_name(err));
    }
    return err;
}

/* ---- Occupancy timing ---- */

esp_err_t config_api_set_occupancy_cooldown(uint8_t ep_idx, uint16_t sec)
{
    esp_err_t err = nvs_config_save_occupancy_cooldown(ep_idx, sec);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save occupancy_cooldown[%u]: %s", ep_idx, esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_api_set_occupancy_delay(uint8_t ep_idx, uint16_t ms)
{
    esp_err_t err = nvs_config_save_occupancy_delay(ep_idx, ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save occupancy_delay[%u]: %s", ep_idx, esp_err_to_name(err));
    }
    return err;
}

/* ---- Coordinator fallback ---- */

esp_err_t config_api_set_fallback_mode(uint8_t mode)
{
    if (mode == 0) {
        coordinator_fallback_clear();
    } else {
        coordinator_fallback_set();
    }
    return nvs_config_save_fallback_mode(mode);
}

esp_err_t config_api_set_fallback_cooldown(uint8_t ep_idx, uint16_t sec)
{
    return nvs_config_save_fallback_cooldown(ep_idx, sec);
}

esp_err_t config_api_set_fallback_enable(uint8_t enable)
{
    coordinator_fallback_set_enable(enable);
    return ESP_OK;
}

esp_err_t config_api_set_hard_timeout(uint8_t sec)
{
    coordinator_fallback_set_hard_timeout(sec);
    return ESP_OK;
}

esp_err_t config_api_set_ack_timeout(uint16_t ms)
{
    coordinator_fallback_set_ack_timeout(ms);
    return ESP_OK;
}

/* ---- Heartbeat watchdog ---- */

esp_err_t config_api_set_heartbeat_enable(uint8_t enable)
{
    coordinator_fallback_set_heartbeat_enable(enable);
    return ESP_OK;
}

esp_err_t config_api_set_heartbeat_interval(uint16_t sec)
{
    coordinator_fallback_set_heartbeat_interval(sec);
    return ESP_OK;
}

esp_err_t config_api_heartbeat(void)
{
    coordinator_fallback_heartbeat();
    return ESP_OK;
}

/* ---- Zone geometry ---- */

esp_err_t config_api_set_zone_vertex_count(uint8_t zone_idx, uint8_t vc)
{
    if (zone_idx >= 10) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_config_t cfg;
    nvs_config_get(&cfg);

    if (vc > MAX_ZONE_VERTICES) {
        vc = 0;  /* clamp invalid to disabled */
    }
    cfg.zones[zone_idx].vertex_count = vc;

    if (vc < 3) {
        /* Disabling zone: zero coords */
        memset(cfg.zones[zone_idx].v, 0, sizeof(cfg.zones[zone_idx].v));
    }

    esp_err_t ze = ld2450_set_zone((size_t)zone_idx, &cfg.zones[zone_idx]);
    if (ze == ESP_OK) {
        return nvs_config_save_zone(zone_idx, &cfg.zones[zone_idx]);
    } else {
        /* vc >= 3 but no coords yet: cache only, wait for coords write */
        nvs_config_update_zone_cache(zone_idx, &cfg.zones[zone_idx]);
        return ESP_OK;
    }
}

esp_err_t config_api_set_zone_coords(uint8_t zone_idx, const char *csv)
{
    if (zone_idx >= 10 || csv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_config_t cfg;
    nvs_config_get(&cfg);

    int pairs = csv_count_pairs(csv);
    if (pairs != cfg.zones[zone_idx].vertex_count) {
        ESP_LOGW(TAG, "zone_%d coords rejected: expected %d pairs, got %d",
                 zone_idx + 1, cfg.zones[zone_idx].vertex_count, pairs);
        return ESP_ERR_INVALID_ARG;
    }

    csv_to_zone(csv, &cfg.zones[zone_idx]);
    ld2450_set_zone((size_t)zone_idx, &cfg.zones[zone_idx]);
    return nvs_config_save_zone(zone_idx, &cfg.zones[zone_idx]);
}

/* ---- Read-all serialization ---- */

esp_err_t config_api_get_all(cJSON **out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_config_t cfg;
    nvs_config_get(&cfg);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Sensor config */
    cJSON_AddNumberToObject(root, "tracking_mode",    cfg.tracking_mode);
    cJSON_AddNumberToObject(root, "publish_coords",   cfg.publish_coords);
    cJSON_AddNumberToObject(root, "max_distance_mm",  cfg.max_distance_mm);
    cJSON_AddNumberToObject(root, "angle_left_deg",   cfg.angle_left_deg);
    cJSON_AddNumberToObject(root, "angle_right_deg",  cfg.angle_right_deg);

    /* Main EP occupancy timing */
    cJSON_AddNumberToObject(root, "occupancy_cooldown_sec", cfg.occupancy_cooldown_sec[0]);
    cJSON_AddNumberToObject(root, "occupancy_delay_ms",     cfg.occupancy_delay_ms[0]);

    /* Coordinator fallback */
    cJSON_AddNumberToObject(root, "fallback_mode",         cfg.fallback_mode);
    cJSON_AddNumberToObject(root, "fallback_enable",       cfg.fallback_enable);
    cJSON_AddNumberToObject(root, "fallback_cooldown_sec", cfg.fallback_cooldown_sec[0]);
    cJSON_AddNumberToObject(root, "hard_timeout_sec",      cfg.hard_timeout_sec);
    cJSON_AddNumberToObject(root, "ack_timeout_ms",        cfg.ack_timeout_ms);

    /* Heartbeat */
    cJSON_AddNumberToObject(root, "heartbeat_enable",       cfg.heartbeat_enable);
    cJSON_AddNumberToObject(root, "heartbeat_interval_sec", cfg.heartbeat_interval_sec);

    /* Zones array */
    cJSON *zones = cJSON_AddArrayToObject(root, "zones");
    if (zones == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < 10; i++) {
        cJSON *z = cJSON_CreateObject();
        if (z == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        /* Coords as CSV string */
        char csv[ZONE_CSV_BUF_SIZE];
        zone_to_csv(&cfg.zones[i], csv, sizeof(csv));

        cJSON_AddNumberToObject(z, "vertex_count",         cfg.zones[i].vertex_count);
        cJSON_AddStringToObject(z, "coords",               csv);
        cJSON_AddNumberToObject(z, "cooldown_sec",         cfg.occupancy_cooldown_sec[i + 1]);
        cJSON_AddNumberToObject(z, "delay_ms",             cfg.occupancy_delay_ms[i + 1]);
        cJSON_AddNumberToObject(z, "fallback_cooldown_sec", cfg.fallback_cooldown_sec[i + 1]);

        cJSON_AddItemToArray(zones, z);
    }

    *out = root;
    return ESP_OK;
}
