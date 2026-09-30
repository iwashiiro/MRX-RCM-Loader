#include "mrx_protocol.h"

#include <string.h>

#include "config.h"
#include "payload_manager.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "storage.h"
#include "tusb.h"

static mrx_parser_t g_parser;
static uint8_t g_tx_buffer[MRX_MAX_PACKET_SIZE];
static uint8_t g_command_payload[MRX_MAX_DATA_SIZE - 1u];
static uint32_t g_payload_ids[(MRX_MAX_DATA_SIZE - 3u) / 4u];
static mrx_packet_t g_rx_packet;
static bool g_cdc_connected;
static uint8_t g_device_state = MRX_STATE_BOOTING;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void wr16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)(value >> 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static void wr32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void wr64(uint8_t *p, uint64_t value) {
    wr32(p, (uint32_t)value);
    wr32(p + 4, (uint32_t)(value >> 32));
}


uint16_t mrx_crc16_ccitt_false(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void mrx_parser_init(mrx_parser_t *parser) {
    memset(parser, 0, sizeof(*parser));
}

static bool magic_at_start(const uint8_t *b) {
    return b[0] == MRX_MAGIC_0 && b[1] == MRX_MAGIC_1 &&
           b[2] == MRX_MAGIC_2 && b[3] == MRX_MAGIC_3;
}


static void parser_resync(mrx_parser_t *parser) {
    size_t start = parser->used;
    for (size_t i = 1; i + 4 <= parser->used; ++i) {
        if (magic_at_start(&parser->buffer[i])) {
            const size_t remaining = parser->used - i;
            if (remaining >= MRX_HEADER_SIZE) {
                const uint16_t length = rd16(&parser->buffer[i + 8]);
                if (length > MRX_MAX_DATA_SIZE ||
                    remaining >= MRX_HEADER_SIZE + length + MRX_CRC_SIZE) {
                    continue;
                }
            }
            start = i;
            break;
        }
    }

    if (start < parser->used) {
        const size_t remaining = parser->used - start;
        memmove(parser->buffer, &parser->buffer[start], remaining);
        parser->used = remaining;
        parser->in_frame = true;
        parser->expected_total = 0;
        if (parser->used >= MRX_HEADER_SIZE) {
            const uint16_t length = rd16(&parser->buffer[8]);
            if (length <= MRX_MAX_DATA_SIZE) {
                parser->expected_total = MRX_HEADER_SIZE + length + MRX_CRC_SIZE;
            }
        }
        return;
    }


    size_t keep = 0;
    const size_t max_prefix = parser->used < 3 ? parser->used : 3;
    for (size_t n = max_prefix; n > 0; --n) {
        if (memcmp(&parser->buffer[parser->used - n], "MRX!", n) == 0) {
            keep = n;
            break;
        }
    }
    if (keep != 0) {
        memmove(parser->buffer, &parser->buffer[parser->used - keep], keep);
    }
    parser->used = keep;
    parser->expected_total = 0;
    parser->in_frame = false;
}

mrx_parse_result_t mrx_parser_feed(mrx_parser_t *parser, uint8_t byte, mrx_packet_t *packet) {
    if (!parser->in_frame) {
        if (parser->used < 4) {
            parser->buffer[parser->used++] = byte;
            if (parser->used < 4) {
                return MRX_PARSE_NONE;
            }
            if (!magic_at_start(parser->buffer)) {
                memmove(parser->buffer, parser->buffer + 1, 3);
                parser->used = 3;
                return MRX_PARSE_NONE;
            }
        }
        parser->in_frame = true;
        parser->expected_total = 0;
    } else {
        if (parser->used >= sizeof(parser->buffer)) {
            parser_resync(parser);
            return MRX_PARSE_BAD_LENGTH;
        }
        parser->buffer[parser->used++] = byte;
    }

    if (parser->in_frame && parser->used >= MRX_HEADER_SIZE) {
        const uint16_t length = rd16(&parser->buffer[8]);
        if (length > MRX_MAX_DATA_SIZE) {
            parser_resync(parser);
            return MRX_PARSE_BAD_LENGTH;
        }
        parser->expected_total = MRX_HEADER_SIZE + length + MRX_CRC_SIZE;
    }

    if (parser->in_frame && parser->expected_total != 0 &&
        parser->used == parser->expected_total) {
        const uint16_t received_crc = rd16(&parser->buffer[parser->used - 2]);
        const uint16_t calculated_crc = mrx_crc16_ccitt_false(parser->buffer, parser->used - 2);

        if (received_crc != calculated_crc) {
            parser_resync(parser);
            return MRX_PARSE_BAD_CRC;
        }

        packet->cmd = parser->buffer[4];
        packet->flags = parser->buffer[5];
        packet->seq = rd16(&parser->buffer[6]);
        packet->length = rd16(&parser->buffer[8]);
        memcpy(packet->data, &parser->buffer[10], packet->length);

        mrx_parser_init(parser);
        return MRX_PARSE_PACKET_READY;
    }

    return MRX_PARSE_NONE;
}

size_t mrx_packet_encode(const mrx_packet_t *packet, uint8_t *out, size_t out_size) {
    if (packet == NULL || out == NULL || packet->length > MRX_MAX_DATA_SIZE) {
        return 0;
    }

    const size_t total = MRX_HEADER_SIZE + packet->length + MRX_CRC_SIZE;
    if (out_size < total) {
        return 0;
    }

    out[0] = MRX_MAGIC_0;
    out[1] = MRX_MAGIC_1;
    out[2] = MRX_MAGIC_2;
    out[3] = MRX_MAGIC_3;
    out[4] = packet->cmd;
    out[5] = packet->flags;
    wr16(&out[6], packet->seq);
    wr16(&out[8], packet->length);
    memcpy(&out[10], packet->data, packet->length);

    const uint16_t crc = mrx_crc16_ccitt_false(out, total - 2);
    wr16(&out[total - 2], crc);
    return total;
}

bool mrx_send_response(uint8_t request_cmd, uint16_t seq, uint8_t status,
                       const uint8_t *payload, uint16_t payload_len) {
    if (payload_len > MRX_MAX_DATA_SIZE - 1u ||
        (payload_len != 0 && payload == NULL) || !tud_cdc_connected()) {
        return false;
    }

    const uint16_t data_length = (uint16_t)(payload_len + 1u);
    g_tx_buffer[0] = MRX_MAGIC_0;
    g_tx_buffer[1] = MRX_MAGIC_1;
    g_tx_buffer[2] = MRX_MAGIC_2;
    g_tx_buffer[3] = MRX_MAGIC_3;
    g_tx_buffer[4] = (uint8_t)(request_cmd | MRX_RESPONSE_BIT);
    g_tx_buffer[5] = 0;
    wr16(&g_tx_buffer[6], seq);
    wr16(&g_tx_buffer[8], data_length);
    g_tx_buffer[10] = status;
    if (payload_len != 0 && payload != NULL) {
        memcpy(&g_tx_buffer[11], payload, payload_len);
    }
    const size_t total = MRX_HEADER_SIZE + data_length + MRX_CRC_SIZE;
    wr16(&g_tx_buffer[total - 2], mrx_crc16_ccitt_false(g_tx_buffer, total - 2));
    size_t offset = 0;
    while (offset < total) {
        if (!tud_cdc_connected()) {
            return false;
        }
        uint32_t available = tud_cdc_write_available();
        if (available == 0) {
            tud_cdc_write_flush();
            tud_task();
            continue;
        }
        size_t remaining = total - offset;
        uint32_t request_size = remaining < available
                ? (uint32_t)remaining : available;
        uint32_t written = tud_cdc_write(&g_tx_buffer[offset], request_size);
        if (written == 0) {
            tud_cdc_write_flush();
            tud_task();
            continue;
        }
        offset += written;
    }
    tud_cdc_write_flush();
    return true;
}

static uint8_t storage_status(storage_err_t result) {
    switch (result) {
        case STORAGE_OK: return MRX_STATUS_OK;
        case STORAGE_ERR_NOT_FOUND: return MRX_STATUS_ERR_NOT_FOUND;
        case STORAGE_ERR_CORRUPT: return MRX_STATUS_ERR_CORRUPT;
        case STORAGE_ERR_VERSION: return MRX_STATUS_ERR_VERSION;
        case STORAGE_ERR_FULL: return MRX_STATUS_ERR_FULL;
        case STORAGE_ERR_INVALID_ARG: return MRX_STATUS_ERR_INVALID;
        case STORAGE_ERR_BUSY: return MRX_STATUS_ERR_BUSY;
        case STORAGE_ERR_NO_SELECTION: return MRX_STATUS_ERR_NO_SEL;
        case STORAGE_ERR_INCOMPLETE: return MRX_STATUS_ERR_INCOMPLETE;
        default: return MRX_STATUS_ERR_IO;
    }
}

static bool request_length(const mrx_packet_t *request, uint16_t expected) {
    if (request->length == expected) return true;
    mrx_send_response(request->cmd, request->seq,
                      MRX_STATUS_ERR_INVALID, NULL, 0);
    return false;
}

static void handle_get_status(const mrx_packet_t *request) {
    if (!request_length(request, 0)) return;
    uint32_t selected_id = 0;
    DeviceConfig config;
    storage_err_t result = config_load(&config);
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    selected_id = config.selected_payload_id;
    size_t count = 0;
    result = payload_list(g_payload_ids,
            sizeof(g_payload_ids) / sizeof(g_payload_ids[0]), &count);
    uint64_t total = 0, used = 0;
    if (result == STORAGE_OK) result = storage_get_space(&total, &used);
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    g_command_payload[0] = g_device_state;
    g_command_payload[1] = 0;
    g_command_payload[2] = 0;
    wr32(&g_command_payload[3], selected_id);
    wr32(&g_command_payload[7], (uint32_t)count);
    wr64(&g_command_payload[11], total - used);
    wr64(&g_command_payload[19], total);
    mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                      g_command_payload, 27);
}

static void handle_list_payloads(const mrx_packet_t *request) {
    if (!request_length(request, 0)) return;
    size_t count = 0;
    storage_err_t result = payload_list(g_payload_ids,
            sizeof(g_payload_ids) / sizeof(g_payload_ids[0]), &count);
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    wr16(g_command_payload, (uint16_t)count);
    for (size_t i = 0; i < count; ++i) {
        wr32(&g_command_payload[2 + i * 4], g_payload_ids[i]);
    }
    mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                      g_command_payload, (uint16_t)(2 + count * 4));
}

