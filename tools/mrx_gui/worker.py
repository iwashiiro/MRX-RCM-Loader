"""Background serial discovery, polling, and payload operations."""

from __future__ import annotations

from datetime import datetime
from pathlib import Path
import queue
import threading
import time

from PySide6.QtCore import QThread, Signal
from serial import Serial, SerialException
from serial.tools import list_ports

from .device import DeviceError, MrxDevice
from .protocol import PROTOCOL_VERSION
from .uf2 import inspect_firmware


class DeviceWorker(QThread):
    connected = Signal(object, str, bool)
    disconnected = Signal()
    status_updated = Signal(dict)
    payloads_updated = Signal(list)
    progress = Signal(int, int, str)
    operation_done = Signal(str, object)
    operation_error = Signal(str, str)
    log_line = Signal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._jobs: queue.Queue[dict] = queue.Queue()
        self._stop_event = threading.Event()
        self._cancel_upload = threading.Event()
        self._device: MrxDevice | None = None
        self._port_name = ""
        self._compatible = False
        self._last_status_poll = 0.0

    def submit(self, action: str, **arguments: object) -> None:
        self._jobs.put({"action": action, **arguments})

    def cancel_upload(self) -> None:
        self._cancel_upload.set()

    def request_stop(self) -> None:
        self._stop_event.set()
        self._cancel_upload.set()
        self._jobs.put({"action": "stop"})

    @staticmethod
    def _stamp() -> str:
        return datetime.now().strftime("%H:%M:%S")

    def _log(self, message: str) -> None:
        self.log_line.emit(f"{self._stamp()}  {message}")

    def _port_candidates(self) -> list[str]:
        ports = list(list_ports.comports())
        preferred = [port.device for port in ports if port.vid == 0x1209 and port.pid == 0x0001]
        fallback = [port.device for port in ports if port.device not in preferred]
        return preferred + fallback

    def _try_connect(self) -> bool:
        for port_name in self._port_candidates():
            if self._stop_event.is_set():
                return False
            port = None
            try:
                port = Serial(port_name, 115200, timeout=0.05, write_timeout=5)
                port.dtr = True
                port.reset_input_buffer()
                device = MrxDevice(port)
                info = device.get_info(timeout=0.5)
                if info.product != "MRX Loader":
                    device.close()
                    continue
                self._device = device
                self._port_name = port_name
                self._compatible = info.protocol_version == PROTOCOL_VERSION
                self.connected.emit(info, port_name, self._compatible)
                if self._compatible:
                    self._log(f"Connected to {info.product} on {port_name}")
                else:
                    self._log(
                        f"Protocol mismatch: device {info.protocol_version}, app {PROTOCOL_VERSION}"
                    )
                if self._compatible:
                    try:
                        self._refresh()
                    except Exception:
                        self._device = None
                        self._port_name = ""
                        self._compatible = False
                        self.disconnected.emit()
                        raise
                return True
            except Exception:
                if port is not None:
                    try:
                        port.close()
                    except Exception:
                        pass
        return False

    def _disconnect(self, message: str = "Device disconnected") -> None:
        if self._device is not None:
            try:
                self._device.close()
            except Exception:
                pass
            self._device = None
            self._port_name = ""
            self._compatible = False
            self.disconnected.emit()
            self._log(message)

    def _refresh(self) -> None:
        if self._device is None or not self._compatible:
            return
        status = self._device.get_status()
        self.status_updated.emit(status)
        self.payloads_updated.emit(self._device.list_payloads())

    def _process_job(self, job: dict) -> None:
        action = str(job["action"])
        if action == "stop":
            return
        if self._device is None:
            self.operation_error.emit(action, "No MRX Loader is connected")
            return
        if not self._compatible:
            self.operation_error.emit(action, "Commands are disabled for this protocol version")
            return

        try:
            if action == "refresh":
                self._refresh()
                result = None
            elif action == "verify":
                payload_id = int(job["payload_id"])
                self.progress.emit(0, 0, "Verifying payload in flash")
                result = self._device.verify_payload(payload_id)
                self.progress.emit(100, 100, "Verification complete")
                self._refresh()
            elif action == "select":
                payload_id = int(job["payload_id"])
                self._device.select_payload(payload_id)
                result = {"id": payload_id}
                self._refresh()
            elif action == "delete":
                payload_id = int(job["payload_id"])
                selection_cleared = self._device.delete_payload(payload_id)
                result = {"id": payload_id, "selection_cleared": selection_cleared}
                self._refresh()
            elif action == "upload":
                self._cancel_upload.clear()
                path = Path(str(job["path"]))
                name = str(job["name"])
                select_after_upload = bool(job["select_after_upload"])
                result = self._device.upload_payload(
                    path,
                    name,
                    select_after_upload,
                    lambda completed, total, message: self.progress.emit(completed, total, message),
                    self._cancel_upload.is_set,
                )
                self._refresh()
            elif action == "firmware_update":
                image = inspect_firmware(str(job["path"]))
                self.progress.emit(0, 0, "Rebooting Feather into BOOTSEL")
                self._device.reboot_to_bootloader()
                self._disconnect("Feather acknowledged BOOTSEL reboot")
                result = {
                    "path": str(image.path),
                    "sha256": image.sha256,
                    "size": image.size,
                    "blocks": image.block_count,
                }
            else:
                raise DeviceError(f"Unknown operation: {action}")
            self.operation_done.emit(action, result)
        except InterruptedError as error:
            self._log(str(error))
            self.operation_error.emit(action, str(error))
        except (SerialException, OSError) as error:
            self.operation_error.emit(action, f"Serial connection failed: {error}")
            self._disconnect("Serial connection lost")
        except Exception as error:
            if action == "firmware_update":
                self._disconnect("Resuming serial discovery after BOOTSEL request")
            self.operation_error.emit(action, str(error))
            self._log(f"{action.title()} failed: {error}")

    def run(self) -> None:
        while not self._stop_event.is_set():
            if self._device is None:
                if not self._try_connect():
                    self.msleep(700)
                    continue
            try:
                job = self._jobs.get(timeout=0.05)
            except queue.Empty:
                job = None

            if job is not None:
                self._process_job(job)
                continue

            if self._device is not None and self._compatible:
                now = time.monotonic()
                if now - self._last_status_poll >= 1.5:
                    try:
                        self.status_updated.emit(self._device.get_status())
                    except (SerialException, OSError) as error:
                        self._disconnect(f"Serial connection lost: {error}")
                    except Exception as error:
                        self._log(f"Status refresh failed: {error}")
                    self._last_status_poll = now

        self._disconnect("Application closed")
