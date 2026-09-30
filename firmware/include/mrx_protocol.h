#ifndef MRX_PROTOCOL_H
#define MRX_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MRX_MAGIC_0              0x4D
#define MRX_MAGIC_1              0x52
#define MRX_MAGIC_2              0x58
#define MRX_MAGIC_3              0x21
#define MRX_PROTOCOL_VERSION     1U
#define MRX_MAX_DATA_SIZE        4096U
#define MRX_MAX_PACKET_SIZE      (10U + MRX_MAX_DATA_SIZE + 2U)
#define MRX_HEADER_SIZE          10U
#define MRX_CRC_SIZE             2U
#define MRX_RESPONSE_BIT         0x80U

#define MRX_CMD_GET_INFO         0x01U
#define MRX_CMD_GET_STATUS       0x02U
#define MRX_CMD_GET_LOG          0x03U
#define MRX_CMD_LIST_PAYLOADS    0x10U
#define MRX_CMD_GET_PAYLOAD_INFO 0x11U
#define MRX_CMD_UPLOAD_BEGIN     0x12U
#define MRX_CMD_UPLOAD_DATA      0x13U
#define MRX_CMD_UPLOAD_END       0x14U
#define MRX_CMD_UPLOAD_ABORT     0x15U
#define MRX_CMD_DELETE_PAYLOAD   0x16U
#define MRX_CMD_SELECT_PAYLOAD   0x17U
#define MRX_CMD_GET_SELECTED     0x18U
#define MRX_CMD_VERIFY_PAYLOAD   0x19U
#define MRX_CMD_REBOOT           0x20U
#define MRX_CMD_FW_UPDATE_BEGIN  0x30U
#define MRX_CMD_FW_UPDATE_DATA   0x31U
#define MRX_CMD_FW_UPDATE_END    0x32U

#define MRX_STATUS_OK             0x00U
#define MRX_STATUS_ERR_GENERAL    0x01U
#define MRX_STATUS_ERR_NOT_FOUND  0x02U
#define MRX_STATUS_ERR_CORRUPT    0x03U
#define MRX_STATUS_ERR_FULL       0x04U
#define MRX_STATUS_ERR_INVALID    0x05U
#define MRX_STATUS_ERR_BUSY       0x06U
#define MRX_STATUS_ERR_NO_SEL     0x07U
#define MRX_STATUS_ERR_VERSION    0x08U
#define MRX_STATUS_ERR_IO         0x09U
#define MRX_STATUS_ERR_INCOMPLETE 0x0AU
#define MRX_STATUS_ERR_ABORTED    0x0BU
#define MRX_STATUS_ERR_UNKNOWN    0x0CU

#define MRX_STATE_BOOTING         0x00U
#define MRX_STATE_PC_MODE        0x01U
#define MRX_STATE_STANDALONE     0x02U
#define MRX_STATE_INJECTING      0x03U
#define MRX_STATE_ERROR          0x04U

#define MRX_REBOOT_NORMAL        0x00U
#define MRX_REBOOT_BOOTLOADER    0x01U

#define MRX_PRODUCT_STRING       "MRX Loader"
#define MRX_HW_STRING            "Feather RP2040 USB Host"


typedef enum {
    MRX_PARSE_NONE = 0,
    MRX_PARSE_PACKET_READY,
    MRX_PARSE_BAD_CRC,
    MRX_PARSE_BAD_LENGTH,
} mrx_parse_result_t;

typedef struct {
    uint8_t cmd;
    uint8_t flags;
    uint16_t seq;
    uint16_t length;
    uint8_t data[MRX_MAX_DATA_SIZE];
} mrx_packet_t;

typedef struct {
    uint8_t buffer[MRX_MAX_PACKET_SIZE];
    size_t used;
    size_t expected_total;
    bool in_frame;
} mrx_parser_t;

uint16_t mrx_crc16_ccitt_false(const uint8_t *data, size_t length);
void mrx_parser_init(mrx_parser_t *parser);
mrx_parse_result_t mrx_parser_feed(mrx_parser_t *parser, uint8_t byte, mrx_packet_t *packet);
size_t mrx_packet_encode(const mrx_packet_t *packet, uint8_t *out, size_t out_size);

bool mrx_send_response(uint8_t request_cmd, uint16_t seq, uint8_t status,
                       const uint8_t *payload, uint16_t payload_len);
void mrx_protocol_init(void);
void mrx_protocol_task(void);
void mrx_protocol_set_device_state(uint8_t state);

#endif
