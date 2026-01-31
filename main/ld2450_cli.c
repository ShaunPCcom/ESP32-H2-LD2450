#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "ld2450.h"

static const char *TAG = "ld2450_cli";

static int32_t m_to_mm(float m)
{
    float mmf = m * 1000.0f;
    if (mmf >= 0) return (int32_t)(mmf + 0.5f);
    return (int32_t)(mmf - 0.5f);
}

static void print_help(void)
{
    printf(
        "\nLD2450 CLI commands:\n"
        "  ld help\n"
        "  ld state\n"
        "  ld en <0|1>\n"
        "  ld mode <single|multi>\n"
        "  ld zones\n"
        "  ld zone <1-5> <on|off>\n"
        "  ld zone <1-5> on x1 y1 x2 y2 x3 y3 x4 y4   (meters)\n"
        "  ld reboot\n\n"
    );
}

static void print_state(void)
{
    ld2450_state_t s;
    if (ld2450_get_state(&s) != ESP_OK) {
        printf("state: error\n");
        return;
    }

    printf("state: occupied=%d raw_count=%u eff_count=%u zone_bitmap=0x%02x\n",
           (int)s.occupied_global,
           (unsigned)s.target_count_raw,
           (unsigned)s.target_count_effective,
           (unsigned)s.zone_bitmap);

    if (s.target_count_effective > 0) {
        printf("selected: x_mm=%d y_mm=%d speed=%d present=%d\n",
               (int)s.selected.x_mm, (int)s.selected.y_mm, (int)s.selected.speed, (int)s.selected.present);
    }
}

static void print_zones(void)
{
    ld2450_zone_t z[5];
    if (ld2450_get_zones(z, 5) != ESP_OK) {
        printf("zones: error\n");
        return;
    }

    for (int i = 0; i < 5; i++) {
        printf("zone%d: %s  v=[(%d,%d) (%d,%d) (%d,%d) (%d,%d)] mm\n",
               i + 1,
               z[i].enabled ? "on " : "off",
               (int)z[i].v[0].x_mm, (int)z[i].v[0].y_mm,
               (int)z[i].v[1].x_mm, (int)z[i].v[1].y_mm,
               (int)z[i].v[2].x_mm, (int)z[i].v[2].y_mm,
               (int)z[i].v[3].x_mm, (int)z[i].v[3].y_mm);
    }
}

