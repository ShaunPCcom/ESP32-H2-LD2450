#include "ld2450_parser.h"

#include <stdlib.h>
#include <string.h>

#define LD2450_UPDATE_FRAME_LEN_TOTAL   (4u + 24u + 2u)  // header + payload + end
#define LD2450_UPDATE_PAYLOAD_LEN       (24u)
#define LD2450_END0                     (0x55u)
#define LD2450_END1                     (0xCCu)

static const uint8_t k_update_header[4] = {0xAA, 0xFF, 0x03, 0x00};

struct ld2450_parser {
    ld2450_report_t report;

    uint8_t *buf;
    size_t cap;
    size_t len;
};

static void buf_consume(ld2450_parser_t *p, size_t n)
{
    if (n >= p->len) {
        p->len = 0;
        return;
    }
    memmove(p->buf, p->buf + n, p->len - n);
    p->len -= n;
}

static int find_update_header(const uint8_t *b, size_t n)
{
    if (n < 4) return -1;
    for (size_t i = 0; i <= n - 4; i++) {
        if (b[i + 0] == k_update_header[0] &&
            b[i + 1] == k_update_header[1] &&
            b[i + 2] == k_update_header[2] &&
            b[i + 3] == k_update_header[3]) {
            return (int)i;
        }
    }
    return -1;
}

// Mirrors upstream sign handling:
// int16_t v = hi<<8 | lo;
// if (hi & 0x80) v = -v + 0x8000;
static int16_t decode_signed_upstream(uint8_t lo, uint8_t hi)
{
    int16_t v = (int16_t)((uint16_t)hi << 8 | (uint16_t)lo);
    if (hi & 0x80) {
        v = (int16_t)(-v + 0x8000);
    }
    return v;
}

// Mirrors upstream Y handling:
// int16_t y = hi<<8 | lo;
// if (y != 0) y -= 0x8000;
static int16_t decode_y_upstream(uint8_t lo, uint8_t hi)
{
    int16_t y = (int16_t)((uint16_t)hi << 8 | (uint16_t)lo);
    if (y != 0) {
        y = (int16_t)(y - 0x8000);
    }
    return y;
}

static void parse_update_payload(ld2450_parser_t *p, const uint8_t payload[LD2450_UPDATE_PAYLOAD_LEN])
{
    uint8_t present_count = 0;

    for (int i = 0; i < 3; i++) {
        const int o = 8 * i;

        // Upstream offsets:
        // x: [o+0..1] signed special
        // y: [o+2..3] offset by 0x8000 when non-zero
        // speed: [o+4..5] signed special
        // resolution: [o+6..7] (we ignore for now)
        const int16_t x = decode_signed_upstream(payload[o + 0], payload[o + 1]);

        const uint16_t y_raw_u = ((uint16_t)payload[o + 3] << 8) | (uint16_t)payload[o + 2];
        const int16_t y = decode_y_upstream(payload[o + 2], payload[o + 3]);

        const int16_t speed = decode_signed_upstream(payload[o + 4], payload[o + 5]);
        // const uint16_t distance_resolution = ((uint16_t)payload[o + 7] << 8) | payload[o + 6];

        // Presence: upstream ultimately keys off "is_present()" from Target::update_values/clear.
        // The simplest faithful signal from the raw frame is y_raw != 0 (because upstream only subtracts 0x8000 when non-zero).
        const bool present = (y_raw_u != 0);

        p->report.targets[i].present = present;
        p->report.targets[i].x_mm = present ? x : 0;
        p->report.targets[i].y_mm = present ? y : 0;
        p->report.targets[i].speed = present ? speed : 0;

        if (present) present_count++;
    }

    p->report.target_count = present_count;
    p->report.occupied = (present_count > 0);
}

ld2450_parser_t *ld2450_parser_create(void)
{
    ld2450_parser_t *p = (ld2450_parser_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->cap = 1024;
    p->buf = (uint8_t *)malloc(p->cap);
    if (!p->buf) {
        free(p);
        return NULL;
    }

    return p;
}

void ld2450_parser_destroy(ld2450_parser_t *p)
{
    if (!p) return;
    free(p->buf);
    free(p);
}

const ld2450_report_t *ld2450_parser_get_report(const ld2450_parser_t *p)
{
    return p ? &p->report : NULL;
}

bool ld2450_parser_feed(ld2450_parser_t *p, const uint8_t *data, size_t len)
{
    if (!p || !data || len == 0) return false;

    // Grow buffer if needed
    if (p->len + len > p->cap) {
        size_t new_cap = p->cap;
        while (new_cap < p->len + len) new_cap *= 2;
        uint8_t *nb = (uint8_t *)realloc(p->buf, new_cap);
        if (!nb) return false;
        p->buf = nb;
        p->cap = new_cap;
    }

    memcpy(p->buf + p->len, data, len);
    p->len += len;

    // Prevent runaway buffer growth if no headers appear (keep only a small tail)
    if (p->len > 8192) {
        // Keep last 64 bytes to preserve partial header possibilities
        const size_t keep = 64;
        const size_t drop = p->len - keep;
        buf_consume(p, drop);
    }

    bool parsed_any = false;

    for (;;) {
        // Find header
        int pos = find_update_header(p->buf, p->len);
        if (pos < 0) {
            // Keep last 3 bytes in case header spans boundary
            if (p->len > 3) {
                buf_consume(p, p->len - 3);
            }
            break;
        }

        // Discard anything before header
        if (pos > 0) {
            buf_consume(p, (size_t)pos);
        }

        // Need full frame
        if (p->len < LD2450_UPDATE_FRAME_LEN_TOTAL) {
            break;
        }

        // Validate end bytes
        const size_t end_idx = 4u + LD2450_UPDATE_PAYLOAD_LEN; // points to first end byte in buffer
        if (p->buf[end_idx + 0] != LD2450_END0 || p->buf[end_idx + 1] != LD2450_END1) {
            // Bad alignment; resync by dropping first byte and searching again
            buf_consume(p, 1);
            continue;
        }

        // Parse payload
        parse_update_payload(p, p->buf + 4);
        parsed_any = true;

        // Consume this frame and continue scanning
        buf_consume(p, LD2450_UPDATE_FRAME_LEN_TOTAL);
    }

    return parsed_any;
}

