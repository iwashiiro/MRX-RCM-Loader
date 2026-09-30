#ifndef MRX_USB_HOST_H
#define MRX_USB_HOST_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MRX_USB_HOST_STARTING = 0,
    MRX_USB_HOST_READY,
    MRX_USB_HOST_ERROR
} mrx_usb_host_state_t;

typedef struct {
    mrx_usb_host_state_t state;
    bool device_mounted;
    uint16_t vendor_id;
    uint16_t product_id;
} mrx_usb_host_status_t;


bool mrx_usb_host_start(uint32_t timeout_ms);


void mrx_usb_host_get_status(mrx_usb_host_status_t *status_out);

#endif
