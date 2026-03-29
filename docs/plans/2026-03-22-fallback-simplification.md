# Fallback Simplification Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove internal light state tracking from the fallback system, send On/Off based purely on occupancy, extend fallback cooldown to cover soft fallback and ACK-awaiting states, and improve Z2M user-facing labels/descriptions.

**Architecture:** Sensor_bridge already handles cooldown switching via `coordinator_fallback_ep_session_active()`. By expanding that function to include `awaiting_ack` and `soft_fallback_active` states, sensor_bridge automatically uses fallback cooldown from the moment a probe is sent — meaning the fallback module never needs its own off-timers or light-state tracking. The fallback module becomes a pure dispatcher: occupied=On, clear=Off, via binding. First coordinator ACK from any communication clears soft fallback globally.

**Tech Stack:** ESP-IDF C firmware, Zigbee2MQTT JavaScript external converter

---

### Task 1: Remove `fallback_light_on` and off-timer infrastructure from fallback struct

**Files:**
- Modify: `main/coordinator_fallback.c:21-29` (struct definition)
- Modify: `main/coordinator_fallback.c:66-67` (forward declarations)

**Step 1: Edit the struct**

Remove `fallback_light_on`, `off_timer_generation`, and `off_timer_param` from `fallback_ep_state_t`:

```c
typedef struct {
    bool     occupied;                /* current occupancy state for this EP */
    bool     awaiting_ack;            /* ACK window is open (alarm scheduled) */
    bool     soft_fallback_active;    /* this EP is in soft fallback (transient) */
    bool     fallback_session_active; /* entered occupancy under hard fallback */
} fallback_ep_state_t;
```

**Step 2: Remove `fallback_off_cb` forward declaration**

Remove line 67: `static void fallback_off_cb(uint8_t param);`

**Step 3: Verify build fails**

Run: `cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2 && idf.py build 2>&1 | tail -20`
Expected: Compilation errors referencing removed fields and `fallback_off_cb`

---

### Task 2: Remove `fallback_off_cb` function entirely

**Files:**
- Modify: `main/coordinator_fallback.c:285-310` (delete entire function)

**Step 1: Delete `fallback_off_cb`**

Remove the entire function (lines 285-310):
```c
static void fallback_off_cb(uint8_t param)
{
    ...
}
```

---

### Task 3: Simplify hard fallback dispatch in `ack_timeout_cb`

**Files:**
- Modify: `main/coordinator_fallback.c:184-214` (hard fallback path in ack_timeout_cb)

**Step 1: Replace the hard fallback path**

The current code (lines 184-214) tracks `fallback_light_on` and schedules off-timers. Replace with simple dispatch:

```c
    /* ---- Hard fallback path: already in hard fallback, dispatch directly ---- */
    if (s_fallback_mode) {
        ESP_LOGW(TAG, "ep%u: ACK timeout (hard fallback active, occ=%d)", endpoint, occupied);
        s_ep[ep_idx].fallback_session_active = true;

        if (occupied) {
            send_onoff_via_binding(endpoint, true);
            ESP_LOGI(TAG, "ep%u: sent On via binding (hard fallback)", endpoint);
        } else {
            send_onoff_via_binding(endpoint, false);
            ESP_LOGI(TAG, "ep%u: sent Off via binding (hard fallback)", endpoint);
        }
        return;
    }
```

---

### Task 4: Simplify soft fallback dispatch in `ack_timeout_cb`

**Files:**
- Modify: `main/coordinator_fallback.c:216-244` (soft fallback path in ack_timeout_cb)

**Step 1: Replace the soft fallback path**

Remove the `fallback_light_on` guard. Send On if occupied, Off if not:

```c
    /* ---- Soft fallback path: transient APS timeout ---- */
    bool already_soft = s_ep[ep_idx].soft_fallback_active;
    s_ep[ep_idx].soft_fallback_active = true;

    if (!already_soft) {
        s_soft_fault_count++;
        set_soft_fault_attr(s_soft_fault_count);
        ESP_LOGW(TAG, "ep%u: soft fallback #%u (occ=%d)",
                 endpoint, s_soft_fault_count, occupied);
    } else {
        ESP_LOGW(TAG, "ep%u: additional soft fault (already soft, occ=%d)", endpoint, occupied);
    }

    /* Dispatch On/Off via binding based on current occupancy */
    if (occupied) {
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
```

