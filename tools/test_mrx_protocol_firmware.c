#include "mrx_protocol.h"
#include "mrx_board.h"
#include "config.h"
#include "payload_manager.h"
#include "storage.h"

#include <assert.h>
#include <string.h>

#include "pico/unique_id.h"
#include "pico/stdlib.h"
#include "tusb.h"

static uint8_t input[MRX_MAX_PACKET_SIZE * 2];
static size_t input_length;
static size_t input_position;
static uint8_t output[MRX_MAX_PACKET_SIZE * 2];
static size_t output_length;
static bool gpio_output[30];
static bool gpio_level[30];
static bool cdc_connected = true;
static size_t cdc_read_limit = SIZE_MAX;
static uint32_t cdc_write_limit = 4096u;
static bool list_all_payloads;
static int reboot_requested = -1;

uint32_t tud_cdc_available(void) {
    return (uint32_t)(input_length - input_position);
}

uint32_t tud_cdc_read(void *buffer, uint32_t size) {
    size_t remaining = input_length - input_position;
    size_t count = remaining < size ? remaining : size;
    if (count > cdc_read_limit) {
        count = cdc_read_limit;
    }
    memcpy(buffer, &input[input_position], count);
    input_position += count;
    return (uint32_t)count;
}

bool tud_cdc_connected(void) {
    return cdc_connected;
}

uint32_t tud_cdc_write(void const *buffer, uint32_t size) {
    assert(output_length + size <= sizeof(output));
    memcpy(&output[output_length], buffer, size);
    output_length += size;
    return size;
}

uint32_t tud_cdc_write_available(void) {
    return cdc_write_limit;
}

void tud_cdc_write_flush(void) {
}

void tud_task(void) {
}

