#include "config.h"

#include <string.h>

#include "pico/unique_id.h"

#define CONFIG_PATH "/config/device.bin"
#define CONFIG_TEMP_PATH "/config/device.bin.tmp"
#define CONFIG_SIZE 52u
#define CONFIG_CRC_OFFSET 48u

static uint32_t g_write_count;
static bool g_write_count_known;

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
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

static void config_make_defaults(DeviceConfig *config) {
    memset(config, 0, sizeof(*config));
    config->next_payload_id = 1;
    config->boot_mode = MRX_BOOT_MODE_NORMAL;
    config->flags = MRX_CONFIG_FLAG_FS_INIT;

    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    memcpy(config->device_id, id.id, sizeof(id.id));
}

static bool config_fields_valid(const DeviceConfig *config) {
    return config->next_payload_id != 0 &&
           (config->boot_mode == MRX_BOOT_MODE_NORMAL ||
            config->boot_mode == MRX_BOOT_MODE_STANDALONE) &&
           (config->flags & ~MRX_CONFIG_FLAG_FS_INIT) == 0;
}

static void config_serialize(const DeviceConfig *config, uint8_t bytes[CONFIG_SIZE],
                            uint32_t write_count) {
    memset(bytes, 0, CONFIG_SIZE);

    memcpy(bytes, "MRX!", 4);
    write_le32(&bytes[4], MRX_CONFIG_FORMAT_VERSION);
    write_le32(&bytes[8], write_count);
    memcpy(&bytes[12], config->device_id, sizeof(config->device_id));
    write_le32(&bytes[28], config->selected_payload_id);
    bytes[32] = config->boot_mode;
    write_le32(&bytes[36], config->next_payload_id);
    write_le32(&bytes[40], config->flags);
    write_le32(&bytes[CONFIG_CRC_OFFSET], crc32(bytes, CONFIG_CRC_OFFSET));
}

static storage_err_t config_deserialize(const uint8_t bytes[CONFIG_SIZE],
                                       DeviceConfig *config_out) {
    if (memcmp(bytes, "MRX!", 4) != 0) {
        return STORAGE_ERR_CORRUPT;
    }

    if (read_le32(&bytes[4]) != MRX_CONFIG_FORMAT_VERSION) {
        return STORAGE_ERR_VERSION;
    }
    if (read_le32(&bytes[CONFIG_CRC_OFFSET]) != crc32(bytes, CONFIG_CRC_OFFSET) ||
        bytes[33] != 0 || bytes[34] != 0 || bytes[35] != 0 ||
        bytes[44] != 0 || bytes[45] != 0 || bytes[46] != 0 || bytes[47] != 0) {
        return STORAGE_ERR_CORRUPT;
    }

    DeviceConfig config = {0};
    config.config_write_count = read_le32(&bytes[8]);
    memcpy(config.device_id, &bytes[12], sizeof(config.device_id));
    config.selected_payload_id = read_le32(&bytes[28]);
    config.boot_mode = bytes[32];
    config.next_payload_id = read_le32(&bytes[36]);
    config.flags = read_le32(&bytes[40]);
    if (!config_fields_valid(&config)) {
        return STORAGE_ERR_CORRUPT;
    }

    *config_out = config;
    return STORAGE_OK;
}

static storage_err_t config_read_valid(DeviceConfig *config_out) {
    uint8_t bytes[CONFIG_SIZE];
    size_t size = 0;
    storage_err_t result = storage_file_read(CONFIG_PATH, bytes, sizeof(bytes), &size);
    if (result == STORAGE_ERR_INVALID_ARG ||
        (result == STORAGE_OK && size != sizeof(bytes))) {
        return STORAGE_ERR_CORRUPT;
    }
    if (result != STORAGE_OK) {
        return result;
    }
    return config_deserialize(bytes, config_out);
}

storage_err_t config_save(const DeviceConfig *config) {
    if (config == NULL || !config_fields_valid(config)) {
        return STORAGE_ERR_INVALID_ARG;
    }

    if (!g_write_count_known) {
        DeviceConfig existing;
        storage_err_t read_result = config_read_valid(&existing);
        if (read_result == STORAGE_OK) {
            g_write_count = existing.config_write_count;
        } else if (read_result != STORAGE_ERR_NOT_FOUND &&
                   read_result != STORAGE_ERR_CORRUPT) {
            return read_result;
        }
        g_write_count_known = true;
    }

    const uint32_t next_write_count = g_write_count + 1u;
    uint8_t bytes[CONFIG_SIZE];
    config_serialize(config, bytes, next_write_count);
    storage_err_t result = storage_file_write_atomic(CONFIG_PATH, bytes, sizeof(bytes));
    if (result == STORAGE_OK) {
        g_write_count = next_write_count;
        g_write_count_known = true;
    }
    return result;
}

storage_err_t config_load(DeviceConfig *config_out) {
    if (config_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }

    storage_err_t result = config_read_valid(config_out);
    if (result == STORAGE_OK) {
        g_write_count = config_out->config_write_count;
        g_write_count_known = true;
        return STORAGE_OK;
    }
    if (result != STORAGE_ERR_NOT_FOUND && result != STORAGE_ERR_CORRUPT) {
        return result;
    }

    config_make_defaults(config_out);
    g_write_count = 0;
    g_write_count_known = true;
    result = config_save(config_out);
    if (result == STORAGE_OK) {
        config_out->config_write_count = g_write_count;
    }
    return result;
}
