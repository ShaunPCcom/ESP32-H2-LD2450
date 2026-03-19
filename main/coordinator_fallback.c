// SPDX-License-Identifier: MIT
#include "coordinator_fallback.h"

#include <string.h>

#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_core.h"
#include "aps/esp_zigbee_aps.h"

#include "nvs_config.h"
#include "zigbee_defs.h"

static const char *TAG = "fallback";

/* ================================================================== */
/*  Per-endpoint state                                                  */
/* ================================================================== */

typedef struct {
    bool     occupied;                /* current occupancy state for this EP */
    bool     awaiting_ack;            /* ACK window is open (alarm scheduled) */
    bool     fallback_session_active; /* entered occupancy under fallback this session */
    bool     fallback_light_on;       /* we sent On via binding for this session */
    uint8_t  off_timer_generation;    /* incremented to invalidate stale off-timers */
    uint8_t  off_timer_param;         /* param used to schedule off-timer (for cancel) */
} fallback_ep_state_t;

/* s_ep[0] = EP1 (main), s_ep[1-10] = EP2-11 (zones) */
static fallback_ep_state_t s_ep[11];

/* ================================================================== */
/*  Global fallback state                                               */
/* ================================================================== */

static bool s_fallback_mode        = false;  /* sticky flag, NVS-backed */
static bool s_coordinator_reachable = true;  /* optimistic: assume reachable until miss */
static bool s_fallback_reported    = false;  /* have we reported flag=1 to coordinator? */

/* ================================================================== */
/*  Forward declarations                                                */
/* ================================================================== */

static void ack_timeout_cb(uint8_t param);
static void fallback_off_cb(uint8_t param);
static void send_onoff_via_binding(uint8_t endpoint, bool on);
static void enter_fallback_mode(void);

/* ================================================================== */
/*  Send-status callback (Step 1: ACK tracking)                        */
/* ================================================================== */

/*
 * NOTE (Step 1 verification required):
 * This callback fires for any ZCL command sent by this device.  The first
 * thing to verify on hardware is whether it fires for automatic reports
 * triggered by esp_zb_zcl_set_attribute_val().  If it does NOT fire for
 * those, occupancy reports need to be sent as explicit
 * esp_zb_zcl_report_attr_cmd_req() calls instead.  See plan Step 1.
 *
 * Log all invocations at VERBOSE level during initial testing:
 *   idf.py monitor | grep "send_status"
 */
static void send_status_cb(esp_zb_zcl_command_send_status_message_t msg)
{
    /* Only care about messages to the coordinator (short addr 0x0000) */
    if (msg.dst_addr.addr_type != 0 /* ESP_ZB_ZCL_ADDR_TYPE_SHORT */
            || msg.dst_addr.u.short_addr != 0x0000) {
        return;
    }

    uint8_t src_ep  = msg.src_endpoint;
    bool    success = (msg.status == ESP_OK);

    ESP_LOGV(TAG, "send_status: ep=%u dst_ep=%u status=%s",
             src_ep, msg.dst_endpoint, success ? "OK" : "FAIL");

    /* ep_idx: EP1→0, EP2-11→1-10 */
    if (src_ep < 1 || src_ep > 11) return;
    uint8_t ep_idx = src_ep - 1;

    if (success) {
        /* Coordinator ACKed a message from this endpoint */
        s_coordinator_reachable = true;

        /* Clear the awaiting_ack flag — ACK arrived in time */
        if (s_ep[ep_idx].awaiting_ack) {
            s_ep[ep_idx].awaiting_ack = false;
            ESP_LOGD(TAG, "ep%u: coordinator ACK received", src_ep);
        }

        /* If in fallback and not yet reported, send the fallback flag now */
        if (s_fallback_mode && !s_fallback_reported) {
            uint8_t val = 1;
            esp_zb_zcl_set_attribute_val(ZB_EP_MAIN,
                ZB_CLUSTER_LD2450_CONFIG,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ZB_ATTR_FALLBACK_MODE,
                &val, false);
            s_fallback_reported = true;
            ESP_LOGI(TAG, "Coordinator reachable — reported fallback_mode=1");
        }
    } else {
        /* Message failed — but don't enter fallback here; let ack_timeout_cb handle it */
        ESP_LOGD(TAG, "ep%u: send failed (status=%d)", src_ep, msg.status);
    }
}

/* ================================================================== */
/*  ACK timeout callback                                                */
/* ================================================================== */

