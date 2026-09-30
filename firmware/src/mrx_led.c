#include "mrx_led.h"

#include <stdbool.h>
#include <stdint.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "mrx_board.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

#define LED_UPDATE_MS 20u


static PIO g_pio = pio1;
static uint g_sm;
static uint g_program_offset;
static bool g_initialized;
static mrx_led_state_t g_state = MRX_LED_BOOTING;
static absolute_time_t g_state_started;
static absolute_time_t g_last_update;
static uint32_t g_last_color = UINT32_MAX;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_color_t;

static void led_write(led_color_t color) {
    if (!g_initialized) return;
    const uint32_t rgb = ((uint32_t)color.green << 16) |
                         ((uint32_t)color.red << 8) | color.blue;
    if (rgb == g_last_color) return;
    g_last_color = rgb;
    pio_sm_put_blocking(g_pio, g_sm, rgb << 8);
}

static led_color_t scale_color(led_color_t color, uint8_t brightness) {
    color.red = (uint8_t)(((uint16_t)color.red * brightness) / 255u);
    color.green = (uint8_t)(((uint16_t)color.green * brightness) / 255u);
    color.blue = (uint8_t)(((uint16_t)color.blue * brightness) / 255u);
    return color;
}

static led_color_t base_color(void) {
    switch (g_state) {
        case MRX_LED_PC_MODE: return (led_color_t){0, 0, 40};
        case MRX_LED_STANDALONE_WAITING: return (led_color_t){35, 35, 0};
        case MRX_LED_DEVICE_DETECTED: return (led_color_t){0, 35, 35};
        case MRX_LED_RCM_DETECTED: return (led_color_t){35, 0, 35};
        case MRX_LED_INJECTING: return (led_color_t){45, 0, 45};
        case MRX_LED_INJECTION_UNCONFIRMED: return (led_color_t){45, 0, 45};
        case MRX_LED_SUCCESS: return (led_color_t){0, 50, 0};
        case MRX_LED_FAILURE: return (led_color_t){50, 0, 0};
        case MRX_LED_STORAGE_ERROR: return (led_color_t){50, 0, 0};
        case MRX_LED_NO_PAYLOAD: return (led_color_t){45, 12, 0};
        case MRX_LED_FORMATTING: return (led_color_t){40, 40, 40};
        case MRX_LED_BOOTING:
        default: return (led_color_t){35, 35, 35};
    }
}

static uint8_t pulse_brightness(uint32_t period_ms, bool triangular) {
    const uint32_t phase = (uint32_t)(absolute_time_diff_us(g_state_started,
                                                             get_absolute_time()) / 1000) % period_ms;
    if (triangular) {
        const uint32_t half = period_ms / 2u;
        const uint32_t ramp = phase < half ? phase : period_ms - phase;
        return (uint8_t)(24u + (231u * ramp) / half);
    }
    return phase < period_ms / 2u ? 255u : 0u;
}

static led_color_t current_color(void) {
    const led_color_t color = base_color();
    switch (g_state) {
        case MRX_LED_BOOTING:
        case MRX_LED_STANDALONE_WAITING:
        case MRX_LED_NO_PAYLOAD:
            return scale_color(color, pulse_brightness(1800u, true));
        case MRX_LED_DEVICE_DETECTED:
        case MRX_LED_RCM_DETECTED:
            return scale_color(color, pulse_brightness(300u, true));
        case MRX_LED_INJECTING:
        case MRX_LED_INJECTION_UNCONFIRMED:
        case MRX_LED_FAILURE:
        case MRX_LED_FORMATTING:
            return scale_color(color, pulse_brightness(160u, false));
        case MRX_LED_STORAGE_ERROR: {
            const uint32_t phase = (uint32_t)(absolute_time_diff_us(g_state_started,
                                                get_absolute_time()) / 1000) % 1400u;
            return scale_color(color, (phase < 100u || (phase >= 250u && phase < 350u))
                                      ? 255u : 0u);
        }
        case MRX_LED_SUCCESS:
            return absolute_time_diff_us(g_state_started, get_absolute_time()) < 3000000
                       ? color : (led_color_t){0, 0, 0};
        case MRX_LED_PC_MODE:
        default:
            return color;
    }
}

void mrx_led_init(void) {
    g_program_offset = pio_add_program(g_pio, &mrx_ws2812_program);
    g_sm = pio_claim_unused_sm(g_pio, true);
    pio_sm_config config = mrx_ws2812_program_get_default_config(g_program_offset);
    sm_config_set_sideset_pins(&config, MRX_NEOPIXEL_DATA_PIN);
    sm_config_set_out_shift(&config, false, true, 24);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    const float divider = (float)clock_get_hz(clk_sys) / 8000000.0f;
    sm_config_set_clkdiv(&config, divider);
    pio_gpio_init(g_pio, MRX_NEOPIXEL_DATA_PIN);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, MRX_NEOPIXEL_DATA_PIN, 1, true);
    pio_sm_init(g_pio, g_sm, g_program_offset, &config);
    pio_sm_set_enabled(g_pio, g_sm, true);
    g_initialized = true;
    g_state_started = get_absolute_time();
    g_last_update = g_state_started;
    led_write(current_color());
}

void mrx_led_set_state(mrx_led_state_t state) {
    if (g_state == state) return;
    g_state = state;
    g_state_started = get_absolute_time();
    g_last_color = UINT32_MAX;
}

void mrx_led_task(void) {
    const absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(g_last_update, now) < (int64_t)LED_UPDATE_MS * 1000) return;
    g_last_update = now;
    led_write(current_color());
}
