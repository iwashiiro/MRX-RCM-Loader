#ifndef MRX_TEST_TUSB_H
#define MRX_TEST_TUSB_H

#include <stdbool.h>
#include <stdint.h>

uint32_t tud_cdc_available(void);
uint32_t tud_cdc_read(void *buffer, uint32_t size);
bool tud_cdc_connected(void);
uint32_t tud_cdc_write(void const *buffer, uint32_t size);
uint32_t tud_cdc_write_available(void);
void tud_cdc_write_flush(void);
void tud_task(void);

#endif