static void handle_get_payload_info(const mrx_packet_t *request) {
    if (!request_length(request, 4)) return;
    PayloadInfo info;
    storage_err_t result = payload_get_info(rd32(request->data), &info);
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    wr32(g_command_payload, info.id);
    wr32(&g_command_payload[4], info.flags);
    wr64(&g_command_payload[8], info.size);
    memcpy(&g_command_payload[16], info.sha256, sizeof(info.sha256));
    memcpy(&g_command_payload[48], info.name, sizeof(info.name));
    mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                      g_command_payload, 112);
}

static void handle_upload_begin(const mrx_packet_t *request) {
    if (request->length < 9 || request->length > 72 ||
        request->data[8] > 63 || request->length != 9u + request->data[8]) {
        mrx_send_response(request->cmd, request->seq,
                          MRX_STATUS_ERR_INVALID, NULL, 0);
        return;
    }
    char name[64];
    for (size_t i = 0; i < request->data[8]; ++i) {
        if (request->data[9 + i] < 0x20 || request->data[9 + i] > 0x7E) {
            mrx_send_response(request->cmd, request->seq,
                              MRX_STATUS_ERR_INVALID, NULL, 0);
            return;
        }
    }
    memcpy(name, &request->data[9], request->data[8]);
    name[request->data[8]] = '\0';
    uint32_t id = 0;
    storage_err_t result = payload_begin_upload(name, rd64(request->data), &id);
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    wr32(g_command_payload, id);
    mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                      g_command_payload, 4);
}

