#include "mrx_usb_host.h"

#include <stdatomic.h>

#include "hardware/clocks.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"

#define MRX_USB_HOST_RHPORT 1u
#define MRX_USB_HOST_DP_PIN 16u

static atomic_int g_host_state = ATOMIC_VAR_INIT(MRX_USB_HOST_STARTING);
static atomic_bool g_device_mounted = ATOMIC_VAR_INIT(false);
static atomic_uint g_device_vid_pid = ATOMIC_VAR_INIT(0);
static atomic_bool g_host_started = ATOMIC_VAR_INIT(false);

static void usb_host_core1(void) {

    if (!flash_safe_execute_core_init()) {
        atomic_store_explicit(&g_host_state, MRX_USB_HOST_ERROR, memory_order_release);
        return;
    }


    if (clock_get_hz(clk_sys) != 120000000u) {
        atomic_store_explicit(&g_host_state, MRX_USB_HOST_ERROR, memory_order_release);
        return;
    }

    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = MRX_USB_HOST_DP_PIN;
    if (!tuh_configure(MRX_USB_HOST_RHPORT,
                       TUH_CFGID_RPI_PIO_USB_CONFIGURATION,
                       &pio_cfg)) {
        atomic_store_explicit(&g_host_state, MRX_USB_HOST_ERROR, memory_order_release);
        return;
    }

    const tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    if (!tuh_rhport_init(MRX_USB_HOST_RHPORT, &host_init)) {
        atomic_store_explicit(&g_host_state, MRX_USB_HOST_ERROR, memory_order_release);
        return;
    }

    atomic_store_explicit(&g_host_state, MRX_USB_HOST_READY, memory_order_release);
    while (true) {

        tuh_task_ext(0, false);
        tight_loop_contents();
    }
}

bool mrx_usb_host_start(uint32_t timeout_ms) {
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&g_host_started, &expected, true,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        return atomic_load_explicit(&g_host_state, memory_order_acquire) == MRX_USB_HOST_READY;
    }

    atomic_store_explicit(&g_host_state, MRX_USB_HOST_STARTING, memory_order_release);
    multicore_launch_core1(usb_host_core1);

    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (atomic_load_explicit(&g_host_state, memory_order_acquire) == MRX_USB_HOST_STARTING) {
        if (time_reached(deadline)) {
            return false;
        }
        tight_loop_contents();
    }
    return atomic_load_explicit(&g_host_state, memory_order_acquire) == MRX_USB_HOST_READY;
}

void mrx_usb_host_get_status(mrx_usb_host_status_t *status_out) {
    if (status_out == NULL) {
        return;
    }
    status_out->state = (mrx_usb_host_state_t)atomic_load_explicit(&g_host_state,
                                                                   memory_order_acquire);
    status_out->device_mounted = atomic_load_explicit(&g_device_mounted, memory_order_acquire);
    const unsigned vid_pid = atomic_load_explicit(&g_device_vid_pid, memory_order_acquire);
    status_out->vendor_id = (uint16_t)(vid_pid >> 16);
    status_out->product_id = (uint16_t)vid_pid;
}

void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid = 0;
    uint16_t pid = 0;
    if (tuh_vid_pid_get(dev_addr, &vid, &pid)) {
        atomic_store_explicit(&g_device_vid_pid, ((unsigned)vid << 16) | pid,
                              memory_order_release);
    }
    atomic_store_explicit(&g_device_mounted, true, memory_order_release);
}

void tuh_umount_cb(uint8_t dev_addr) {
    (void)dev_addr;
    atomic_store_explicit(&g_device_mounted, false, memory_order_release);
    atomic_store_explicit(&g_device_vid_pid, 0, memory_order_release);
}
