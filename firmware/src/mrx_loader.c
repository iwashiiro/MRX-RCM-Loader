#include "mrx_loader.h"

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "mrx_board.h"
#include "mrx_led.h"
#include "mrx_protocol.h"
#include "rcm.h"
#include "mrx_usb_host.h"
#include "payload_manager.h"
#include "pico/stdlib.h"
#include "storage.h"
#include "tusb.h"

#define MRX_PC_DETECT_WINDOW_MS 750u
#define MRX_RCM_INDICATOR_MS 250u
#define MRX_RESULT_DISPLAY_MS 1500u
#define MRX_HOST_RESET_LOW_MS 200u

typedef enum {
    LOADER_DETECTING_PC = 0,
    LOADER_PC_MODE,
    LOADER_ERROR,
    LOADER_STANDALONE_NO_PAYLOAD,
    LOADER_STANDALONE_WAITING,
    LOADER_DEVICE_DETECTED,
    LOADER_RCM_INDICATOR,
    LOADER_INJECTION_RESULT,
    LOADER_HOST_RESET
} loader_state_t;

static loader_state_t g_state = LOADER_DETECTING_PC;
static DeviceConfig g_config;
static bool g_storage_ready;
static bool g_selection_valid;
static bool g_injection_attempted;
static bool g_host_powered;
static absolute_time_t g_deadline;
static absolute_time_t g_result_deadline;

static void set_device_state(uint8_t state) {
    mrx_protocol_set_device_state(state);
}

static bool refresh_payload_selection(void) {
    if (!g_storage_ready || payload_manager_validate_selection() != STORAGE_OK ||
        config_load(&g_config) != STORAGE_OK || g_config.selected_payload_id == 0) {
        return false;
    }

    PayloadInfo info;
    return payload_get_info(g_config.selected_payload_id, &info) == STORAGE_OK &&
           (info.flags & MRX_PAYLOAD_FLAG_VALID) != 0 &&
           (info.flags & MRX_PAYLOAD_FLAG_UPLOADING) == 0;
}

static void enter_pc_mode(void) {
    if (g_host_powered) {
        mrx_host_vbus_enable(false);
        g_host_powered = false;
    }
    g_state = LOADER_PC_MODE;
    set_device_state(MRX_STATE_PC_MODE);
    mrx_led_set_state(MRX_LED_PC_MODE);
}

static void enter_standalone(void) {
    mrx_usb_host_status_t host_status;
    mrx_usb_host_get_status(&host_status);
    if (host_status.state != MRX_USB_HOST_READY) {
        g_state = LOADER_ERROR;
        set_device_state(MRX_STATE_ERROR);
        mrx_led_set_state(MRX_LED_FAILURE);
        return;
    }

    set_device_state(MRX_STATE_STANDALONE);
    g_selection_valid = refresh_payload_selection();
    if (!g_selection_valid) {
        g_state = LOADER_STANDALONE_NO_PAYLOAD;
        mrx_led_set_state(g_storage_ready ? MRX_LED_NO_PAYLOAD : MRX_LED_STORAGE_ERROR);
        return;
    }

    mrx_host_vbus_enable(true);
    g_host_powered = true;
    g_state = LOADER_STANDALONE_WAITING;
    mrx_led_set_state(MRX_LED_STANDALONE_WAITING);
}

static void start_injection(void) {
    g_state = LOADER_INJECTION_RESULT;
    g_injection_attempted = true;
    set_device_state(MRX_STATE_INJECTING);
    mrx_led_set_state(MRX_LED_INJECTING);

    const mrx_rcm_result_t result = mrx_rcm_inject_selected();
    set_device_state(MRX_STATE_STANDALONE);
    if (result == MRX_RCM_ERR_STORAGE || result == MRX_RCM_ERR_INVALID_PAYLOAD) {
        mrx_led_set_state(MRX_LED_STORAGE_ERROR);
    } else if (result == MRX_RCM_OK) {
        mrx_led_set_state(MRX_LED_SUCCESS);
    } else if (result == MRX_RCM_ERR_TRIGGER_UNCONFIRMED) {

        mrx_led_set_state(MRX_LED_INJECTION_UNCONFIRMED);
    } else {
        mrx_led_set_state(MRX_LED_FAILURE);
    }
    g_state = LOADER_INJECTION_RESULT;
    const uint32_t result_display_ms = result == MRX_RCM_OK ? 3000u
                                                            : MRX_RESULT_DISPLAY_MS;
    g_result_deadline = make_timeout_time_ms(result_display_ms);
    g_deadline = g_result_deadline;

    if (result == MRX_RCM_ERR_TRANSFER) {
        mrx_host_vbus_enable(false);
        g_host_powered = false;
        g_deadline = make_timeout_time_ms(MRX_HOST_RESET_LOW_MS);
        g_state = LOADER_HOST_RESET;
    }
}

