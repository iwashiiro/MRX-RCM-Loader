#include "config.h"
#include "mrx_sha256.h"
#include "payload_manager.h"
#include "rp2040_flash_bd.h"
#include "storage.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pico/unique_id.h"

#define TEST_BLOCK_SIZE 4096u
#define TEST_BLOCK_COUNT 1792u
#define TEST_FLASH_SIZE (TEST_BLOCK_SIZE * TEST_BLOCK_COUNT)

static uint8_t flash_bytes[TEST_FLASH_SIZE];
static bool flash_initialized;

static int fake_read(const struct lfs_config *config, lfs_block_t block,
                     lfs_off_t offset, void *buffer, lfs_size_t size) {
    (void)config;
    if (block >= TEST_BLOCK_COUNT || offset > TEST_BLOCK_SIZE ||
        size > TEST_BLOCK_SIZE - offset) {
        return LFS_ERR_INVAL;
    }
    memcpy(buffer, &flash_bytes[block * TEST_BLOCK_SIZE + offset], size);
    return 0;
}

static int fake_program(const struct lfs_config *config, lfs_block_t block,
                        lfs_off_t offset, const void *buffer, lfs_size_t size) {
    (void)config;
    if (block >= TEST_BLOCK_COUNT || offset > TEST_BLOCK_SIZE ||
        size > TEST_BLOCK_SIZE - offset || (offset % 256u) != 0 ||
        (size % 256u) != 0) {
        return LFS_ERR_INVAL;
    }
    const uint8_t *source = (const uint8_t *)buffer;
    uint8_t *destination = &flash_bytes[block * TEST_BLOCK_SIZE + offset];
    for (size_t i = 0; i < size; ++i) {
        if ((destination[i] & source[i]) != source[i]) {
            return LFS_ERR_CORRUPT;
        }
        destination[i] &= source[i];
    }
    return 0;
}

static int fake_erase(const struct lfs_config *config, lfs_block_t block) {
    (void)config;
    if (block >= TEST_BLOCK_COUNT) {
        return LFS_ERR_INVAL;
    }
    memset(&flash_bytes[block * TEST_BLOCK_SIZE], 0xFF, TEST_BLOCK_SIZE);
    return 0;
}

static int fake_sync(const struct lfs_config *config) {
    (void)config;
    return 0;
}

static const struct lfs_config fake_config = {
    .read = fake_read,
    .prog = fake_program,
    .erase = fake_erase,
    .sync = fake_sync,
    .read_size = 1,
    .prog_size = 256,
    .block_size = TEST_BLOCK_SIZE,
    .block_count = TEST_BLOCK_COUNT,
    .block_cycles = 500,
    .cache_size = TEST_BLOCK_SIZE,
    .lookahead_size = 32,
};

int rp2040_flash_bd_init(const struct lfs_config **config_out) {
    if (config_out == NULL) {
        return LFS_ERR_INVAL;
    }
    if (!flash_initialized) {
        memset(flash_bytes, 0xFF, sizeof(flash_bytes));
        flash_initialized = true;
    }
    *config_out = &fake_config;
    return 0;
}

int rp2040_flash_bd_is_erased(bool *erased_out) {
    if (erased_out == NULL) {
        return LFS_ERR_INVAL;
    }
    *erased_out = true;
    for (size_t i = 0; i < sizeof(flash_bytes); ++i) {
        if (flash_bytes[i] != 0xFF) {
            *erased_out = false;
            break;
        }
    }
    return 0;
}

void pico_get_unique_board_id(pico_unique_board_id_t *id_out) {
    for (size_t i = 0; i < sizeof(id_out->id); ++i) {
        id_out->id[i] = (uint8_t)(i + 1u);
    }
}

static uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void test_mount_defaults_and_config_round_trip(void) {
    assert(storage_file_exists("/config/device.bin", &(bool){false}) ==
           STORAGE_ERR_NOT_MOUNTED);
    assert(storage_init() == STORAGE_OK);
    assert(storage_mount() == STORAGE_OK);
    assert(storage_is_mounted());

    uint8_t version[16];
    size_t size = 0;
    assert(storage_file_read("/system/filesystem.version", version,
                             sizeof(version), &size) == STORAGE_OK);
    assert(size == sizeof(version));
    assert(memcmp(version, "MRXf", 4) == 0);
    assert(read_le32(&version[4]) == 1);
    assert(read_le32(&version[8]) == 0);
    assert(read_le32(&version[12]) == crc32(version, 12));

    DeviceConfig config;
    assert(config_load(&config) == STORAGE_OK);
    assert(config.selected_payload_id == 0);
    assert(config.next_payload_id == 1);
    assert(config.config_write_count == 1);
    assert(config.boot_mode == MRX_BOOT_MODE_NORMAL);
    assert(config.flags == MRX_CONFIG_FLAG_FS_INIT);
    for (size_t i = 0; i < sizeof(config.device_id); ++i) {
        assert(config.device_id[i] == (i < 8 ? i + 1 : 0));
    }

    config.selected_payload_id = 0x12345678u;
    config.boot_mode = MRX_BOOT_MODE_STANDALONE;
    config.next_payload_id = 9;
    assert(config_save(&config) == STORAGE_OK);
    DeviceConfig loaded;
    assert(config_load(&loaded) == STORAGE_OK);
    assert(loaded.selected_payload_id == 0x12345678u);
    assert(loaded.boot_mode == MRX_BOOT_MODE_STANDALONE);
    assert(loaded.next_payload_id == 9);
    assert(loaded.config_write_count == 2);

    uint64_t total = 0;
    uint64_t used = 0;
    assert(storage_get_space(&total, &used) == STORAGE_OK);
    assert(total == 7u * 1024u * 1024u);
    assert(used <= total);
}

static void test_mount_recovery_removes_temporary_files_and_uploads(void) {
    const uint8_t partial[] = {1, 2, 3};
    assert(storage_directory_create("/payloads/00000001") == STORAGE_OK);
    assert(storage_file_write_atomic("/payloads/00000001/payload.bin.uploading",
                                     partial, sizeof(partial)) == STORAGE_OK);
    assert(storage_directory_create("/payloads/00000002") == STORAGE_OK);
    assert(storage_file_write_atomic("/payloads/00000002/payload.bin",
                                     partial, sizeof(partial)) == STORAGE_OK);
    assert(storage_file_write_atomic("/config/device.bin.tmp",
                                     partial, sizeof(partial)) == STORAGE_OK);

    assert(storage_unmount() == STORAGE_OK);
    assert(storage_mount() == STORAGE_OK);

    bool exists = true;
    assert(storage_file_exists("/config/device.bin.tmp", &exists) == STORAGE_OK);
    assert(!exists);
    assert(storage_file_exists("/payloads/00000001", &exists) == STORAGE_OK);
    assert(!exists);
    assert(storage_file_exists("/payloads/00000002/payload.bin", &exists) == STORAGE_OK);
    assert(exists);
    DeviceConfig config;
    assert(config_load(&config) == STORAGE_OK);
    assert(config.selected_payload_id == 0x12345678u);
    assert(config.config_write_count == 2);
}

static void test_corrupt_config_falls_back_and_unknown_version_is_preserved(void) {
    uint8_t corrupt[52] = {0};
    assert(storage_file_write_atomic("/config/device.bin", corrupt,
                                     sizeof(corrupt)) == STORAGE_OK);
    DeviceConfig config;
    assert(config_load(&config) == STORAGE_OK);
    assert(config.selected_payload_id == 0);
    assert(config.next_payload_id == 1);
    assert(config.config_write_count == 1);

    uint8_t incompatible[52] = {0};
    memcpy(incompatible, "MRX!", 4);
    write_le32(&incompatible[4], 2);
    write_le32(&incompatible[8], 7);
    write_le32(&incompatible[36], 1);
    write_le32(&incompatible[40], MRX_CONFIG_FLAG_FS_INIT);
    write_le32(&incompatible[48], crc32(incompatible, 48));
    assert(storage_file_write_atomic("/config/device.bin", incompatible,
                                     sizeof(incompatible)) == STORAGE_OK);
    assert(config_load(&config) == STORAGE_ERR_VERSION);
}

static void test_nonblank_corrupt_filesystem_is_not_formatted(void) {
    assert(storage_unmount() == STORAGE_OK);
    memset(flash_bytes, 0, sizeof(flash_bytes));
    assert(storage_mount() == STORAGE_ERR_CORRUPT);
    assert(flash_bytes[0] == 0);
}

