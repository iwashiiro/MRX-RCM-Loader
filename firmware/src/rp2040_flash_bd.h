#ifndef MRX_RP2040_FLASH_BD_H
#define MRX_RP2040_FLASH_BD_H

#include <stdbool.h>

#include "lfs.h"

int rp2040_flash_bd_init(const struct lfs_config **config_out);
int rp2040_flash_bd_is_erased(bool *erased_out);

#endif
