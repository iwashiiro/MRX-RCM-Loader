#ifndef MRX_RCM_H
#define MRX_RCM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MRX_RCM_OK = 0,
    MRX_RCM_ERR_NOT_READY,
    MRX_RCM_ERR_NO_DEVICE,
    MRX_RCM_ERR_NO_SELECTION,
    MRX_RCM_ERR_INVALID_PAYLOAD,
    MRX_RCM_ERR_STORAGE,
    MRX_RCM_ERR_TRANSFER,
    MRX_RCM_ERR_TRIGGER_UNCONFIRMED,
    MRX_RCM_ERR_TIMEOUT
} mrx_rcm_result_t;


mrx_rcm_result_t mrx_rcm_inject_selected(void);

#ifdef __cplusplus
}
#endif

#endif
