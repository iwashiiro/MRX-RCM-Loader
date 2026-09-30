#!/usr/bin/env python3
"""Host-side conformance tests for MRX protocol v1 framing and CDC transport."""

from __future__ import annotations

import unittest
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
from probe_mrx_cdc import (
    encode_get_info,
    extract_packet,
    parse_get_info_response,
)
from mrx_gui.protocol import PacketParser, ProtocolError, crc16_ccitt_false as gui_crc16
from mrx_gui.protocol import encode_packet as gui_encode_packet
from mrx_gui.uf2 import (
    RP2040_FAMILY_ID,
    RP2040_FLASH_BASE,
    UF2_BLOCK_SIZE,
    Uf2Error,
    inspect_firmware,
)

MAGIC = b"MRX!"
HEADER_SIZE = 10
CRC_SIZE = 2
MAX_DATA = 4096
PROTOCOL_VERSION = 1
CMD_GET_INFO = 0x01
STATUS_OK = 0x00
STATUS_ERR_INVALID = 0x05
STATUS_ERR_UNKNOWN_CMD = 0x0C


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(cmd: int, seq: int, data: bytes = b"") -> bytes:
    if not 0 <= cmd <= 0xFF or not 0 <= seq <= 0xFFFF:
        raise ValueError("CMD and SEQ are out of range")
    if len(data) > MAX_DATA:
        raise ValueError("DATA exceeds MRX_MAX_DATA_SIZE")
    body = MAGIC + bytes((cmd, 0)) + seq.to_bytes(2, "little") + len(data).to_bytes(2, "little") + data
    return body + crc16_ccitt_false(body).to_bytes(2, "little")


def decode(packet: bytes) -> tuple[int, int, int, bytes]:
    if len(packet) < HEADER_SIZE + CRC_SIZE or packet[:4] != MAGIC:
        raise ValueError("invalid magic or truncated packet")
    length = int.from_bytes(packet[8:10], "little")
    if length > MAX_DATA:
        raise ValueError("oversized DATA")
    if len(packet) != HEADER_SIZE + length + CRC_SIZE:
        raise ValueError("truncated or trailing packet bytes")
    if int.from_bytes(packet[-2:], "little") != crc16_ccitt_false(packet[:-2]):
        raise ValueError("invalid CRC")
    return packet[4], int.from_bytes(packet[6:8], "little"), packet[5], packet[10:-2]


