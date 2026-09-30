#ifndef MRX_LED_H
#define MRX_LED_H

typedef enum {
    MRX_LED_BOOTING = 0,
    MRX_LED_PC_MODE,
    MRX_LED_STANDALONE_WAITING,
    MRX_LED_DEVICE_DETECTED,
    MRX_LED_RCM_DETECTED,
    MRX_LED_INJECTING,
    MRX_LED_INJECTION_UNCONFIRMED,
    MRX_LED_SUCCESS,
    MRX_LED_FAILURE,
    MRX_LED_STORAGE_ERROR,
    MRX_LED_NO_PAYLOAD,
    MRX_LED_FORMATTING
} mrx_led_state_t;

void mrx_led_init(void);
void mrx_led_set_state(mrx_led_state_t state);
void mrx_led_task(void);

#endif