static void handle_upload_data(const mrx_packet_t *request) {
    if (request->length < 17) {
        if (request->length >= 4) {
            (void)payload_abort_upload(rd32(request->data));
        }
        mrx_send_response(request->cmd, request->seq,
                          MRX_STATUS_ERR_INVALID, NULL, 0);
        return;
    }
    if (request->data[4] != 0 || request->data[5] != 0 ||
        request->data[6] != 0 || request->data[7] != 0) {
        (void)payload_abort_upload(rd32(request->data));
        mrx_send_response(request->cmd, request->seq,
                          MRX_STATUS_ERR_INVALID, NULL, 0);
        return;
    }
    uint64_t bytes_received = 0;
    storage_err_t result = payload_write_chunk(rd32(request->data),
            rd64(&request->data[8]), &request->data[16],
            request->length - 16u, &bytes_received);
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    wr64(g_command_payload, bytes_received);
    mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                      g_command_payload, 8);
}

static void handle_upload_end(const mrx_packet_t *request) {
    if (!request_length(request, 44)) return;
    const uint32_t id = rd32(request->data);
    const uint64_t expected_size = rd64(&request->data[4]);
    storage_err_t result = payload_finalize_upload(id, expected_size,
                                                    &request->data[12]);
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    PayloadInfo info;
    result = payload_get_info(id, &info);
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    wr32(g_command_payload, info.id);
    wr64(&g_command_payload[4], info.size);
    memcpy(&g_command_payload[12], info.sha256, sizeof(info.sha256));
    mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                      g_command_payload, 44);
}

