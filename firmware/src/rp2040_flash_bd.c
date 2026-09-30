#include "rp2040_flash_bd.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/platform.h"

#define MRX_FLASH_TOTAL_SIZE   (8u * 1024u * 1024u)
#define MRX_FIRMWARE_SIZE      (1u * 1024u * 1024u)
#define MRX_FILESYSTEM_SIZE    (MRX_FLASH_TOTAL_SIZE - MRX_FIRMWARE_SIZE)
#define MRX_FILESYSTEM_BLOCK   FLASH_SECTOR_SIZE
#define MRX_FILESYSTEM_BLOCKS  (MRX_FILESYSTEM_SIZE / MRX_FILESYSTEM_BLOCK)
#define MRX_FLASH_SAFE_TIMEOUT_MS 1000u

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    size_t size;
    bool erase;
} flash_write_t;

static int block_read(const struct lfs_config *config, lfs_block_t block,
                      lfs_off_t offset, void *buffer, lfs_size_t size) {
    (void)config;
    if (block >= MRX_FILESYSTEM_BLOCKS || offset > MRX_FILESYSTEM_BLOCK ||
        size > MRX_FILESYSTEM_BLOCK - offset) {
        return LFS_ERR_INVAL;
    }

    const uint32_t flash_offset = MRX_FIRMWARE_SIZE +
            block * MRX_FILESYSTEM_BLOCK + offset;
    memcpy(buffer, (const void *)(XIP_BASE + flash_offset), size);
    return 0;
}

static void __not_in_flash_func(flash_write_callback)(void *context) {
    const flash_write_t *write = (const flash_write_t *)context;
    if (write->erase) {
        flash_range_erase(write->offset, write->size);
    } else {
        flash_range_program(write->offset, write->data, write->size);
    }
}

static int block_program(const struct lfs_config *config, lfs_block_t block,
                         lfs_off_t offset, const void *buffer, lfs_size_t size) {
    (void)config;
    if (block >= MRX_FILESYSTEM_BLOCKS || offset > MRX_FILESYSTEM_BLOCK ||
        size > MRX_FILESYSTEM_BLOCK - offset ||
        (offset % FLASH_PAGE_SIZE) != 0 || (size % FLASH_PAGE_SIZE) != 0) {
        return LFS_ERR_INVAL;
    }

    flash_write_t write = {
        .offset = MRX_FIRMWARE_SIZE + block * MRX_FILESYSTEM_BLOCK + offset,
        .data = (const uint8_t *)buffer,
        .size = size,
        .erase = false,
    };
    return flash_safe_execute(flash_write_callback, &write,
                              MRX_FLASH_SAFE_TIMEOUT_MS) == PICO_OK
            ? 0 : LFS_ERR_IO;
}

static int block_erase(const struct lfs_config *config, lfs_block_t block) {
    (void)config;
    if (block >= MRX_FILESYSTEM_BLOCKS) {
        return LFS_ERR_INVAL;
    }

    flash_write_t write = {
        .offset = MRX_FIRMWARE_SIZE + block * MRX_FILESYSTEM_BLOCK,
        .data = NULL,
        .size = MRX_FILESYSTEM_BLOCK,
        .erase = true,
    };
    return flash_safe_execute(flash_write_callback, &write,
                              MRX_FLASH_SAFE_TIMEOUT_MS) == PICO_OK
            ? 0 : LFS_ERR_IO;
}

static int block_sync(const struct lfs_config *config) {
    (void)config;
    return 0;
}

static const struct lfs_config g_lfs_config = {
    .read = block_read,
    .prog = block_program,
    .erase = block_erase,
    .sync = block_sync,
    .read_size = 1,
    .prog_size = FLASH_PAGE_SIZE,
    .block_size = MRX_FILESYSTEM_BLOCK,
    .block_count = MRX_FILESYSTEM_BLOCKS,
    .block_cycles = 500,
    .cache_size = MRX_FILESYSTEM_BLOCK,
    .lookahead_size = 32,
};

int rp2040_flash_bd_init(const struct lfs_config **config_out) {
    if (config_out == NULL || PICO_FLASH_SIZE_BYTES != MRX_FLASH_TOTAL_SIZE) {
        return LFS_ERR_INVAL;
    }
    *config_out = &g_lfs_config;
    return 0;
}

int rp2040_flash_bd_is_erased(bool *erased_out) {
    if (erased_out == NULL) {
        return LFS_ERR_INVAL;
    }

    uint8_t buffer[MRX_FILESYSTEM_BLOCK];
    *erased_out = true;
    for (uint32_t offset = 0; offset < MRX_FILESYSTEM_SIZE;
         offset += sizeof(buffer)) {
        memcpy(buffer, (const void *)(XIP_BASE + MRX_FIRMWARE_SIZE + offset),
               sizeof(buffer));
        for (size_t i = 0; i < sizeof(buffer); ++i) {
            if (buffer[i] != 0xFFu) {
                *erased_out = false;
                return 0;
            }
        }
    }
    return 0;
}
