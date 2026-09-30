#ifndef MRX_PAYLOAD_MANAGER_H
#define MRX_PAYLOAD_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "storage.h"

#define MRX_PAYLOAD_FLAG_VALID     (1u << 0)
#define MRX_PAYLOAD_FLAG_UPLOADING (1u << 1)
#define MRX_PAYLOAD_NAME_SIZE     64u
#define MRX_PAYLOAD_SHA256_SIZE   32u

typedef struct {
    uint32_t id;
    uint32_t flags;
    uint64_t size;
    uint8_t sha256[MRX_PAYLOAD_SHA256_SIZE];
    char name[MRX_PAYLOAD_NAME_SIZE];
} PayloadInfo;

#ifdef __cplusplus
extern "C" {
#endif

storage_err_t payload_manager_validate_selection(void);
storage_err_t payload_begin_upload(const char *name, uint64_t expected_size,
                                   uint32_t *id_out);
storage_err_t payload_write_chunk(uint32_t id, uint64_t offset,
                                  const void *data, size_t length,
                                  uint64_t *bytes_received_out);
storage_err_t payload_finalize_upload(uint32_t id, uint64_t expected_size,
                                      const uint8_t expected_sha256[32]);
storage_err_t payload_abort_upload(uint32_t id);
storage_err_t payload_exists(uint32_t id, bool *exists_out);
storage_err_t payload_get_info(uint32_t id, PayloadInfo *info_out);
storage_err_t payload_list(uint32_t *id_array, size_t max_count,
                           size_t *count_out);
storage_err_t payload_read_chunk(uint32_t id, uint64_t offset, void *buffer,
                                 size_t length, size_t *bytes_read_out);
storage_err_t payload_delete(uint32_t id, bool *selection_cleared_out);
storage_err_t payload_verify(uint32_t id, bool *matches_out,
                             uint8_t actual_sha256_out[32]);
storage_err_t payload_set_selected(uint32_t id);
storage_err_t payload_get_selected(uint32_t *id_out);
storage_err_t payload_clear_selected(void);

#ifdef __cplusplus
}
#endif

#endif
