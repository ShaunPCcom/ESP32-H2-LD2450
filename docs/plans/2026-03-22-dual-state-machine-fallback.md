# Dual State Machine Fallback Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Decouple Z2M occupancy reporting (normal SM, always uses normal cooldown) from fallback light control (fallback SM, uses fallback cooldown) into two independent state machines. On fallback clear, fallback SM pushes its EP states to Z2M for earliest recovery.

**Architecture:** Sensor_bridge always uses normal cooldown and reports to Z2M. The fallback module maintains its own `fallback_occupied` per EP with its own cooldown timer. When fallback activates, it dispatches On/Off based on `fallback_occupied`. When fallback clears, it pushes all EP `fallback_occupied` states to Z2M attributes, cancels its timers, and lets normal SM take over. Reconciliation is one-way: fallback → normal on clear only.

**Tech Stack:** ESP-IDF C firmware (coordinator_fallback.c, sensor_bridge.c, coordinator_fallback.h)

---

### Task 1: Add fallback SM state to struct and forward declarations

**Files:**
- Modify: `main/coordinator_fallback.c:21-26` (struct)
- Modify: `main/coordinator_fallback.c:62-68` (forward declarations)

**Step 1: Add fields to struct**

Add `fallback_occupied` and `fb_cooldown_gen` to `fallback_ep_state_t`:

```c
typedef struct {
    bool     occupied;                /* current occupancy state for this EP (from normal SM) */
    bool     awaiting_ack;            /* ACK window is open (alarm scheduled) */
    bool     soft_fallback_active;    /* this EP is in soft fallback (transient) */
    bool     fallback_session_active; /* entered occupancy under hard fallback */
    bool     fallback_occupied;       /* fallback SM occupancy (independent cooldown) */
    uint8_t  fb_cooldown_gen;         /* generation counter for stale timer invalidation */
} fallback_ep_state_t;
```

**Step 2: Add forward declaration**

After line 63 (`static void hard_timeout_cb(uint8_t param);`), add:

```c
static void fallback_cooldown_cb(uint8_t param);
```

---

### Task 2: Add the fallback cooldown timer callback

**Files:**
- Modify: `main/coordinator_fallback.c` (new function, after `hard_timeout_cb`)

**Step 1: Add the function after `hard_timeout_cb` (after line 258)**

```c
/* ================================================================== */
/*  Fallback cooldown timer callback                                    */
/* ================================================================== */

static void fallback_cooldown_cb(uint8_t param)
{
    /* Param: (ep_idx << 4) | (gen & 0x0F) */
    uint8_t ep_idx = (param >> 4) & 0x0F;
    uint8_t gen    = param & 0x0F;

    if (ep_idx > 10) return;
    uint8_t endpoint = ep_idx + 1;

    /* Stale timer guard */
    if ((s_ep[ep_idx].fb_cooldown_gen & 0x0F) != gen) {
        ESP_LOGD(TAG, "ep%u: stale fallback cooldown (gen mismatch), ignoring", endpoint);
        return;
    }

    s_ep[ep_idx].fallback_occupied = false;
    ESP_LOGI(TAG, "ep%u: fallback cooldown expired, fallback_occupied=0", endpoint);

    /* If in active fallback, send Off via binding */
    if (s_fallback_mode || s_ep[ep_idx].soft_fallback_active) {
        send_onoff_via_binding(endpoint, false);
        ESP_LOGI(TAG, "ep%u: sent Off via binding (fallback cooldown expired)", endpoint);
    }
}
```

---

### Task 3: Add helper to push fallback state to Z2M (reconciliation)

**Files:**
- Modify: `main/coordinator_fallback.c` (new static function, after `set_soft_fault_attr`)

**Step 1: Add reconciliation helper after `set_soft_fault_attr` (after line 82)**

This pushes all EP `fallback_occupied` states to ZCL occupancy attributes, cancels fallback cooldown timers, and syncs `fallback_occupied` to current `occupied`:

