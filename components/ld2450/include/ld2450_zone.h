// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t x_mm;
    int16_t y_mm;
} ld2450_point_t;

typedef struct {
    bool enabled;
    ld2450_point_t v[4];  // 4-sided polygon, any order (but should be consistent: CW or CCW)
} ld2450_zone_t;

/**
 * Returns true if point p is inside the polygon (or on its boundary).
 * Uses ray casting; supports concave quads.
 */
bool ld2450_zone_contains_point(const ld2450_zone_t *z, ld2450_point_t p);

#ifdef __cplusplus
}
#endif

