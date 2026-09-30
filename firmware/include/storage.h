#ifndef MRX_STORAGE_H
#define MRX_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERR_NOT_MOUNTED,
    STORAGE_ERR_NOT_FOUND,
    STORAGE_ERR_CORRUPT,
    STORAGE_ERR_VERSION,
    STORAGE_ERR_IO,
    STORAGE_ERR_FULL,
    STORAGE_ERR_INVALID_ARG,
    STORAGE_ERR_BUSY,
    STORAGE_ERR_NO_SELECTION,
    STORAGE_ERR_INCOMPLETE,
    STORAGE_ERR_NOT_INITIALIZED,
} storage_err_t;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} storage_file_handle_t;

typedef enum {
    STORAGE_FILE_READ = 0,
    STORAGE_FILE_WRITE_TRUNCATE,
} storage_file_mode_t;

typedef enum {
    STORAGE_ENTRY_FILE = 1,
    STORAGE_ENTRY_DIRECTORY = 2,
} storage_entry_type_t;

typedef bool (*storage_directory_visitor_t)(const char *name,
                                            storage_entry_type_t type,
                                            void *context);


storage_err_t storage_init(void);


storage_err_t storage_mount(void);
storage_err_t storage_unmount(void);
bool storage_is_mounted(void);


storage_err_t storage_file_read(const char *path, void *buffer,
                                size_t capacity, size_t *size_out);
storage_err_t storage_file_write_atomic(const char *path,
                                        const void *data, size_t size);
storage_err_t storage_file_remove(const char *path);
storage_err_t storage_file_exists(const char *path, bool *exists_out);
storage_err_t storage_directory_create(const char *path);
storage_err_t storage_directory_remove_tree(const char *path);
storage_err_t storage_directory_visit(const char *path,
                                      storage_directory_visitor_t visitor,
                                      void *context);
storage_err_t storage_file_rename(const char *source, const char *destination);
storage_err_t storage_file_stream_open(const char *path, storage_file_mode_t mode,
                                       storage_file_handle_t *handle_out);
storage_err_t storage_file_stream_read(storage_file_handle_t handle,
                                       void *buffer, size_t capacity,
                                       size_t *bytes_read_out);
storage_err_t storage_file_stream_write(storage_file_handle_t handle,
                                        const void *data, size_t size,
                                        size_t *bytes_written_out);
storage_err_t storage_file_stream_seek(storage_file_handle_t handle,
                                       uint64_t offset);
storage_err_t storage_file_stream_size(storage_file_handle_t handle,
                                       uint64_t *size_out);
storage_err_t storage_file_stream_sync(storage_file_handle_t handle);
storage_err_t storage_file_stream_close(storage_file_handle_t handle);
storage_err_t storage_get_space(uint64_t *total_bytes_out,
                                uint64_t *used_bytes_out);

#endif
