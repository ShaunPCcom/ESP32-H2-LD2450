#pragma once

#include <stdint.h>  // uint8_t, uint16_t, uint32_t types

/**
 * @file project_defaults.hpp
 * @brief Project-wide default configuration values for LD2450 Zigbee Sensor
 *
 * PURPOSE:
 * This file centralizes ALL built-in default values, constants, and configuration
 * parameters for the LD2450 radar sensor Zigbee gateway project. It replaces scattered
 * magic numbers across the codebase with well-documented, single-source-of-truth values.
 *
 * RULES FOR USE:
 * 1. This is the ONLY place default values should live - no magic numbers in source files
 * 2. All project code references defaults:: namespace for their default values
 * 3. This file replaces board_config.h for GPIO pins and hardware configuration
 * 4. Shared components (LD2450 driver, etc.) receive defaults as constructor/init
 *    parameters - they don't include this file directly to avoid circular dependencies
 * 5. When adding a new constant:
 *    - Place it in the appropriate logical section
 *    - Write a clear comment explaining WHAT it is, WHY this value, and any context
 *    - Use descriptive names (not abbreviations unless industry-standard)
 *
 * USAGE EXAMPLES:
 * - In project code:
 *     gpio_set_direction(defaults::LD2450_UART_TX_GPIO, GPIO_MODE_OUTPUT);
 *     sensor_poll_interval = defaults::SENSOR_POLL_INTERVAL_MS;
 *
 * - Passing to components:
 *     ld2450_config_t cfg = {
 *         .uart_num = defaults::LD2450_UART_NUM,
 *         .tx_gpio = defaults::LD2450_UART_TX_GPIO,
 *         .rx_gpio = defaults::LD2450_UART_RX_GPIO,
 *         .baud_rate = defaults::LD2450_UART_BAUD,
 *     };
 *
 * - NVS default values:
 *     if (nvs_read_failed) {
 *         max_distance = defaults::LD2450_MAX_DISTANCE_MM;
 *     }
 *
 * WHAT SHOULD NOT GO HERE:
 * - Runtime variables (those belong in their respective modules)
 * - Zigbee stack constants from esp-zigbee-sdk (use SDK headers)
 * - Hardware capabilities determined at runtime (flash size, chip revision)
 * - Values that must be calculated based on other values (derived constants)
 */

