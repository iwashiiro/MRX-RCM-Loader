#!/usr/bin/env python3
"""Probe the MRX Loader USB CDC connection with the GET_INFO handshake."""

from __future__ import annotations

import argparse
import time
from typing import Any

MAGIC = b"MRX!"
HEADER_SIZE = 10
CRC_SIZE = 2
MAX_DATA_SIZE = 4096
CMD_GET_INFO = 0x01
CMD_GET_INFO_RESPONSE = CMD_GET_INFO | 0x80
STATUS_OK = 0x00
EXPECTED_VID = 0x1209
EXPECTED_PID = 0x0001


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_get_info(sequence: int) -> bytes:
    header = MAGIC + bytes((CMD_GET_INFO, 0)) + sequence.to_bytes(2, "little") + b"\0\0"
    return header + crc16_ccitt_false(header).to_bytes(2, "little")


def extract_packet(buffer: bytearray) -> bytes | None:
    """Remove noise and return the first complete CRC-valid MRX packet."""
    while True:
        start = buffer.find(MAGIC)
        if start < 0:
            keep = 0
            for size in range(min(len(buffer), len(MAGIC) - 1), 0, -1):
                if buffer[-size:] == MAGIC[:size]:
                    keep = size
                    break
            if keep:
                del buffer[:-keep]
            else:
                buffer.clear()
            return None
        if start:
            del buffer[:start]
        if len(buffer) < HEADER_SIZE:
            return None

        payload_length = int.from_bytes(buffer[8:10], "little")
        if payload_length > MAX_DATA_SIZE:
            del buffer[0]
            continue
        packet_length = HEADER_SIZE + payload_length + CRC_SIZE
        if len(buffer) < packet_length:
            return None

        candidate = bytes(buffer[:packet_length])
        expected_crc = int.from_bytes(candidate[-CRC_SIZE:], "little")
        if crc16_ccitt_false(candidate[:-CRC_SIZE]) == expected_crc:
            del buffer[:packet_length]
            return candidate
        del buffer[0]


def parse_get_info_response(packet: bytes, sequence: int) -> dict[str, Any]:
    if len(packet) < HEADER_SIZE + CRC_SIZE or packet[:4] != MAGIC:
        raise ValueError("Response has invalid MRX framing")
    if packet[4] != CMD_GET_INFO_RESPONSE:
        raise ValueError(f"Unexpected response command 0x{packet[4]:02X}")
    if packet[5] != 0:
        raise ValueError(f"Unexpected response flags 0x{packet[5]:02X}")
    if int.from_bytes(packet[6:8], "little") != sequence:
        raise ValueError("Response sequence does not match the request")

    data_length = int.from_bytes(packet[8:10], "little")
    if data_length != 86 or len(packet) != HEADER_SIZE + data_length + CRC_SIZE:
        raise ValueError(f"GET_INFO returned {data_length} data bytes; expected 86")
    if int.from_bytes(packet[-CRC_SIZE:], "little") != crc16_ccitt_false(packet[:-CRC_SIZE]):
        raise ValueError("Response CRC does not match")
    data = packet[HEADER_SIZE:-CRC_SIZE]
    if data[0] != STATUS_OK:
        raise ValueError(f"GET_INFO failed with status 0x{data[0]:02X}")

    product = data[6:38].split(b"\0", 1)[0].decode("ascii", errors="strict")
    hardware = data[38:70].split(b"\0", 1)[0].decode("ascii", errors="strict")
    device_id = data[70:86]
    if data[1] != 1:
        raise ValueError(f"Unsupported MRX protocol version {data[1]}")
    if product != "MRX Loader":
        raise ValueError(f"Unexpected product string {product!r}")
    if hardware != "Feather RP2040 USB Host":
        raise ValueError(f"Unexpected hardware string {hardware!r}")
    if any(device_id[8:]):
        raise ValueError("RP2040 device_id padding is not zero")

    return {
        "firmware_version": f"{data[2]}.{data[3]}.{data[4]}",
        "product": product,
        "hardware": hardware,
        "device_id": device_id.hex().upper(),
        "usb_serial": device_id[:8].hex().upper(),
    }


def probe_port(port: Any, serial_module: Any, timeout: float) -> dict[str, Any]:
    sequence = 0x4D52
    deadline = time.monotonic() + timeout
    response_buffer = bytearray()

    with serial_module.Serial(
        port.device,
        baudrate=115200,
        timeout=0.02,
        write_timeout=0.2,
        dsrdtr=False,
        rtscts=False,
    ) as connection:
        connection.dtr = True
        connection.reset_input_buffer()
        connection.write(encode_get_info(sequence))
        connection.flush()

        while time.monotonic() < deadline:
            count = min(max(connection.in_waiting, 1), 64)
            response_buffer.extend(connection.read(count))
            response = extract_packet(response_buffer)
            if response is not None:
                info = parse_get_info_response(response, sequence)
                if port.serial_number and port.serial_number.upper() != info["usb_serial"]:
                    raise ValueError("USB serial descriptor does not match the RP2040 unique ID")
                return info

    raise TimeoutError(f"No valid GET_INFO response within {timeout:.3f}s")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=0.5,
                        help="maximum response wait per port, in seconds (default: 0.5)")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        parser.error("pyserial is required; install it with: python -m pip install pyserial")

    ports = list(list_ports.comports())
    matching = [port for port in ports
                if port.vid == EXPECTED_VID and port.pid == EXPECTED_PID]
    candidates = matching or ports
    if not candidates:
        print("No serial ports found. Connect the Feather over USB-C and try again.")
        return 1

    for port in candidates:
        print(f"Checking {port.device} ({port.description}) ...")
        try:
            info = probe_port(port, serial, args.timeout)
        except Exception as error:
            print(f"  No MRX Loader handshake: {error}")
            continue

        if port.vid is not None and port.pid is not None:
            print(f"  USB ID: {port.vid:04X}:{port.pid:04X}")
        print(f"  Product: {info['product']}")
        print(f"  Hardware: {info['hardware']}")
        print(f"  Firmware: {info['firmware_version']}")
        print(f"  Device ID: {info['device_id']}")
        print("CDC GET_INFO handshake passed.")
        return 0

    print("No serial port completed the MRX Loader GET_INFO handshake.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