storage_err_t config_load(DeviceConfig *config_out) {
    (void)config_out;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t payload_list(uint32_t *ids, size_t max_count, size_t *count_out) {
    if (!list_all_payloads) return STORAGE_ERR_NOT_MOUNTED;
    for (size_t i = 0; i < max_count; ++i) ids[i] = (uint32_t)(i + 1u);
    *count_out = max_count;
    return STORAGE_OK;
}

storage_err_t payload_get_info(uint32_t id, PayloadInfo *info_out) {
    (void)id; (void)info_out;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t payload_begin_upload(const char *name, uint64_t size, uint32_t *id_out) {
    (void)name; (void)size; (void)id_out;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t payload_write_chunk(uint32_t id, uint64_t offset, const void *data,
                                  size_t length, uint64_t *received_out) {
    (void)id; (void)offset; (void)data; (void)length; (void)received_out;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t payload_finalize_upload(uint32_t id, uint64_t size,
                                      const uint8_t sha256[32]) {
    (void)id; (void)size; (void)sha256;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t payload_abort_upload(uint32_t id) {
    (void)id;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t payload_delete(uint32_t id, bool *cleared_out) {
    (void)id; (void)cleared_out;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t payload_set_selected(uint32_t id) {
    (void)id;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t payload_verify(uint32_t id, bool *matches_out, uint8_t sha256_out[32]) {
    (void)id; (void)matches_out; (void)sha256_out;
    return STORAGE_ERR_NOT_MOUNTED;
}

storage_err_t storage_get_space(uint64_t *total_out, uint64_t *used_out) {
    (void)total_out; (void)used_out;
    return STORAGE_ERR_NOT_MOUNTED;
}

void pico_get_unique_board_id(pico_unique_board_id_t *id_out) {
    for (size_t i = 0; i < sizeof(id_out->id); ++i) {
        id_out->id[i] = (uint8_t)(i + 1);
    }
}

void gpio_init(uint gpio) {
    assert(gpio < 30);
}

void gpio_set_dir(uint gpio, bool out) {
    assert(gpio < 30);
    gpio_output[gpio] = out;
}

void gpio_put(uint gpio, bool value) {
    assert(gpio < 30);
    gpio_level[gpio] = value;
}

void sleep_ms(uint32_t ms) {
    (void)ms;
}

void reset_usb_boot(uint32_t usb_activity_gpio_pin_mask, uint32_t disable_interface_mask) {
    assert(usb_activity_gpio_pin_mask == 0);
    assert(disable_interface_mask == 0);
    reboot_requested = MRX_REBOOT_BOOTLOADER;
}

void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    assert(pc == 0 && sp == 0 && delay_ms == 0);
    reboot_requested = MRX_REBOOT_NORMAL;
}

static size_t make_packet(uint8_t cmd, uint16_t seq, const uint8_t *data,
                          uint16_t length, uint8_t *buffer, size_t capacity) {
    mrx_packet_t packet = { .cmd = cmd, .flags = 0, .seq = seq, .length = length };
    if (length != 0) {
        memcpy(packet.data, data, length);
    }
    return mrx_packet_encode(&packet, buffer, capacity);
}

static void feed_packet(mrx_parser_t *parser, const uint8_t *bytes, size_t length,
                        mrx_packet_t *packet, size_t *ready_count) {
    for (size_t i = 0; i < length; ++i) {
        if (mrx_parser_feed(parser, bytes[i], packet) == MRX_PARSE_PACKET_READY) {
            ++*ready_count;
        }
    }
}

static mrx_packet_t parse_single(const uint8_t *bytes, size_t length) {
    mrx_parser_t parser;
    mrx_packet_t packet = {0};
    size_t ready_count = 0;
    mrx_parser_init(&parser);
    feed_packet(&parser, bytes, length, &packet, &ready_count);
    assert(ready_count == 1);
    return packet;
}

static void set_input(const uint8_t *bytes, size_t length) {
    assert(length <= sizeof(input));
    if (length != 0) {
        memcpy(input, bytes, length);
    }
    input_length = length;
    input_position = 0;
    output_length = 0;
}

static void test_crc_and_encoding(void) {
    static const uint8_t vector[] = "123456789";
    uint8_t packet[12];
    mrx_packet_t empty = { .cmd = 1, .seq = 0, .length = 0 };
    assert(mrx_crc16_ccitt_false(vector, 9) == 0x29B1);
    assert(mrx_packet_encode(&empty, packet, sizeof(packet)) == sizeof(packet));
    assert(mrx_packet_encode(&empty, packet, sizeof(packet) - 1) == 0);
}

static void test_stream_parser(void) {
    const uint8_t first_data[] = { 'a', 'b' };
    const uint8_t second_data[] = { 'c', 'd', 'e' };
    uint8_t first[32], second[32], stream[96];
    size_t first_size = make_packet(1, 10, first_data, sizeof(first_data), first, sizeof(first));
    size_t second_size = make_packet(2, 11, second_data, sizeof(second_data), second, sizeof(second));
    memcpy(stream, "noiseMR", 7);
    memcpy(&stream[7], first, first_size);
    memcpy(&stream[7 + first_size], second, second_size);

    mrx_parser_t parser;
    mrx_packet_t packet = {0};
    size_t ready_count = 0;
    mrx_parser_init(&parser);
    feed_packet(&parser, stream, 7 + first_size + second_size, &packet, &ready_count);
    assert(ready_count == 2);
    assert(packet.cmd == 2 && packet.seq == 11 && packet.length == sizeof(second_data));
    assert(memcmp(packet.data, second_data, sizeof(second_data)) == 0);

    first[first_size - 1] ^= 1;
    memcpy(stream, first, first_size);
    memcpy(&stream[first_size], second, second_size);
    mrx_parser_init(&parser);
    ready_count = 0;
    feed_packet(&parser, stream, first_size + second_size, &packet, &ready_count);
    assert(ready_count == 1 && packet.cmd == 2 && packet.seq == 11);

    mrx_parser_init(&parser);
    ready_count = 0;
    feed_packet(&parser, second, 13, &packet, &ready_count);
    feed_packet(&parser, second, second_size, &packet, &ready_count);
    assert(ready_count == 1 && packet.cmd == 2 && packet.seq == 11);

    uint8_t oversized[MRX_HEADER_SIZE] = {
        MRX_MAGIC_0, MRX_MAGIC_1, MRX_MAGIC_2, MRX_MAGIC_3,
        1, 0, 0, 0, (uint8_t)(MRX_MAX_DATA_SIZE + 1),
        (uint8_t)((MRX_MAX_DATA_SIZE + 1) >> 8),
    };
    mrx_parser_init(&parser);
    mrx_parse_result_t result = MRX_PARSE_NONE;
    for (size_t i = 0; i < sizeof(oversized); ++i) {
        result = mrx_parser_feed(&parser, oversized[i], &packet);
    }
    assert(result == MRX_PARSE_BAD_LENGTH);
    ready_count = 0;
    feed_packet(&parser, second, second_size, &packet, &ready_count);
    assert(ready_count == 1 && packet.cmd == 2 && packet.seq == 11);
}

static void test_dispatch(void) {
    uint8_t request[32];
    size_t request_size = make_packet(0x55, 0x1234, NULL, 0, request, sizeof(request));
    mrx_protocol_init();
    set_input(request, request_size);
    mrx_protocol_task();
    mrx_packet_t response = parse_single(output, output_length);
    assert(response.cmd == (uint8_t)(0x55 | MRX_RESPONSE_BIT));
    assert(response.seq == 0x1234 && response.length == 1);
    assert(response.data[0] == MRX_STATUS_ERR_UNKNOWN);

    const uint8_t invalid_reboot_mode[] = { 2 };
    request_size = make_packet(MRX_CMD_REBOOT, 0x1235, invalid_reboot_mode,
                               sizeof(invalid_reboot_mode), request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.cmd == (uint8_t)(MRX_CMD_REBOOT | MRX_RESPONSE_BIT));
    assert(response.seq == 0x1235 && response.data[0] == MRX_STATUS_ERR_INVALID);
    assert(reboot_requested == -1);

    request_size = make_packet(MRX_CMD_REBOOT, 0x1237, NULL, 0, request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.seq == 0x1237 && response.data[0] == MRX_STATUS_ERR_INVALID);
    assert(reboot_requested == -1);

    const uint8_t bootloader_mode[] = { MRX_REBOOT_BOOTLOADER };
    request_size = make_packet(MRX_CMD_REBOOT, 0x1236, bootloader_mode,
                               sizeof(bootloader_mode), request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.cmd == (uint8_t)(MRX_CMD_REBOOT | MRX_RESPONSE_BIT));
    assert(response.seq == 0x1236 && response.length == 1);
    assert(response.data[0] == MRX_STATUS_OK);
    assert(reboot_requested == MRX_REBOOT_BOOTLOADER);

    reboot_requested = -1;
    const uint8_t normal_mode[] = { MRX_REBOOT_NORMAL };
    request_size = make_packet(MRX_CMD_REBOOT, 0x1238, normal_mode,
                               sizeof(normal_mode), request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.seq == 0x1238 && response.data[0] == MRX_STATUS_OK);
    assert(reboot_requested == MRX_REBOOT_NORMAL);

    request_size = make_packet(MRX_CMD_GET_INFO, 9, NULL, 0, request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.cmd == (uint8_t)(MRX_CMD_GET_INFO | MRX_RESPONSE_BIT));
    assert(response.seq == 9 && response.length == 86);
    assert(response.data[0] == MRX_STATUS_OK);
    assert(response.data[1] == MRX_PROTOCOL_VERSION);
    assert(response.data[2] == 0 && response.data[3] == 1 && response.data[4] == 0);
    assert(strcmp((const char *)&response.data[6], "MRX Loader") == 0);
    assert(strcmp((const char *)&response.data[38], "Feather RP2040 USB Host") == 0);
    assert(memcmp(&response.data[70], "\1\2\3\4\5\6\7\10", 8) == 0);
    for (size_t i = 78; i < 86; ++i) {
        assert(response.data[i] == 0);
    }

    const uint8_t unexpected[] = { 0xAA };
    request_size = make_packet(MRX_CMD_GET_INFO, 10, unexpected, sizeof(unexpected), request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.data[0] == MRX_STATUS_ERR_INVALID);

    request_size = make_packet(MRX_CMD_UPLOAD_BEGIN, 11, NULL, 0,
                               request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.cmd == (uint8_t)(MRX_CMD_UPLOAD_BEGIN | MRX_RESPONSE_BIT));
    assert(response.data[0] == MRX_STATUS_ERR_INVALID);

    uint8_t bad_upload_data[17] = {0};
    bad_upload_data[4] = 1;
    request_size = make_packet(MRX_CMD_UPLOAD_DATA, 12, bad_upload_data,
                               sizeof(bad_upload_data), request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.data[0] == MRX_STATUS_ERR_INVALID);

    list_all_payloads = true;
    cdc_write_limit = 64;
    request_size = make_packet(MRX_CMD_LIST_PAYLOADS, 13, NULL, 0,
                               request, sizeof(request));
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.cmd == (uint8_t)(MRX_CMD_LIST_PAYLOADS | MRX_RESPONSE_BIT));
    assert(response.data[0] == MRX_STATUS_OK);
    assert(response.length == MRX_MAX_DATA_SIZE - 1u);
    assert(response.data[1] == 0xFF && response.data[2] == 0x03);
    assert(response.data[3] == 1 && response.data[4] == 0);
    list_all_payloads = false;
    cdc_write_limit = 4096u;
}

static void test_cdc_chunking_and_reconnect(void) {
    uint8_t request[32];
    const size_t request_size = make_packet(MRX_CMD_GET_INFO, 0x3412, NULL, 0,
                                            request, sizeof(request));
    cdc_read_limit = 3;
    cdc_connected = true;
    mrx_protocol_init();

    set_input(request, request_size);
    mrx_protocol_task();
    mrx_packet_t response = parse_single(output, output_length);
    assert(response.cmd == (uint8_t)(MRX_CMD_GET_INFO | MRX_RESPONSE_BIT));
    assert(response.seq == 0x3412 && response.data[0] == MRX_STATUS_OK);


    set_input(request, 6);
    mrx_protocol_task();
    assert(output_length == 0);

    cdc_connected = false;
    set_input(NULL, 0);
    mrx_protocol_task();

    cdc_connected = true;
    set_input(request, request_size);
    mrx_protocol_task();
    response = parse_single(output, output_length);
    assert(response.cmd == (uint8_t)(MRX_CMD_GET_INFO | MRX_RESPONSE_BIT));
    assert(response.seq == 0x3412 && response.data[0] == MRX_STATUS_OK);

    cdc_read_limit = SIZE_MAX;
}

static void test_board_safe_startup(void) {
    mrx_board_init();
    assert(gpio_output[MRX_USB_HOST_VBUS_PIN]);
    assert(!gpio_level[MRX_USB_HOST_VBUS_PIN]);
    assert(gpio_output[MRX_NEOPIXEL_POWER_PIN]);
    assert(gpio_level[MRX_NEOPIXEL_POWER_PIN]);
    assert(gpio_output[MRX_NEOPIXEL_DATA_PIN]);
    assert(!gpio_level[MRX_NEOPIXEL_DATA_PIN]);
}

int main(void) {
    test_crc_and_encoding();
    test_stream_parser();
    test_dispatch();
    test_cdc_chunking_and_reconnect();
    test_board_safe_startup();
    return 0;
}
