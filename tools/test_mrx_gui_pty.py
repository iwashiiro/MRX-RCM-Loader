#!/usr/bin/env python3
"""PTY-backed checks that MRX Loader device discovery works on Linux hosts.

These tests exercise the real ``DeviceWorker`` discovery path against a fake MRX
device served over a pseudo-terminal. They are the closest available stand-in
for a physical RP2040 and require no hardware.
"""

from __future__ import annotations

import os
import struct
import sys
import threading
import unittest
from pathlib import Path

try:
    import tty
except ImportError:
    tty = None

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mrx_gui.protocol import PacketParser, encode_packet
from mrx_gui.worker import DeviceWorker
from serial import Serial as _Serial

HAS_PTY = hasattr(os, "openpty") and tty is not None

CMD_GET_INFO = 0x01
CMD_GET_STATUS = 0x02
CMD_LIST_PAYLOADS = 0x10
CMD_GET_SELECTED = 0x18

PRODUCT = b"MRX Loader"
HARDWARE = b"Feather RP2040 USB Host"
DEVICE_ID = bytes(range(16))


def build_get_info_body() -> bytes:
    body = bytearray(86)
    body[0] = 0
    body[1] = 1
    body[2:5] = bytes((0, 1, 0))
    body[6:6 + len(PRODUCT)] = PRODUCT
    body[38:38 + len(HARDWARE)] = HARDWARE
    body[70:86] = DEVICE_ID
    return bytes(body)


class FakeMrxDevice:
    """Answers the MRX handshake on one end of a pseudo-terminal."""

    def __init__(self, file_descriptor: int, product: bytes = PRODUCT) -> None:
        self._fd = file_descriptor
        self._product = product
        self._parser = PacketParser()
        self._thread: threading.Thread | None = None
        self._running = True

    @property
    def _info_body(self) -> bytes:
        body = bytearray(build_get_info_body())
        body[6:6 + len(self._product)] = self._product
        body[6 + len(self._product)] = 0
        return bytes(body)

    def _reply(self, command: int, sequence: int, data: bytes) -> None:
        os.write(self._fd, encode_packet(command | 0x80, sequence, data))

    def _serve(self) -> None:
        while self._running:
            try:
                chunk = os.read(self._fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            for packet in self._parser.feed(chunk):
                if packet.command == CMD_GET_INFO:
                    self._reply(CMD_GET_INFO, packet.sequence, self._info_body)
                elif packet.command == CMD_GET_STATUS:
                    status = bytearray(28)
                    status[1] = 1
                    struct.pack_into("<Q", status, 12, 0)
                    struct.pack_into("<Q", status, 20, 7 * 1024 * 1024)
                    self._reply(CMD_GET_STATUS, packet.sequence, bytes(status))
                elif packet.command == CMD_LIST_PAYLOADS:
                    self._reply(CMD_LIST_PAYLOADS, packet.sequence, bytes((0, 0, 0)))
                elif packet.command == CMD_GET_SELECTED:
                    selected = bytearray(5)
                    struct.pack_into("<I", selected, 1, 0)
                    self._reply(CMD_GET_SELECTED, packet.sequence, bytes(selected))
                else:
                    self._reply(packet.command, packet.sequence, bytes((0x0C,)))

    def start(self) -> None:
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        try:
            os.close(self._fd)
        except OSError:
            pass
        if self._thread is not None:
            self._thread.join(timeout=2.0)


class FakePort:
    def __init__(self, device: str, vid: int, pid: int) -> None:
        self.device = device
        self.vid = vid
        self.pid = pid
        self.serial_number = None
        self.manufacturer = None
        self.product = None
        self.description = None
        self.hwid = None


class PtySerial:
    """A serial port that tolerates the modem-control lines a PTY cannot set."""

    def __init__(self, *arguments, **keywords) -> None:
        self._serial = _Serial(*arguments, **keywords)

    def __getattr__(self, name):
        return getattr(self._serial, name)

    @property
    def dtr(self):
        return self._serial.dtr

    @dtr.setter
    def dtr(self, value) -> None:
        try:
            self._serial.dtr = value
        except OSError:
            pass


@unittest.skipUnless(HAS_PTY, "pseudo-terminals are not available on this platform")
class PtyDiscoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.master, self.slave = os.openpty()
        for file_descriptor in (self.master, self.slave):
            try:
                tty.setraw(file_descriptor)
            except Exception:
                pass
        self.slave_name = os.ttyname(self.slave)
        self.device = FakeMrxDevice(self.master)
        self.device.start()
        self._original_comports = None
        self._original_serial = None
        import mrx_gui.worker as worker_module

        self.worker_module = worker_module
        self._original_serial = worker_module.Serial
        worker_module.Serial = PtySerial

    def tearDown(self) -> None:
        if self._original_serial is not None:
            self.worker_module.Serial = self._original_serial
        self.device.stop()
        try:
            os.close(self.slave)
        except OSError:
            pass

    def _patch_ports(self, ports):
        module = self.worker_module
        self._original_comports = module.list_ports.comports
        module.list_ports.comports = lambda: list(ports)

    def _restore_ports(self):
        if self._original_comports is not None:
            self.worker_module.list_ports.comports = self._original_comports
            self._original_comports = None

    def test_discovers_and_identifies_device_over_pty(self) -> None:
        self._patch_ports([FakePort(self.slave_name, 0x1209, 0x0001)])
        try:
            worker = DeviceWorker()
            seen = []
            worker.connected.connect(lambda *args: seen.append(args))
            self.assertTrue(worker._try_connect())
            worker.request_stop()
        finally:
            self._restore_ports()

        self.assertEqual(len(seen), 1)
        info, port_name, compatible = seen[0]
        self.assertEqual(port_name, self.slave_name)
        self.assertTrue(compatible)
        self.assertEqual(info.product, "MRX Loader")
        self.assertEqual(info.hardware, "Feather RP2040 USB Host")
        self.assertEqual(info.protocol_version, 1)
        self.assertEqual(info.device_id, DEVICE_ID.hex(" ").upper())

    def test_rejects_foreign_device_on_pty(self) -> None:
        self.device.stop()
        self.master, self.slave = os.openpty()
        for file_descriptor in (self.master, self.slave):
            try:
                tty.setraw(file_descriptor)
            except Exception:
                pass
        self.slave_name = os.ttyname(self.slave)
        self.device = FakeMrxDevice(self.master, product=b"Some Other Device")
        self.device.start()
        self._patch_ports([FakePort(self.slave_name, 0x1209, 0x0001)])
        try:
            worker = DeviceWorker()
            seen = []
            worker.connected.connect(lambda *args: seen.append(args))
            self.assertFalse(worker._try_connect())
            worker.request_stop()
        finally:
            self._restore_ports()
        self.assertEqual(seen, [])

    def test_preferred_usb_id_is_tried_first(self) -> None:
        preferred = FakePort("/dev/ttyFAKE1", 0x1209, 0x0001)
        other = FakePort("/dev/ttyFAKE2", 0x1234, 0x5678)
        self._patch_ports([other, preferred])
        try:
            worker = DeviceWorker()
            self.assertEqual(
                worker._port_candidates(),
                ["/dev/ttyFAKE1", "/dev/ttyFAKE2"],
            )
        finally:
            self._restore_ports()


if __name__ == "__main__":
    unittest.main(verbosity=2)
