#include "storage.h"

#include <stdio.h>
#include <string.h>

#include "lfs.h"
#include "rp2040_flash_bd.h"

#define STORAGE_PATH_CAPACITY 128u
#define STORAGE_FILE_VERIFY_CHUNK 64u
#define STORAGE_FS_VERSION_SIZE 16u
#define STORAGE_FS_SCHEMA_VERSION 1u
#define STORAGE_FS_VERSION_PATH "/system/filesystem.version"
#define STORAGE_CONFIG_TEMP_PATH "/config/device.bin.tmp"
#define STORAGE_FS_VERSION_TEMP_PATH "/system/filesystem.version.tmp"
#define STORAGE_PAYLOADS_PATH "/payloads"
#define STORAGE_FLASH_TOTAL_SIZE (8u * 1024u * 1024u)
#define STORAGE_FIRMWARE_SIZE    (1u * 1024u * 1024u)
#define STORAGE_FILESYSTEM_SIZE  (STORAGE_FLASH_TOTAL_SIZE - STORAGE_FIRMWARE_SIZE)
#define STORAGE_BLOCK_SIZE       4096u
#define STORAGE_BLOCK_COUNT      (STORAGE_FILESYSTEM_SIZE / STORAGE_BLOCK_SIZE)
#define STORAGE_FILE_STREAMS     4u

typedef struct {
    lfs_file_t file;
    uint32_t generation;
    bool active;
} storage_stream_t;

static lfs_t g_lfs;
static storage_stream_t g_streams[STORAGE_FILE_STREAMS];
static const struct lfs_config *g_lfs_config;
static bool g_initialized;
static bool g_mounted;

static storage_err_t map_lfs_error(int error) {
    switch (error) {
        case 0: return STORAGE_OK;
        case LFS_ERR_NOENT: return STORAGE_ERR_NOT_FOUND;
        case LFS_ERR_CORRUPT: return STORAGE_ERR_CORRUPT;
        case LFS_ERR_NOSPC: return STORAGE_ERR_FULL;
        case LFS_ERR_INVAL:
        case LFS_ERR_NAMETOOLONG: return STORAGE_ERR_INVALID_ARG;
        default: return STORAGE_ERR_IO;
    }
}

static storage_err_t require_mounted(void) {
    return g_mounted ? STORAGE_OK : STORAGE_ERR_NOT_MOUNTED;
}

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

static storage_err_t ensure_directory(const char *path) {
    int result = lfs_mkdir(&g_lfs, path);
    if (result == LFS_ERR_EXIST) {
        struct lfs_info info;
        result = lfs_stat(&g_lfs, path, &info);
        if (result == 0 && info.type == LFS_TYPE_DIR) {
            return STORAGE_OK;
        }
        return result == 0 ? STORAGE_ERR_CORRUPT : map_lfs_error(result);
    }
    return map_lfs_error(result);
}

static storage_err_t remove_tree(const char *path) {
    lfs_dir_t directory;
    int result = lfs_dir_open(&g_lfs, &directory, path);
    if (result != 0) {
        return map_lfs_error(result);
    }

    struct lfs_info info;
    char child[STORAGE_PATH_CAPACITY];
    storage_err_t status = STORAGE_OK;
    while ((result = lfs_dir_read(&g_lfs, &directory, &info)) > 0) {
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }
        int written = snprintf(child, sizeof(child), "%s/%s", path, info.name);
        if (written < 0 || (size_t)written >= sizeof(child)) {
            status = STORAGE_ERR_INVALID_ARG;
            break;
        }
        if (info.type == LFS_TYPE_DIR) {
            status = remove_tree(child);
        } else {
            status = map_lfs_error(lfs_remove(&g_lfs, child));
        }
        if (status != STORAGE_OK) {
            break;
        }
    }
    if (result < 0 && status == STORAGE_OK) {
        status = map_lfs_error(result);
    }
    int close_result = lfs_dir_close(&g_lfs, &directory);
    if (close_result != 0 && status == STORAGE_OK) {
        status = map_lfs_error(close_result);
    }
    if (status != STORAGE_OK) {
        return status;
    }
    return map_lfs_error(lfs_remove(&g_lfs, path));
}

