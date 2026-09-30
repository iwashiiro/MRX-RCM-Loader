#ifndef MRX_BOARD_H
#define MRX_BOARD_H

#include "pico/stdlib.h"

#define MRX_USB_HOST_VBUS_PIN 18u
#define MRX_NEOPIXEL_POWER_PIN 20u
#define MRX_NEOPIXEL_DATA_PIN 21u

static inline void mrx_board_init(void) {
    gpio_init(MRX_USB_HOST_VBUS_PIN);
    gpio_set_dir(MRX_USB_HOST_VBUS_PIN, GPIO_OUT);
    gpio_put(MRX_USB_HOST_VBUS_PIN, 0);

    gpio_init(MRX_NEOPIXEL_POWER_PIN);
    gpio_set_dir(MRX_NEOPIXEL_POWER_PIN, GPIO_OUT);
    gpio_put(MRX_NEOPIXEL_POWER_PIN, 1);

    gpio_init(MRX_NEOPIXEL_DATA_PIN);
    gpio_set_dir(MRX_NEOPIXEL_DATA_PIN, GPIO_OUT);
    gpio_put(MRX_NEOPIXEL_DATA_PIN, 0);
}

static inline void mrx_host_vbus_enable(bool enabled) {
    gpio_put(MRX_USB_HOST_VBUS_PIN, enabled ? 1 : 0);
}

#endif