static void test_payload_upload_access_selection_and_delete(void) {
    static uint8_t contents[9000];
    static const uint8_t sha_abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    uint8_t abc_digest[32];
    mrx_sha256_t abc_context;
    mrx_sha256_init(&abc_context);
    mrx_sha256_update(&abc_context, "abc", 3);
    mrx_sha256_final(&abc_context, abc_digest);
    assert(memcmp(abc_digest, sha_abc, sizeof(sha_abc)) == 0);

    uint8_t expected_hash[32];
    for (size_t i = 0; i < sizeof(contents); ++i) {
        contents[i] = (uint8_t)((i * 37u + 11u) & 0xFFu);
    }
    mrx_sha256_t sha;
    mrx_sha256_init(&sha);
    mrx_sha256_update(&sha, contents, sizeof(contents));
    mrx_sha256_final(&sha, expected_hash);

    uint32_t id = 0;
    assert(payload_begin_upload("Test payload", sizeof(contents), &id) ==
           STORAGE_OK);
    assert(id == 9);
    uint64_t received = 0;
    assert(payload_write_chunk(id, 0, contents, 4080, &received) == STORAGE_OK);
    assert(received == 4080);
    assert(payload_write_chunk(id, 4080, contents + 4080, 4080,
                               &received) == STORAGE_OK);
    assert(received == 8160);
    assert(payload_write_chunk(id, 8160, contents + 8160,
                               sizeof(contents) - 8160u, &received) == STORAGE_OK);
    assert(received == sizeof(contents));
    assert(payload_finalize_upload(id, sizeof(contents),
                                   expected_hash) == STORAGE_OK);

    PayloadInfo info;
    assert(payload_get_info(id, &info) == STORAGE_OK);
    assert(info.id == id && info.flags == MRX_PAYLOAD_FLAG_VALID);
    assert(info.size == sizeof(contents));
    assert(strcmp(info.name, "Test payload") == 0);
    assert(memcmp(info.sha256, expected_hash, sizeof(expected_hash)) == 0);

    uint32_t ids[4] = {0};
    size_t count = 0;
    assert(payload_list(ids, 4, &count) == STORAGE_OK);
    assert(count == 1 && ids[0] == id);
    assert(payload_set_selected(id) == STORAGE_OK);
    assert(payload_manager_validate_selection() == STORAGE_OK);

    bool matches = false;
    uint8_t actual_hash[32];
    assert(payload_verify(id, &matches, actual_hash) == STORAGE_OK);
    assert(matches && memcmp(actual_hash, expected_hash, 32) == 0);
    uint8_t readback[64] = {0};
    size_t read_count = 0;
    assert(payload_read_chunk(id, 4070, readback, sizeof(readback),
                              &read_count) == STORAGE_OK);
    assert(read_count == sizeof(readback));
    assert(memcmp(readback, contents + 4070, read_count) == 0);

    bool selection_cleared = false;
    assert(payload_delete(id, &selection_cleared) == STORAGE_OK);
    assert(selection_cleared);
    assert(payload_get_selected(&(uint32_t){0}) == STORAGE_ERR_NO_SELECTION);

    assert(payload_begin_upload("Bad offset", 3, &id) == STORAGE_OK);
    assert(payload_write_chunk(id, 1, contents, 1, &received) ==
           STORAGE_ERR_INVALID_ARG);
    bool exists = true;
    char directory[40];
    assert(snprintf(directory, sizeof(directory), "/payloads/%08lx",
                    (unsigned long)id) > 0);
    assert(storage_file_exists(directory, &exists) == STORAGE_OK);
    assert(!exists);

    assert(payload_begin_upload("Hash mismatch", 3, &id) == STORAGE_OK);
    const uint8_t wrong_data[3] = {0xA5, 0x5A, 0xC3};
    assert(payload_write_chunk(id, 0, wrong_data, sizeof(wrong_data),
                               &received) == STORAGE_OK);
    uint8_t wrong_hash[32] = {0};
    assert(payload_finalize_upload(id, sizeof(wrong_data), wrong_hash) ==
           STORAGE_ERR_CORRUPT);
    assert(snprintf(directory, sizeof(directory), "/payloads/%08lx",
                    (unsigned long)id) > 0);
    assert(storage_file_exists(directory, &exists) == STORAGE_OK);
    assert(!exists);

    assert(payload_begin_upload("Abort me", 0, &id) == STORAGE_OK);
    assert(payload_abort_upload(id) == STORAGE_OK);
}

int main(void) {
    test_mount_defaults_and_config_round_trip();
    test_mount_recovery_removes_temporary_files_and_uploads();
    test_payload_upload_access_selection_and_delete();
    test_corrupt_config_falls_back_and_unknown_version_is_preserved();
    test_nonblank_corrupt_filesystem_is_not_formatted();
    return 0;
}