namespace defaults {

// ============================================================================
// Hardware Configuration - ESP32-H2 GPIO Pin Assignments
// ============================================================================

/**
 * UART number for LD2450 sensor communication
 *
 * ESP32-H2 has 3 UART peripherals (UART_NUM_0, UART_NUM_1, UART_NUM_2).
 * UART0 is reserved for console/debugging. UART1 chosen for LD2450 sensor.
 *
 * Why UART1: Available GPIOs pair well with UART1 alternate functions,
 * avoiding routing conflicts with other peripherals.
 */
constexpr uint8_t LD2450_UART_NUM = 1;  // UART_NUM_1

/**
 * GPIO for UART TX to LD2450 sensor (ESP32 TX -> Sensor RX)
 *
 * Transmits command frames to configure sensor (distance limits, angles,
 * tracking mode, zone definitions). Command rate is low (~1 per second max),
 * so timing not critical.
 *
 * Why GPIO12: Paired with GPIO22 for UART1, good signal integrity at 256000 baud.
 * GPIO9 avoided (shared with BOOT button).
 */
constexpr uint8_t LD2450_UART_TX_GPIO = 12;

/**
 * GPIO for UART RX from LD2450 sensor (ESP32 RX <- Sensor TX)
 *
 * Receives continuous data stream at 10Hz (100ms intervals). Each frame
 * contains up to 3 target positions (x,y coordinates in mm) plus zone
 * occupancy state. High reliability required - sensor data drives real-time
 * Zigbee occupancy reporting for Home Assistant automations.
 *
 * Why GPIO22: Natural pairing with GPIO12 for UART1. No conflicts with
 * Zigbee radio (2.4GHz) or other critical pins.
 */
constexpr uint8_t LD2450_UART_RX_GPIO = 22;

/**
 * UART baud rate for LD2450 sensor communication (256000 bps)
 *
 * Fixed by LD2450 hardware - not configurable. Sensor transmits at 10Hz,
 * each frame ~60 bytes, so 600 bytes/sec = 4800 bps data rate. 256000 baud
 * provides 53× safety margin for bursts and overhead.
 *
 * Critical: Must match sensor's fixed rate exactly. Wrong baud causes frame
 * corruption and lost occupancy events.
 */
constexpr uint32_t LD2450_UART_BAUD = 256000;

/**
 * UART RX buffer size (2048 bytes)
 *
 * Sized to buffer ~34 complete frames (60 bytes each) before overflow.
 * At 10Hz sensor rate, this provides 3.4 seconds of buffering if processing
 * is delayed (e.g., during Zigbee network activity or NVS writes).
 *
 * Why 2048: Power-of-2 size optimal for ESP32-H2 DMA. Balances memory
 * usage (only 2KB) vs overflow protection. Typical frame processing takes
 * <10ms, so 3.4s buffer is extremely conservative.
 */
constexpr uint16_t LD2450_UART_RX_BUFFER_SIZE = 2048;

/**
 * GPIO for onboard status LED (ESP32-H2-DevKitM-1 built-in WS2812)
 *
 * Single RGB LED used for device status indication, driven via RMT TX channel 0.
 * Status colors:
 *   - Amber (blinking): Device not joined to Zigbee network
 *   - Blue (blinking): Pairing mode active (waiting for coordinator)
 *   - Green (solid 5s): Successfully joined network (then turns off)
 *   - Red (blinking fast): Error condition (5s, then back to pairing mode)
 *   - Off: Normal operation (after successful join)
 *
 * Why RMT: WS2812 requires precise timing (±150ns tolerance). RMT peripheral
 * generates bit-level timing independent of CPU load. Alternative (bit-banging)
 * would block CPU and miss UART frames from sensor.
 *
 * Why GPIO8: ESP32-H2-DevKitM-1 board routes onboard WS2812 to GPIO8.
 * Hardware constraint, not user choice.
 */
constexpr uint8_t BOARD_LED_GPIO = 8;

/**
 * Number of LEDs in status LED strip (1 LED)
 *
 * ESP32-H2-DevKitM-1 has single WS2812 RGB LED on board. Some code paths
 * (led_strip library) expect LED count parameter even for single LED.
 *
 * Future hardware (custom PCB) might have multiple status LEDs for richer
 * feedback (e.g., separate network/sensor/zone indicators). This constant
 * makes that expansion trivial.
 */
constexpr uint8_t BOARD_LED_COUNT = 1;

/**
 * GPIO for boot/user button (ESP32-H2-DevKitM-1 built-in)
 *
 * Multi-function button with hold-time detection for factory reset operations:
 *   - 3 second hold: Zigbee network reset (leave network, keep config)
 *   - 10 second hold: Full factory reset (Zigbee + NVS erase, all settings lost)
 *   - Visual feedback: Red blinking indicates hold progress
 *
 * Technical: Active-low with internal pull-up. Polled every 100ms by button
 * task for hold detection.
 *
 * Why GPIO9: Hardware constraint - ESP32-H2-DevKitM-1 boot button routes to
 * GPIO9. Also used during chip boot (hold low = download mode), so unavailable
 * for UART or other critical functions.
 */
constexpr uint8_t BOARD_BUTTON_GPIO = 9;

// ============================================================================
// LD2450 Sensor Specifications and Defaults
// ============================================================================

/**
 * Default maximum detection distance (6000 millimeters = 6 meters)
 *
 * LD2450 radar maximum range. Targets beyond this distance are ignored.
 * Trade-off: Longer range increases coverage area but adds false positives
 * from distant movement (e.g., adjacent rooms).
 *
 * Why 6000mm: Sensor hardware maximum. Users typically reduce via Zigbee
 * attribute (2000-4000mm common for room occupancy). Default at max allows
 * full flexibility.
 *
 * Range: 0-6000mm per sensor datasheet. Values >6000 are clamped by driver.
 */
constexpr uint16_t LD2450_MAX_DISTANCE_MM = 6000;

/**
 * Default left-side field-of-view angle (60 degrees)
 *
 * LD2450 detects targets in cone-shaped FOV. Left angle defines left boundary.
 * 0° = straight ahead, 90° = perpendicular left.
 *
 * Why 60°: Balances wide coverage (120° total when combined with right angle)
 * vs false positives from sides. Typical room corner placement needs ~60-70°
 * each side to cover room without seeing through walls.
 *
 * Range: 0-90° per sensor datasheet. Sensor coordinate system: +X right, +Y forward.
 */
constexpr uint8_t LD2450_ANGLE_LEFT_DEG = 60;

/**
 * Default right-side field-of-view angle (60 degrees)
 *
 * Symmetric with left angle for balanced coverage. See LD2450_ANGLE_LEFT_DEG
 * for detailed explanation.
 *
 * Range: 0-90° per sensor datasheet.
 */
constexpr uint8_t LD2450_ANGLE_RIGHT_DEG = 60;

/**
 * Default tracking mode (0 = multi-target)
 *
 * LD2450 can track up to 3 simultaneous targets. Modes:
 *   - 0 (multi-target): Reports all detected targets (up to 3). Occupancy = any target present.
 *   - 1 (single-target): Reports only closest/strongest target. Useful for presence detection
 *                        where multiple targets cause jitter in automations.
 *
 * Why multi-target default: Provides maximum information. Home Assistant can filter
 * via automations if needed. Single-target mode discards data irreversibly.
 *
 * Use case: Multi for occupancy counting, single for simple presence detection.
 */
constexpr uint8_t LD2450_TRACKING_MODE_MULTI = 0;

/**
 * Default coordinate publishing (0 = off)
 *
 * When enabled, LD2450 firmware publishes target (x,y) coordinates as Zigbee
 * attribute 0xFC00:0x0001 (CHAR_STRING format: "x1,y1;x2,y2;x3,y3").
 *
 * Why disabled by default: Coordinate updates are high-frequency (10Hz) and
 * increase Zigbee network traffic significantly. Most users only need occupancy
 * binary (present/not present). Advanced users enable for heatmap visualization
 * or zone debugging.
 *
 * Trade-off: Coordinates useful for tuning zones, but rapid attribute updates
 * can saturate low-power Zigbee networks (especially with many sensors).
 */
constexpr uint8_t LD2450_COORD_PUBLISHING_OFF = 0;

/**
 * Default Bluetooth state (1 = disabled)
 *
 * LD2450 sensor has built-in Bluetooth for mobile app configuration. When
 * disabled via command, sensor saves ~20mA power and eliminates BT interference
 * risk with Zigbee 2.4GHz radio.
 *
 * Why disabled by default: After initial Zigbee setup, all configuration happens
 * via Z2M/Home Assistant. BT not needed. Disabling reduces power consumption and
 * eliminates one potential RF interference source.
 *
 * Note: BT state persists in sensor's onboard flash. Re-enable requires UART command.
 */
constexpr uint8_t LD2450_BT_DISABLED = 1;

// ============================================================================
// Zigbee Configuration - Network and Device Settings
// ============================================================================

/**
 * Zigbee manufacturer name string (ZCL format)
 *
 * First byte (0x07) = string length, followed by 7 ASCII characters.
 * Displayed in Z2M device info and HA device registry.
 *
 * Why "LD2450Z": Identifies sensor model (LD2450) with Zigbee suffix.
 * Avoids confusion with non-Zigbee variants (WiFi, UART-only).
 *
 * ZCL format: CharString type, length-prefixed (not null-terminated).
 * Max 254 characters per ZCL spec.
 */
constexpr const char* ZB_MANUFACTURER_NAME = "\x07""LD2450Z";

/**
 * Zigbee model identifier string (ZCL format)
 *
 * First byte (0x09) = string length, followed by 9 ASCII characters.
 * Identifies hardware variant (ESP32-H2 implementation).
 *
 * Why "LD2450-H2": Distinguishes from future ESP32-C6 port or other hardware.
 * Z2M external converter uses this string to identify device for attribute
 * mappings and UI generation.
 */
constexpr const char* ZB_MODEL_IDENTIFIER = "\x09""LD2450-H2";

/**
 * Zigbee device type ID (Occupancy Sensor)
 *
 * Value 0x0107 = Occupancy Sensor per Zigbee Home Automation profile.
 * Determines which clusters are expected by coordinator:
 *   - Basic cluster (0x0000) - device identity
 *   - Identify cluster (0x0003) - find device via blinking
 *   - Occupancy Sensing cluster (0x0406) - binary occupancy state
 *
 * Why Occupancy Sensor: LD2450's primary purpose is presence detection.
 * Other Zigbee device types (contact sensor, motion sensor) don't fit
 * radar's continuous detection model.
 *
 * Note: ESP-IDF Zigbee SDK doesn't define constant for 0x0107, so raw hex used.
 */
constexpr uint16_t ZB_DEVICE_ID_OCCUPANCY_SENSOR = 0x0107;

// ============================================================================
// Zigbee Endpoints - Multi-Endpoint Device Structure
// ============================================================================

/**
 * Main endpoint number (1)
 *
 * Endpoint 0 reserved by Zigbee spec for ZDO (device management). Application
 * endpoints start at 1.
 *
 * Main endpoint (EP1) provides:
 *   - Overall occupancy (any target detected anywhere)
 *   - Occupancy Sensing cluster (0x0406)
 *   - Custom cluster 0xFC00 (target count, coordinates, sensor config)
 *   - Basic, Identify clusters
 *   - OTA Upgrade cluster (0x0019)
 *
 * Why EP1 for main: Follows Zigbee convention. Coordinator expects primary
 * functionality on lowest endpoint number.
 */
constexpr uint8_t ZB_EP_MAIN = 1;

/**
 * Base endpoint number for zone endpoints (2)
 *
 * LD2450 supports up to 5 user-defined zones (polygons in sensor's coordinate
 * space). Each zone gets dedicated endpoint for independent occupancy reporting.
 *
 * Zone-to-endpoint mapping:
 *   - Zone 0 (array index 0) = Endpoint 2
 *   - Zone 1 (array index 1) = Endpoint 3
 *   - Zone 2 (array index 2) = Endpoint 4
 *   - Zone 3 (array index 3) = Endpoint 5
 *   - Zone 4 (array index 4) = Endpoint 6
 *
 * Each zone endpoint provides:
 *   - Per-zone occupancy (target inside zone polygon)
 *   - Occupancy Sensing cluster (0x0406)
 *   - Custom cluster 0xFC01 (zone vertex coordinates)
 *   - Basic, Identify clusters
 *
 * Why separate endpoints: Allows Home Assistant to create separate binary_sensor
 * entities per zone. Users can trigger different automations based on which
 * room area is occupied (e.g., "desk zone" vs "doorway zone").
 */
constexpr uint8_t ZB_EP_ZONE_BASE = 2;

/**
 * Number of zone endpoints (5)
 *
 * LD2450 hardware limitation. Sensor internal processing supports max 5 zones.
 *
 * Why 5: Balance between granularity (more zones = finer room coverage) and
 * complexity (each zone needs 4 vertex coordinates = 16 values total).
 * Most rooms need 2-3 zones (bed area, doorway, desk). 5 provides headroom.
 */
constexpr uint8_t ZB_EP_ZONE_COUNT = 5;

/**
 * Macro to calculate zone endpoint number from zone index
 *
 * Usage: ZB_EP_ZONE(0) = 2, ZB_EP_ZONE(1) = 3, ..., ZB_EP_ZONE(4) = 6
 *
 * Not a constexpr because it takes parameter. Use inline function in C++ code:
 *   constexpr uint8_t zb_ep_zone(uint8_t idx) { return ZB_EP_ZONE_BASE + idx; }
 */
// #define ZB_EP_ZONE(n)  (ZB_EP_ZONE_BASE + (n))  // Provided as macro in zigbee_defs.h

// ============================================================================
// Zigbee Custom Cluster IDs (Manufacturer-Specific)
// ============================================================================

/**
 * Custom cluster for LD2450 configuration and target data (0xFC00)
 *
 * Manufacturer-specific cluster on EP1 for sensor-specific functionality:
 *   - Attribute 0x0000: target_count (U8, read-only) - number of detected targets (0-3)
 *   - Attribute 0x0001: target_coords (CHAR_STRING, read-only) - "(x1,y1;x2,y2;x3,y3)"
 *   - Attribute 0x0010: max_distance (U16, read-write) - detection range limit (0-6000 mm)
 *   - Attribute 0x0011: angle_left (U8, read-write) - left FOV angle (0-90°)
 *   - Attribute 0x0012: angle_right (U8, read-write) - right FOV angle (0-90°)
 *   - Attribute 0x0020: tracking_mode (U8, read-write) - 0=multi, 1=single target
 *   - Attribute 0x0021: coord_publishing (U8, read-write) - 0=off, 1=on
 *   - Attribute 0x0022: occupancy_cooldown (U16, read-write) - clear delay (0-300 seconds)
 *   - Attribute 0x0023: occupancy_delay (U16, read-write) - detect delay (0-65535 milliseconds)
 *   - Attribute 0x00F0: restart (U8, write-only) - write any value to reboot device
 *
 * Why 0xFC00: Range 0xFC00-0xFFFE reserved for manufacturer-specific clusters
 * per Zigbee spec. 0xFC00 chosen as first available, simple to remember.
 *
 * Design: Single cluster holds both live data (target count/coords) and
 * configuration (distance, angles). Keeps EP1 focused vs scattering across
 * multiple custom clusters.
 */
constexpr uint16_t ZB_CLUSTER_LD2450_CONFIG = 0xFC00;

/**
 * Custom cluster for zone vertex configuration (0xFC01)
 *
 * Defines polygon shapes for 5 detection zones. Each zone endpoint (EP2-EP6)
 * has this cluster with 8 consecutive attributes (4 vertices × 2 coords each):
 *   - Attribute 0x0000: vertex_0_x (S16, mm)
 *   - Attribute 0x0001: vertex_0_y (S16, mm)
 *   - Attribute 0x0002: vertex_1_x (S16, mm)
 *   - Attribute 0x0003: vertex_1_y (S16, mm)
 *   - ... (up to vertex_3_y at 0x0007)
 *   - Attribute 0x0022: occupancy_cooldown (U16, 0-300 seconds) - per-zone cooldown
 *   - Attribute 0x0023: occupancy_delay (U16, 0-65535 ms) - per-zone delay
 *
 * Coordinate system: LD2450 sensor reference frame (+X right, +Y forward).
 * Range: -6000 to +6000 mm (full sensor FOV).
 *
 * Why separate cluster: Zone geometry is separate concern from sensor config.
 * 8 attributes × 5 endpoints = 40 zone attributes. Would clutter 0xFC00 cluster.
 *
 * Polygon rules: Vertices must form non-self-intersecting quadrilateral.
 * Firmware doesn't validate - user responsible via Z2M or CLI.
 */
constexpr uint16_t ZB_CLUSTER_LD2450_ZONE = 0xFC01;

/**
 * Number of vertex coordinate attributes per zone (8)
 *
 * 4 vertices × 2 coordinates (x, y) = 8 attributes per zone endpoint.
 * Attributes 0x0000-0x0007 on cluster 0xFC01.
 */
constexpr uint8_t ZB_ATTR_ZONE_VERTEX_COUNT = 8;

// ============================================================================
// Timing Constants - Sensor Polling and Reporting
// ============================================================================

/**
 * Sensor polling interval (100 milliseconds)
 *
 * LD2450 sensor outputs data frames at fixed 10Hz rate (every 100ms). Firmware
 * polls Zigbee attribute store at same rate to update occupancy state.
 *
 * Why 100ms: Matches sensor native rate exactly. Faster polling wastes CPU
 * (no new data available). Slower polling misses frames and delays occupancy
 * updates (bad for responsive automations like "turn on light when entering room").
 *
 * Implementation: esp_zb_scheduler_alarm() periodic callback in Zigbee task.
 */
constexpr uint16_t SENSOR_POLL_INTERVAL_MS = 100;

/**
 * Zigbee reporting minimum interval (0 seconds)
 *
 * Minimum time between occupancy attribute reports sent to coordinator.
 * 0 = no minimum, report immediately when state changes.
 *
 * Why 0: Occupancy state changes are infrequent (seconds to minutes) and
 * critical for automations. Immediate reporting provides best user experience.
 * Network traffic negligible (~20 bytes per event).
 *
 * ZCL reporting: Coordinator subscribes to attribute changes. Device sends
 * unsolicited reports when value changes, throttled by min/max intervals.
 */
constexpr uint16_t REPORT_MIN_INTERVAL_SEC = 0;

/**
 * Zigbee reporting maximum interval (300 seconds = 5 minutes)
 *
 * Maximum time between occupancy reports even if state hasn't changed.
 * Periodic "heartbeat" to verify device still connected.
 *
 * Why 300s: Balances network health monitoring (coordinator knows device alive)
 * vs traffic overhead. Commercial sensors use 5-15 minute heartbeats.
 * Shorter intervals waste battery (not applicable for mains-powered LD2450)
 * and network bandwidth.
 *
 * Note: State change reports (min interval = 0) bypass this. Max interval
 * only applies to repeated "no change" reports.
 */
constexpr uint16_t REPORT_MAX_INTERVAL_SEC = 300;

/**
 * Default occupancy cooldown time (0 seconds)
 *
 * After targets disappear, firmware waits this duration before reporting
 * occupancy = false. Prevents flicker when person briefly obscured or sensor
 * loses tracking momentarily.
 *
 * Why 0 default: Conservative - reports state changes immediately. Users can
 * increase per-endpoint via Zigbee attribute 0xFC00:0x0022 (main) or 0xFC01:0x0022
 * (zones) to reduce automation chatter.
 *
 * Typical values: 5-30 seconds for room presence (tolerate brief absence like
 * bending down), 60-300 seconds for room occupancy (tolerate bathroom visit).
 *
 * Range: 0-300 seconds (5 minutes max). Enforced by NVS storage and Z2M converter.
 */
constexpr uint16_t OCCUPANCY_COOLDOWN_SEC_DEFAULT = 0;

/**
 * Default occupancy delay time (250 milliseconds)
 *
 * After targets appear, firmware waits this duration before reporting
 * occupancy = true. Filters transient detections (person walking past doorway,
 * pets, reflections).
 *
 * Why 250ms: Long enough to filter single spurious frames (sensor occasionally
 * reports ghost target for 1-2 frames at 10Hz = 100-200ms). Short enough for
 * responsive automations (human doesn't notice <500ms latency).
 *
 * Typical values: 0ms (immediate, accept all detections), 250-500ms (filter
 * glitches, still responsive), 1000-2000ms (require sustained presence).
 *
 * Range: 0-65535 milliseconds (~65 seconds max). U16 limits upper bound.
 */
constexpr uint16_t OCCUPANCY_DELAY_MS_DEFAULT = 250;

// ============================================================================
// Button Configuration - Factory Reset Timing
// ============================================================================

/**
 * Button hold duration for Zigbee network reset (3000 milliseconds = 3 seconds)
 *
 * Hold boot button for 3 seconds to trigger Zigbee network leave operation.
 * Clears network credentials and steering state, but preserves:
 *   - Sensor configuration (max distance, angles, tracking mode)
 *   - Zone definitions (all 5 zone polygons)
 *   - Occupancy timing (cooldown, delay)
 *
 * After reset, device enters pairing mode (blue status LED) waiting for
 * coordinator to permit join.
 *
 * Why 3 seconds: Long enough to prevent accidental activation (requires
 * deliberate action), short enough for user convenience. Industry standard
 * for Zigbee device reset (matches Philips Hue, IKEA Tradfri patterns).
 *
 * Visual feedback: Red LED blinks fast (5Hz) during 1-3 second hold to
 * indicate reset arming.
 */
constexpr uint32_t BOARD_BUTTON_HOLD_ZIGBEE_MS = 3000;

/**
 * Button hold duration for full factory reset (10000 milliseconds = 10 seconds)
 *
 * Hold boot button for 10 seconds to trigger complete device reset:
 *   - Zigbee network leave (same as 3s hold)
 *   - NVS flash erase (all saved settings lost)
 *   - Returns to factory defaults:
 *     - Max distance: 6000mm, angles: 60°/60°
 *     - Tracking: multi-target, coordinates: off
 *     - All zones: disabled (vertices zeroed)
 *     - Occupancy timing: 0s cooldown, 250ms delay
 *
 * Why 10 seconds: Extremely long hold prevents accidental data loss. User
 * must WANT this destructive operation. Comparable to long-press factory
 * reset on consumer routers (often 10-30 seconds).
 *
 * Visual feedback: Red LED blinks slow (1Hz) during 3-10 second hold,
 * then solid red at 10+ seconds to indicate full reset armed.
 *
 * Use case: Preparing device for resale, troubleshooting corrupted NVS.
 */
constexpr uint32_t BOARD_BUTTON_HOLD_FULL_MS = 10000;

/**
 * Button polling interval (100 milliseconds)
 *
 * FreeRTOS task wakes every 100ms to check button state for hold detection.
 *
 * Why 100ms: Balances responsiveness vs CPU efficiency. Human can't detect
 * <200ms latency in button response. 100ms = 10Hz poll rate, trivial CPU
 * load on dedicated task.
 *
 * Debouncing: Mechanical switches bounce 5-20ms. 100ms interval naturally
 * filters bounce without extra logic (first stable read after bounce wins).
 *
 * Hold detection: Task increments counter when button pressed. At 30 counts
 * (3000ms), triggers Zigbee reset. At 100 counts (10000ms), triggers full reset.
 */
constexpr uint32_t BUTTON_POLL_INTERVAL_MS = 100;

// ============================================================================
// Board LED Configuration - Status Indication
// ============================================================================

/**
 * RMT peripheral resolution for WS2812 LED (10 MHz = 100ns per tick)
 *
 * RMT (Remote Control Transceiver) generates precise timing for WS2812 protocol:
 *   - 10MHz clock = 100ns tick resolution
 *   - WS2812 timing: T0H=400ns (4 ticks), T0L=850ns (8.5 ticks),
 *                    T1H=800ns (8 ticks), T1L=450ns (4.5 ticks)
 *
 * Why 10MHz: Divides evenly into 80MHz APB clock (ESP32-H2). Provides sufficient
 * resolution for WS2812 timing (±150ns tolerance) without excessive tick counts.
 *
 * Alternative: SPI peripheral with 3-bit encoding (used by LED controller project).
 * RMT simpler for single LED, SPI more efficient for long strips.
 */
constexpr uint32_t RMT_RESOLUTION_HZ = 10000000;

/**
 * Status LED blink period for "not joined" state (250ms = 4Hz)
 *
 * Amber LED blinks at 4Hz (250ms on, 250ms off) when device not joined to
 * Zigbee network. Indicates "waiting for setup" state.
 *
 * Why 4Hz: Visually distinct from pairing (also 4Hz but blue). Fast enough
 * to grab attention ("something needs attention"), slow enough to not be
 * annoying for prolonged operation.
 *
 * Pattern: 250ms on, 250ms off, indefinite until network joined.
 */
constexpr uint32_t BOARD_LED_BLINK_PERIOD_NOT_JOINED_US = 250000;

/**
 * Status LED blink period for "pairing" state (250ms = 4Hz)
 *
 * Blue LED blinks at 4Hz when device in pairing mode (steering active,
 * waiting for coordinator permit-join).
 *
 * Why same rate as not-joined: Consistency across "waiting" states. Color
 * (blue vs amber) distinguishes intent ("actively pairing" vs "network lost").
 */
constexpr uint32_t BOARD_LED_BLINK_PERIOD_PAIRING_US = 250000;

/**
 * Status LED blink period for "error" state (100ms = 10Hz)
 *
 * Red LED blinks at 10Hz (100ms on, 100ms off) for 5 seconds when error
 * occurs or factory reset triggered. Fast blink grabs attention.
 *
 * Why 10Hz: Much faster than other states (2.5× faster). Unmistakable
 * error indication. Not so fast that LED appears dim (persistence of vision
 * at >20Hz makes blink look like solid dim LED).
 *
 * Timeout: After 5 seconds, transitions back to pairing mode (blue 4Hz).
 */
constexpr uint32_t BOARD_LED_BLINK_PERIOD_ERROR_US = 100000;

/**
 * Status LED timeout for "joined" and "error" states (5 seconds)
 *
 * After successful join, green LED stays solid for 5 seconds, then turns off.
 * After error, red LED blinks fast for 5 seconds, then returns to pairing mode.
 *
 * Why 5 seconds: Long enough for user to see confirmation, short enough to
 * avoid wasting power (LED off during normal operation). Commercial devices
 * use 3-10 second timeouts for similar feedback.
 *
 * Implementation: esp_timer one-shot callback triggers state transition.
 */
constexpr uint32_t TIMED_STATE_DURATION_US = 5000000;

// ============================================================================
// NVS Storage Keys and Namespaces
// ============================================================================

/**
 * NVS namespace for LD2450 configuration ("ld2450_cfg")
 *
 * All device settings stored under this namespace:
 *   - "track_mode": U8, tracking mode (0=multi, 1=single)
 *   - "pub_coords": U8, coordinate publishing (0=off, 1=on)
 *   - "max_dist": U16, max detection distance (mm)
 *   - "angle_l": U8, left FOV angle (degrees)
 *   - "angle_r": U8, right FOV angle (degrees)
 *   - "bt_off": U8, Bluetooth disabled (0=on, 1=off)
 *   - "zone_0" through "zone_4": blob, ld2450_zone_t (5 zones)
 *   - "occ_cool_0" through "occ_cool_5": U16, cooldown seconds per endpoint
 *   - "occ_dly_0" through "occ_dly_5": U16, delay milliseconds per endpoint
 *
 * Why separate namespace: Isolates LD2450 config from other ESP-IDF components
 * (WiFi, Bluetooth, system). Makes factory reset surgical (erase only ld2450_cfg).
 */
constexpr const char* NVS_NAMESPACE = "ld2450_cfg";

/**
 * NVS key for tracking mode ("track_mode")
 *
 * Stored as U8. Range: 0 (multi-target) or 1 (single-target).
 */
constexpr const char* NVS_KEY_TRACKING_MODE = "track_mode";

/**
 * NVS key for coordinate publishing ("pub_coords")
 *
 * Stored as U8. Range: 0 (off) or 1 (on).
 */
constexpr const char* NVS_KEY_PUBLISH_COORDS = "pub_coords";

/**
 * NVS key for maximum distance ("max_dist")
 *
 * Stored as U16. Range: 0-6000 (millimeters).
 */
constexpr const char* NVS_KEY_MAX_DISTANCE = "max_dist";

/**
 * NVS key for left angle ("angle_l")
 *
 * Stored as U8. Range: 0-90 (degrees).
 */
constexpr const char* NVS_KEY_ANGLE_LEFT = "angle_l";

/**
 * NVS key for right angle ("angle_r")
 *
 * Stored as U8. Range: 0-90 (degrees).
 */
constexpr const char* NVS_KEY_ANGLE_RIGHT = "angle_r";

/**
 * NVS key for Bluetooth state ("bt_off")
 *
 * Stored as U8. Range: 0 (BT enabled) or 1 (BT disabled).
 */
constexpr const char* NVS_KEY_BT_DISABLED = "bt_off";

/**
 * NVS key prefix for zone data ("zone_")
 *
 * Full keys: "zone_0", "zone_1", "zone_2", "zone_3", "zone_4"
 * Each blob: sizeof(ld2450_zone_t) bytes
 *   - 1 byte: enabled (0=disabled, 1=enabled)
 *   - 16 bytes: 4 vertices × 2 coords (x,y) × 2 bytes (int16_t)
 *   Total: 17 bytes per zone
 */
constexpr const char* NVS_KEY_ZONE_PREFIX = "zone_";

/**
 * NVS key prefix for occupancy cooldown ("occ_cool_")
 *
 * Full keys: "occ_cool_0" through "occ_cool_5"
 * Index mapping: [0]=main EP1, [1-5]=zones EP2-6
 * Stored as U16, range 0-300 (seconds)
 */
constexpr const char* NVS_KEY_OCCUPANCY_COOLDOWN_PREFIX = "occ_cool_";

/**
 * NVS key prefix for occupancy delay ("occ_dly_")
 *
 * Full keys: "occ_dly_0" through "occ_dly_5"
 * Index mapping: [0]=main EP1, [1-5]=zones EP2-6
 * Stored as U16, range 0-65535 (milliseconds)
 */
constexpr const char* NVS_KEY_OCCUPANCY_DELAY_PREFIX = "occ_dly_";

// ============================================================================
// Zone Configuration
// ============================================================================

/**
 * Maximum number of zones (5)
 *
 * LD2450 sensor supports up to 5 user-defined detection zones. Each zone
 * is a quadrilateral polygon defined by 4 vertices in sensor coordinate space.
 *
 * Why 5: Hardware limitation of LD2450 sensor firmware. More zones would
 * require more processing power (point-in-polygon tests per frame) and memory.
 *
 * Zone usage: Most users define 2-3 zones for room areas (e.g., "desk",
 * "bed", "doorway"). 5 provides headroom for complex layouts.
 */
constexpr uint8_t MAX_ZONES = 5;

/**
 * Coordinate range minimum (-6000 millimeters)
 *
 * LD2450 sensor coordinate system:
 *   - Origin: Sensor mounting position
 *   - +X axis: Right (sensor's perspective)
 *   - +Y axis: Forward (away from sensor)
 *   - Range: ±6000mm (±6 meters) in both axes
 *
 * Vertices outside this range are clamped by driver.
 */
constexpr int16_t ZONE_COORD_MIN_MM = -6000;

/**
 * Coordinate range maximum (+6000 millimeters)
 *
 * See ZONE_COORD_MIN_MM for coordinate system explanation.
 */
constexpr int16_t ZONE_COORD_MAX_MM = 6000;

// ============================================================================
// Zigbee Stack Configuration
// ============================================================================

/**
 * Maximum child devices for router role (10)
 *
 * As Zigbee Router (CONFIG_LD2450_ZB_ROUTER=y), this device can relay traffic
 * for other devices. Max children = 10 means up to 10 battery-powered devices
 * can use this sensor as their parent for network connectivity.
 *
 * Why 10: Conservative for mains-powered router. ESP32-H2 could handle more
 * (Zigbee spec allows up to 255), but 10 balances mesh reliability vs memory.
 * Typical home has <10 devices per router.
 *
 * Note: Doesn't limit total network size (coordinator handles that). Only
 * limits direct children of THIS device.
 *
 * End Device mode (CONFIG_LD2450_ZB_ROUTER=n): Value ignored, device has no children.
 */
constexpr uint8_t ZB_MAX_CHILDREN = 10;

/**
 * Zigbee task stack size (8192 bytes)
 *
 * FreeRTOS task stack for Zigbee main loop. Needs headroom for:
 *   - Zigbee stack internal state (~4KB)
 *   - Callback execution (attribute handlers, command processors)
 *   - esp_zb_scheduler_alarm deferred calls
 *
 * Why 8KB: Espressif recommendation for complex Zigbee devices (multi-endpoint,
 * custom clusters). Smaller stacks risk overflow during network join or attribute
 * writes. Larger stacks waste RAM (ESP32-H2 has only 256KB total).
 */
constexpr uint32_t ZB_TASK_STACK_SIZE = 8192;

/**
 * Zigbee task priority (5, above default task priority 1)
 *
 * Higher priority ensures Zigbee stack responds to network traffic without
 * being starved by lower-priority tasks (sensor polling, CLI, NVS writes).
 *
 * FreeRTOS priority scale: 0 = idle, 1-10 = normal, >10 = real-time
 * Priority 5 = responsive networking without blocking time-critical tasks.
 *
 * Trade-off: Too high (>10) can starve application tasks. Too low (<3) causes
 * delayed ACKs and coordinator retries.
 */
constexpr uint8_t ZB_TASK_PRIORITY = 5;

/**
 * Button task stack size (2048 bytes)
 *
 * FreeRTOS task for boot button monitoring and hold-time detection. Minimal
 * requirements: GPIO read, timer math, LED state changes.
 *
 * Why 2KB: Conservative for simple task. Only needs ~512 bytes for stack,
 * but 2KB is smallest practical size (avoids stack overflow if ESP-IDF adds
 * overhead in future versions).
 */
constexpr uint32_t BUTTON_TASK_STACK_SIZE = 2048;

/**
 * Button task priority (5, same as Zigbee task)
 *
 * Factory reset operations need to interrupt Zigbee task quickly to prevent
 * new network traffic during shutdown. Same priority ensures fair scheduling.
 *
 * Why 5: Matches ZB_TASK_PRIORITY. Lower priority would delay reset trigger.
 * Higher priority unnecessary (button press is low-frequency event).
 */
constexpr uint8_t BUTTON_TASK_PRIORITY = 5;

/**
 * OTA manufacturer code (0x131B = 4891 decimal, Espressif Systems)
 *
 * Registered Zigbee manufacturer code for Espressif. Used in OTA firmware
 * updates to identify device manufacturer and ensure firmware compatibility.
 *
 * Why Espressif code: This project uses ESP32-H2 and ESP-IDF framework.
 * Using Espressif's official code ensures compatibility with their OTA
 * infrastructure.
 *
 * OTA query: Device sends manufacturer_code + image_type in query. Coordinator
 * checks OTA index for matching firmware file.
 */
constexpr uint16_t OTA_MANUFACTURER_CODE = 0x131B;

/**
 * OTA image type identifier (0x0001 = LD2450 sensor)
 *
 * Distinguishes LD2450 sensor firmware from other Espressif devices. Each
 * device type needs unique image_type to prevent cross-device firmware updates
 * (e.g., LED controller receiving sensor firmware).
 *
 * Why 0x0001: First device in personal Espressif namespace.
 *   - 0x0001: LD2450 Zigbee sensor
 *   - 0x0002: LED controller (separate project)
 *
 * OTA index: Maps (manufacturer_code=0x131B, image_type=0x0001) → firmware URL.
 */
constexpr uint16_t OTA_IMAGE_TYPE = 0x0001;

/**
 * OTA firmware version (0x00010001 = v1.0.1)
 *
 * Current firmware version for OTA upgrade comparison. Encoding:
 *   - Bits 16-23: Major version (0x01 = 1)
 *   - Bits 8-15: Minor version (0x00 = 0)
 *   - Bits 0-7: Patch version (0x01 = 1)
 *
 * Device queries coordinator: "Is newer firmware available than v1.0.1?"
 * Coordinator compares against OTA index. If newer version exists, triggers
 * image transfer.
 *
 * Note: Manual definition risky (version string and hex can drift). Future
 * improvement: Derive from version.h macros (see LED controller project).
 */
constexpr uint32_t OTA_CURRENT_FILE_VERSION = 0x00010001;

/**
 * OTA hardware version (1)
 *
 * Identifies physical hardware variant. Prevents flashing ESP32-C6 firmware
 * to ESP32-H2 hardware.
 *
 * Why 1: First hardware revision. Future variants (ESP32-C6 port, custom PCB)
 * would use hw_version=2, etc.
 *
 * OTA index: Can include hw_version constraints. Coordinator rejects firmware
 * if hw_version doesn't match.
 */
constexpr uint16_t OTA_HW_VERSION = 1;

/**
 * OTA query interval (1440 minutes = 24 hours)
 *
 * Device asks coordinator for firmware updates at this interval. 1440 minutes
 * = 24 hours = reasonable balance between:
 *   - Timely updates (users see new firmware within a day)
 *   - Low network overhead (one query per day negligible)
 *
 * Note: Manual update checks always possible via Z2M UI regardless of interval.
 */
constexpr uint16_t OTA_QUERY_INTERVAL_MINUTES = 1440;

} // namespace defaults
