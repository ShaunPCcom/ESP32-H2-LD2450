# Safe Boot Config — Wait for Sensor Before Sending Commands

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate intermittent boot freeze where LD2450 sensor stops streaming data because config commands are sent before the sensor is ready.

**Architecture:** Add a "first frame received" event to the UART driver. Move config application to a dedicated FreeRTOS task that waits for this event + 1s before entering config mode. Harden `exit_config()` with retry logic and sensor restart fallback. This fixes both boot and runtime config-mode exits.

**Tech Stack:** ESP-IDF FreeRTOS, ESP32-H2, LD2450 UART protocol

**Root Cause:** The LD2450 sensor is powered from the H2's 5V rail, so both boot simultaneously. Current code sends config commands after only 200ms — if the sensor isn't ready, `exit_config()` can fail silently, leaving the sensor stuck in config mode (no data streaming). The sensor data freezes at whatever state was captured before config mode was entered.

---

### Task 1: Harden exit_config with retry + restart fallback

The core safety fix. Applies to ALL config-mode usage (boot AND runtime).

**Files:**
- Modify: `components/ld2450/ld2450_cmd.c:142-147` (exit_config)
- Modify: `components/ld2450/ld2450_cmd.c:151-186` (send_config_command)

**Step 1: Add retry logic to exit_config**

Replace the current `exit_config()` function:

```c
/* Exit config mode — retry up to 3 times */
static esp_err_t exit_config(void)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        esp_err_t err = send_frame(CMD_DISABLE_CONF, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "exit_config send failed (attempt %d)", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        err = read_ack(CMD_DISABLE_CONF);
        if (err == ESP_OK) return ESP_OK;
        ESP_LOGW(TAG, "exit_config ACK failed (attempt %d): %s", attempt + 1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGE(TAG, "exit_config failed after 3 attempts");
    return ESP_FAIL;
}
```

**Step 2: Add sensor restart fallback to send_config_command**

Replace `send_config_command` to check exit_config result and restart sensor if it fails:

```c
static esp_err_t send_config_command(uint8_t cmd_id, const uint8_t *value, uint16_t value_len)
{
    esp_err_t err;

    ld2450_rx_pause();

    err = enter_config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter config mode");
        ld2450_rx_resume();
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    err = send_frame(cmd_id, value, value_len);
    if (err != ESP_OK) {
        exit_config();
        ld2450_rx_resume();
        return err;
    }

    err = read_ack(cmd_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Command 0x%02X ACK failed", cmd_id);
        exit_config();
        ld2450_rx_resume();
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    esp_err_t exit_err = exit_config();
    if (exit_err != ESP_OK) {
        ESP_LOGE(TAG, "Config mode exit failed — restarting sensor");
        /* Send restart command directly (already in config mode) */
        send_frame(CMD_RESTART, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ld2450_rx_resume();
    return err;  /* return the command result, not exit result */
}
```

**Step 3: Build to verify no compile errors**

Run: `cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2 && idf.py build`
Expected: Clean build

**Step 4: Commit**

```bash
git add components/ld2450/ld2450_cmd.c
git commit -m "fix: harden exit_config with retry and sensor restart fallback

exit_config() previously had no error handling — if the ACK failed,
the sensor could stay in config mode and stop streaming data frames.

Now retries 3 times, and send_config_command restarts the sensor
as a last resort if config mode exit fails entirely."
```

---

### Task 2: Add "first frame received" event to UART driver

**Files:**
- Modify: `components/ld2450/ld2450.c` (add event group + signal on first frame)
- Modify: `components/ld2450/include/ld2450.h` (add public API)

**Step 1: Add event group and first-frame signaling to ld2450.c**

Add near the top of ld2450.c with other static variables:

```c
#include "freertos/event_groups.h"

#define LD2450_FIRST_FRAME_BIT  BIT0

static EventGroupHandle_t s_event_group = NULL;
```

In `ld2450_init()`, after creating `s_rx_paused_sem` and before `xTaskCreate`:

```c
s_event_group = xEventGroupCreate();
if (!s_event_group) return ESP_ERR_NO_MEM;
```

In `ld2450_uart_task`, inside the `if (ld2450_parser_feed(...))` block, after the first successful parse (just before or after the `changed` check), add a one-shot signal:

```c
static bool s_first_frame_signaled = false;
if (!s_first_frame_signaled) {
    xEventGroupSetBits(s_event_group, LD2450_FIRST_FRAME_BIT);
    s_first_frame_signaled = true;
    ESP_LOGI(TAG, "First data frame received — sensor ready");
}
```

**Step 2: Add public wait API to ld2450.h**

```c
/**
 * Block until the first valid data frame is received from the sensor.
 * Returns ESP_OK if frame received, ESP_ERR_TIMEOUT if timeout_ms elapsed.
 * Use this to confirm the sensor is alive before sending config commands.
 */
esp_err_t ld2450_wait_for_first_frame(uint32_t timeout_ms);
```