static void ack_timeout_cb(uint8_t param)
{
    /* Param: (endpoint << 1) | (occupied ? 1 : 0) */
    uint8_t endpoint = param >> 1;
    bool    occupied = (param & 1) != 0;

    if (endpoint < 1 || endpoint > 11) return;
    uint8_t ep_idx = endpoint - 1;

    /* If ACK arrived already, nothing to do */
    if (!s_ep[ep_idx].awaiting_ack) {
        ESP_LOGD(TAG, "ep%u: ACK arrived before timeout, no fallback", endpoint);
        return;
    }

    s_ep[ep_idx].awaiting_ack = false;
    s_coordinator_reachable   = false;
    ESP_LOGW(TAG, "ep%u: coordinator ACK timeout — entering fallback", endpoint);

    /* Enter fallback mode if not already active */
    if (!s_fallback_mode) {
        enter_fallback_mode();
    }

    /* Mark this EP as having started its session under fallback */
    s_ep[ep_idx].fallback_session_active = true;

    if (occupied) {
        /* Send On to bound lights */
        send_onoff_via_binding(endpoint, true);
        s_ep[ep_idx].fallback_light_on = true;
        ESP_LOGI(TAG, "ep%u: sent On via binding (fallback)", endpoint);
    } else if (s_ep[ep_idx].fallback_light_on) {
        /* Occupancy cleared — check cooldown and schedule off */
        nvs_config_t cfg;
        nvs_config_get(&cfg);
        uint16_t cooldown_sec = cfg.fallback_cooldown_sec[ep_idx];

        /* Increment generation to invalidate any previous off-timer */
        s_ep[ep_idx].off_timer_generation++;
        uint8_t gen = s_ep[ep_idx].off_timer_generation;
        uint8_t off_param = (uint8_t)((ep_idx << 4) | (gen & 0x0F));
        s_ep[ep_idx].off_timer_param = off_param;

        if (cooldown_sec == 0) {
            /* Send Off immediately */
            send_onoff_via_binding(endpoint, false);
            s_ep[ep_idx].fallback_light_on = false;
            ESP_LOGI(TAG, "ep%u: sent Off via binding (fallback, no cooldown)", endpoint);
        } else {
            esp_zb_scheduler_alarm(fallback_off_cb, off_param,
                                   (uint32_t)cooldown_sec * 1000);
            ESP_LOGI(TAG, "ep%u: Off scheduled in %us (fallback cooldown)", endpoint, cooldown_sec);
        }
    }
}

/* ================================================================== */
/*  Fallback off callback                                               */
/* ================================================================== */

static void fallback_off_cb(uint8_t param)
{
    uint8_t ep_idx = (param >> 4) & 0x0F;
    uint8_t gen    = param & 0x0F;

    if (ep_idx > 10) return;
    uint8_t endpoint = ep_idx + 1;

    /* Stale timer guard: generation must still match */
    if ((s_ep[ep_idx].off_timer_generation & 0x0F) != gen) {
        ESP_LOGD(TAG, "ep%u: stale off-timer (gen mismatch), ignoring", endpoint);
        return;
    }

    /* Only send Off if all conditions still hold */
    if (!s_ep[ep_idx].fallback_session_active
            || !s_ep[ep_idx].fallback_light_on
            || s_ep[ep_idx].occupied) {
        ESP_LOGD(TAG, "ep%u: off-timer fired but conditions changed, skipping Off", endpoint);
        return;
    }

    send_onoff_via_binding(endpoint, false);
    s_ep[ep_idx].fallback_light_on = false;
    ESP_LOGI(TAG, "ep%u: sent Off via binding (fallback cooldown expired)", endpoint);
}

/* ================================================================== */
/*  On/Off dispatch via binding                                         */
/* ================================================================== */

static void send_onoff_via_binding(uint8_t endpoint, bool on)
{
    esp_zb_zcl_on_off_cmd_t cmd = {0};
    cmd.zcl_basic_cmd.src_endpoint = endpoint;
    cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
    cmd.on_off_cmd_id = on ? ESP_ZB_ZCL_CMD_ON_OFF_ON_ID : ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID;
    esp_zb_zcl_on_off_cmd_req(&cmd);
}

/* ================================================================== */
/*  Enter fallback mode (internal)                                      */
/* ================================================================== */

static void enter_fallback_mode(void)
{
    s_fallback_mode     = true;
    s_fallback_reported = false;

    /* Save to NVS so reboot resumes in fallback */
    nvs_config_save_fallback_mode(1);

    /* Set ZCL attribute — will be reported when coordinator reconnects */
    uint8_t val = 1;
    esp_zb_zcl_set_attribute_val(ZB_EP_MAIN,
        ZB_CLUSTER_LD2450_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ATTR_FALLBACK_MODE,
        &val, false);

    ESP_LOGW(TAG, "FALLBACK MODE ACTIVE — On/Off via binding until HA clears");
}

/* ================================================================== */
/*  Public API                                                          */
/* ================================================================== */