static storage_err_t directory_has_upload_marker(const char *path, bool *found_out) {
    lfs_dir_t directory;
    int result = lfs_dir_open(&g_lfs, &directory, path);
    if (result != 0) {
        return map_lfs_error(result);
    }

    *found_out = false;
    struct lfs_info info;
    storage_err_t status = STORAGE_OK;
    while ((result = lfs_dir_read(&g_lfs, &directory, &info)) > 0) {
        const size_t name_length = strlen(info.name);
        static const char suffix[] = ".uploading";
        if (info.type == LFS_TYPE_REG && name_length >= sizeof(suffix) - 1u &&
            strcmp(&info.name[name_length - (sizeof(suffix) - 1u)], suffix) == 0) {
            *found_out = true;
            break;
        }
    }
    if (result < 0) {
        status = map_lfs_error(result);
    }
    int close_result = lfs_dir_close(&g_lfs, &directory);
    if (close_result != 0 && status == STORAGE_OK) {
        status = map_lfs_error(close_result);
    }
    return status;
}

static storage_err_t recover_incomplete_uploads(void) {
    for (;;) {
        lfs_dir_t directory;
        int result = lfs_dir_open(&g_lfs, &directory, STORAGE_PAYLOADS_PATH);
        if (result != 0) {
            return map_lfs_error(result);
        }

        struct lfs_info info;
        char payload_path[STORAGE_PATH_CAPACITY] = {0};
        storage_err_t status = STORAGE_OK;
        while ((result = lfs_dir_read(&g_lfs, &directory, &info)) > 0) {
            if (info.type != LFS_TYPE_DIR || strcmp(info.name, ".") == 0 ||
                strcmp(info.name, "..") == 0) {
                continue;
            }
            char candidate[STORAGE_PATH_CAPACITY];
            int written = snprintf(candidate, sizeof(candidate),
                                   STORAGE_PAYLOADS_PATH "/%s", info.name);
            if (written < 0 || (size_t)written >= sizeof(candidate)) {
                status = STORAGE_ERR_INVALID_ARG;
                break;
            }
            bool has_marker = false;
            status = directory_has_upload_marker(candidate, &has_marker);
            if (status != STORAGE_OK) {
                break;
            }
            if (has_marker) {
                memcpy(payload_path, candidate, (size_t)written + 1u);
                break;
            }
        }
        if (result < 0 && status == STORAGE_OK) {
            status = map_lfs_error(result);
        }
        int close_result = lfs_dir_close(&g_lfs, &directory);
        if (close_result != 0 && status == STORAGE_OK) {
            status = map_lfs_error(close_result);
        }
        if (status != STORAGE_OK) {
            return status;
        }
        if (payload_path[0] == '\0') {
            return STORAGE_OK;
        }
        status = remove_tree(payload_path);
        if (status != STORAGE_OK) {
            return status;
        }
    }
}

static storage_err_t remove_if_present(const char *path) {
    int result = lfs_remove(&g_lfs, path);
    return result == LFS_ERR_NOENT ? STORAGE_OK : map_lfs_error(result);
}

