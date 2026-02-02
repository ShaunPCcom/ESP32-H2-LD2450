#include "ld2450_cmd.h"
#include "ld2450.h"

#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "ld2450_cmd";

/* LD2450 command protocol constants */
static const uint8_t CMD_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};

#define CMD_ENABLE_CONF   0xFF
#define CMD_DISABLE_CONF  0xFE
#define CMD_SINGLE_TARGET 0x80
#define CMD_MULTI_TARGET  0x90
#define CMD_RESTART       0xA3
#define CMD_BLUETOOTH     0xA4
#define CMD_FACTORY_RESET 0xA2
#define CMD_SET_ZONE      0xC2

#define ACK_TIMEOUT_MS    200
#define CMD_DELAY_MS       50
#define MAX_FRAME_SIZE     64

static SemaphoreHandle_t s_cmd_mutex = NULL;

/* Build and send a command frame. Returns ESP_OK on success. */
static esp_err_t send_frame(uint8_t cmd_id, const uint8_t *value, uint16_t value_len)
{
    uart_port_t port = ld2450_get_uart_port();
    if (port >= UART_NUM_MAX) return ESP_ERR_INVALID_STATE;

    uint16_t intra_len = 2 + value_len;  /* cmd_word(2) + value */
    uint8_t frame[MAX_FRAME_SIZE];
    size_t pos = 0;

    /* Header */
    memcpy(&frame[pos], CMD_HEADER, 4); pos += 4;
    /* Length (LE) */
    frame[pos++] = (uint8_t)(intra_len & 0xFF);
    frame[pos++] = (uint8_t)(intra_len >> 8);
    /* Command word (LE): cmd_id, 0x00 */
    frame[pos++] = cmd_id;
    frame[pos++] = 0x00;
    /* Value */
    if (value && value_len > 0) {
        memcpy(&frame[pos], value, value_len);
        pos += value_len;
    }
    /* Footer */
    memcpy(&frame[pos], CMD_FOOTER, 4); pos += 4;

    int written = uart_write_bytes(port, (const char *)frame, pos);
    if (written != (int)pos) {
        ESP_LOGE(TAG, "UART write failed: wrote %d/%d", written, (int)pos);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* Read ACK from sensor. Returns ESP_OK if ACK status == success. */
static esp_err_t read_ack(uint8_t expected_cmd)
{
    uart_port_t port = ld2450_get_uart_port();
    uint8_t buf[32];

    /* Flush any pending data output frames first */
    uart_flush_input(port);

    int n = uart_read_bytes(port, buf, sizeof(buf), pdMS_TO_TICKS(ACK_TIMEOUT_MS));
    if (n < 12) {
        ESP_LOGW(TAG, "ACK timeout or short read (%d bytes) for cmd 0x%02X", n, expected_cmd);
        return ESP_ERR_TIMEOUT;
    }

    /* Verify header */
    if (memcmp(buf, CMD_HEADER, 4) != 0) {
        ESP_LOGW(TAG, "ACK bad header for cmd 0x%02X", expected_cmd);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Verify ACK command word: should be (expected_cmd, 0x01) */
    if (buf[6] != expected_cmd || buf[7] != 0x01) {
        ESP_LOGW(TAG, "ACK unexpected cmd word: 0x%02X 0x%02X (expected 0x%02X 0x01)",
                 buf[6], buf[7], expected_cmd);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Check status bytes */
    if (buf[8] != 0x00 || buf[9] != 0x00) {
        ESP_LOGW(TAG, "ACK failure status for cmd 0x%02X: 0x%02X%02X",
                 expected_cmd, buf[8], buf[9]);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "ACK OK for cmd 0x%02X", expected_cmd);
    return ESP_OK;
}

/* Enter config mode */
static esp_err_t enter_config(void)
{
    uint8_t val[] = {0x01, 0x00};
    esp_err_t err = send_frame(CMD_ENABLE_CONF, val, sizeof(val));
    if (err != ESP_OK) return err;
    return read_ack(CMD_ENABLE_CONF);
}

/* Exit config mode */
static esp_err_t exit_config(void)
{
    esp_err_t err = send_frame(CMD_DISABLE_CONF, NULL, 0);
    if (err != ESP_OK) return err;
    return read_ack(CMD_DISABLE_CONF);
}

/* Send a command wrapped in enter/exit config. */
static esp_err_t send_config_command(uint8_t cmd_id, const uint8_t *value, uint16_t value_len)
{
    esp_err_t err;

    err = enter_config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter config mode");
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    err = send_frame(cmd_id, value, value_len);
    if (err != ESP_OK) {
        exit_config();
        return err;
    }

    err = read_ack(cmd_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Command 0x%02X ACK failed", cmd_id);
        exit_config();
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    exit_config();
    return ESP_OK;
}

/* ---- Public API ---- */

esp_err_t ld2450_cmd_init(void)
{
    if (s_cmd_mutex) return ESP_OK;  /* already initialized */
    s_cmd_mutex = xSemaphoreCreateMutex();
    if (!s_cmd_mutex) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t ld2450_cmd_set_single_target(void)
{
    if (!s_cmd_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    esp_err_t err = send_config_command(CMD_SINGLE_TARGET, NULL, 0);
    xSemaphoreGive(s_cmd_mutex);
    ESP_LOGI(TAG, "Set single-target: %s", esp_err_to_name(err));
    return err;
}

esp_err_t ld2450_cmd_set_multi_target(void)
{
    if (!s_cmd_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    esp_err_t err = send_config_command(CMD_MULTI_TARGET, NULL, 0);
    xSemaphoreGive(s_cmd_mutex);
    ESP_LOGI(TAG, "Set multi-target: %s", esp_err_to_name(err));
    return err;
}

esp_err_t ld2450_cmd_set_bluetooth(bool enable)
{
    if (!s_cmd_mutex) return ESP_ERR_INVALID_STATE;
    uint8_t val[] = {enable ? 0x01 : 0x00, 0x00};
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    esp_err_t err = send_config_command(CMD_BLUETOOTH, val, sizeof(val));
    xSemaphoreGive(s_cmd_mutex);
    ESP_LOGI(TAG, "Set bluetooth %s: %s", enable ? "on" : "off", esp_err_to_name(err));
    return err;
}

esp_err_t ld2450_cmd_restart(void)
{
    if (!s_cmd_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    esp_err_t err = send_config_command(CMD_RESTART, NULL, 0);
    xSemaphoreGive(s_cmd_mutex);
    ESP_LOGI(TAG, "Sensor restart: %s", esp_err_to_name(err));
    return err;
}

esp_err_t ld2450_cmd_factory_reset(void)
{
    if (!s_cmd_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    esp_err_t err = send_config_command(CMD_FACTORY_RESET, NULL, 0);
    xSemaphoreGive(s_cmd_mutex);
    ESP_LOGI(TAG, "Factory reset: %s", esp_err_to_name(err));
    return err;
}

esp_err_t ld2450_cmd_set_region(uint16_t zone_type,
                                int16_t x1, int16_t y1,
                                int16_t x2, int16_t y2)
{
    if (!s_cmd_mutex) return ESP_ERR_INVALID_STATE;

    /* 26-byte payload: zone_type(2) + zone1(8) + zone2(8) + zone3(8) */
    uint8_t payload[26];
    memset(payload, 0, sizeof(payload));

    /* Zone type (LE) */
    payload[0] = (uint8_t)(zone_type & 0xFF);
    payload[1] = (uint8_t)(zone_type >> 8);

    /* Zone 1 coordinates (LE int16) */
    payload[2]  = (uint8_t)(x1 & 0xFF);
    payload[3]  = (uint8_t)((uint16_t)x1 >> 8);
    payload[4]  = (uint8_t)(y1 & 0xFF);
    payload[5]  = (uint8_t)((uint16_t)y1 >> 8);
    payload[6]  = (uint8_t)(x2 & 0xFF);
    payload[7]  = (uint8_t)((uint16_t)x2 >> 8);
    payload[8]  = (uint8_t)(y2 & 0xFF);
    payload[9]  = (uint8_t)((uint16_t)y2 >> 8);

    /* Zones 2 & 3 left as zeros (unused) */

    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
    esp_err_t err = send_config_command(CMD_SET_ZONE, payload, sizeof(payload));
    xSemaphoreGive(s_cmd_mutex);

    ESP_LOGI(TAG, "Set region type=%u (%d,%d)-(%d,%d): %s",
             zone_type, x1, y1, x2, y2, esp_err_to_name(err));
    return err;
}

esp_err_t ld2450_cmd_clear_region(void)
{
    return ld2450_cmd_set_region(0, 0, 0, 0, 0);
}

esp_err_t ld2450_cmd_apply_distance_angle(uint16_t max_dist_mm,
                                          uint8_t angle_left_deg,
                                          uint8_t angle_right_deg)
{
    /* Clamp inputs */
    if (max_dist_mm > 6000) max_dist_mm = 6000;
    if (angle_left_deg > 90) angle_left_deg = 90;
    if (angle_right_deg > 90) angle_right_deg = 90;

    /* If at max range and max angles, no filtering needed */
    if (max_dist_mm >= 6000 && angle_left_deg >= 90 && angle_right_deg >= 90) {
        return ld2450_cmd_clear_region();
    }

    /* Compute X boundaries from angles using trig */
    double left_rad  = (double)angle_left_deg  * M_PI / 180.0;
    double right_rad = (double)angle_right_deg * M_PI / 180.0;

    int16_t x_left  = (int16_t)(-((double)max_dist_mm * tan(left_rad)));
    int16_t x_right = (int16_t)( ((double)max_dist_mm * tan(right_rad)));

    /* Clamp to sensor limits */
    if (x_left  < -6000) x_left  = -6000;
    if (x_right >  6000) x_right =  6000;

    return ld2450_cmd_set_region(1, x_left, 0, x_right, (int16_t)max_dist_mm);
}
