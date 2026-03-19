// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Coordinator offline fallback module.
 *
 * When the Zigbee coordinator (Z2M/HA) stops ACKing occupancy reports within
 * ACK_TIMEOUT_MS, the device enters fallback mode and sends On/Off commands
 * directly to bound lights via the Zigbee binding table.
 *
 * Fallback mode is sticky — it persists in NVS across reboots and is only
 * cleared when HA explicitly writes fallback_mode=0.  No firmware code may
 * clear fallback mode except coordinator_fallback_clear().
 *
 * Design notes:
 * - "Always armed": if On/Off bindings exist, fallback can activate. To
 *   prevent fallback, remove the On/Off binding — not a firmware config flag.
 * - "Auto re-arm": after HA clears fallback, the device immediately watches
 *   for ACK failures again.
 * - "Lights stay on": exiting fallback does NOT send Off commands. HA
 *   reconciles light state.
 */

/** Initialise fallback state, load NVS, register send_status callback. */
void coordinator_fallback_init(void);

/**
 * Called from sensor_bridge after each occupancy attribute change.
 * Starts the ACK timeout window for the given endpoint.
 *
 * @param endpoint  Zigbee endpoint (1=main, 2-11=zones)
 * @param occupied  New occupancy state
 */
void coordinator_fallback_on_occupancy_change(uint8_t endpoint, bool occupied);

/** Returns true if fallback mode is currently active. */
bool coordinator_fallback_is_active(void);

/**
 * Returns true if the given endpoint index entered occupancy under fallback.
 * Used by sensor_bridge to switch cooldown values for in-progress sessions.
 *
 * @param ep_idx  0=EP1/main, 1-10=EP2-11/zones
 */
bool coordinator_fallback_ep_session_active(uint8_t ep_idx);

/**
 * Clear fallback mode.  Called when HA writes fallback_mode=0.
 * Saves to NVS, resets all per-EP session state, auto re-arms.
 * Does NOT send Off commands to lights — HA reconciles.
 */
void coordinator_fallback_clear(void);

/**
 * Enter fallback mode manually.  Called when HA or CLI writes fallback_mode=1.
 * Normal operation: firmware sets this internally via ACK timeout.
 */
void coordinator_fallback_set(void);