---

### Task 5: Simplify hard fallback dispatch in `coordinator_fallback_on_occupancy_change`

**Files:**
- Modify: `main/coordinator_fallback.c:514-546` (hard fallback section of on_occupancy_change)

**Step 1: Replace hard fallback handling**

Remove off-timer scheduling and `fallback_light_on` tracking. Simple dispatch:

```c
    /* If already in hard fallback, handle immediately without ACK probing */
    if (s_fallback_mode) {
        s_ep[ep_idx].fallback_session_active = true;

        if (occupied) {
            send_onoff_via_binding(endpoint, true);
            ESP_LOGI(TAG, "ep%u: sent On via binding (hard fallback)", endpoint);
        } else {
            send_onoff_via_binding(endpoint, false);
            ESP_LOGI(TAG, "ep%u: sent Off via binding (hard fallback)", endpoint);
        }
        return;
    }
```

---

### Task 6: Add soft fallback dispatch in `coordinator_fallback_on_occupancy_change`

**Files:**
- Modify: `main/coordinator_fallback.c` (after hard fallback check, before probing)

**Step 1: Add soft fallback bypass**

After the hard fallback `return` and before the `if (!s_fallback_enabled) return;` gate, add:

```c
    /* If soft fallback is active for this EP, dispatch directly (skip probing) */
    if (s_ep[ep_idx].soft_fallback_active) {
        if (occupied) {
            send_onoff_via_binding(endpoint, true);
            ESP_LOGI(TAG, "ep%u: sent On via binding (soft fallback, occ change)", endpoint);
        } else {
            send_onoff_via_binding(endpoint, false);
            ESP_LOGI(TAG, "ep%u: sent Off via binding (soft fallback, occ change)", endpoint);
        }
        return;
    }
```

---

### Task 7: Expand `coordinator_fallback_ep_session_active` to cover soft and awaiting states

**Files:**
- Modify: `main/coordinator_fallback.c:578-582` (ep_session_active function)

**Step 1: Update the function**

This is the key change that makes sensor_bridge use fallback cooldown from the moment a probe is sent:

```c
bool coordinator_fallback_ep_session_active(uint8_t ep_idx)
{
    if (ep_idx > 10) return false;
    return s_ep[ep_idx].fallback_session_active
        || s_ep[ep_idx].soft_fallback_active
        || s_ep[ep_idx].awaiting_ack;
}
```

**Why:** Sensor_bridge checks this to decide normal vs fallback cooldown. By returning true during `awaiting_ack`, the fallback cooldown takes effect immediately when a probe is sent — before the soft timeout even fires. This gives the user's desired behavior: occ=1 at t=0, real occ=0 at t=1s, but occupancy holds for 60s (fallback cooldown) rather than 1s (normal cooldown).

---

### Task 8: Clean up `coordinator_fallback_clear` — remove off-timer references

**Files:**
- Modify: `main/coordinator_fallback.c:584-631` (coordinator_fallback_clear function)

**Step 1: Remove off-timer cancellation and `fallback_light_on` reset**

```c
void coordinator_fallback_clear(void)
{
    ESP_LOGI(TAG, "Hard fallback cleared by HA (fallback_mode=0)");

    s_fallback_mode        = false;
    s_fallback_reported    = false;
    s_coordinator_reachable = true;

    /* Cancel pending hard timeout */
    if (s_hard_timeout_pending) {
        esp_zb_scheduler_alarm_cancel(hard_timeout_cb, s_hard_timeout_gen);
        s_hard_timeout_pending = false;
    }

    /* Reset per-EP state */
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

    /* Do NOT send Off -- HA reconciles */

    if (s_heartbeat_enabled) {
        cancel_heartbeat_watchdog();
        start_heartbeat_watchdog();
    }

    ESP_LOGI(TAG, "Hard fallback cleared -- normal ACK tracking resumed");
}
```

