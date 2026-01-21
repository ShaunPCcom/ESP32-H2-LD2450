#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "ld2450_parser.h"

static void dump_report(const ld2450_report_t *r)
{
    printf("occupied=%d target_count=%u\n", (int)r->occupied, (unsigned)r->target_count);
    for (int i = 0; i < 3; i++) {
        printf("T%d: present=%d x=%dmm y=%dmm speed=%d\n",
               i,
               (int)r->targets[i].present,
               (int)r->targets[i].x_mm,
               (int)r->targets[i].y_mm,
               (int)r->targets[i].speed);
    }
}

int main(void)
{
    // Same 30-byte update frame structure as upstream:
    // header AA FF 03 00 + 24 payload + end 55 CC
    static const uint8_t frame[30] = {
        0xAA, 0xFF, 0x03, 0x00,

        // Target 0 (present): x=0x0010, y_raw=0x8010, speed=0x0001, res=0x0001
        0x10, 0x00,  0x10, 0x80,  0x01, 0x00,  0x01, 0x00,

        // Target 1 (absent): y_raw=0
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x01, 0x00,

        // Target 2 (present): x=0x0008, y_raw=0x8020, speed=0x0002, res=0x0001
        0x08, 0x00,  0x20, 0x80,  0x02, 0x00,  0x01, 0x00,

        0x55, 0xCC
    };

    ld2450_parser_t *p = ld2450_parser_create();
    if (!p) {
        fprintf(stderr, "parser_create failed\n");
        return 1;
    }

    bool ok = ld2450_parser_feed(p, frame, sizeof(frame));
    if (!ok) {
        fprintf(stderr, "parser_feed returned false\n");
        ld2450_parser_destroy(p);
        return 2;
    }

    const ld2450_report_t *r = ld2450_parser_get_report(p);
    dump_report(r);

    ld2450_parser_destroy(p);
    return 0;
}
