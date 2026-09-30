"""Synchronous MRX device client; call it only from the serial worker thread."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct
import time
from typing import Callable

import serial

from .protocol import Packet, PacketParser, ProtocolError, encode_packet

CMD_GET_INFO = 0x01
CMD_GET_STATUS = 0x02
CMD_LIST_PAYLOADS = 0x10
CMD_GET_PAYLOAD_INFO = 0x11
CMD_UPLOAD_BEGIN = 0x12
CMD_UPLOAD_DATA = 0x13
CMD_UPLOAD_END = 0x14
CMD_UPLOAD_ABORT = 0x15
CMD_DELETE_PAYLOAD = 0x16
CMD_SELECT_PAYLOAD = 0x17
CMD_GET_SELECTED = 0x18
CMD_VERIFY_PAYLOAD = 0x19
CMD_REBOOT = 0x20
REBOOT_NORMAL = 0x00
REBOOT_BOOTLOADER = 0x01

STATUS_NAMES = {
    0x00: "OK",
    0x01: "General error",
    0x02: "Not found",
    0x03: "Corrupt data",
    0x04: "Storage full",
    0x05: "Invalid request",
    0x06: "Device busy",
    0x07: "No payload selected",
    0x08: "Unsupported version",
    0x09: "Storage I/O error",
    0x0A: "Incomplete payload",
    0x0B: "Operation aborted",
    0x0C: "Unknown command",
}
INVALID_PAYLOAD_NAME_CHARS = set('\\/:*?"<>|')

DEVICE_STATES = {
    0: "Starting",
    1: "Connected to PC",
    2: "Standalone",
    3: "Starting payload",
    4: "Device error",
}


class DeviceError(Exception):
    pass


class DeviceStatusError(DeviceError):
    def __init__(self, command: int, status: int) -> None:
        self.command = command
        self.status = status
        super().__init__(f"{STATUS_NAMES.get(status, f'Status 0x{status:02X}')} (0x{status:02X})")


@dataclass(frozen=True)
class DeviceInfo:
    protocol_version: int
    firmware_version: str
    product: str
    hardware: str
    device_id: str


class MrxDevice:
    def __init__(self, port: serial.Serial) -> None:
        self.port = port
        self.parser = PacketParser()
        self.sequence = 0

    def close(self) -> None:
        if self.port.is_open:
            self.port.close()

    def request(self, command: int, data: bytes = b"", timeout: float = 5.0) -> bytes:
        self.sequence = (self.sequence + 1) & 0xFFFF
        sequence = self.sequence
        self.port.write(encode_packet(command, sequence, data))
        self.port.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            waiting = min(max(self.port.in_waiting, 1), 1024)
            chunk = self.port.read(waiting)
            if not chunk:
                continue
            for packet in self.parser.feed(chunk):
                if packet.sequence != sequence or packet.command != (command | 0x80):
                    continue
                if not packet.data:
                    raise ProtocolError("Response is missing its status byte")
                if packet.data[0] != 0:
                    raise DeviceStatusError(command, packet.data[0])
                return packet.data
        raise TimeoutError(f"No response to command 0x{command:02X} within {timeout:g} s")

    @staticmethod
    def _text(field: bytes) -> str:
        return field.split(b"\0", 1)[0].decode("utf-8", errors="replace")

    def get_info(self, timeout: float = 0.5) -> DeviceInfo:
        response = self.request(CMD_GET_INFO, timeout=timeout)
        if len(response) < 86:
            raise ProtocolError(f"GET_INFO response is only {len(response)} bytes")
        return DeviceInfo(
            protocol_version=response[1],
            firmware_version=f"{response[2]}.{response[3]}.{response[4]}",
            product=self._text(response[6:38]),
            hardware=self._text(response[38:70]),
            device_id=response[70:86].hex(" ").upper(),
        )

    def get_status(self) -> dict:
        response = self.request(CMD_GET_STATUS)
        if len(response) < 28:
            raise ProtocolError("GET_STATUS response has an invalid length")
        return {
            "state": DEVICE_STATES.get(response[1], f"Unknown state 0x{response[1]:02X}"),
            "selected_id": struct.unpack_from("<I", response, 4)[0],
            "payload_count": struct.unpack_from("<I", response, 8)[0],
            "free_bytes": struct.unpack_from("<Q", response, 12)[0],
            "total_bytes": struct.unpack_from("<Q", response, 20)[0],
        }

    def list_payloads(self) -> list[dict]:
        response = self.request(CMD_LIST_PAYLOADS)
        if len(response) < 3:
            raise ProtocolError("LIST_PAYLOADS response has an invalid length")
        count = struct.unpack_from("<H", response, 1)[0]
        if len(response) != 3 + count * 4:
            raise ProtocolError("LIST_PAYLOADS returned a mismatched item count")
        ids = struct.unpack_from(f"<{count}I", response, 3) if count else ()
        return [self.get_payload_info(payload_id) for payload_id in ids]

    def get_payload_info(self, payload_id: int) -> dict:
        response = self.request(CMD_GET_PAYLOAD_INFO, struct.pack("<I", payload_id))
        if len(response) < 113:
            raise ProtocolError("GET_PAYLOAD_INFO response has an invalid length")
        return {
            "id": struct.unpack_from("<I", response, 1)[0],
            "flags": struct.unpack_from("<I", response, 5)[0],
            "size": struct.unpack_from("<Q", response, 9)[0],
            "sha256": response[17:49].hex(),
            "name": self._text(response[49:113]),
        }

    def verify_payload(self, payload_id: int, timeout: float = 120.0) -> dict:
        response = self.request(CMD_VERIFY_PAYLOAD, struct.pack("<I", payload_id), timeout)
        if len(response) < 66:
            raise ProtocolError("VERIFY_PAYLOAD response has an invalid length")
        return {
            "matches": response[1] == 1,
            "stored_sha256": response[2:34].hex(),
            "actual_sha256": response[34:66].hex(),
        }

    def select_payload(self, payload_id: int) -> None:
        self.request(CMD_SELECT_PAYLOAD, struct.pack("<I", payload_id))
        response = self.request(CMD_GET_SELECTED)
        if len(response) < 5 or struct.unpack_from("<I", response, 1)[0] != payload_id:
            raise ProtocolError("The device did not confirm the saved payload selection")

    def delete_payload(self, payload_id: int) -> bool:
        response = self.request(CMD_DELETE_PAYLOAD, struct.pack("<I", payload_id))
        return len(response) > 1 and response[1] != 0

    def reboot_to_bootloader(self) -> None:
        response = self.request(CMD_REBOOT, bytes((REBOOT_BOOTLOADER,)), timeout=2.0)
        if len(response) != 1:
            raise ProtocolError("CMD_REBOOT returned an invalid response")

    def upload_payload(
        self,
        path: Path,
        name: str,
        select_after_upload: bool,
        progress: Callable[[int, int, str], None],
        cancelled: Callable[[], bool],
    ) -> dict:
        size = path.stat().st_size
        if size <= 0:
            raise DeviceError("The selected file is empty")
        encoded_name = name.encode("ascii", errors="strict")
        if (
            not encoded_name
            or len(encoded_name) > 63
            or any(byte < 0x20 or byte > 0x7E for byte in encoded_name)
            or any(character in INVALID_PAYLOAD_NAME_CHARS for character in name)
        ):
            raise DeviceError("Payload name must be 1-63 printable ASCII characters without reserved filename characters")

        digest = hashlib.sha256()
        hashed = 0
        with path.open("rb") as payload_file:
            while True:
                chunk = payload_file.read(64 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
                hashed += len(chunk)
                progress(hashed, size, "Hashing payload")
        expected_hash = digest.digest()

        begin_data = struct.pack("<QB", size, len(encoded_name)) + encoded_name
        begin_response = self.request(CMD_UPLOAD_BEGIN, begin_data)
        if len(begin_response) < 5:
            raise ProtocolError("UPLOAD_BEGIN response has an invalid length")
        payload_id = struct.unpack_from("<I", begin_response, 1)[0]

        offset = 0
        try:
            with path.open("rb") as payload_file:
                while True:
                    if cancelled():
                        self.request(CMD_UPLOAD_ABORT, struct.pack("<I", payload_id))
                        raise InterruptedError("Upload cancelled")
                    chunk = payload_file.read(4080)
                    if not chunk:
                        break
                    request_data = struct.pack("<IIQ", payload_id, 0, offset) + chunk
                    response = self.request(CMD_UPLOAD_DATA, request_data, timeout=10.0)
                    if len(response) < 9 or struct.unpack_from("<Q", response, 1)[0] != offset + len(chunk):
                        raise ProtocolError("The Feather reported an unexpected upload offset")
                    offset += len(chunk)
                    progress(offset, size, "Uploading payload")
                if cancelled():
                    self.request(CMD_UPLOAD_ABORT, struct.pack("<I", payload_id))
                    raise InterruptedError("Upload cancelled")

                end_data = struct.pack("<IQ", payload_id, size) + expected_hash
                end_response = self.request(CMD_UPLOAD_END, end_data, timeout=60.0)
        except DeviceStatusError as error:
            if error.command in (CMD_UPLOAD_DATA, CMD_UPLOAD_END):
                raise
            try:
                self.request(CMD_UPLOAD_ABORT, struct.pack("<I", payload_id))
            except Exception:
                pass
            raise
        except InterruptedError:
            raise
        except Exception:
            try:
                self.request(CMD_UPLOAD_ABORT, struct.pack("<I", payload_id))
            except Exception:
                pass
            raise

        if len(end_response) < 45:
            raise ProtocolError("UPLOAD_END response has an invalid length")
        returned_id = struct.unpack_from("<I", end_response, 1)[0]
        actual_size = struct.unpack_from("<Q", end_response, 5)[0]
        actual_hash = end_response[13:45]
        if returned_id != payload_id or actual_size != size or actual_hash != expected_hash:
            raise ProtocolError("The committed payload does not match the source file")

        progress(0, 0, "Verifying payload in flash")
        verification = self.verify_payload(payload_id)
        if not verification["matches"] or verification["actual_sha256"] != expected_hash.hex():
            raise DeviceError("Flash verification failed; the device reports a hash mismatch")
        progress(size, size, "Verification complete")

        if select_after_upload:
            self.select_payload(payload_id)
        return {"id": payload_id, "size": size, "sha256": expected_hash.hex(), "selected": select_after_upload}
