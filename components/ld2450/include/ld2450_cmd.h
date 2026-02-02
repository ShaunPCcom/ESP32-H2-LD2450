#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * LD2450 command interface.
 *
 * All commands enter config mode, send the command, then exit config mode.
 * A mutex serializes access so callers don't need external locking.
 *
 * NOTE: There is no dedicated "set max distance" or "set angle" command.
 * Distance/angle limiting is done via the hardware zone filter (0xC2).
 * Use ld2450_cmd_set_region() to configure a detection region that
 * implements the desired distance and angle limits.
 */

/** Initialize the command module (creates mutex). Call after ld2450_init(). */
esp_err_t ld2450_cmd_init(void);

/** Set single-target tracking mode on the sensor. Persists in sensor NVRAM. */
esp_err_t ld2450_cmd_set_single_target(void);

/** Set multi-target tracking mode on the sensor. Persists in sensor NVRAM. */
esp_err_t ld2450_cmd_set_multi_target(void);

/** Enable or disable Bluetooth on the sensor. Requires sensor restart. */
esp_err_t ld2450_cmd_set_bluetooth(bool enable);

/** Restart the sensor module. */
esp_err_t ld2450_cmd_restart(void);

/** Factory reset the sensor. Requires restart to take effect. */
esp_err_t ld2450_cmd_factory_reset(void);

/**
 * Set the hardware detection region via the zone filter command (0xC2).
 *
 * zone_type: 0 = disabled, 1 = detect only inside, 2 = exclude inside
 * x1/y1, x2/y2: corners of the rectangular region, in mm (signed).
 *
 * Only zone slot 1 is used (slots 2 & 3 set to zero).
 * For distance+angle limiting, compute the rectangle from:
 *   x_left  = -(max_dist_mm * tan(left_angle_deg))
 *   x_right =  (max_dist_mm * tan(right_angle_deg))
 *   y_min   = 0, y_max = max_dist_mm
 * and call with zone_type=1 (detect only inside).
 */
esp_err_t ld2450_cmd_set_region(uint16_t zone_type,
                                int16_t x1, int16_t y1,
                                int16_t x2, int16_t y2);

/** Disable hardware zone filtering (zone_type=0). */
esp_err_t ld2450_cmd_clear_region(void);

/**
 * Apply distance and angle limits by computing a detection rectangle.
 * max_dist_mm: 0-6000. angle_left/right: 0-90 degrees.
 * If max_dist=6000 and both angles=90, clears the region filter.
 */
esp_err_t ld2450_cmd_apply_distance_angle(uint16_t max_dist_mm,
                                          uint8_t angle_left_deg,
                                          uint8_t angle_right_deg);