```c
static void reconcile_fallback_to_normal(void)
{
    for (int i = 0; i < 11; i++) {
        /* Push fallback occupancy to ZCL attribute */
        uint8_t endpoint = i + 1;
        uint16_t cluster = ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING;
        uint16_t attr    = ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID;
        uint8_t  val     = s_ep[i].fallback_occupied ? 1 : 0;

        if (i == 0) {
            esp_zb_zcl_set_attribute_val(ZB_EP_MAIN, cluster,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr, &val, false);
        } else {
            esp_zb_zcl_set_attribute_val(ZB_EP_ZONE(i - 1), cluster,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr, &val, false);
        }

        /* Cancel pending fallback cooldown timer */
        uint8_t cancel_param = (uint8_t)((i << 4) | (s_ep[i].fb_cooldown_gen & 0x0F));
        esp_zb_scheduler_alarm_cancel(fallback_cooldown_cb, cancel_param);

        /* Sync fallback_occupied to current normal SM state */
        s_ep[i].fallback_occupied = s_ep[i].occupied;

        ESP_LOGI(TAG, "ep%u: reconciled to Z2M (occ=%d)", endpoint, val);
    }
}
```

---

### Task 4: Update `coordinator_fallback_on_occupancy_change` — manage fallback SM first, then dispatch

**Files:**
- Modify: `main/coordinator_fallback.c:455-511` (on_occupancy_change function)

**Step 1: Replace the entire function**

The function now: (1) updates normal SM state, (2) updates fallback SM state (always, regardless of fallback active), (3) dispatches based on `fallback_occupied`:

```c
void coordinator_fallback_on_occupancy_change(uint8_t endpoint, bool occupied)
{
    if (endpoint < 1 || endpoint > 11) return;
    uint8_t ep_idx = endpoint - 1;

    s_ep[ep_idx].occupied = occupied;

    /* ---- Fallback SM: always track, independent of normal SM ---- */
    if (occupied) {
        /* Cancel pending fallback cooldown timer */
        uint8_t cancel_param = (uint8_t)((ep_idx << 4) | (s_ep[ep_idx].fb_cooldown_gen & 0x0F));
        esp_zb_scheduler_alarm_cancel(fallback_cooldown_cb, cancel_param);
        s_ep[ep_idx].fallback_occupied = true;
    } else {
        /* Start fallback cooldown timer -- fallback_occupied stays true until it fires */
        nvs_config_t cfg;
        nvs_config_get(&cfg);
        uint16_t cooldown_sec = cfg.fallback_cooldown_sec[ep_idx];

        s_ep[ep_idx].fb_cooldown_gen++;
        uint8_t gen   = s_ep[ep_idx].fb_cooldown_gen;
        uint8_t param = (uint8_t)((ep_idx << 4) | (gen & 0x0F));

        if (cooldown_sec == 0) {
            s_ep[ep_idx].fallback_occupied = false;
            ESP_LOGD(TAG, "ep%u: fallback cooldown=0, fallback_occupied=0 immediately", endpoint);
        } else {
            esp_zb_scheduler_alarm(fallback_cooldown_cb, param,
                                   (uint32_t)cooldown_sec * 1000);
            ESP_LOGD(TAG, "ep%u: fallback cooldown started (%us), fallback_occupied stays 1",
                     endpoint, cooldown_sec);
        }
    }

    /* ---- Dispatch: hard fallback path ---- */
    if (s_fallback_mode) {
        s_ep[ep_idx].fallback_session_active = true;

        if (s_ep[ep_idx].fallback_occupied) {
            send_onoff_via_binding(endpoint, true);
            ESP_LOGI(TAG, "ep%u: sent On via binding (hard fallback)", endpoint);
        } else {
            send_onoff_via_binding(endpoint, false);
            ESP_LOGI(TAG, "ep%u: sent Off via binding (hard fallback)", endpoint);
        }
        return;
    }

    /* ---- Dispatch: soft fallback path (skip probing) ---- */
    if (s_ep[ep_idx].soft_fallback_active) {
        if (s_ep[ep_idx].fallback_occupied) {
            send_onoff_via_binding(endpoint, true);
            ESP_LOGI(TAG, "ep%u: sent On via binding (soft fallback, occ change)", endpoint);
        } else {
            send_onoff_via_binding(endpoint, false);
            ESP_LOGI(TAG, "ep%u: sent Off via binding (soft fallback, occ change)", endpoint);
        }
        return;
    }

    /* ---- Normal mode: probe coordinator ---- */
    if (!s_fallback_enabled) return;

    s_ep[ep_idx].awaiting_ack = true;

    esp_zb_zcl_custom_cluster_cmd_t probe = {0};
    probe.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
    probe.zcl_basic_cmd.dst_endpoint          = 1;
    probe.zcl_basic_cmd.src_endpoint          = ZB_EP_MAIN;
    probe.address_mode    = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    probe.profile_id      = ESP_ZB_AF_HA_PROFILE_ID;
    probe.cluster_id      = ZB_CLUSTER_LD2450_CONFIG;
    probe.direction       = 0;
    probe.dis_default_resp = 0;
    probe.custom_cmd_id   = 0x00;
    probe.data.size       = 0;
    probe.data.value      = NULL;
    esp_zb_zcl_custom_cluster_cmd_req(&probe);
    ESP_LOGI(TAG, "ep%u: probe sent (occ=%d), ack window=%ums",
             endpoint, (int)occupied, (unsigned)s_ack_timeout_ms);

    uint8_t ack_param = (uint8_t)((endpoint << 1) | (occupied ? 1 : 0));
    esp_zb_scheduler_alarm(ack_timeout_cb, ack_param, s_ack_timeout_ms);
}
```