void mrx_loader_init(void) {
    g_config = (DeviceConfig){0};
    mrx_led_set_state(MRX_LED_BOOTING);
    set_device_state(MRX_STATE_BOOTING);

    g_storage_ready = storage_init() == STORAGE_OK && storage_mount() == STORAGE_OK;
    if (g_storage_ready) g_selection_valid = refresh_payload_selection();
    else g_selection_valid = false;

    mrx_protocol_init();
    tud_init(0);

    (void)mrx_usb_host_start(1000u);

    if (g_config.boot_mode == MRX_BOOT_MODE_STANDALONE) {
        g_deadline = get_absolute_time();
    } else {
        g_deadline = make_timeout_time_ms(MRX_PC_DETECT_WINDOW_MS);
    }
}

void mrx_loader_task(void) {
    mrx_usb_host_status_t host_status;
    mrx_usb_host_get_status(&host_status);

    if (tud_connected() && g_state != LOADER_PC_MODE) {
        enter_pc_mode();
        return;
    }

    if (g_state == LOADER_DETECTING_PC) {
        if (time_reached(g_deadline)) enter_standalone();
        return;
    }

    if (g_state == LOADER_PC_MODE) {
        if (!tud_connected()) {
            enter_standalone();
        }
        return;
    }

    if (g_state == LOADER_ERROR) {
        mrx_host_vbus_enable(false);
        g_host_powered = false;
        set_device_state(MRX_STATE_ERROR);
        mrx_led_set_state(MRX_LED_FAILURE);
        return;
    }

    if (g_state == LOADER_STANDALONE_NO_PAYLOAD) {
        if (!g_storage_ready) {
            mrx_led_set_state(MRX_LED_STORAGE_ERROR);
        } else {
            mrx_led_set_state(MRX_LED_NO_PAYLOAD);
        }
        return;
    }

    if (g_state == LOADER_HOST_RESET) {
        if (time_reached(g_deadline)) {
            if (host_status.state != MRX_USB_HOST_READY) {
                g_state = LOADER_ERROR;
                set_device_state(MRX_STATE_ERROR);
                mrx_led_set_state(MRX_LED_FAILURE);
                return;
            }
            mrx_host_vbus_enable(true);
            g_host_powered = true;
            if (!time_reached(g_result_deadline)) {
                g_state = LOADER_INJECTION_RESULT;
                g_deadline = g_result_deadline;
            } else {
                g_state = LOADER_STANDALONE_WAITING;
                mrx_led_set_state(MRX_LED_STANDALONE_WAITING);
            }
        }
        return;
    }

    if (host_status.state != MRX_USB_HOST_READY) {
        mrx_host_vbus_enable(false);
        g_host_powered = false;
        set_device_state(MRX_STATE_ERROR);
        mrx_led_set_state(MRX_LED_FAILURE);
        return;
    }

    if (g_state == LOADER_RCM_INDICATOR) {
        if (time_reached(g_deadline)) start_injection();
        return;
    }

    if (g_state == LOADER_INJECTION_RESULT) {
        if (!time_reached(g_deadline)) return;
        g_state = LOADER_STANDALONE_WAITING;
        if (!host_status.device_mounted) g_injection_attempted = false;
        mrx_led_set_state(MRX_LED_STANDALONE_WAITING);
        return;
    }

    if (!host_status.device_mounted) {
        if (g_state != LOADER_STANDALONE_WAITING) {
            g_state = LOADER_STANDALONE_WAITING;
            mrx_led_set_state(MRX_LED_STANDALONE_WAITING);
        }
        g_injection_attempted = false;
        return;
    }

    if (host_status.vendor_id == 0x0955u && host_status.product_id == 0x7321u) {
        if (!g_injection_attempted && g_state != LOADER_RCM_INDICATOR) {
            g_state = LOADER_RCM_INDICATOR;
            g_deadline = make_timeout_time_ms(MRX_RCM_INDICATOR_MS);
            mrx_led_set_state(MRX_LED_RCM_DETECTED);
        }
    } else if (g_state != LOADER_DEVICE_DETECTED) {
        g_state = LOADER_DEVICE_DETECTED;
        mrx_led_set_state(MRX_LED_DEVICE_DETECTED);
    }
}