class StreamParser:
    """Small stream model matching the firmware parser's byte-at-a-time contract."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.in_frame = False

    def feed(self, byte: int) -> bytes | None:
        self.buffer.append(byte)
        if not self.in_frame:
            while len(self.buffer) >= 4 and self.buffer[:4] != MAGIC:
                del self.buffer[0]
            if len(self.buffer) < 4:
                return None
            self.in_frame = True
        if len(self.buffer) >= HEADER_SIZE:
            length = int.from_bytes(self.buffer[8:10], "little")
            if length > MAX_DATA:
                self._resync()
                return None
            total = HEADER_SIZE + length + CRC_SIZE
            if len(self.buffer) == total:
                candidate = bytes(self.buffer)
                try:
                    decode(candidate)
                except ValueError:
                    self._resync()
                    return None
                self.buffer.clear()
                self.in_frame = False
                return candidate
            if len(self.buffer) > total:
                self._resync()
        return None

    def _resync(self) -> None:
        index = self.buffer.find(MAGIC, 1)
        if index >= 0:
            del self.buffer[:index]
            self.in_frame = True
            return
        keep = 0
        for count in range(min(3, len(self.buffer)), 0, -1):
            if self.buffer[-count:] == MAGIC[:count]:
                keep = count
                break
        suffix = self.buffer[-keep:] if keep else b""
        self.buffer = bytearray(suffix)
        self.in_frame = False


def parse_stream(data: bytes) -> list[bytes]:
    parser = StreamParser()
    packets = []
    for byte in data:
        packet = parser.feed(byte)
        if packet is not None:
            packets.append(packet)
    return packets


def response_cmd(request_cmd: int) -> int:
    return request_cmd | 0x80


def dispatch_status(cmd: int, data: bytes) -> tuple[int, bytes]:
    if cmd == CMD_GET_INFO:
        if data:
            return STATUS_ERR_INVALID, b""
        return STATUS_OK, bytes((PROTOCOL_VERSION, 0, 1, 0, 0)) + b"MRX Loader" + bytes(22) + b"Feather RP2040 USB Host" + bytes(9) + bytes(16)
    return STATUS_ERR_UNKNOWN_CMD, b""


class ProtocolTests(unittest.TestCase):
    def test_crc_check_value_and_empty_request_vector(self) -> None:
        self.assertEqual(crc16_ccitt_false(b"123456789"), 0x29B1)
        self.assertEqual(encode(0x01, 0).hex(), "4d525821010000000000d197")

    def test_desktop_client_packet_matches_protocol_v1_vector(self) -> None:
        self.assertEqual(gui_crc16(b"123456789"), 0x29B1)
        self.assertEqual(gui_encode_packet(0x01, 0).hex(), "4d525821010000000000d197")

    def test_desktop_client_stream_parser_recovers_fragmented_and_corrupt_frames(self) -> None:
        parser = PacketParser()
        damaged = bytearray(gui_encode_packet(0x01, 7, b"bad"))
        damaged[-1] ^= 1
        good = gui_encode_packet(0x02, 8, b"status")
        self.assertEqual(parser.feed(b"noise" + bytes(damaged) + b"M" + good[:4]), [])
        packets = parser.feed(good[4:])
        self.assertEqual(len(packets), 1)
        self.assertEqual((packets[0].command, packets[0].sequence, packets[0].data), (0x02, 8, b"status"))

    def test_desktop_client_rejects_oversized_packet_data(self) -> None:
        with self.assertRaises(ProtocolError):
            gui_encode_packet(0x13, 1, bytes(MAX_DATA + 1))

    @staticmethod
    def _make_uf2(addresses: tuple[int, ...], family_id: int = RP2040_FAMILY_ID) -> bytes:
        blocks = []
        total = len(addresses)
        for block_no, address in enumerate(addresses):
            block = bytearray(UF2_BLOCK_SIZE)
            struct.pack_into(
                "<8I",
                block,
                0,
                0x0A324655,
                0x9E5D5157,
                0x2000,
                address,
                256,
                block_no,
                total,
                family_id,
            )
            block[32:288] = bytes([block_no + 1]) * 256
            struct.pack_into("<I", block, 508, 0x0AB16F30)
            blocks.append(bytes(block))
        return b"".join(blocks)

    def test_uf2_validation_accepts_rp2040_image_within_firmware_partition(self) -> None:
        image_data = self._make_uf2((RP2040_FLASH_BASE, RP2040_FLASH_BASE + 256))
        with tempfile.TemporaryDirectory(prefix="mrx-uf2-") as temp_dir:
            image_path = Path(temp_dir) / "mrx_loader.uf2"
            image_path.write_bytes(image_data)
            image = inspect_firmware(image_path)
        self.assertEqual(image.block_count, 2)
        self.assertEqual(image.size, 2 * UF2_BLOCK_SIZE)
        self.assertEqual(len(image.sha256), 64)

    def test_uf2_validation_rejects_wrong_family_out_of_range_and_overlap(self) -> None:
        invalid_images = (
            self._make_uf2((RP2040_FLASH_BASE,), family_id=0x12345678),
            self._make_uf2((RP2040_FLASH_BASE + 1024 * 1024 - 128,)),
            self._make_uf2((RP2040_FLASH_BASE, RP2040_FLASH_BASE)),
        )
        with tempfile.TemporaryDirectory(prefix="mrx-uf2-") as temp_dir:
            image_path = Path(temp_dir) / "invalid.uf2"
            for invalid_data in invalid_images:
                image_path.write_bytes(invalid_data)
                with self.assertRaises(Uf2Error):
                    inspect_firmware(image_path)

    def test_round_trip_sizes_flags_and_sequence(self) -> None:
        for data in (b"", bytes(range(256)), bytes((i % 251 for i in range(MAX_DATA)))):
            packet = encode(0x13, 0xABCD, data)
            cmd, seq, flags, decoded = decode(packet)
            self.assertEqual((cmd, seq, flags, decoded), (0x13, 0xABCD, 0, data))
        self.assertEqual(decode(encode(1, 0xFFFF))[:2], (1, 0xFFFF))

    def test_rejects_invalid_magic_crc_truncation_and_oversized_data(self) -> None:
        valid = encode(1, 0)
        with self.assertRaises(ValueError):
            decode(b"NOPE" + valid[4:])
        damaged = bytearray(valid)
        damaged[-1] ^= 0x80
        with self.assertRaises(ValueError):
            decode(damaged)
        with self.assertRaises(ValueError):
            decode(valid[:-1])
        oversized = bytearray(valid[:10])
        oversized[8:10] = (MAX_DATA + 1).to_bytes(2, "little")
        oversized.extend(bytes(CRC_SIZE))
        with self.assertRaises(ValueError):
            decode(oversized)

    def test_streaming_concatenated_packets_and_garbage_resynchronization(self) -> None:
        first, second = encode(1, 7, b"abc"), encode(2, 8, b"xyz123")
        self.assertEqual(parse_stream(b"garbage" + first + second), [first, second])

    def test_stream_resynchronizes_after_crc_error_and_partial_magic(self) -> None:
        bad = bytearray(encode(1, 1, b"bad"))
        bad[-1] ^= 1
        good = encode(2, 2, b"after error")
        self.assertEqual(parse_stream(bytes(bad) + b"M" + good), [good])

    def test_response_command_conversion_and_get_info_contract(self) -> None:
        status, body = dispatch_status(CMD_GET_INFO, b"")
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(body[0], PROTOCOL_VERSION)
        self.assertEqual(body[5:16], b"MRX Loader\0")
        self.assertEqual(len(body), 85)
        self.assertEqual(response_cmd(CMD_GET_INFO), 0x81)

    def test_cdc_probe_builds_and_parses_get_info_handshake(self) -> None:
        sequence = 0x4D52
        self.assertEqual(decode(encode_get_info(sequence))[:2], (CMD_GET_INFO, sequence))

        product = b"MRX Loader".ljust(32, b"\0")
        hardware = b"Feather RP2040 USB Host".ljust(32, b"\0")
        payload = bytes((STATUS_OK, PROTOCOL_VERSION, 0, 1, 0, 0)) + product + hardware
        payload += bytes.fromhex("0102030405060708") + bytes(8)
        response = encode(CMD_GET_INFO | 0x80, sequence, payload)
        parsed = parse_get_info_response(response, sequence)
        self.assertEqual(parsed["product"], "MRX Loader")
        self.assertEqual(parsed["hardware"], "Feather RP2040 USB Host")
        self.assertEqual(parsed["firmware_version"], "0.1.0")
        self.assertEqual(parsed["usb_serial"], "0102030405060708")

    def test_cdc_probe_handles_fragmented_and_corrupt_stream_data(self) -> None:
        response = encode(CMD_GET_INFO | 0x80, 7, bytes(86))
        buffer = bytearray(b"noise" + response[:5])
        self.assertIsNone(extract_packet(buffer))
        buffer.extend(response[5:])
        self.assertEqual(extract_packet(buffer), response)

        corrupt = bytearray(response)
        corrupt[-1] ^= 1
        buffer = bytearray(corrupt + response)
        self.assertEqual(extract_packet(buffer), response)

    def test_main_does_not_enable_host_vbus(self) -> None:
        root = Path(__file__).resolve().parents[1]
        main_source = (root / "firmware" / "src" / "main.c").read_text()
        self.assertNotIn("mrx_host_vbus_enable(", main_source)

    def test_unknown_commands_get_unknown_status(self) -> None:
        self.assertEqual(dispatch_status(0x55, b"")[0], STATUS_ERR_UNKNOWN_CMD)
        self.assertEqual(dispatch_status(0x30, b"")[0], STATUS_ERR_UNKNOWN_CMD)
        self.assertEqual(dispatch_status(0x01, b"unexpected")[0], STATUS_ERR_INVALID)
        self.assertEqual(response_cmd(0x55), 0xD5)

    def test_firmware_c_implementation(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("A host GCC or Clang compiler is needed for the firmware C harness")

        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="mrx-protocol-") as temp_dir:
            executable = Path(temp_dir) / ("mrx_protocol_test.exe" if os.name == "nt" else "mrx_protocol_test")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{root / 'firmware' / 'include'}",
                    f"-I{root / 'tools' / 'test_stubs'}",
                    str(root / "firmware" / "src" / "mrx_protocol.c"),
                    str(root / "tools" / "test_mrx_protocol_firmware.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_firmware_storage_implementation(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("A host GCC or Clang compiler is needed for the storage harness")

        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="mrx-storage-") as temp_dir:
            executable = Path(temp_dir) / ("mrx_storage_test.exe" if os.name == "nt" else "mrx_storage_test")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{root / 'firmware' / 'include'}",
                    f"-I{root / 'firmware' / 'src'}",
                    f"-I{root / 'firmware' / 'third_party' / 'littlefs'}",
                    f"-I{root / 'tools' / 'test_stubs'}",
                    str(root / "firmware" / "src" / "storage.c"),
                    str(root / "firmware" / "src" / "config.c"),
                    str(root / "firmware" / "src" / "payload_manager.c"),
                    str(root / "firmware" / "src" / "mrx_sha256.c"),
                    str(root / "firmware" / "third_party" / "littlefs" / "lfs.c"),
                    str(root / "firmware" / "third_party" / "littlefs" / "lfs_util.c"),
                    str(root / "tools" / "test_mrx_storage.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_cmake_generates_a_one_mib_linker_flash_region(self) -> None:
        cmake = shutil.which("cmake")
        if cmake is None:
            self.skipTest("CMake is needed to check the firmware linker configuration")

        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="mrx-cmake-") as temp_dir:
            temp = Path(temp_dir)
            sdk = temp / "pico-sdk"
            template = sdk / "src" / "rp2_common" / "pico_standard_link" / "pico_flash_region.template.ld"
            template.parent.mkdir(parents=True)
            template.write_text("FLASH(rx) : ORIGIN = 0x10000000, LENGTH = ${PICO_FLASH_SIZE_BYTES_STRING}\n")
            (sdk / "pico_sdk_init.cmake").write_text(
                """function(pico_sdk_init)
  foreach(target pico_stdlib pico_unique_id tinyusb_device tinyusb_board)
    add_library(${target} INTERFACE)
  endforeach()
