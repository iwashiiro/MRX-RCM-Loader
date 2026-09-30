#include "payload_manager.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "mrx_sha256.h"

#define PAYLOAD_META_SIZE 128u
#define PAYLOAD_META_CRC_OFFSET 124u
#define PAYLOAD_CHUNK_SIZE 4096u
#define PAYLOAD_ROOT "/payloads"

typedef struct {
    bool active;
    uint32_t id;
    uint64_t expected_size;
    uint64_t bytes_received;
    char directory[40];
    storage_file_handle_t file;
    mrx_sha256_t sha256;
} upload_state_t;

static upload_state_t g_upload;
static uint8_t g_payload_buffer[PAYLOAD_CHUNK_SIZE];

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_le64(const uint8_t *bytes) {
    return (uint64_t)read_le32(bytes) | ((uint64_t)read_le32(bytes + 4) << 32);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *bytes, uint64_t value) {
    write_le32(bytes, (uint32_t)value);
    write_le32(bytes + 4, (uint32_t)(value >> 32));
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

static storage_err_t make_path(char *out, size_t capacity, uint32_t id,
                               const char *filename) {
    int written = snprintf(out, capacity, PAYLOAD_ROOT "/%08lx/%s",
                           (unsigned long)id, filename);
    return written < 0 || (size_t)written >= capacity
            ? STORAGE_ERR_INVALID_ARG : STORAGE_OK;
}

static storage_err_t make_directory(char *out, size_t capacity, uint32_t id) {
    int written = snprintf(out, capacity, PAYLOAD_ROOT "/%08lx",
                           (unsigned long)id);
    return written < 0 || (size_t)written >= capacity
            ? STORAGE_ERR_INVALID_ARG : STORAGE_OK;
}

static void metadata_serialize(const PayloadInfo *info, uint8_t bytes[PAYLOAD_META_SIZE]) {
    memset(bytes, 0, PAYLOAD_META_SIZE);
    memcpy(bytes, "MRXz", 4);
    write_le32(bytes + 4, 1u);
    write_le32(bytes + 8, info->id);
    write_le32(bytes + 12, info->flags);
    write_le64(bytes + 16, info->size);
    memcpy(bytes + 24, info->sha256, sizeof(info->sha256));
    memcpy(bytes + 56, info->name, sizeof(info->name));
    write_le32(bytes + PAYLOAD_META_CRC_OFFSET,
               crc32(bytes, PAYLOAD_META_CRC_OFFSET));
}

static storage_err_t metadata_parse(const uint8_t bytes[PAYLOAD_META_SIZE],
                                    uint32_t expected_id, PayloadInfo *info_out) {
    if (memcmp(bytes, "MRXz", 4) != 0 || read_le32(bytes + 120) != 0 ||
        read_le32(bytes + PAYLOAD_META_CRC_OFFSET) !=
                crc32(bytes, PAYLOAD_META_CRC_OFFSET)) {
        return STORAGE_ERR_CORRUPT;
    }
    if (read_le32(bytes + 4) != 1u) {
        return STORAGE_ERR_VERSION;
    }
    const uint32_t id = read_le32(bytes + 8);
    const uint32_t flags = read_le32(bytes + 12);
    if (id != expected_id || flags != MRX_PAYLOAD_FLAG_VALID ||
        bytes[119] != 0) {
        return flags == MRX_PAYLOAD_FLAG_UPLOADING || flags == 0 ||
               flags == (MRX_PAYLOAD_FLAG_VALID | MRX_PAYLOAD_FLAG_UPLOADING)
                ? STORAGE_ERR_INCOMPLETE : STORAGE_ERR_CORRUPT;
    }

    size_t name_length = 0;
    while (name_length < MRX_PAYLOAD_NAME_SIZE && bytes[56 + name_length] != 0) {
        uint8_t ch = bytes[56 + name_length];
        if (ch < 0x20 || ch > 0x7E) {
            return STORAGE_ERR_CORRUPT;
        }
        ++name_length;
    }
    if (name_length == MRX_PAYLOAD_NAME_SIZE) {
        return STORAGE_ERR_CORRUPT;
    }

    memset(info_out, 0, sizeof(*info_out));
    info_out->id = id;
    info_out->flags = flags;
    info_out->size = read_le64(bytes + 16);
    memcpy(info_out->sha256, bytes + 24, sizeof(info_out->sha256));
    memcpy(info_out->name, bytes + 56, sizeof(info_out->name));
    return STORAGE_OK;
}

static storage_err_t read_metadata(uint32_t id, PayloadInfo *info_out) {
    if (id == 0 || info_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    char path[64];
    storage_err_t result = make_path(path, sizeof(path), id, "metadata.bin");
    if (result != STORAGE_OK) {
        return result;
    }
    uint8_t bytes[PAYLOAD_META_SIZE];
    size_t size = 0;
    result = storage_file_read(path, bytes, sizeof(bytes), &size);
    if (result == STORAGE_ERR_INVALID_ARG ||
        (result == STORAGE_OK && size != sizeof(bytes))) {
        return STORAGE_ERR_CORRUPT;
    }
    if (result != STORAGE_OK) {
        return result;
    }
    return metadata_parse(bytes, id, info_out);
}

static storage_err_t calculate_file_sha256(const char *path, uint64_t expected_size,
                                           uint8_t digest_out[32]) {
    storage_file_handle_t file;
    storage_err_t result = storage_file_stream_open(path, STORAGE_FILE_READ, &file);
    if (result != STORAGE_OK) {
        return result;
    }
    uint64_t file_size = 0;
    result = storage_file_stream_size(file, &file_size);
    if (result == STORAGE_OK && file_size != expected_size) {
        result = STORAGE_ERR_CORRUPT;
    }
    mrx_sha256_t sha;
    mrx_sha256_init(&sha);
    while (result == STORAGE_OK) {
        size_t count = 0;
        result = storage_file_stream_read(file, g_payload_buffer,
                                          sizeof(g_payload_buffer), &count);
        if (result != STORAGE_OK || count == 0) {
            break;
        }
        mrx_sha256_update(&sha, g_payload_buffer, count);
    }
    if (result == STORAGE_OK) {
        mrx_sha256_final(&sha, digest_out);
    }
    storage_err_t close_result = storage_file_stream_close(file);
    if (result == STORAGE_OK && close_result != STORAGE_OK) {
        result = close_result;
    }
    return result;
}

static storage_err_t verify_payload_info(const PayloadInfo *info,
                                         bool *matches_out,
                                         uint8_t actual_sha256_out[32]) {
    char path[64];
    storage_err_t result = make_path(path, sizeof(path), info->id, "payload.bin");
    if (result != STORAGE_OK) {
        return result;
    }
    uint8_t digest[32];
    result = calculate_file_sha256(path, info->size, digest);
    if (result == STORAGE_OK) {
        *matches_out = memcmp(digest, info->sha256, sizeof(digest)) == 0;
        if (actual_sha256_out != NULL) {
            memcpy(actual_sha256_out, digest, sizeof(digest));
        }
    }
    return result;
}

static void clear_upload_state(void) {
    memset(&g_upload, 0, sizeof(g_upload));
}

static storage_err_t discard_upload(void) {
    storage_err_t result = STORAGE_OK;
    if (g_upload.active) {
        result = storage_file_stream_close(g_upload.file);
        if (result == STORAGE_ERR_INVALID_ARG) {
            result = STORAGE_OK;
        }
        storage_err_t remove_result =
                storage_directory_remove_tree(g_upload.directory);
        if (result == STORAGE_OK && remove_result != STORAGE_OK &&
            remove_result != STORAGE_ERR_NOT_FOUND) {
            result = remove_result;
        }
    }
    clear_upload_state();
    return result;
}

static storage_err_t name_valid(const char *name) {
    if (name == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    size_t length = 0;
    while (length < MRX_PAYLOAD_NAME_SIZE && name[length] != '\0') {
        const unsigned char ch = (unsigned char)name[length];
        if (ch < 0x20 || ch > 0x7E || ch == '/' || ch == '\\' ||
            ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
            ch == '<' || ch == '>' || ch == '|') {
            return STORAGE_ERR_INVALID_ARG;
        }
        ++length;
    }
    return length == 0 || length >= MRX_PAYLOAD_NAME_SIZE
            ? STORAGE_ERR_INVALID_ARG : STORAGE_OK;
}

storage_err_t payload_begin_upload(const char *name, uint64_t expected_size,
                                   uint32_t *id_out) {
    if (id_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t result = name_valid(name);
    if (result != STORAGE_OK) {
        return result;
    }
    if (g_upload.active) {
        return STORAGE_ERR_BUSY;
    }

    uint64_t total = 0;
    uint64_t used = 0;
    result = storage_get_space(&total, &used);
    if (result != STORAGE_OK) {
        return result;
    }
    if (used > total || expected_size > total - used) {
        return STORAGE_ERR_FULL;
    }

    DeviceConfig config;
    result = config_load(&config);
    if (result != STORAGE_OK) {
        return result;
    }
    uint32_t id = config.next_payload_id;
    if (id == 0) {
        return STORAGE_ERR_FULL;
    }
    bool exists = false;
    char directory[40];
    for (;;) {
        result = make_directory(directory, sizeof(directory), id);
        if (result != STORAGE_OK) {
            return result;
        }
        result = storage_file_exists(directory, &exists);
        if (result != STORAGE_OK) {
            return result;
        }
        if (!exists) {
            break;
        }
        if (id == UINT32_MAX) {
            return STORAGE_ERR_FULL;
        }
        ++id;
    }

    result = storage_directory_create(directory);
    if (result != STORAGE_OK) {
        return result;
    }
    PayloadInfo info = {0};
    info.id = id;
    info.flags = MRX_PAYLOAD_FLAG_UPLOADING;
    memcpy(info.name, name, strlen(name) + 1u);
    uint8_t metadata[PAYLOAD_META_SIZE];
    metadata_serialize(&info, metadata);
    char metadata_path[64];
    result = make_path(metadata_path, sizeof(metadata_path), id,
                       "metadata.bin.uploading");
    storage_file_handle_t metadata_file;
    if (result == STORAGE_OK) {
        result = storage_file_stream_open(metadata_path,
                STORAGE_FILE_WRITE_TRUNCATE, &metadata_file);
    }
    if (result == STORAGE_OK) {
        size_t written = 0;
        result = storage_file_stream_write(metadata_file, metadata,
                                            sizeof(metadata), &written);
        if (result == STORAGE_OK && written != sizeof(metadata)) {
            result = STORAGE_ERR_FULL;
        }
        if (result == STORAGE_OK) result = storage_file_stream_sync(metadata_file);
        storage_err_t close_result = storage_file_stream_close(metadata_file);
        if (result == STORAGE_OK) result = close_result;
    }
    char payload_path[64];
    if (result == STORAGE_OK) {
        result = make_path(payload_path, sizeof(payload_path), id,
                           "payload.bin.uploading");
    }
    storage_file_handle_t file;
    if (result == STORAGE_OK) {
        result = storage_file_stream_open(payload_path,
                STORAGE_FILE_WRITE_TRUNCATE, &file);
    }
    if (result != STORAGE_OK) {
        (void)storage_directory_remove_tree(directory);
        return result;
    }
    memset(&g_upload, 0, sizeof(g_upload));
    g_upload.active = true;
    g_upload.id = id;
    g_upload.expected_size = expected_size;
    g_upload.file = file;
    memcpy(g_upload.directory, directory, strlen(directory) + 1u);
    mrx_sha256_init(&g_upload.sha256);
    *id_out = id;
    return STORAGE_OK;
}

storage_err_t payload_write_chunk(uint32_t id, uint64_t offset,
                                  const void *data, size_t length,
                                  uint64_t *bytes_received_out) {
    if (!g_upload.active || g_upload.id != id) {
        return STORAGE_ERR_NOT_FOUND;
    }
    if ((length != 0 && data == NULL) || length == 0 ||
        offset != g_upload.bytes_received ||
        (uint64_t)length > g_upload.expected_size - g_upload.bytes_received) {
        (void)discard_upload();
        return STORAGE_ERR_INVALID_ARG;
    }
    size_t written = 0;
    storage_err_t result = storage_file_stream_write(g_upload.file, data,
                                                      length, &written);
    if (result == STORAGE_OK && written != length) {
        result = STORAGE_ERR_FULL;
    }
    if (result != STORAGE_OK) {
        (void)discard_upload();
        return result;
    }
    mrx_sha256_update(&g_upload.sha256, data, length);
    g_upload.bytes_received += length;
    if (bytes_received_out != NULL) {
        *bytes_received_out = g_upload.bytes_received;
    }
    return STORAGE_OK;
}

storage_err_t payload_finalize_upload(uint32_t id, uint64_t expected_size,
                                      const uint8_t expected_sha256[32]) {
    if (!g_upload.active || g_upload.id != id) {
        return STORAGE_ERR_NOT_FOUND;
    }
    if (expected_sha256 == NULL || expected_size != g_upload.expected_size ||
        g_upload.bytes_received != g_upload.expected_size) {
        (void)discard_upload();
        return STORAGE_ERR_INVALID_ARG;
    }

    uint8_t streamed_sha[32];
    mrx_sha256_final(&g_upload.sha256, streamed_sha);
    if (memcmp(streamed_sha, expected_sha256, sizeof(streamed_sha)) != 0) {
        (void)discard_upload();
        return STORAGE_ERR_CORRUPT;
    }
    storage_err_t result = storage_file_stream_sync(g_upload.file);
    storage_err_t close_result = storage_file_stream_close(g_upload.file);
    if (result == STORAGE_OK) {
        result = close_result;
    }
    if (result != STORAGE_OK) {
        (void)storage_directory_remove_tree(g_upload.directory);
        clear_upload_state();
        return result;
    }

    PayloadInfo info = {0};
    info.id = id;
    info.flags = MRX_PAYLOAD_FLAG_UPLOADING;
    info.size = g_upload.bytes_received;
    memcpy(info.name, g_upload.directory, 1);
    char metadata_upload_path[64];
    char metadata_path[64];
    char payload_upload_path[64];
    char payload_path[64];
    result = make_path(metadata_upload_path, sizeof(metadata_upload_path), id,
                       "metadata.bin.uploading");
    if (result == STORAGE_OK) {
        uint8_t old_metadata[PAYLOAD_META_SIZE];
        size_t size = 0;
        result = storage_file_read(metadata_upload_path, old_metadata,
                                   sizeof(old_metadata), &size);
        if (result == STORAGE_OK && size == sizeof(old_metadata) &&
            memcmp(old_metadata, "MRXz", 4) == 0) {
            memcpy(info.name, old_metadata + 56, sizeof(info.name));
        } else if (result == STORAGE_OK) {
            result = STORAGE_ERR_CORRUPT;
        }
    }
    if (result == STORAGE_OK) {
        result = make_path(metadata_path, sizeof(metadata_path), id, "metadata.bin");
    }
    if (result == STORAGE_OK) {
        result = make_path(payload_upload_path, sizeof(payload_upload_path), id,
                           "payload.bin.uploading");
    }
    if (result == STORAGE_OK) {
        result = make_path(payload_path, sizeof(payload_path), id, "payload.bin");
    }
    uint8_t readback_sha[32];
    if (result == STORAGE_OK) {
        result = calculate_file_sha256(payload_upload_path, info.size, readback_sha);
        if (result == STORAGE_OK &&
            memcmp(readback_sha, streamed_sha, sizeof(readback_sha)) != 0) {
            result = STORAGE_ERR_CORRUPT;
        }
    }
    if (result == STORAGE_OK) {
        result = storage_file_rename(payload_upload_path, payload_path);
    }
    if (result == STORAGE_OK) {
        info.flags = MRX_PAYLOAD_FLAG_VALID;
        memcpy(info.sha256, readback_sha, sizeof(info.sha256));
        uint8_t metadata[PAYLOAD_META_SIZE];
        metadata_serialize(&info, metadata);
        result = storage_file_write_atomic(metadata_upload_path,
                                            metadata, sizeof(metadata));
    }
    if (result == STORAGE_OK) {
        result = storage_file_rename(metadata_upload_path, metadata_path);
    }
    if (result == STORAGE_OK) {
        DeviceConfig config;
        result = config_load(&config);
        if (result == STORAGE_OK) {
            if (id == UINT32_MAX) {
                result = STORAGE_ERR_FULL;
            } else if (config.next_payload_id <= id) {
                config.next_payload_id = id + 1u;
                result = config_save(&config);
            }
        }
    }
    if (result != STORAGE_OK) {
        (void)storage_directory_remove_tree(g_upload.directory);
    }
    clear_upload_state();
    return result;
}

storage_err_t payload_abort_upload(uint32_t id) {
    if (!g_upload.active || g_upload.id != id) {
        return STORAGE_ERR_NOT_FOUND;
    }
    return discard_upload();
}

storage_err_t payload_exists(uint32_t id, bool *exists_out) {
    if (id == 0 || exists_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    PayloadInfo info;
    storage_err_t result = read_metadata(id, &info);
    if (result == STORAGE_ERR_NOT_FOUND || result == STORAGE_ERR_INCOMPLETE ||
        result == STORAGE_ERR_CORRUPT || result == STORAGE_ERR_VERSION) {
        *exists_out = false;
        return STORAGE_OK;
    }
    if (result != STORAGE_OK) {
        return result;
    }
    char path[64];
    result = make_path(path, sizeof(path), id, "payload.bin");
    if (result != STORAGE_OK) {
        return result;
    }
    storage_file_handle_t file;
    result = storage_file_stream_open(path, STORAGE_FILE_READ, &file);
    if (result == STORAGE_ERR_NOT_FOUND) {
        *exists_out = false;
        return STORAGE_OK;
    }
    if (result != STORAGE_OK) {
        return result;
    }
    uint64_t size = 0;
    result = storage_file_stream_size(file, &size);
    storage_err_t close_result = storage_file_stream_close(file);
    if (result == STORAGE_OK) result = close_result;
    *exists_out = result == STORAGE_OK && size == info.size;
    return result;
}

storage_err_t payload_get_info(uint32_t id, PayloadInfo *info_out) {
    if (info_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t result = read_metadata(id, info_out);
    if (result != STORAGE_OK) {
        return result;
    }
    bool exists = false;
    result = payload_exists(id, &exists);
    if (result != STORAGE_OK) return result;
    return exists ? STORAGE_OK : STORAGE_ERR_CORRUPT;
}

typedef struct {
    uint32_t *ids;
    size_t maximum;
    size_t count;
    storage_err_t status;
} payload_list_context_t;

static bool list_visitor(const char *name, storage_entry_type_t type,
                         void *context_ptr) {
    payload_list_context_t *context = (payload_list_context_t *)context_ptr;
    if (type != STORAGE_ENTRY_DIRECTORY || strlen(name) != 8u) {
        return true;
    }
    uint32_t id = 0;
    for (size_t i = 0; i < 8; ++i) {
        const char ch = name[i];
        uint8_t digit;
        if (ch >= '0' && ch <= '9') digit = (uint8_t)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') digit = (uint8_t)(ch - 'a' + 10);
        else return true;
        id = (id << 4) | digit;
    }
    if (id == 0) return true;
    PayloadInfo info;
    storage_err_t result = payload_get_info(id, &info);
    if (result == STORAGE_ERR_NOT_FOUND || result == STORAGE_ERR_INCOMPLETE ||
        result == STORAGE_ERR_CORRUPT) {
        return true;
    }
    if (result != STORAGE_OK) {
        context->status = result;
        return false;
    }
    if (context->count < context->maximum) {
        context->ids[context->count++] = id;
    } else {
        return false;
    }
    return true;
}

storage_err_t payload_list(uint32_t *id_array, size_t max_count,
                           size_t *count_out) {
    if (count_out == NULL || (max_count != 0 && id_array == NULL)) {
        return STORAGE_ERR_INVALID_ARG;
    }
    payload_list_context_t context = {
        .ids = id_array,
        .maximum = max_count,
        .count = 0,
        .status = STORAGE_OK,
    };
    storage_err_t result = storage_directory_visit(PAYLOAD_ROOT,
                                                    list_visitor, &context);
    if (result == STORAGE_OK) result = context.status;
    if (result == STORAGE_OK) {
        for (size_t i = 1; i < context.count; ++i) {
            const uint32_t value = id_array[i];
            size_t j = i;
            while (j > 0 && id_array[j - 1] > value) {
                id_array[j] = id_array[j - 1];
                --j;
            }
            id_array[j] = value;
        }
        *count_out = context.count;
    }
    return result;
}

storage_err_t payload_read_chunk(uint32_t id, uint64_t offset, void *buffer,
                                 size_t length, size_t *bytes_read_out) {
    if (bytes_read_out == NULL || (length != 0 && buffer == NULL)) {
        return STORAGE_ERR_INVALID_ARG;
    }
    PayloadInfo info;
    storage_err_t result = payload_get_info(id, &info);
    if (result != STORAGE_OK) return result;
    *bytes_read_out = 0;
    if (offset >= info.size || length == 0) return STORAGE_OK;
    uint64_t available = info.size - offset;
    size_t request = available < length ? (size_t)available : length;
    char path[64];
    result = make_path(path, sizeof(path), id, "payload.bin");
    if (result != STORAGE_OK) return result;
    storage_file_handle_t file;
    result = storage_file_stream_open(path, STORAGE_FILE_READ, &file);
    if (result != STORAGE_OK) return result;
    result = storage_file_stream_seek(file, offset);
    if (result == STORAGE_OK) {
        result = storage_file_stream_read(file, buffer, request, bytes_read_out);
    }
    storage_err_t close_result = storage_file_stream_close(file);
    if (result == STORAGE_OK) result = close_result;
    return result;
}

storage_err_t payload_delete(uint32_t id, bool *selection_cleared_out) {
    if (id == 0 || selection_cleared_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    if (g_upload.active && g_upload.id == id) {
        return STORAGE_ERR_BUSY;
    }
    DeviceConfig config;
    storage_err_t result = config_load(&config);
    if (result != STORAGE_OK) return result;
    const bool clear_selection = config.selected_payload_id == id;
    char directory[40];
    bool exists = false;
    result = make_directory(directory, sizeof(directory), id);
    if (result == STORAGE_OK) result = storage_file_exists(directory, &exists);
    if (result == STORAGE_OK && !exists) return STORAGE_ERR_NOT_FOUND;
    if (result == STORAGE_OK) result = storage_directory_remove_tree(directory);
    if (result == STORAGE_OK && clear_selection) {
        config.selected_payload_id = 0;
        result = config_save(&config);
    }
    if (result == STORAGE_OK) *selection_cleared_out = clear_selection;
    return result;
}

storage_err_t payload_verify(uint32_t id, bool *matches_out,
                             uint8_t actual_sha256_out[32]) {
    if (matches_out == NULL) return STORAGE_ERR_INVALID_ARG;
    PayloadInfo info;
    storage_err_t result = payload_get_info(id, &info);
    if (result != STORAGE_OK) return result;
    return verify_payload_info(&info, matches_out, actual_sha256_out);
}

storage_err_t payload_set_selected(uint32_t id) {
    PayloadInfo info;
    storage_err_t result = payload_get_info(id, &info);
    if (result != STORAGE_OK) return result;
    DeviceConfig config;
    result = config_load(&config);
    if (result != STORAGE_OK) return result;
    config.selected_payload_id = id;
    return config_save(&config);
}

storage_err_t payload_get_selected(uint32_t *id_out) {
    if (id_out == NULL) return STORAGE_ERR_INVALID_ARG;
    DeviceConfig config;
    storage_err_t result = config_load(&config);
    if (result != STORAGE_OK) return result;
    *id_out = config.selected_payload_id;
    return *id_out == 0 ? STORAGE_ERR_NO_SELECTION : STORAGE_OK;
}

storage_err_t payload_clear_selected(void) {
    DeviceConfig config;
    storage_err_t result = config_load(&config);
    if (result != STORAGE_OK) return result;
    config.selected_payload_id = 0;
    return config_save(&config);
}

storage_err_t payload_manager_validate_selection(void) {
    DeviceConfig config;
    storage_err_t result = config_load(&config);
    if (result != STORAGE_OK || config.selected_payload_id == 0) {
        return result;
    }
    PayloadInfo info;
    result = payload_get_info(config.selected_payload_id, &info);
    if (result == STORAGE_OK) return STORAGE_OK;
    if (result != STORAGE_ERR_NOT_FOUND && result != STORAGE_ERR_INCOMPLETE &&
        result != STORAGE_ERR_CORRUPT && result != STORAGE_ERR_VERSION) {
        return result;
    }
    config.selected_payload_id = 0;
    return config_save(&config);
}