static storage_err_t initialize_fs_version(void) {
    uint8_t bytes[STORAGE_FS_VERSION_SIZE];
    size_t size = 0;
    storage_err_t result = storage_file_read(STORAGE_FS_VERSION_PATH,
                                             bytes, sizeof(bytes), &size);
    if (result == STORAGE_ERR_NOT_FOUND) {
        bool config_exists = false;
        result = storage_file_exists("/config/device.bin", &config_exists);
        if (result != STORAGE_OK) {
            return result;
        }
        lfs_dir_t payloads;
        int open_result = lfs_dir_open(&g_lfs, &payloads, STORAGE_PAYLOADS_PATH);
        if (open_result != 0) {
            return map_lfs_error(open_result);
        }
        struct lfs_info info;
        int first_entry;
        do {
            first_entry = lfs_dir_read(&g_lfs, &payloads, &info);
        } while (first_entry > 0 &&
                 (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0));
        int close_result = lfs_dir_close(&g_lfs, &payloads);
        if (first_entry < 0 || close_result != 0) {
            return map_lfs_error(first_entry < 0 ? first_entry : close_result);
        }
        if (config_exists || first_entry > 0) {
            return STORAGE_ERR_CORRUPT;
        }

        memset(bytes, 0, sizeof(bytes));
        memcpy(bytes, "MRXf", 4);
        write_le32(&bytes[4], STORAGE_FS_SCHEMA_VERSION);
        write_le32(&bytes[8], 0);
        write_le32(&bytes[12], crc32(bytes, 12));
        return storage_file_write_atomic(STORAGE_FS_VERSION_PATH, bytes, sizeof(bytes));
    }
    if (result == STORAGE_ERR_INVALID_ARG ||
        (result == STORAGE_OK && size != sizeof(bytes))) {
        return STORAGE_ERR_CORRUPT;
    }
    if (result != STORAGE_OK) {
        return result;
    }
    if (memcmp(bytes, "MRXf", 4) != 0 || read_le32(&bytes[8]) != 0 ||
        read_le32(&bytes[12]) != crc32(bytes, 12)) {
        return STORAGE_ERR_CORRUPT;
    }
    if (read_le32(&bytes[4]) != STORAGE_FS_SCHEMA_VERSION) {
        return STORAGE_ERR_VERSION;
    }
    return STORAGE_OK;
}

static void leave_unmounted(void) {
    (void)lfs_unmount(&g_lfs);
    g_mounted = false;
}

storage_err_t storage_init(void) {
    if (g_initialized) {
        return STORAGE_OK;
    }
    int result = rp2040_flash_bd_init(&g_lfs_config);
    if (result != 0 || g_lfs_config == NULL ||
        g_lfs_config->block_size != STORAGE_BLOCK_SIZE ||
        g_lfs_config->block_count != STORAGE_BLOCK_COUNT) {
        return STORAGE_ERR_IO;
    }
    g_initialized = true;
    return STORAGE_OK;
}

storage_err_t storage_mount(void) {
    if (!g_initialized) {
        return STORAGE_ERR_NOT_INITIALIZED;
    }
    if (g_mounted) {
        return STORAGE_OK;
    }

    int result = lfs_mount(&g_lfs, g_lfs_config);
    if (result == LFS_ERR_CORRUPT) {
        bool erased = false;
        int blank_result = rp2040_flash_bd_is_erased(&erased);
        if (blank_result != 0) {
            return map_lfs_error(blank_result);
        }
        if (!erased) {
            return STORAGE_ERR_CORRUPT;
        }
        result = lfs_format(&g_lfs, g_lfs_config);
        if (result != 0) {
            return map_lfs_error(result);
        }
        result = lfs_mount(&g_lfs, g_lfs_config);
    }
    if (result != 0) {
        return map_lfs_error(result);
    }
    g_mounted = true;

    storage_err_t status = ensure_directory("/config");
    if (status == STORAGE_OK) status = ensure_directory(STORAGE_PAYLOADS_PATH);
    if (status == STORAGE_OK) status = ensure_directory("/system");
    if (status == STORAGE_OK) status = remove_if_present(STORAGE_CONFIG_TEMP_PATH);
    if (status == STORAGE_OK) status = remove_if_present(STORAGE_FS_VERSION_TEMP_PATH);
    if (status == STORAGE_OK) status = recover_incomplete_uploads();
    if (status == STORAGE_OK) status = initialize_fs_version();
    if (status != STORAGE_OK) {
        leave_unmounted();
        return status;
    }

    return STORAGE_OK;
}

