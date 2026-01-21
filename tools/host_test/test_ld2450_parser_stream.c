#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ld2450_parser.h"

static int g_parsed = 0;

static void print_report(const ld2450_report_t *r)
{
    printf("frame#%d: occupied=%d target_count=%u\n",
           g_parsed, (int)r->occupied, (unsigned)r->target_count);
}

static void feed_and_count(ld2450_parser_t *p, const uint8_t *d, size_t n)
{
    bool ok = ld2450_parser_feed(p, d, n);
    if (ok) {
        g_parsed++;
        print_report(ld2450_parser_get_report(p));
    }
}

static void make_frame(uint8_t out[30], uint8_t y0_raw_lo, uint8_t y0_raw_hi)
{
    // Header
    out[0]=0xAA; out[1]=0xFF; out[2]=0x03; out[3]=0x00;

    // T0: x=0x0010, y_raw variable, speed=1, res=1
    out[4]=0x10; out[5]=0x00;
    out[6]=y0_raw_lo; out[7]=y0_raw_hi;
    out[8]=0x01; out[9]=0x00;
    out[10]=0x01; out[11]=0x00;

    // T1: absent
    out[12]=0x00; out[13]=0x00; out[14]=0x00; out[15]=0x00;
    out[16]=0x00; out[17]=0x00; out[18]=0x01; out[19]=0x00;

    // T2: present fixed y_raw=0x8020
    out[20]=0x08; out[21]=0x00;
    out[22]=0x20; out[23]=0x80;
    out[24]=0x02; out[25]=0x00;
    out[26]=0x01; out[27]=0x00;

    // End
    out[28]=0x55; out[29]=0xCC;
}

int main(void)
{
    ld2450_parser_t *p = ld2450_parser_create();
    if (!p) { fprintf(stderr, "parser_create failed\n"); return 1; }

    uint8_t f1[30], f2[30];
    make_frame(f1, 0x10, 0x80); // present
    make_frame(f2, 0x00, 0x00); // absent for T0 -> should reduce count by 1

    // 1) Garbage then a split frame across feeds
    const uint8_t garbage[] = {0x00,0x11,0x22,0x33,0x44,0x55};
    feed_and_count(p, garbage, sizeof(garbage));
    feed_and_count(p, f1, 7);                 // partial
    feed_and_count(p, f1 + 7, 30 - 7);        // remainder -> should parse 1 frame

    // 2) Two frames sequentially (API only signals "at least one frame parsed" per feed call)
    feed_and_count(p, f1, 30);   // should parse
    feed_and_count(p, f2, 30);   // should parse

    // 3) Corrupted end bytes, followed by a good frame
    uint8_t bad[30];
    memcpy(bad, f1, 30);
    bad[28] = 0x00;
    bad[29] = 0x00;
    feed_and_count(p, bad, 30);               // should NOT parse
    feed_and_count(p, f1, 30);                // should parse

    ld2450_parser_destroy(p);

    // Expectations:
    // - after split frame: parsed=1
    // - after back-to-back: parsed=3
    // - after bad+good: parsed=4
    if (g_parsed != 4) {
        fprintf(stderr, "FAIL: expected 4 parsed frames, got %d\n", g_parsed);
        return 2;
    }
    printf("PASS: stream test parsed %d frames\n", g_parsed);
    return 0;
}
