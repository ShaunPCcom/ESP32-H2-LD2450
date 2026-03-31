# Changelog

## v2.2.4 - 2026-03-31

### Features (C6)
- **Web UI OTA trigger**: System tab now has a "Check for Updates" button and an "Update Firmware" button (enabled only when an update is available). An amber banner appears at the top of the page when a newer version is found — clicking it jumps to the System tab.
- **Background update check**: Device polls the OTA index every 12 hours. Interval is configurable (1–72 h) via a slider in the System tab and persists across reboots.
- **Wi-Fi OTA transport**: When Wi-Fi is connected, firmware downloads directly over HTTPS instead of transferring block-by-block over Zigbee — cuts update time from minutes to seconds.

### Internal
- Modular OTA component refactor: `zigbee_ota` split into `ota_state`, `ota_writer`, `ota_header`, `ota_zigbee_transport`, `ota_wifi_transport`, `ota_trigger_z2m`, `ota_trigger_web` — public API unchanged.

---

## v2.2.3 - 2026-03-21

### Fixes
- Enable Boya flash driver for nanoESP32-C6 boards
- Enable WiFi modem sleep and larger OTA blocks to reduce radio coexistence interference on C6

---

## v2.2.2 - 2026-02-28

### Features
- Dual-target support: firmware now builds for both ESP32-H2 and ESP32-C6
- C6 adds Wi-Fi provisioning, web UI (radar visualisation + settings), and mDNS
- Separate OTA images per target (imageType 0x0001 H2 / 0x0003 C6)

---

## v2.2.1 - 2026-01-15

### Fixes
- Coordinator fallback: soft fallback correctly distinguishes per-zone ack timeouts from global hard timeout
- Zigbee signal handler extraction into dedicated source file

---

## v2.2.0 - 2025-12-10

### Features
- Two-tier coordinator fallback: soft (per-zone latency assist) and hard (full device autonomy)
- Heartbeat watchdog with configurable interval
- Fallback cooldown to suppress re-entry noise after recovery
