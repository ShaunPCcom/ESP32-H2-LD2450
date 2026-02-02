#include "board_led.h"

#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"

#define BOARD_LED_GPIO      8
#define BOARD_LED_COUNT     1

static const char *TAG = "board_led";

static led_strip_handle_t s_strip;
static esp_timer_handle_t s_timer;
static board_led_state_t s_state;
static bool s_blink_on;

static void led_apply_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void led_clear(void)
{
    led_apply_rgb(0, 0, 0);
}

static void timer_cb(void *arg)
{
    (void)arg;

    // Blink patterns:
    // - PAIRING: cyan blink ~2Hz
    // - ERROR: red fast blink ~5Hz
    // Others: timer not used
    s_blink_on = !s_blink_on;

    switch (s_state) {
        case BOARD_LED_PAIRING:
            if (s_blink_on) led_apply_rgb(0, 40, 40);
            else led_clear();
            break;

        case BOARD_LED_ERROR:
            if (s_blink_on) led_apply_rgb(60, 0, 0);
            else led_clear();
            break;

        default:
            // Should never be running for non-blink states
            break;
    }
}

static void timer_stop_if_running(void)
{
    if (!s_timer) return;
    (void)esp_timer_stop(s_timer);
}

static void timer_start_periodic_us(uint64_t period_us)
{
    if (!s_timer) return;
    (void)esp_timer_stop(s_timer);
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, period_us));
}

void board_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BOARD_LED_GPIO,
        .max_leds = BOARD_LED_COUNT,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz works well for WS2812
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip));
    led_clear();

    const esp_timer_create_args_t targs = {
        .callback = timer_cb,
        .name = "board_led",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_timer));

    ESP_LOGI(TAG, "WS2812 status LED init on GPIO%d", BOARD_LED_GPIO);
}

void board_led_set_state(board_led_state_t state)
{
    s_state = state;
    s_blink_on = false;

    switch (state) {
        case BOARD_LED_OFF:
            timer_stop_if_running();
            led_clear();
            break;

        case BOARD_LED_BOOT:
            timer_stop_if_running();
            led_apply_rgb(30, 30, 30); // dim white
            break;

        case BOARD_LED_PAIRING:
            // ~2Hz blink => toggle at 250ms
            timer_start_periodic_us(250 * 1000);
            break;

        case BOARD_LED_JOINED:
            timer_stop_if_running();
            led_apply_rgb(0, 60, 0);   // green
            break;

        case BOARD_LED_ERROR:
            // ~5Hz blink => toggle at 100ms
            timer_start_periodic_us(100 * 1000);
            break;

        default:
            timer_stop_if_running();
            led_clear();
            break;
    }
}

