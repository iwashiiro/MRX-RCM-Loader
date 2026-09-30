"""MRX protocol v1 framing and CRC helpers."""

from __future__ import annotations

from dataclasses import dataclass
import struct

MAGIC = b"MRX!"
PROTOCOL_VERSION = 1
MAX_DATA_SIZE = 4096
HEADER = struct.Struct("<4sBBHH")
CRC = struct.Struct("<H")


class ProtocolError(Exception):
    """A malformed or unexpected MRX packet was received."""


@dataclass(frozen=True)
class Packet:
    command: int
    flags: int
    sequence: int
    data: bytes


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_packet(command: int, sequence: int, data: bytes = b"") -> bytes:
    if len(data) > MAX_DATA_SIZE:
        raise ProtocolError(f"Packet data exceeds {MAX_DATA_SIZE} bytes")
    header = HEADER.pack(MAGIC, command, 0, sequence & 0xFFFF, len(data))
    body = header + data
    return body + CRC.pack(crc16_ccitt_false(body))


class PacketParser:
    """Incrementally scans a CDC byte stream and emits valid frames."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def reset(self) -> None:
        self._buffer.clear()

    def feed(self, chunk: bytes) -> list[Packet]:
        self._buffer.extend(chunk)
        packets: list[Packet] = []
        while True:
            start = self._buffer.find(MAGIC)
            if start < 0:
                keep = min(len(self._buffer), len(MAGIC) - 1)
                if keep:
                    del self._buffer[:-keep]
                else:
                    self._buffer.clear()
                break
            if start:
                del self._buffer[:start]
            if len(self._buffer) < HEADER.size:
                break

            _, command, flags, sequence, length = HEADER.unpack_from(self._buffer)
            if length > MAX_DATA_SIZE:
                del self._buffer[0]
                continue
            frame_size = HEADER.size + length + CRC.size
            if len(self._buffer) < frame_size:
                break

            body = bytes(self._buffer[: HEADER.size + length])
            expected_crc = CRC.unpack_from(self._buffer, HEADER.size + length)[0]
            if crc16_ccitt_false(body) != expected_crc:
                del self._buffer[0]
                continue
            packets.append(Packet(command, flags, sequence, body[HEADER.size:]))
            del self._buffer[:frame_size]
        return packets