static void cli_task(void *arg)
{
    (void)arg;

    print_help();

    const uart_port_t console_uart = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;

    char line[256];
    size_t len = 0;

    while (1) {
        uint8_t ch;
        int n = uart_read_bytes(console_uart, &ch, 1, pdMS_TO_TICKS(100));
        if (n <= 0) {
            continue;
        }

        // Echo (idf.py monitor often doesn't echo)
        uart_write_bytes(console_uart, (const char *)&ch, 1);

        if (ch == '\r' || ch == '\n') {
            // Normalize line endings; ensure we have a clean C-string
            line[len] = '\0';
            len = 0;

            // trim leading spaces
            char *p = line;
            while (*p && isspace((unsigned char)*p)) p++;

            // require "ld"
            if (strncmp(p, "ld", 2) != 0 || (p[2] && !isspace((unsigned char)p[2]) && p[2] != '\0')) {
                continue;
            }
            p += 2;
            while (*p && isspace((unsigned char)*p)) p++;

            // tokenize
            char *cmd = strtok(p, " \t\r\n");
            if (!cmd) { print_help(); continue; }

            if (strcmp(cmd, "help") == 0) { print_help(); continue; }
            if (strcmp(cmd, "state") == 0) { print_state(); continue; }

            if (strcmp(cmd, "en") == 0) {
                char *v = strtok(NULL, " \t\r\n");
                if (!v) { printf("usage: ld en <0|1>\n"); continue; }
                int en = atoi(v);
                ld2450_set_enabled(en ? true : false);
                printf("enabled=%d\n", en ? 1 : 0);
                continue;
            }

            if (strcmp(cmd, "mode") == 0) {
                char *v = strtok(NULL, " \t\r\n");
                if (!v) { printf("usage: ld mode <single|multi>\n"); continue; }
                if (strcmp(v, "single") == 0) {
                    ld2450_set_tracking_mode(LD2450_TRACK_SINGLE);
                    printf("mode=single\n");
                } else if (strcmp(v, "multi") == 0) {
                    ld2450_set_tracking_mode(LD2450_TRACK_MULTI);
                    printf("mode=multi\n");
                } else {
                    printf("usage: ld mode <single|multi>\n");
                }
                continue;
            }

            if (strcmp(cmd, "zones") == 0) { print_zones(); continue; }

            if (strcmp(cmd, "zone") == 0) {
                char *zid = strtok(NULL, " \t\r\n");
                char *onoff = strtok(NULL, " \t\r\n");
                if (!zid || !onoff) { printf("usage: ld zone <1-5> <on|off> [coords...]\n"); continue; }

                int zi = atoi(zid) - 1;
                if (zi < 0 || zi >= 5) { printf("zone id must be 1-5\n"); continue; }

                ld2450_zone_t all[5];
                if (ld2450_get_zones(all, 5) != ESP_OK) { printf("zones: error\n"); continue; }
                ld2450_zone_t z = all[zi];

                if (strcmp(onoff, "off") == 0) {
                    z.enabled = false;
                    printf(ld2450_set_zone((size_t)zi, &z) == ESP_OK ? "zone%d disabled\n" : "zone%d update failed\n", zi + 1);
                    continue;
                }

                if (strcmp(onoff, "on") != 0) {
                    printf("usage: ld zone <1-5> <on|off> [coords...]\n");
                    continue;
                }

                // optional coords (8 floats meters)
                char *coords[8];
                for (int i = 0; i < 8; i++) coords[i] = strtok(NULL, " \t\r\n");

                z.enabled = true;

                if (!coords[0]) {
                    printf(ld2450_set_zone((size_t)zi, &z) == ESP_OK ? "zone%d enabled\n" : "zone%d update failed\n", zi + 1);
                    continue;
                }

                for (int i = 0; i < 8; i++) {
                    if (!coords[i]) {
                        printf("usage: ld zone <1-5> on x1 y1 x2 y2 x3 y3 x4 y4 (meters)\n");
                        goto zone_done;
                    }
                }

                for (int i = 0; i < 4; i++) {
                    float xm = strtof(coords[i*2 + 0], NULL);
                    float ym = strtof(coords[i*2 + 1], NULL);
                    z.v[i].x_mm = m_to_mm(xm);
                    z.v[i].y_mm = m_to_mm(ym);
                }

                printf(ld2450_set_zone((size_t)zi, &z) == ESP_OK ? "zone%d set\n" : "zone%d update failed\n", zi + 1);

zone_done:
                continue;
            }

            if (strcmp(cmd, "reboot") == 0) {
                printf("Rebooting...\n");
                fflush(stdout);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }

            printf("unknown command\n");
            print_help();
            continue;
        }

        // backspace/delete
        if (ch == 0x7f || ch == 0x08) {
            if (len > 0) len--;
            continue;
        }

        if (isprint((unsigned char)ch) && len + 1 < sizeof(line)) {
            line[len++] = (char)ch;
        }
    }
}

void ld2450_cli_start(void)
{
    const uart_port_t console_uart = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;

    // Ensure UART driver is installed for console UART so uart_read_bytes() works.
    // If already installed, ESP_ERR_INVALID_STATE is fine.
    esp_err_t err = uart_driver_install(console_uart, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install(console_uart=%d) failed: %s", (int)console_uart, esp_err_to_name(err));
        return;
    }

    BaseType_t ok = xTaskCreate(cli_task, "ld2450_cli", 4096, NULL, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to start CLI task");
    }
}