void coordinator_fallback_init(void)
{
    memset(s_ep, 0, sizeof(s_ep));

    /* Load persisted fallback state */
    nvs_config_t cfg;
    nvs_config_get(&cfg);
    s_fallback_mode        = (cfg.fallback_mode != 0);
    s_coordinator_reachable = true;  /* optimistic start */
    s_fallback_reported    = false;  /* will report on first successful ACK */

    if (s_fallback_mode) {
        ESP_LOGW(TAG, "Resuming fallback mode from NVS (coordinator was offline at last reboot)");
    }

    /* Register send-status callback for ACK tracking */
    esp_zb_zcl_command_send_status_handler_register(send_status_cb);

    ESP_LOGI(TAG, "Fallback init: mode=%u", s_fallback_mode);
}

void coordinator_fallback_on_occupancy_change(uint8_t endpoint, bool occupied)
{
    if (endpoint < 1 || endpoint > 11) return;
    uint8_t ep_idx = endpoint - 1;

    s_ep[ep_idx].occupied = occupied;

    /* If already in fallback, handle immediately without waiting for ACK */
    if (s_fallback_mode) {
        s_ep[ep_idx].fallback_session_active = true;

        if (occupied) {
            /* Cancel any pending off-timer */
            esp_zb_scheduler_alarm_cancel(fallback_off_cb, s_ep[ep_idx].off_timer_param);
            s_ep[ep_idx].off_timer_generation++;  /* extra safety */

            if (!s_ep[ep_idx].fallback_light_on) {
                send_onoff_via_binding(endpoint, true);
                s_ep[ep_idx].fallback_light_on = true;
                ESP_LOGI(TAG, "ep%u: sent On via binding (already in fallback)", endpoint);
            }
        } else if (s_ep[ep_idx].fallback_light_on) {
            nvs_config_t cfg;
            nvs_config_get(&cfg);
            uint16_t cooldown_sec = cfg.fallback_cooldown_sec[ep_idx];

            s_ep[ep_idx].off_timer_generation++;
            uint8_t gen       = s_ep[ep_idx].off_timer_generation;
            uint8_t off_param = (uint8_t)((ep_idx << 4) | (gen & 0x0F));
            s_ep[ep_idx].off_timer_param = off_param;

            if (cooldown_sec == 0) {
                send_onoff_via_binding(endpoint, false);
                s_ep[ep_idx].fallback_light_on = false;
            } else {
                esp_zb_scheduler_alarm(fallback_off_cb, off_param,
                                       (uint32_t)cooldown_sec * 1000);
            }
        }
        return;
    }

    /* Normal mode: open ACK window */
    s_ep[ep_idx].awaiting_ack = true;

    /* Param: (endpoint << 1) | occupied */
    uint8_t param = (uint8_t)((endpoint << 1) | (occupied ? 1 : 0));
    esp_zb_scheduler_alarm(ack_timeout_cb, param, ACK_TIMEOUT_MS);
}

bool coordinator_fallback_is_active(void)
{
    return s_fallback_mode;
}

bool coordinator_fallback_ep_session_active(uint8_t ep_idx)
{
    if (ep_idx > 10) return false;
    return s_ep[ep_idx].fallback_session_active;
}

void coordinator_fallback_clear(void)
{
    ESP_LOGI(TAG, "Fallback cleared by coordinator (HA wrote fallback_mode=0)");

    s_fallback_mode        = false;
    s_fallback_reported    = false;
    s_coordinator_reachable = true;  /* optimistic re-arm */

    /* Invalidate all pending off-timers and reset per-EP session state */
    for (int i = 0; i < 11; i++) {
        if (s_ep[i].fallback_session_active) {
            esp_zb_scheduler_alarm_cancel(fallback_off_cb, s_ep[i].off_timer_param);
        }
        s_ep[i].fallback_session_active = false;
        s_ep[i].fallback_light_on       = false;
        s_ep[i].awaiting_ack            = false;
        s_ep[i].off_timer_generation++;  /* invalidate any in-flight timers */
    }

    /* Save to NVS */
    nvs_config_save_fallback_mode(0);

    /* Update ZCL attribute */
    uint8_t val = 0;
    esp_zb_zcl_set_attribute_val(ZB_EP_MAIN,
        ZB_CLUSTER_LD2450_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ATTR_FALLBACK_MODE,
        &val, false);

    /* Do NOT send Off to lights — HA reconciles state */
    ESP_LOGI(TAG, "Fallback cleared — normal ACK tracking resumed");
}

void coordinator_fallback_set(void)
{
    ESP_LOGI(TAG, "Fallback manually set (HA/CLI)");
    if (!s_fallback_mode) {
        enter_fallback_mode();
    }
}