endfunction()
function(pico_add_linker_script_override_path target path)
  file(APPEND \"${CMAKE_BINARY_DIR}/linker_override_paths.txt\" \"${path}\\n\")
endfunction()
function(pico_enable_stdio_usb target enabled)
endfunction()
function(pico_enable_stdio_uart target enabled)
endfunction()
function(pico_add_extra_outputs target)
endfunction()
function(pico_generate_pio_header target source)
endfunction()
function(pico_set_program_name target value)
endfunction()
function(pico_set_program_version target value)
endfunction()
function(pico_set_program_description target value)
endfunction()
"""
            )
            build_dir = temp / "build"
            subprocess.run(
                [cmake, "-S", str(root / "firmware"), "-B", str(build_dir),
                 f"-DPICO_SDK_PATH={sdk}", "-DPICO_BOARD=adafruit_feather_rp2040_usb_host"],
                check=True,
                capture_output=True,
                text=True,
            )

            generated = build_dir / "generated" / "mrx_loader_flash_region" / "pico_flash_region.ld"
            self.assertEqual(
                generated.read_text().strip(),
                "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 1 * 1024 * 1024",
            )
            override_log = (build_dir / "linker_override_paths.txt").read_text()
            self.assertIn(generated.parent.as_posix(), override_log.replace("\\", "/"))

            gcc = shutil.which("gcc")
            if gcc is not None:
                linker_script = temp / "flash_limit.ld"
                linker_script.write_text(
                    "MEMORY {\n" + generated.read_text() + "}\n"
                    "SECTIONS { .text : { *(.text) } > FLASH }\n"
                )
                oversized_assembly = temp / "oversized.s"
                oversized_assembly.write_text(
                    ".section .text\n.global _start\n_start:\n.space 1048577\n"
                )
                oversized_object = temp / "oversized.o"
                subprocess.run(
                    [gcc, "-c", "-x", "assembler", str(oversized_assembly), "-o", str(oversized_object)],
                    check=True,
                    capture_output=True,
                    text=True,
                )
                oversized_link = subprocess.run(
                    [gcc, "-nostdlib", f"-Wl,-T,{linker_script}", "-Wl,--build-id=none",
                     str(oversized_object), "-o", str(temp / "oversized.elf")],
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(oversized_link.returncode, 0, "The linker accepted a >1 MiB firmware image")
                diagnostics = oversized_link.stderr.lower()
                self.assertTrue("overflow" in diagnostics or "will not fit in region" in diagnostics)


if __name__ == "__main__":
    unittest.main(verbosity=2)