---

### Task 5: Update `ack_timeout_cb` — use `fallback_occupied` for dispatch

**Files:**
- Modify: `main/coordinator_fallback.c:162-225` (ack_timeout_cb function)

**Step 1: Replace the dispatch logic to use `fallback_occupied`**

The `occupied` variable from the timer param is still decoded (useful for logging), but dispatch uses `s_ep[ep_idx].fallback_occupied`:

```c
static void ack_timeout_cb(uint8_t param)
{
    /* Param: (endpoint << 1) | (occupied ? 1 : 0) */
    uint8_t endpoint = param >> 1;
    bool    occupied = (param & 1) != 0;

    if (endpoint < 1 || endpoint > 11) return;
    uint8_t ep_idx = endpoint - 1;

    /* If ACK arrived already, nothing to do */
    if (!s_ep[ep_idx].awaiting_ack) {
        ESP_LOGI(TAG, "ep%u: ACK arrived before timeout, no fallback", endpoint);
        return;
    }

    s_ep[ep_idx].awaiting_ack = false;
    s_coordinator_reachable   = false;

    bool fb_occ = s_ep[ep_idx].fallback_occupied;

    /* ---- Hard fallback path: already in hard fallback, dispatch directly ---- */
    if (s_fallback_mode) {
        ESP_LOGW(TAG, "ep%u: ACK timeout (hard fallback active, fb_occ=%d, raw_occ=%d)",
                 endpoint, fb_occ, occupied);
        s_ep[ep_idx].fallback_session_active = true;

        if (fb_occ) {
            send_onoff_via_binding(endpoint, true);
            ESP_LOGI(TAG, "ep%u: sent On via binding (hard fallback)", endpoint);
        } else {
            send_onoff_via_binding(endpoint, false);
            ESP_LOGI(TAG, "ep%u: sent Off via binding (hard fallback)", endpoint);
        }
        return;
    }

    /* ---- Soft fallback path: transient APS timeout ---- */
    bool already_soft = s_ep[ep_idx].soft_fallback_active;
    s_ep[ep_idx].soft_fallback_active = true;

    if (!already_soft) {
        s_soft_fault_count++;
        set_soft_fault_attr(s_soft_fault_count);
        ESP_LOGW(TAG, "ep%u: soft fallback #%u (fb_occ=%d, raw_occ=%d)",
                 endpoint, s_soft_fault_count, fb_occ, occupied);
    } else {
        ESP_LOGW(TAG, "ep%u: additional soft fault (already soft, fb_occ=%d)", endpoint, fb_occ);
    }

    /* Dispatch On/Off via binding based on fallback SM occupancy */
    if (fb_occ) {
        send_onoff_via_binding(endpoint, true);
        ESP_LOGI(TAG, "ep%u: sent On via binding (soft fallback)", endpoint);
    } else {
        send_onoff_via_binding(endpoint, false);
        ESP_LOGI(TAG, "ep%u: sent Off via binding (soft fallback)", endpoint);
    }

    /* Schedule hard timeout if not already pending */
    if (!s_hard_timeout_pending) {
        s_hard_timeout_gen++;
        esp_zb_scheduler_alarm(hard_timeout_cb, s_hard_timeout_gen,
                               (uint32_t)s_hard_timeout_sec * 1000);
        s_hard_timeout_pending = true;
        ESP_LOGI(TAG, "Hard timeout scheduled in %us (gen=%u)", s_hard_timeout_sec, s_hard_timeout_gen);
    }
}
```

