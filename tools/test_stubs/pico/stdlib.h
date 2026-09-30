#ifndef MRX_TEST_PICO_STDLIB_H
#define MRX_TEST_PICO_STDLIB_H

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;
#define GPIO_OUT 1

void gpio_init(uint gpio);
void gpio_set_dir(uint gpio, bool out);
void gpio_put(uint gpio, bool value);
void sleep_ms(uint32_t ms);

#endif