storage_err_t storage_unmount(void) {
    if (!g_mounted) {
        return STORAGE_ERR_NOT_MOUNTED;
    }
    for (size_t i = 0; i < STORAGE_FILE_STREAMS; ++i) {
        if (g_streams[i].active) {
            return STORAGE_ERR_BUSY;
        }
    }
    int result = lfs_unmount(&g_lfs);
    if (result == 0) {
        g_mounted = false;
    }
    return map_lfs_error(result);
}

bool storage_is_mounted(void) {
    return g_mounted;
}

storage_err_t storage_file_read(const char *path, void *buffer,
                                size_t capacity, size_t *size_out) {
    if (path == NULL || size_out == NULL || (capacity != 0 && buffer == NULL)) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    if (status != STORAGE_OK) {
        return status;
    }

    lfs_file_t file;
    int result = lfs_file_open(&g_lfs, &file, path, LFS_O_RDONLY);
    if (result != 0) {
        return map_lfs_error(result);
    }
    lfs_soff_t file_size = lfs_file_size(&g_lfs, &file);
    if (file_size < 0) {
        status = map_lfs_error((int)file_size);
    } else if ((uint64_t)file_size > capacity) {
        status = STORAGE_ERR_INVALID_ARG;
    } else {
        size_t total = 0;
        while (total < (size_t)file_size) {
            lfs_ssize_t count = lfs_file_read(&g_lfs, &file,
                    (uint8_t *)buffer + total, (lfs_size_t)((size_t)file_size - total));
            if (count <= 0) {
                status = count < 0 ? map_lfs_error((int)count) : STORAGE_ERR_IO;
                break;
            }
            total += (size_t)count;
        }
        if (status == STORAGE_OK) {
            *size_out = total;
        }
    }
    int close_result = lfs_file_close(&g_lfs, &file);
    if (status == STORAGE_OK && close_result != 0) {
        status = map_lfs_error(close_result);
    }
    return status;
}

storage_err_t storage_file_write_atomic(const char *path,
                                        const void *data, size_t size) {
    if (path == NULL || (size != 0 && data == NULL) ||
        size > STORAGE_FILESYSTEM_SIZE) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    if (status != STORAGE_OK) {
        return status;
    }

    char temp_path[STORAGE_PATH_CAPACITY];
    int written = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof(temp_path)) {
        return STORAGE_ERR_INVALID_ARG;
    }

    lfs_file_t file;
    int result = lfs_file_open(&g_lfs, &file, temp_path,
                               LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (result != 0) {
        return map_lfs_error(result);
    }
    size_t offset = 0;
    while (offset < size) {
        lfs_size_t remaining = (lfs_size_t)(size - offset);
        lfs_ssize_t count = lfs_file_write(&g_lfs, &file,
                (const uint8_t *)data + offset, remaining);
        if (count <= 0) {
            status = count < 0 ? map_lfs_error((int)count) : STORAGE_ERR_IO;
            break;
        }
        offset += (size_t)count;
    }
    if (status == STORAGE_OK) {
        result = lfs_file_sync(&g_lfs, &file);
        if (result != 0) {
            status = map_lfs_error(result);
        }
    }
    int close_result = lfs_file_close(&g_lfs, &file);
    if (status == STORAGE_OK && close_result != 0) {
        status = map_lfs_error(close_result);
    }
    if (status != STORAGE_OK) {
        return status;
    }

    result = lfs_file_open(&g_lfs, &file, temp_path, LFS_O_RDONLY);
    if (result != 0) {
        return map_lfs_error(result);
    }
    uint8_t verify[STORAGE_FILE_VERIFY_CHUNK];
    offset = 0;
    while (offset < size) {
        const size_t request = (size - offset) < sizeof(verify)
                ? size - offset : sizeof(verify);
        lfs_ssize_t count = lfs_file_read(&g_lfs, &file, verify, (lfs_size_t)request);
        if (count <= 0) {
            status = count < 0 ? map_lfs_error((int)count) : STORAGE_ERR_CORRUPT;
            break;
        }
        if (memcmp(verify, (const uint8_t *)data + offset, (size_t)count) != 0) {
            status = STORAGE_ERR_CORRUPT;
            break;
        }
        offset += (size_t)count;
    }
    if (status == STORAGE_OK && offset != size) {
        status = STORAGE_ERR_CORRUPT;
    }
    close_result = lfs_file_close(&g_lfs, &file);
    if (status == STORAGE_OK && close_result != 0) {
        status = map_lfs_error(close_result);
    }
    if (status != STORAGE_OK) {
        return status;
    }

    return map_lfs_error(lfs_rename(&g_lfs, temp_path, path));
}