static void handle_verify_payload(const mrx_packet_t *request) {
    if (!request_length(request, 4)) return;
    PayloadInfo info;
    bool matches = false;
    uint8_t actual[32];
    storage_err_t result = payload_get_info(rd32(request->data), &info);
    if (result == STORAGE_OK) {
        result = payload_verify(info.id, &matches, actual);
    }
    if (result != STORAGE_OK) {
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    g_command_payload[0] = matches ? 1u : 0u;
    memcpy(&g_command_payload[1], info.sha256, 32);
    memcpy(&g_command_payload[33], actual, 32);
    mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                      g_command_payload, 65);
}

static void handle_get_info(const mrx_packet_t *request) {
    uint8_t data[85] = {0};
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);

    data[0] = MRX_PROTOCOL_VERSION;
    data[1] = 0;
    data[2] = 1;
    data[3] = 0;
    data[4] = 0;
    strncpy((char *)&data[5], MRX_PRODUCT_STRING, 32);
    strncpy((char *)&data[37], MRX_HW_STRING, 32);

    memcpy(&data[69], id.id, sizeof(id.id));
    mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK, data, sizeof(data));
}

static void handle_reboot(const mrx_packet_t *request) {
    if (!request_length(request, 1)) return;
    const uint8_t mode = request->data[0];
    if (mode > MRX_REBOOT_BOOTLOADER) {
        mrx_send_response(request->cmd, request->seq,
                          MRX_STATUS_ERR_INVALID, NULL, 0);
        return;
    }


    if (!mrx_send_response(request->cmd, request->seq,
                           MRX_STATUS_OK, NULL, 0)) {
        return;
    }


    for (unsigned i = 0; i < 12; ++i) {
        tud_task();
        sleep_ms(10);
    }

    if (mode == MRX_REBOOT_BOOTLOADER) {
        reset_usb_boot(0, 0);
    } else {
        watchdog_reboot(0, 0, 0);
    }
}

