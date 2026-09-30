#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "tusb.h"

#include "mrx_board.h"
#include "mrx_protocol.h"
#include "mrx_led.h"
#include "mrx_loader.h"

static void firmware_init(void) {
    (void)set_sys_clock_khz(120000, true);
    mrx_board_init();
    mrx_led_init();
    mrx_loader_init();
}

int main(void) {
    firmware_init();

    while (true) {
        tud_task();
        mrx_protocol_task();
        mrx_loader_task();
        mrx_led_task();
        tight_loop_contents();
    }
}