storage_err_t storage_file_remove(const char *path) {
    if (path == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    return status == STORAGE_OK ? map_lfs_error(lfs_remove(&g_lfs, path)) : status;
}

storage_err_t storage_file_exists(const char *path, bool *exists_out) {
    if (path == NULL || exists_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    if (status != STORAGE_OK) {
        return status;
    }
    struct lfs_info info;
    int result = lfs_stat(&g_lfs, path, &info);
    if (result == LFS_ERR_NOENT) {
        *exists_out = false;
        return STORAGE_OK;
    }
    if (result != 0) {
        return map_lfs_error(result);
    }
    *exists_out = true;
    return STORAGE_OK;
}

storage_err_t storage_directory_create(const char *path) {
    if (path == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    return status == STORAGE_OK ? ensure_directory(path) : status;
}

storage_err_t storage_directory_remove_tree(const char *path) {
    if (path == NULL || strcmp(path, "/") == 0) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    return status == STORAGE_OK ? remove_tree(path) : status;
}

storage_err_t storage_directory_visit(const char *path,
                                      storage_directory_visitor_t visitor,
                                      void *context) {
    if (path == NULL || visitor == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    if (status != STORAGE_OK) {
        return status;
    }

    lfs_dir_t directory;
    int result = lfs_dir_open(&g_lfs, &directory, path);
    if (result != 0) {
        return map_lfs_error(result);
    }
    struct lfs_info info;
    while ((result = lfs_dir_read(&g_lfs, &directory, &info)) > 0) {
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }
        const storage_entry_type_t type = info.type == LFS_TYPE_DIR
                ? STORAGE_ENTRY_DIRECTORY : STORAGE_ENTRY_FILE;
        if (!visitor(info.name, type, context)) {
            break;
        }
    }
    if (result < 0) {
        status = map_lfs_error(result);
    }
    int close_result = lfs_dir_close(&g_lfs, &directory);
    if (status == STORAGE_OK && close_result != 0) {
        status = map_lfs_error(close_result);
    }
    return status;
}

storage_err_t storage_file_rename(const char *source, const char *destination) {
    if (source == NULL || destination == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    return status == STORAGE_OK
            ? map_lfs_error(lfs_rename(&g_lfs, source, destination)) : status;
}

static storage_stream_t *get_stream(storage_file_handle_t handle) {
    if (handle.slot >= STORAGE_FILE_STREAMS) {
        return NULL;
    }
    storage_stream_t *stream = &g_streams[handle.slot];
    return stream->active && stream->generation == handle.generation
            ? stream : NULL;
}

storage_err_t storage_file_stream_open(const char *path, storage_file_mode_t mode,
                                       storage_file_handle_t *handle_out) {
    if (path == NULL || handle_out == NULL ||
        (mode != STORAGE_FILE_READ && mode != STORAGE_FILE_WRITE_TRUNCATE)) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    if (status != STORAGE_OK) {
        return status;
    }
    uint32_t flags = mode == STORAGE_FILE_READ ? LFS_O_RDONLY
            : LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    for (uint32_t i = 0; i < STORAGE_FILE_STREAMS; ++i) {
        storage_stream_t *stream = &g_streams[i];
        if (stream->active) {
            continue;
        }
        int result = lfs_file_open(&g_lfs, &stream->file, path, (int)flags);
        if (result != 0) {
            return map_lfs_error(result);
        }
        stream->generation++;
        if (stream->generation == 0) {
            stream->generation = 1;
        }
        stream->active = true;
        handle_out->slot = i;
        handle_out->generation = stream->generation;
        return STORAGE_OK;
    }
    return STORAGE_ERR_BUSY;
}

storage_err_t storage_file_stream_read(storage_file_handle_t handle,
                                       void *buffer, size_t capacity,
                                       size_t *bytes_read_out) {
    if (bytes_read_out == NULL || (capacity != 0 && buffer == NULL) ||
        capacity > UINT32_MAX) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_stream_t *stream = get_stream(handle);
    if (stream == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    lfs_ssize_t result = lfs_file_read(&g_lfs, &stream->file, buffer,
                                       (lfs_size_t)capacity);
    if (result < 0) {
        return map_lfs_error((int)result);
    }
    *bytes_read_out = (size_t)result;
    return STORAGE_OK;
}

storage_err_t storage_file_stream_write(storage_file_handle_t handle,
                                        const void *data, size_t size,
                                        size_t *bytes_written_out) {
    if (bytes_written_out == NULL || (size != 0 && data == NULL) ||
        size > UINT32_MAX) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_stream_t *stream = get_stream(handle);
    if (stream == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    lfs_ssize_t result = lfs_file_write(&g_lfs, &stream->file, data,
                                        (lfs_size_t)size);
    if (result < 0) {
        return map_lfs_error((int)result);
    }
    *bytes_written_out = (size_t)result;
    return STORAGE_OK;
}

storage_err_t storage_file_stream_seek(storage_file_handle_t handle,
                                       uint64_t offset) {
    storage_stream_t *stream = get_stream(handle);
    if (stream == NULL || offset > UINT32_MAX) {
        return STORAGE_ERR_INVALID_ARG;
    }
    lfs_soff_t result = lfs_file_seek(&g_lfs, &stream->file,
                                     (lfs_soff_t)offset, LFS_SEEK_SET);
    return result < 0 ? map_lfs_error((int)result) : STORAGE_OK;
}

storage_err_t storage_file_stream_size(storage_file_handle_t handle,
                                       uint64_t *size_out) {
    if (size_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_stream_t *stream = get_stream(handle);
    if (stream == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    lfs_soff_t result = lfs_file_size(&g_lfs, &stream->file);
    if (result < 0) {
        return map_lfs_error((int)result);
    }
    *size_out = (uint64_t)result;
    return STORAGE_OK;
}

storage_err_t storage_file_stream_sync(storage_file_handle_t handle) {
    storage_stream_t *stream = get_stream(handle);
    if (stream == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    return map_lfs_error(lfs_file_sync(&g_lfs, &stream->file));
}

storage_err_t storage_file_stream_close(storage_file_handle_t handle) {
    storage_stream_t *stream = get_stream(handle);
    if (stream == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    int result = lfs_file_close(&g_lfs, &stream->file);
    stream->active = false;
    return map_lfs_error(result);
}

storage_err_t storage_get_space(uint64_t *total_bytes_out,
                                uint64_t *used_bytes_out) {
    if (total_bytes_out == NULL || used_bytes_out == NULL) {
        return STORAGE_ERR_INVALID_ARG;
    }
    storage_err_t status = require_mounted();
    if (status != STORAGE_OK) {
        return status;
    }
    lfs_ssize_t used_blocks = lfs_fs_size(&g_lfs);
    if (used_blocks < 0 || (uint64_t)used_blocks > STORAGE_BLOCK_COUNT) {
        return STORAGE_ERR_IO;
    }
    *total_bytes_out = STORAGE_FILESYSTEM_SIZE;
    *used_bytes_out = (uint64_t)used_blocks * STORAGE_BLOCK_SIZE;
    return STORAGE_OK;
}