---

### Task 6: Add reconciliation to soft fallback clear in `send_status_cb`

**Files:**
- Modify: `main/coordinator_fallback.c:128-140` (soft fallback clear block in send_status_cb)

**Step 1: Replace the soft fallback clear block**

After clearing soft fallback flags and hard timeout, call `reconcile_fallback_to_normal()`:

```c
        if (any_soft) {
            for (int i = 0; i < 11; i++) {
                s_ep[i].soft_fallback_active = false;
            }
            if (s_hard_timeout_pending) {
                esp_zb_scheduler_alarm_cancel(hard_timeout_cb, s_hard_timeout_gen);
                s_hard_timeout_pending = false;
            }
            s_soft_fault_count = 0;
            set_soft_fault_attr(0);

            /* Reconcile: push fallback SM state to Z2M for earliest recovery */
            reconcile_fallback_to_normal();
            ESP_LOGI(TAG, "Coordinator ACK -- soft fallbacks cleared, state reconciled to Z2M");
        }
```

---

### Task 7: Update `coordinator_fallback_clear` with reconciliation

**Files:**
- Modify: `main/coordinator_fallback.c:526-568` (coordinator_fallback_clear function)

**Step 1: Replace the function**

Add reconciliation before resetting state:

```c
void coordinator_fallback_clear(void)
{
    ESP_LOGI(TAG, "Hard fallback cleared by HA (fallback_mode=0)");

    /* Reconcile: push fallback SM state to Z2M for earliest recovery */
    reconcile_fallback_to_normal();

    s_fallback_mode        = false;
    s_fallback_reported    = false;
    s_coordinator_reachable = true;

    /* Cancel pending hard timeout */
    if (s_hard_timeout_pending) {
        esp_zb_scheduler_alarm_cancel(hard_timeout_cb, s_hard_timeout_gen);
        s_hard_timeout_pending = false;
    }

    /* Reset per-EP session state */
    for (int i = 0; i < 11; i++) {
        s_ep[i].fallback_session_active = false;
        s_ep[i].soft_fallback_active    = false;
        s_ep[i].awaiting_ack            = false;
    }

    /* Reset soft fault counter */
    s_soft_fault_count = 0;
    set_soft_fault_attr(0);

    nvs_config_save_fallback_mode(0);

    uint8_t val = 0;
    esp_zb_zcl_set_attribute_val(ZB_EP_MAIN,
        ZB_CLUSTER_LD2450_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ZB_ATTR_FALLBACK_MODE,
        &val, false);

    if (s_heartbeat_enabled) {
        cancel_heartbeat_watchdog();
        start_heartbeat_watchdog();
    }

    ESP_LOGI(TAG, "Hard fallback cleared -- state reconciled, normal tracking resumed");
}
```

---

### Task 8: Revert `coordinator_fallback_ep_session_active`

**Files:**
- Modify: `main/coordinator_fallback.c:518-524` (ep_session_active function)

**Step 1: Revert to only check `fallback_session_active`**