static void dispatch_packet(const mrx_packet_t *request) {
    if ((request->cmd & MRX_RESPONSE_BIT) != 0) {
        return;
    }

    if (request->cmd == MRX_CMD_GET_INFO) {
        if (request->length == 0) {
            handle_get_info(request);
        } else {
            mrx_send_response(request->cmd, request->seq, MRX_STATUS_ERR_INVALID, NULL, 0);
        }
        return;
    }

    if (request->cmd == MRX_CMD_GET_STATUS) {
        handle_get_status(request);
        return;
    }
    if (request->cmd == MRX_CMD_LIST_PAYLOADS) {
        handle_list_payloads(request);
        return;
    }
    if (request->cmd == MRX_CMD_GET_PAYLOAD_INFO) {
        handle_get_payload_info(request);
        return;
    }
    if (request->cmd == MRX_CMD_UPLOAD_BEGIN) {
        handle_upload_begin(request);
        return;
    }
    if (request->cmd == MRX_CMD_UPLOAD_DATA) {
        handle_upload_data(request);
        return;
    }
    if (request->cmd == MRX_CMD_UPLOAD_END) {
        handle_upload_end(request);
        return;
    }
    if (request->cmd == MRX_CMD_UPLOAD_ABORT) {
        if (!request_length(request, 4)) return;
        storage_err_t result = payload_abort_upload(rd32(request->data));
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    if (request->cmd == MRX_CMD_DELETE_PAYLOAD) {
        if (!request_length(request, 4)) return;
        bool selection_cleared = false;
        storage_err_t result = payload_delete(rd32(request->data),
                                               &selection_cleared);
        if (result == STORAGE_OK) {
            g_command_payload[0] = selection_cleared ? 1u : 0u;
            mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                              g_command_payload, 1);
        } else {
            mrx_send_response(request->cmd, request->seq,
                              storage_status(result), NULL, 0);
        }
        return;
    }
    if (request->cmd == MRX_CMD_SELECT_PAYLOAD) {
        if (!request_length(request, 4)) return;
        storage_err_t result = payload_set_selected(rd32(request->data));
        mrx_send_response(request->cmd, request->seq,
                          storage_status(result), NULL, 0);
        return;
    }
    if (request->cmd == MRX_CMD_GET_SELECTED) {
        if (!request_length(request, 0)) return;
        DeviceConfig config;
        storage_err_t result = config_load(&config);
        if (result == STORAGE_OK) {
            wr32(g_command_payload, config.selected_payload_id);
            mrx_send_response(request->cmd, request->seq, MRX_STATUS_OK,
                              g_command_payload, 4);
        } else {
            mrx_send_response(request->cmd, request->seq,
                              storage_status(result), NULL, 0);
        }
        return;
    }
    if (request->cmd == MRX_CMD_VERIFY_PAYLOAD) {
        handle_verify_payload(request);
        return;
    }
    if (request->cmd == MRX_CMD_REBOOT) {
        handle_reboot(request);
        return;
    }

    mrx_send_response(request->cmd, request->seq, MRX_STATUS_ERR_UNKNOWN, NULL, 0);
}

void mrx_protocol_task(void) {
    if (!tud_cdc_connected()) {
        if (g_cdc_connected) {

            mrx_parser_init(&g_parser);
        }
        g_cdc_connected = false;
        return;
    }
    g_cdc_connected = true;

    uint8_t input[64];
    while (tud_cdc_available()) {
        const uint32_t count = tud_cdc_read(input, sizeof(input));
        if (count == 0) {
            break;
        }

        for (uint32_t i = 0; i < count; ++i) {
            const mrx_parse_result_t result = mrx_parser_feed(&g_parser, input[i], &g_rx_packet);
            if (result == MRX_PARSE_PACKET_READY) {
                dispatch_packet(&g_rx_packet);
            }
        }

    }
}

void mrx_protocol_init(void) {
    mrx_parser_init(&g_parser);
    g_cdc_connected = false;
    g_device_state = MRX_STATE_BOOTING;
}

void mrx_protocol_set_device_state(uint8_t state) {
    g_device_state = state;
}