Implement in ld2450.c:

```c
esp_err_t ld2450_wait_for_first_frame(uint32_t timeout_ms)
{
    if (!s_event_group) return ESP_ERR_INVALID_STATE;
    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                            LD2450_FIRST_FRAME_BIT,
                                            pdFALSE,  /* don't clear */
                                            pdTRUE,
                                            pdMS_TO_TICKS(timeout_ms));
    return (bits & LD2450_FIRST_FRAME_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}
```

**Step 3: Build to verify**

Run: `cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2 && idf.py build`
Expected: Clean build

**Step 4: Commit**

```bash
git add components/ld2450/ld2450.c components/ld2450/include/ld2450.h
git commit -m "feat: add first-frame event to confirm sensor is alive

Adds ld2450_wait_for_first_frame() which blocks until the UART task
receives a valid data frame. This lets callers confirm the LD2450
sensor has fully booted before sending config commands."
```

---

### Task 3: Move config application to wait for sensor readiness

**Files:**
- Modify: `main/main.cpp:34-61` (apply_saved_config)

**Step 1: Rewrite apply_saved_config to wait for first frame + 1s**

Replace the `apply_saved_config` function:

```c
static void apply_saved_config(const nvs_config_t *cfg)
{
    /* Apply software config to driver (no UART commands, safe immediately) */
    ld2450_set_tracking_mode(cfg->tracking_mode == 1 ? LD2450_TRACK_SINGLE : LD2450_TRACK_MULTI);
    ld2450_set_publish_coords(cfg->publish_coords != 0);

    for (int i = 0; i < 10; i++) {
        ld2450_set_zone((size_t)i, &cfg->zones[i]);
    }

    /* Wait for sensor to prove it's alive before sending UART commands */
    ESP_LOGI(TAG, "Waiting for LD2450 first data frame...");
    esp_err_t err = ld2450_wait_for_first_frame(5000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LD2450 sensor not responding — skipping hardware config");
        return;
    }

    /* Extra settle time after first frame */
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Sensor ready — applying hardware config");

    /* Apply hardware config via sensor commands */
    if (cfg->bt_disabled) {
        ld2450_cmd_set_bluetooth(false);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ld2450_cmd_apply_distance_angle(cfg->max_distance_mm,
                                     cfg->angle_left_deg,
                                     cfg->angle_right_deg);

    ESP_LOGI(TAG, "Saved config applied");
}
```

**Step 2: Note on blocking**

`apply_saved_config` is called from `app_main` before `zigbee_init()`. This is fine — we need the sensor configured before Zigbee reports occupancy. The 1s extra delay is negligible compared to Zigbee steering time.

However, we need to verify `app_main` can tolerate the wait. Check the call site — `apply_saved_config` is called at line 99, before `zigbee_init()` at line 105. The wait blocks `app_main` but the UART task is already running (started at line 95). This is correct.

**Step 3: Build to verify**

Run: `cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2 && idf.py build`
Expected: Clean build

**Step 4: Commit**

```bash
git add main/main.cpp
git commit -m "fix: wait for LD2450 first data frame before sending config commands

Previously sent config commands after only 200ms, before the sensor
was guaranteed to be ready. If the sensor wasn't ready, exit_config
could fail and leave it stuck in config mode (no data streaming).

Now waits for a valid data frame + 1s settle time before entering
config mode. Normal boot delay is ~1-2s total, negligible vs Zigbee
steering time."
```

---

### Task 4: Verify end-to-end

**Step 1: Full build**

Run: `cd /data/shaun/Nextcloud/coding/src/ld2450-zb-h2/ld2450_zb_h2 && idf.py build`
Expected: Clean build, no warnings in modified files

**Step 2: Review boot log sequence**

Flash to a test device and verify serial output shows:
1. `"Waiting for LD2450 first data frame..."`
2. `"First data frame received — sensor ready"` (from UART task)
3. 1s pause
4. `"Sensor ready — applying hardware config"`
5. `"Saved config applied"`
6. Normal Zigbee steering and occupancy reporting

**Step 3: Power-cycle test**

Power cycle the device 10+ times. Verify:
- No frozen zones
- Occupancy updates consistently after Zigbee joins
- Log never shows "exit_config ACK failed" or "restarting sensor" (indicating clean exits)

---

## Summary of Changes

| File | Change | Purpose |
|------|--------|---------|
| `components/ld2450/ld2450_cmd.c` | Retry exit_config 3x, restart sensor on failure | Prevent sensor stuck in config mode |
| `components/ld2450/ld2450.c` | Add event group, signal on first parsed frame | "Sensor alive" confirmation |
| `components/ld2450/include/ld2450.h` | Add `ld2450_wait_for_first_frame()` | Public API for waiting |
| `main/main.cpp` | Wait for first frame + 1s before config commands | Safe boot timing |