Sensor_bridge no longer uses this for cooldown switching. It's only used for backwards compatibility or future use:

```c
bool coordinator_fallback_ep_session_active(uint8_t ep_idx)
{
    if (ep_idx > 10) return false;
    return s_ep[ep_idx].fallback_session_active;
}
```

---

### Task 9: Remove fallback cooldown switching from sensor_bridge

**Files:**
- Modify: `main/sensor_bridge.c:124-128` (EP1 cooldown)
- Modify: `main/sensor_bridge.c:191-195` (zone cooldown)

**Step 1: Simplify EP1 cooldown (lines 124-128)**

Replace:
```c
    /* Use fallback cooldown if global fallback is active OR this EP is mid-session */
    uint32_t main_cooldown_sec = (coordinator_fallback_is_active()
                                  || coordinator_fallback_ep_session_active(0))
        ? cfg.fallback_cooldown_sec[0]
        : cfg.occupancy_cooldown_sec[0];
```

With:
```c
    uint32_t main_cooldown_sec = cfg.occupancy_cooldown_sec[0];
```

**Step 2: Simplify zone cooldown (lines 191-195)**

Replace:
```c
        /* Use fallback cooldown if global fallback is active OR this zone is mid-session */
        uint32_t zone_cooldown_sec = (coordinator_fallback_is_active()
                                      || coordinator_fallback_ep_session_active((uint8_t)(i + 1)))
            ? cfg.fallback_cooldown_sec[i + 1]
            : cfg.occupancy_cooldown_sec[i + 1];
```

With:
```c
        uint32_t zone_cooldown_sec = cfg.occupancy_cooldown_sec[i + 1];
```

---

### Task 10: Update coordinator_fallback.h comments

**Files:**
- Modify: `main/coordinator_fallback.h:7-33` (module doc comment)
- Modify: `main/coordinator_fallback.h:50-57` (ep_session_active doc)

**Step 1: Replace module doc comment**

```c
/**
 * Coordinator offline fallback module — dual state machine design.
 *
 * Two independent state machines run in parallel:
 *
 * Normal SM (sensor_bridge): Always uses normal cooldown. Reports occupancy
 * to Z2M via ZCL attributes. Feeds every transition to the fallback module.
 *
 * Fallback SM (this module): Maintains its own fallback_occupied per EP with
 * independent fallback cooldown timers. When fallback activates (soft or hard),
 * dispatches On/Off via binding based on fallback_occupied.
 *
 * Reconciliation is one-way: when fallback clears (ACK during soft, or HA
 * clears hard), the fallback SM pushes all EP fallback_occupied states to Z2M
 * attributes, cancels its timers, and lets the normal SM take over. The normal
 * SM then corrects any stale state on its next poll cycle using its own cooldown.
 *
 * Design notes:
 * - "Always armed": if On/Off bindings exist, fallback can activate. To
 *   prevent fallback, remove the On/Off binding — not a firmware config flag.
 * - "Auto re-arm": after HA clears fallback, the device immediately watches
 *   for ACK failures again.
 * - "Earliest recovery": on clear, all EP states are pushed to Z2M so HA
 *   can reconcile immediately.
 */
```

**Step 2: Update `ep_session_active` doc**

```c
/**
 * Returns true if the given endpoint is in a hard fallback session.
 *
 * @param ep_idx  0=EP1/main, 1-10=EP2-11/zones
 */
bool coordinator_fallback_ep_session_active(uint8_t ep_idx);
```

---

### Task 11: Clean up `coordinator_fallback_set_enable` — cancel fallback cooldown timers on disable

**Files:**
- Modify: `main/coordinator_fallback.c:367-391` (set_enable function)

**Step 1: Add fallback cooldown timer cancellation when disabling**

After clearing `soft_fallback_active` and `awaiting_ack` in the disable path, also cancel fallback cooldown timers:

```c
void coordinator_fallback_set_enable(uint8_t enable)
{
    s_fallback_enabled = (enable != 0);
    nvs_config_save_fallback_enable(enable);

    if (!s_fallback_enabled) {
        /* Cancel pending hard timeout and clear all soft fallbacks */
        if (s_hard_timeout_pending) {
            esp_zb_scheduler_alarm_cancel(hard_timeout_cb, s_hard_timeout_gen);
            s_hard_timeout_pending = false;
        }
        for (int i = 0; i < 11; i++) {
            s_ep[i].soft_fallback_active = false;
            s_ep[i].awaiting_ack = false;
            /* Cancel pending fallback cooldown timer */
            uint8_t cancel_param = (uint8_t)((i << 4) | (s_ep[i].fb_cooldown_gen & 0x0F));
            esp_zb_scheduler_alarm_cancel(fallback_cooldown_cb, cancel_param);
        }
        if (s_soft_fault_count > 0) {
            s_soft_fault_count = 0;
            set_soft_fault_attr(0);
        }
        /* Do NOT clear hard fallback -- that requires explicit HA clear */
        ESP_LOGI(TAG, "Fallback disabled -- soft state cleared, hard fallback preserved");
    } else {
        ESP_LOGI(TAG, "Fallback enabled -- monitoring from next occupancy change");
    }
}
```

---

### Task 12: Build, verify, and commit

**Step 1: Build**

Run: `cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2 && idf.py build 2>&1 | tail -20`
Expected: Clean build, no errors.

**Step 2: Commit all firmware changes**

```bash
cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2
git add main/coordinator_fallback.c main/coordinator_fallback.h main/sensor_bridge.c
git commit -m "refactor: dual state machine fallback with independent cooldowns

Normal SM (sensor_bridge) always uses normal cooldown for Z2M reporting.
Fallback SM (coordinator_fallback) maintains fallback_occupied per EP with
its own cooldown timers. On fallback clear, fallback SM pushes all EP
states to Z2M for earliest recovery, then normal SM takes over.

Removes cooldown switching from sensor_bridge — the two state machines
are fully decoupled."
```

---

## Behavioral Verification

**Scenario: normal_cooldown=0s, fallback_cooldown=60s, soft_timeout=2s**

| Time | Event | Normal SM (Z2M) | Fallback SM |
|------|-------|-----------------|-------------|
| t=0 | occ=1 | Reports occ=1, probe sent | fallback_occupied=1, cancels timer |
| t=0.5s | occ=0 | Reports occ=0 (cooldown=0s) | Starts 60s timer, fallback_occupied=1 |
| t=2s | No ACK → soft | Z2M shows 0 | fb_occ=1 → sends On via binding |
| t=3.5s | ACK arrives | Reconcile: Z2M set to occ=1 | Clears soft. Cancels timers. fb_occ synced to occupied=0. |
| t=3.6s | Next poll | Real sensor occ=0, cooldown=0s → reports occ=0 | fb_occ=0 (synced). No dispatch (not in fallback). |

**Scenario: normal_cooldown=1s, fallback_cooldown=60s, soft_timeout=2s**

| Time | Event | Normal SM (Z2M) | Fallback SM |
|------|-------|-----------------|-------------|
| t=0 | occ=1 | Reports occ=1, probe sent | fallback_occupied=1 |
| t=0.5s | occ=0 | Starts 1s cooldown | Starts 60s timer, fb_occ=1 |
| t=1.5s | normal cooldown done | Reports occ=0 | fb_occ still 1 (timer has 59s left) |
| t=2s | No ACK → soft | Z2M shows 0 | fb_occ=1 → sends On via binding |
| t=62s | fallback cooldown expires | Z2M shows 0 | fb_occ=0. In soft fallback → sends Off via binding |

**Scenario: fallback clear while fallback_occupied=1**

| Time | Event | Normal SM (Z2M) | Fallback SM |
|------|-------|-----------------|-------------|
| — | ACK or HA clear | Reconcile: Z2M set to occ=1 | Timers cancelled. fb_occ synced to occupied. |
| — | Next poll | Normal cooldown takes over | Inactive. fb_occ evolves naturally from future on_occ_change calls. |