---

### Task 9: Clean up `coordinator_fallback_set_enable` — remove `fallback_light_on` reference

**Files:**
- Modify: `main/coordinator_fallback.c:419-443` (set_enable function)

No `fallback_light_on` references here, but verify no off-timer references either. The current code only clears `soft_fallback_active` and `awaiting_ack` — should be clean after struct changes.

---

### Task 10: Clean up soft→hard escalation in `hard_timeout_cb`

**Files:**
- Modify: `main/coordinator_fallback.c:251-279` (hard_timeout_cb)

**Step 1: Remove the `fallback_light_on` comment**

```c
    /* Escalate: move soft EP state to hard fallback session state */
    for (int i = 0; i < 11; i++) {
        if (s_ep[i].soft_fallback_active) {
            s_ep[i].soft_fallback_active    = false;
            s_ep[i].fallback_session_active = true;
        }
    }
```

Remove the line: `/* fallback_light_on already set from soft dispatch */`

---

### Task 11: Build and verify firmware compiles

**Step 1: Build**

Run: `cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2 && idf.py build 2>&1 | tail -20`
Expected: Clean build, no errors or warnings related to removed fields.

**Step 2: Commit firmware changes**

```bash
cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2
git add main/coordinator_fallback.c
git commit -m "refactor: remove internal light state tracking from fallback

Remove fallback_light_on, off-timer infrastructure, and fallback_off_cb.
Fallback now dispatches On/Off purely based on occupancy — no guards.
Expand ep_session_active to include soft_fallback and awaiting_ack
states so sensor_bridge uses fallback cooldown from probe send time.
Add direct dispatch when occupancy changes during active soft fallback."
```

---

### Task 12: Rename Z2M labels and update descriptions

**Files:**
- Modify: `z2m/ld2450_zb_h2.js` (expose definitions around lines 467-484)

**Step 1: Rename "ACK timeout" to "Soft fallback timeout"**

Find the `numericExpose` for `ack_timeout_ms` and update:

```javascript
    numericExpose('ack_timeout_ms', 'Soft fallback timeout', ACCESS_ALL,
        'Time in milliseconds the device waits for a coordinator response after an occupancy change. ' +
        'If no response arrives within this window, the device enters soft fallback: it sends On/Off ' +
        'commands directly to bound lights based on occupancy, bypassing the coordinator. Soft fallback ' +
        'is temporary — the first successful coordinator response clears it globally. ' +
        'Default: 2000ms. Increase if your Zigbee network has high latency.',
        {unit: 'ms', value_min: 500, value_max: 10000, value_step: 100}),
```

**Step 2: Rename "Hard timeout" to "Hard fallback timeout"**

Find the `numericExpose` for `hard_timeout_sec` and update:

```javascript
    numericExpose('hard_timeout_sec', 'Hard fallback timeout', ACCESS_ALL,
        'Time in seconds after soft fallback activates before the device escalates to hard (sticky) fallback. ' +
        'In hard fallback, the device permanently controls bound lights based on occupancy until Home Assistant ' +
        'explicitly clears the fallback state. Hard fallback persists across reboots. ' +
        'Default: 10s. Set higher to tolerate brief coordinator outages without escalating.',
        {unit: 's', value_min: 5, value_max: 120, value_step: 1}),
```

**Step 3: Update the fallback_enable description for completeness**

Find the `binaryExpose` for `fallback_enable` and update:

```javascript
    binaryExpose('fallback_enable', 'Fallback enable', ACCESS_ALL,
        'Enable the soft/hard two-tier fallback system. When off, the device relies entirely on Home Assistant ' +
        'for light control. When on, the device monitors coordinator responsiveness: if no response arrives ' +
        'within the soft fallback timeout, it temporarily controls bound lights directly. If the coordinator ' +
        'remains unresponsive beyond the hard fallback timeout, the device enters persistent autonomous mode ' +
        'until Home Assistant explicitly clears it.'),
```

