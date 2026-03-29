// SPDX-License-Identifier: MIT
#pragma once

#include "esp_err.h"

/**
 * @file web_server.h
 * @brief HTTP server for LD2450 web configuration interface (C6 only).
 *
 * REST endpoints:
 *   GET  /                    Setup page (AP mode) or status page (STA mode)
 *   GET  /generate_204        Android captive portal trigger (302 → /)
 *   GET  /hotspot-detect.html iOS captive portal trigger (302 → /)
 *   GET  /ncsi.txt            Windows captive portal trigger (302 → /)
 *   GET  /api/wifi-scan       JSON array of visible SSIDs
 *   GET  /api/config          JSON of full device config
 *   POST /api/config          Partial config update
 *   GET  /api/status          Firmware version, uptime, heap, WiFi state
 *   POST /api/wifi            Save WiFi credentials + hostname (triggers reboot)
 *   POST /api/wifi-reset      Clear WiFi credentials (triggers reboot to AP mode)
 *   POST /api/restart         Reboot device
 *   POST /api/factory-reset   Full factory reset
 *
 * WebSocket:
 *   WS   /ws/targets          Target stream at 2 Hz (operational mode)
 *                             Frame: {"t":[{"x":mm,"y":mm,"p":bool},...],
 *                                     "occ":bool,"z":[bool x10]}
 */

esp_err_t web_server_start(void);
void      web_server_stop(void);
