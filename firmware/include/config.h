#ifndef MRX_CONFIG_H
#define MRX_CONFIG_H

#include <stdint.h>

#include "storage.h"

#define MRX_CONFIG_MAGIC          0x4D525821UL
#define MRX_CONFIG_FORMAT_VERSION 1U
#define MRX_BOOT_MODE_NORMAL      0x00U
#define MRX_BOOT_MODE_STANDALONE  0x01U
#define MRX_CONFIG_FLAG_FS_INIT   (1UL << 0)

typedef struct {
    uint32_t selected_payload_id;
    uint32_t next_payload_id;
    uint32_t config_write_count;
    uint8_t device_id[16];
    uint8_t boot_mode;
    uint32_t flags;
} DeviceConfig;


storage_err_t config_load(DeviceConfig *config_out);


storage_err_t config_save(const DeviceConfig *config);

#endif