**Step 4: Commit Z2M changes**

```bash
cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2
git add z2m/ld2450_zb_h2.js
git commit -m "docs(z2m): rename fallback timeouts and improve descriptions

Rename 'ACK timeout' → 'Soft fallback timeout' and
'Hard timeout' → 'Hard fallback timeout' for clarity.
Expand descriptions to explain the full soft→hard fallback lifecycle."
```

---

### Task 13: Update coordinator_fallback.h comments

**Files:**
- Modify: `main/coordinator_fallback.h`

**Step 1: Update the module doc comment and function docs**

Update the header comment (lines 7-25) to reflect new behavior:
- Remove mention of "always armed" binding requirement (unchanged)
- Update the description to reflect that On/Off is dispatched purely on occupancy
- Update `coordinator_fallback_ep_session_active` doc to mention it now covers soft and awaiting states

```c
/**
 * Coordinator offline fallback module.
 *
 * When the Zigbee coordinator (Z2M/HA) stops ACKing occupancy reports within
 * the soft fallback timeout, the device enters soft fallback and sends On/Off
 * commands directly to bound lights based on current occupancy.  No internal
 * light state is tracked — the device sends On when occupied, Off when clear,
 * regardless of assumed light state.
 *
 * Soft fallback is transient — the first coordinator ACK clears it globally.
 * If no ACK arrives within the hard fallback timeout (counted from soft
 * activation), the device escalates to hard fallback: a sticky NVS-backed
 * mode that persists across reboots until HA explicitly clears it.
 *
 * Cooldown switching: sensor_bridge uses fallback cooldown (instead of normal
 * cooldown) whenever any fallback state is active for an endpoint, including
 * the ACK-awaiting window.  This ensures occupancy holds long enough for the
 * fallback system to act.
 *
 * Design notes:
 * - "Always armed": if On/Off bindings exist, fallback can activate. To
 *   prevent fallback, remove the On/Off binding — not a firmware config flag.
 * - "Auto re-arm": after HA clears fallback, the device immediately watches
 *   for ACK failures again.
 * - "Lights stay on": exiting fallback does NOT send Off commands. HA
 *   reconciles light state.
 */
```

Update `coordinator_fallback_ep_session_active` doc:

```c
/**
 * Returns true if the given endpoint is in any fallback-related state:
 * awaiting ACK, soft fallback active, or hard fallback session active.
 * Used by sensor_bridge to switch from normal to fallback cooldown.
 *
 * @param ep_idx  0=EP1/main, 1-10=EP2-11/zones
 */
bool coordinator_fallback_ep_session_active(uint8_t ep_idx);
```

**Step 2: Commit**

```bash
cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2
git add main/coordinator_fallback.h
git commit -m "docs: update fallback header comments for simplified model"
```

---

## Behavioral Summary (for verification)

**Scenario: occ=1, real occ=0 at 1s, no ACK by 2s (soft=2s, hard=10s, normal_cooldown=1s, fallback_cooldown=60s)**

| Time | Event | Behavior |
|------|-------|----------|
| t=0 | occ=1 | sensor_bridge reports occupied, calls `on_occupancy_change(ep, true)`. Probe sent, `awaiting_ack=true`. `ep_session_active` now returns true → fallback cooldown (60s) applies. |
| t=1s | real occ=0 | sensor_bridge starts cooldown timer with 60s (not 1s). Occupancy attribute stays 1. |
| t=2s | ACK timeout | `ack_timeout_cb` fires. `occupied` param = true (baked at t=0). Soft fallback activates. Sends On via binding. Hard timeout scheduled for t=12s. |
| t=61s | cooldown expires | sensor_bridge reports clear, calls `on_occupancy_change(ep, false)`. Soft fallback still active → sends Off via binding directly. |
| — | OR: ACK at any time | First ACK clears soft fallback globally. Cancels hard timeout. HA reconciles. |
| t=12s | hard timeout (if no ACK) | Escalates to hard fallback. Sticky until HA clears. |
