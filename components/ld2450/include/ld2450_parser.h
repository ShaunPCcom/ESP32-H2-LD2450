// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t x_mm;
    int16_t y_mm;
    int16_t speed;     // raw, interpretation later
    bool present;
} ld2450_target_t;

typedef struct {
    ld2450_target_t targets[3];
    uint8_t target_count;
    bool occupied;
} ld2450_report_t;

typedef struct ld2450_parser ld2450_parser_t;

/** Create/destroy parser instance */
ld2450_parser_t *ld2450_parser_create(void);
void ld2450_parser_destroy(ld2450_parser_t *p);

/**
 * Feed bytes into the parser.
 * Returns true if at least one complete UPDATE frame was parsed and report updated.
 */
bool ld2450_parser_feed(ld2450_parser_t *p, const uint8_t *data, size_t len);

/** Get the most recent parsed report (valid after ld2450_parser_feed returns true at least once). */
const ld2450_report_t *ld2450_parser_get_report(const ld2450_parser_t *p);

#ifdef __cplusplus
}
#endif
