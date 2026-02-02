#include "board_led.h"

#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"

#define BOARD_LED_GPIO      8
#define BOARD_LED_COUNT     1
#define TIMED_STATE_US      (5 * 1000 * 1000)   /* 5 seconds */

static const char *TAG = "board_led";

static led_strip_handle_t s_strip;
static esp_timer_handle_t s_blink_timer;    /* periodic blink */
static esp_timer_handle_t s_timeout_timer;  /* one-shot for timed states */
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

static void blink_cb(void *arg)
{
    (void)arg;
    s_blink_on = !s_blink_on;

    switch (s_state) {
        case BOARD_LED_NOT_JOINED:
            if (s_blink_on) led_apply_rgb(40, 20, 0);  /* amber */
            else led_clear();
            break;

        case BOARD_LED_PAIRING:
            if (s_blink_on) led_apply_rgb(0, 0, 40);   /* blue */
            else led_clear();
            break;

        case BOARD_LED_ERROR:
            if (s_blink_on) led_apply_rgb(60, 0, 0);   /* red */
            else led_clear();
            break;

        default:
            break;
    }
}

static void timeout_cb(void *arg)
{
    (void)arg;

    switch (s_state) {
        case BOARD_LED_JOINED:
            board_led_set_state(BOARD_LED_OFF);
            break;

        case BOARD_LED_ERROR:
            board_led_set_state(BOARD_LED_NOT_JOINED);
            break;

        default:
            break;
    }
}

static void blink_stop(void)
{
    if (s_blink_timer) (void)esp_timer_stop(s_blink_timer);
}

static void blink_start(uint64_t period_us)
{
    if (!s_blink_timer) return;
    (void)esp_timer_stop(s_blink_timer);
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_blink_timer, period_us));
}

static void timeout_stop(void)
{
    if (s_timeout_timer) (void)esp_timer_stop(s_timeout_timer);
}

static void timeout_start(void)
{
    if (!s_timeout_timer) return;
    (void)esp_timer_stop(s_timeout_timer);
    ESP_ERROR_CHECK(esp_timer_start_once(s_timeout_timer, TIMED_STATE_US));
}

void board_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BOARD_LED_GPIO,
        .max_leds = BOARD_LED_COUNT,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip));
    led_clear();

    const esp_timer_create_args_t blink_args = {
        .callback = blink_cb,
        .name = "led_blink",
    };
    ESP_ERROR_CHECK(esp_timer_create(&blink_args, &s_blink_timer));

    const esp_timer_create_args_t timeout_args = {
        .callback = timeout_cb,
        .name = "led_timeout",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timeout_args, &s_timeout_timer));

    ESP_LOGI(TAG, "WS2812 status LED init on GPIO%d", BOARD_LED_GPIO);
}

void board_led_set_state(board_led_state_t state)
{
    s_state = state;
    s_blink_on = false;
    blink_stop();
    timeout_stop();

    switch (state) {
        case BOARD_LED_OFF:
            led_clear();
            break;

        case BOARD_LED_NOT_JOINED:
            /* blinking amber ~2Hz, indefinite */
            blink_start(250 * 1000);
            break;

        case BOARD_LED_PAIRING:
            /* blinking blue ~2Hz, indefinite */
            blink_start(250 * 1000);
            break;

        case BOARD_LED_JOINED:
            /* solid green for 5s, then OFF */
            led_apply_rgb(0, 60, 0);
            timeout_start();
            break;

        case BOARD_LED_ERROR:
            /* blinking red ~5Hz for 5s, then NOT_JOINED */
            blink_start(100 * 1000);
            timeout_start();
            break;

        default:
            led_clear();
            break;
    }
}
