"""MRX Loader desktop application."""

from __future__ import annotations

import sys
import ctypes
from pathlib import Path

if sys.platform == "win32":
    from ctypes import wintypes

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QIcon, QPalette, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QFileDialog,
    QFrame,
    QHeaderView,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QMainWindow,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from . import __version__
from .device import DeviceInfo
from .uf2 import Uf2Error, inspect_firmware
from .worker import DeviceWorker


_single_instance_handle = None


def _activate_existing_gui() -> None:
    """Raise the existing window when the application is launched again."""
    if sys.platform != "win32":
        return
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.FindWindowW.argtypes = (wintypes.LPCWSTR, wintypes.LPCWSTR)
    user32.FindWindowW.restype = wintypes.HWND
    user32.ShowWindow.argtypes = (wintypes.HWND, ctypes.c_int)
    user32.ShowWindow.restype = wintypes.BOOL
    user32.SetForegroundWindow.argtypes = (wintypes.HWND,)
    user32.SetForegroundWindow.restype = wintypes.BOOL
    window = user32.FindWindowW(None, "MRX Loader")
    if window:
        user32.ShowWindow(window, 9)
        user32.SetForegroundWindow(window)


def acquire_single_instance() -> bool:
    """Allow a single GUI instance and focus the existing window otherwise."""
    global _single_instance_handle
    if sys.platform != "win32":
        return True

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateMutexW.argtypes = (wintypes.LPVOID, wintypes.BOOL, wintypes.LPCWSTR)
    kernel32.CreateMutexW.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.CloseHandle.restype = wintypes.BOOL
    handle = kernel32.CreateMutexW(None, False, "Local\\MRXLoaderDesktopGUI")
    if not handle:
        return True
    if ctypes.get_last_error() == 183:
        kernel32.CloseHandle(handle)
        _activate_existing_gui()
        return False
    _single_instance_handle = handle
    return True


def format_bytes(value: int) -> str:
    size = float(value)
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} GB"


def card() -> QFrame:
    frame = QFrame()
    frame.setObjectName("Card")
    return frame


def app_icon_path() -> Path:
    return Path(__file__).resolve().parents[2] / "icon.png"


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("MRX Loader")
        self.setWindowIcon(QIcon(str(app_icon_path())))
        self.setMinimumSize(1020, 680)
        self.resize(1180, 790)
        self.worker = DeviceWorker(self)
        self.device_info: DeviceInfo | None = None
        self.compatible = False
        self.payloads: list[dict] = []
        self.busy_action: str | None = None

        root = QWidget()
        root_layout = QHBoxLayout(root)
        root_layout.setContentsMargins(20, 20, 20, 20)
        root_layout.setSpacing(18)
        root_layout.addWidget(self._build_sidebar())
        root_layout.addWidget(self._build_main_panel(), 1)
        self.setCentralWidget(root)
        self.setStyleSheet(self._stylesheet())

        self.worker.connected.connect(self._on_connected)
        self.worker.disconnected.connect(self._on_disconnected)
        self.worker.status_updated.connect(self._on_status)
        self.worker.payloads_updated.connect(self._on_payloads)
        self.worker.progress.connect(self._on_progress)
        self.worker.operation_done.connect(self._on_operation_done)
        self.worker.operation_error.connect(self._on_operation_error)
        self.worker.log_line.connect(self._append_log)
        self.worker.start()

    def _build_sidebar(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("Sidebar")
        panel.setFixedWidth(242)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(18, 18, 18, 18)
        layout.setSpacing(16)

        brand = QHBoxLayout()
        mark = QLabel()
        mark.setObjectName("BrandIcon")
        mark.setFixedSize(56, 56)
        logo = QPixmap(str(app_icon_path()))
        mark.setPixmap(
            logo.scaled(
                mark.size(),
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )
        brand_text = QVBoxLayout()
        title = QLabel("MRX Loader")
        title.setObjectName("BrandTitle")
        subtitle = QLabel("FEATHER RP2040")
        subtitle.setObjectName("Eyebrow")
        brand_text.addWidget(title)
        brand_text.addWidget(subtitle)
        brand.addWidget(mark)
        brand.addLayout(brand_text)
        brand.addStretch()
        layout.addLayout(brand)

        layout.addSpacing(10)
        layout.addWidget(self._section_label("DEVICE"))
        connection = card()
        connection_layout = QVBoxLayout(connection)
        connection_layout.setContentsMargins(14, 13, 14, 13)
        connection_layout.setSpacing(8)
        top = QHBoxLayout()
        self.connection_dot = QLabel("●")
        self.connection_dot.setObjectName("StatusDotOff")
        self.connection_label = QLabel("Searching for device")
        self.connection_label.setObjectName("CardTitle")
        top.addWidget(self.connection_dot)
        top.addWidget(self.connection_label, 1)
        connection_layout.addLayout(top)
        self.port_label = QLabel("Scanning serial ports…")
        self.port_label.setObjectName("Muted")
        connection_layout.addWidget(self.port_label)
        layout.addWidget(connection)

        summary = card()
        summary_layout = QVBoxLayout(summary)
        summary_layout.setContentsMargins(14, 13, 14, 13)
        summary_layout.setSpacing(7)
        summary_layout.addWidget(self._section_label("FIRMWARE STATE"))
        self.state_label = QLabel("Waiting for connection")
        self.state_label.setObjectName("ValueLabel")
        summary_layout.addWidget(self.state_label)
        self.firmware_label = QLabel("Firmware -")
        self.firmware_label.setObjectName("Muted")
        summary_layout.addWidget(self.firmware_label)
        self.device_id_label = QLabel("Device ID -")
        self.device_id_label.setObjectName("MutedSmall")
        self.device_id_label.setWordWrap(True)
        summary_layout.addWidget(self.device_id_label)
        layout.addWidget(summary)

        storage = card()
        storage_layout = QVBoxLayout(storage)
        storage_layout.setContentsMargins(14, 13, 14, 13)
        storage_layout.setSpacing(8)
        storage_title = QLabel("Payload storage")
        storage_title.setObjectName("CardTitle")
        storage_layout.addWidget(storage_title)
        self.storage_label = QLabel("- free")
        self.storage_label.setObjectName("Muted")
        storage_layout.addWidget(self.storage_label)
        self.storage_bar = QProgressBar()
        self.storage_bar.setRange(0, 1000)
        self.storage_bar.setValue(0)
        self.storage_bar.setTextVisible(False)
        self.storage_bar.setFixedHeight(7)
        storage_layout.addWidget(self.storage_bar)
        self.count_label = QLabel("0 payloads")
        self.count_label.setObjectName("MutedSmall")
        storage_layout.addWidget(self.count_label)
        layout.addWidget(storage)

        layout.addStretch()
        note = QLabel("Connect USB-C to manage payloads.\nNo external Feather pin wiring required.")
        note.setObjectName("MutedSmall")
        note.setWordWrap(True)
        layout.addWidget(note)
        return panel

    def _build_main_panel(self) -> QWidget:
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(13)

        header = QHBoxLayout()
        heading = QVBoxLayout()
        eyebrow = QLabel("DEVICE MANAGEMENT")
        eyebrow.setObjectName("Eyebrow")
        title = QLabel("Payload library")
        title.setObjectName("PageTitle")
        heading.addWidget(eyebrow)
        heading.addWidget(title)
        header.addLayout(heading)
        header.addStretch()
        self.refresh_button = QPushButton("Refresh")
        self.refresh_button.setObjectName("SecondaryButton")
        self.refresh_button.clicked.connect(self._refresh_device)
        self.upload_button = QPushButton("+  Upload payload")
        self.upload_button.setObjectName("PrimaryButton")
        self.upload_button.clicked.connect(self._choose_upload)
        self.firmware_button = QPushButton("Update firmware")
        self.firmware_button.setObjectName("SecondaryButton")
        self.firmware_button.clicked.connect(self._choose_firmware_update)
        header.addWidget(self.refresh_button)
        header.addWidget(self.firmware_button)
        header.addWidget(self.upload_button)
        layout.addLayout(header)

        self.version_banner = QLabel()
        self.version_banner.setObjectName("WarningBanner")
        self.version_banner.setWordWrap(True)
        self.version_banner.hide()
        layout.addWidget(self.version_banner)

        library = card()
        library_layout = QVBoxLayout(library)
        library_layout.setContentsMargins(16, 14, 16, 12)
        library_layout.setSpacing(10)
        library_head = QHBoxLayout()
        library_title_layout = QVBoxLayout()
        library_title = QLabel("Stored payloads")
        library_title.setObjectName("CardTitle")
        self.selection_summary = QLabel("Select a payload to inspect or manage it")
        self.selection_summary.setObjectName("Muted")
        library_title_layout.addWidget(library_title)
        library_title_layout.addWidget(self.selection_summary)
        library_head.addLayout(library_title_layout)
        library_head.addStretch()
        library_layout.addLayout(library_head)

        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(["NAME", "ID", "SIZE", "SHA-256", "STATUS"])
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.table.setAlternatingRowColors(False)
        self.table.setShowGrid(False)
        self.table.setMinimumHeight(225)
        self.table.itemSelectionChanged.connect(self._selection_changed)
        library_layout.addWidget(self.table)

        actions = QHBoxLayout()
        self.active_label = QLabel("Active payload: none")
        self.active_label.setObjectName("Muted")
        actions.addWidget(self.active_label, 1)
        self.verify_button = QPushButton("Verify")
        self.verify_button.setObjectName("SecondaryButton")
        self.verify_button.clicked.connect(self._verify_selected)
        self.select_button = QPushButton("Set active")
        self.select_button.setObjectName("SecondaryButton")
        self.select_button.clicked.connect(self._select_selected)
        self.delete_button = QPushButton("Delete")
        self.delete_button.setObjectName("DangerButton")
        self.delete_button.clicked.connect(self._delete_selected)
        for button in (self.verify_button, self.select_button, self.delete_button):
            button.setEnabled(False)
            actions.addWidget(button)
        library_layout.addLayout(actions)
        layout.addWidget(library, 1)

        upload_card = card()
        upload_layout = QVBoxLayout(upload_card)
        upload_layout.setContentsMargins(16, 13, 16, 13)
        upload_layout.setSpacing(9)
        upload_head = QHBoxLayout()
        upload_title = QLabel("Upload a payload")
        upload_title.setObjectName("CardTitle")
        self.select_after_upload = QCheckBox("Select as active after verification")
        self.select_after_upload.setChecked(True)
        self.select_after_upload.setObjectName("CheckBox")
        upload_head.addWidget(upload_title)
        upload_head.addStretch()
        upload_head.addWidget(self.select_after_upload)
        upload_layout.addLayout(upload_head)
        progress_row = QHBoxLayout()
        self.progress_label = QLabel("Choose a payload file to begin")
        self.progress_label.setObjectName("Muted")
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 1000)
        self.progress_bar.setValue(0)
        self.progress_bar.setTextVisible(False)
        self.progress_bar.setFixedWidth(260)
        self.progress_bar.setFixedHeight(8)
        self.progress_bar.hide()
        self.cancel_button = QPushButton("Cancel")
        self.cancel_button.setObjectName("SecondaryButton")
        self.cancel_button.hide()
        self.cancel_button.clicked.connect(self.worker.cancel_upload)
        progress_row.addWidget(self.progress_label, 1)
        progress_row.addWidget(self.progress_bar)
        progress_row.addWidget(self.cancel_button)
        upload_layout.addLayout(progress_row)
        layout.addWidget(upload_card)

        activity = card()
        activity_layout = QVBoxLayout(activity)
        activity_layout.setContentsMargins(16, 10, 16, 10)
        activity_layout.setSpacing(6)
        activity_title = QLabel("Activity")
        activity_title.setObjectName("CardTitle")
        activity_layout.addWidget(activity_title)
        self.log_view = QTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setObjectName("ActivityLog")
        self.log_view.setFixedHeight(85)
        activity_layout.addWidget(self.log_view)
        layout.addWidget(activity)
        return panel

    @staticmethod
    def _section_label(text: str) -> QLabel:
        label = QLabel(text)
        label.setObjectName("Eyebrow")
        return label

    @staticmethod
    def _stylesheet() -> str:
        return """
        QMainWindow, QWidget { background: #090a0f; color: #f4f2fa; font-family: 'Segoe UI'; font-size: 13px; }
        QLabel { background: transparent; }
        #Sidebar { background: #101116; border: 1px solid #25212f; border-radius: 16px; }
        #Card { background: #14151c; border: 1px solid #2a2634; border-radius: 13px; }
        #BrandIcon { background: transparent; }
        #BrandTitle { color: #faf8fc; font-size: 15px; font-weight: 700; }
        #Eyebrow { color: #a39aae; font-size: 10px; font-weight: 800; letter-spacing: 1px; }
        #PageTitle { color: #faf8fc; font-size: 27px; font-weight: 750; }
        #CardTitle { color: #f4f1f8; font-size: 13px; font-weight: 700; }
        #Muted { color: #b2acbd; font-size: 12px; }
        #MutedSmall { color: #918a9c; font-size: 11px; }
        #ValueLabel { color: #e9e2fa; font-size: 16px; font-weight: 700; }
        #StatusDotOn { color: #40d6a2; font-size: 14px; }
        #StatusDotOff { color: #68758a; font-size: 14px; }
        QPushButton { border: 1px solid #393344; border-radius: 8px; padding: 8px 12px; font-weight: 650; }
        QPushButton:hover { border-color: #823caa; background: #25202e; }
        QPushButton:disabled { color: #625c6c; background: #17171e; border-color: #292630; }
        #PrimaryButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #173cf0, stop:0.55 #8315a5, stop:1 #ed0718); border: 1px solid #7130bd; color: white; padding: 9px 15px; }
        #PrimaryButton:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #3558ff, stop:0.55 #a323c1, stop:1 #ff2633); }
        #SecondaryButton { background: #211e29; color: #eee9f4; }
        #DangerButton { background: #28151d; border-color: #702232; color: #ff9aa8; }
        #WarningBanner { background: #292016; color: #f1d08a; border: 1px solid #604a29; border-radius: 9px; padding: 11px 13px; }
        QTableWidget { background: #101117; alternate-background-color: #17151e; border: 1px solid #2a2634; border-radius: 8px; selection-background-color: #382448; selection-color: white; }
        QTableWidget::item { padding: 8px 7px; border-bottom: 1px solid #26222e; }
        QHeaderView::section { background: #101117; color: #a39aae; border: none; padding: 8px 7px; font-size: 10px; font-weight: 800; }
        QProgressBar { background: #2b2534; border: none; border-radius: 4px; }
        QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #173cf0, stop:0.55 #8315a5, stop:1 #ed0718); border-radius: 4px; }
        #ActivityLog { background: #101117; color: #bdb5c7; border: none; font-family: Consolas; font-size: 11px; }
        QCheckBox { color: #bdb5c7; spacing: 7px; font-size: 11px; }
        QCheckBox::indicator { width: 14px; height: 14px; }
        """

    def _on_connected(self, info: DeviceInfo, port_name: str, compatible: bool) -> None:
        self.device_info = info
        self.compatible = compatible
        self.connection_dot.setObjectName("StatusDotOn")
        self.connection_dot.style().unpolish(self.connection_dot)
        self.connection_dot.style().polish(self.connection_dot)
        self.connection_label.setText("Connected")
        self.port_label.setText(f"{port_name}  ·  {info.hardware}")
        self.firmware_label.setText(f"Firmware {info.firmware_version}  ·  Protocol {info.protocol_version}")
        self.device_id_label.setText(f"ID {info.device_id}")
        self.version_banner.setVisible(not compatible)
        if not compatible:
            self.version_banner.setText(
                f"Protocol version mismatch. This app supports v1; the device reports v{info.protocol_version}. "
                "Payload commands are disabled."
            )
        self._set_controls_enabled(compatible)

    def _on_disconnected(self) -> None:
        self.busy_action = None
        self.device_info = None
        self.compatible = False
        self.version_banner.hide()
        self.connection_dot.setObjectName("StatusDotOff")
        self.connection_dot.style().unpolish(self.connection_dot)
        self.connection_dot.style().polish(self.connection_dot)
        self.connection_label.setText("Searching for device")
        self.port_label.setText("Scanning serial ports…")
        self.firmware_label.setText("Firmware -")
        self.device_id_label.setText("Device ID -")
        self.state_label.setText("Waiting for connection")
        self.storage_label.setText("- free")
        self.storage_bar.setValue(0)
        self.count_label.setText("0 payloads")
        self.active_label.setText("Active payload: none")
        self.payloads = []
        self._populate_table()
        self._set_controls_enabled(False)

    def _set_controls_enabled(self, enabled: bool) -> None:
        available = enabled and self.busy_action is None
        self.refresh_button.setEnabled(available)
        self.firmware_button.setEnabled(available)
        self.upload_button.setEnabled(available)
        has_selection = available and self._selected_payload_id() is not None
        self.verify_button.setEnabled(has_selection)
        self.select_button.setEnabled(has_selection)
        self.delete_button.setEnabled(has_selection)
        self.select_after_upload.setEnabled(available)

    def _on_status(self, status: dict) -> None:
        self.state_label.setText(status["state"])
        self.count_label.setText(f"{status['payload_count']} payloads")
        total = status["total_bytes"]
        free = status["free_bytes"]
        used = max(0, total - free)
        self.storage_label.setText(f"{format_bytes(free)} free of {format_bytes(total)}")
        self.storage_bar.setValue(int(1000 * used / total) if total else 0)
        selected = status["selected_id"]
        self.active_label.setText(f"Active payload: #{selected:08X}" if selected else "Active payload: none")
        self._populate_table()

    def _on_payloads(self, payloads: list) -> None:
        self.payloads = payloads
        self._populate_table()
        self._append_log(f"{len(payloads)} stored payload(s) loaded")

    def _populate_table(self) -> None:
        selected_id = None
        if self.table.selectionModel() and self.table.currentRow() >= 0:
            selected_id = self._selected_payload_id()
        self.table.setRowCount(len(self.payloads))
        active_text = self.active_label.text()
        active_id = None
        if active_text.startswith("Active payload: #"):
            try:
                active_id = int(active_text.rsplit("#", 1)[1], 16)
            except ValueError:
                pass
        for row, payload in enumerate(self.payloads):
            values = [
                payload["name"],
                f"{payload['id']:08X}",
                format_bytes(payload["size"]),
                payload["sha256"][:16] + "…",
                "ACTIVE" if payload["id"] == active_id else "READY",
            ]
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                item.setData(Qt.ItemDataRole.UserRole, payload["id"])
                if column == 4 and payload["id"] == active_id:
                    item.setForeground(QColor("#62dfb2"))
                self.table.setItem(row, column, item)
        if selected_id is not None:
            for row, payload in enumerate(self.payloads):
                if payload["id"] == selected_id:
                    self.table.selectRow(row)
                    break
        if not self.payloads:
            self.selection_summary.setText("No payloads stored yet")
        elif self.table.currentRow() < 0:
            self.selection_summary.setText("Select a payload to inspect or manage it")

    def _selected_payload_id(self) -> int | None:
        row = self.table.currentRow()
        if row < 0 or self.table.item(row, 0) is None:
            return None
        return int(self.table.item(row, 0).data(Qt.ItemDataRole.UserRole))

    def _selection_changed(self) -> None:
        payload_id = self._selected_payload_id()
        if payload_id is None:
            self.selection_summary.setText("Select a payload to inspect or manage it")
        else:
            payload = next((item for item in self.payloads if item["id"] == payload_id), None)
            if payload:
                self.selection_summary.setText(
                    f"{payload['name']}  ·  SHA-256 {payload['sha256'][:20]}…"
                )
        self._set_controls_enabled(self.compatible)

    def _set_busy(self, action: str | None) -> None:
        self.busy_action = action
        self.cancel_button.setVisible(action == "upload")
        self.progress_bar.setVisible(action == "upload" or action == "verify")
        if action is None:
            self.progress_bar.hide()
            self.progress_label.setText("Choose a payload file to begin")
        self._set_controls_enabled(self.compatible)

    def _on_progress(self, completed: int, total: int, message: str) -> None:
        self.progress_label.setText(message)
        self.progress_bar.show()
        if total > 0:
            self.progress_bar.setRange(0, 1000)
            self.progress_bar.setValue(min(1000, int(completed * 1000 / total)))
        else:
            self.progress_bar.setRange(0, 0)

    def _on_operation_done(self, action: str, result: object) -> None:
        if action == "upload" and isinstance(result, dict):
            text = f"Uploaded payload #{result['id']:08X} · {format_bytes(result['size'])}"
            if result["selected"]:
                text += " · verified and set active"
            self._append_log(text)
        elif action == "verify" and isinstance(result, dict):
            payload_id = self._selected_payload_id()
            if result["matches"]:
                self._append_log(f"Payload #{payload_id:08X} integrity check passed")
                QMessageBox.information(self, "Verification passed", "The stored payload hash matches.")
            else:
                self._append_log(f"Payload #{payload_id:08X} hash mismatch")
                QMessageBox.warning(self, "Verification failed", "The stored payload does not match its SHA-256 metadata.")
        elif action == "select":
            self._append_log("Active payload selection saved and confirmed")
        elif action == "delete" and isinstance(result, dict):
            detail = " Active selection was cleared." if result["selection_cleared"] else ""
            self._append_log(f"Deleted payload #{result['id']:08X}.{detail}")
        elif action == "refresh":
            self._append_log("Device and payload list refreshed")
        elif action == "firmware_update":
            self._append_log("Device acknowledged BOOTSEL reboot request")
            QMessageBox.information(
                self,
                "Copy firmware to the Feather",
                "The Feather is rebooting into its USB bootloader. In your file "
                "manager, open the RPI-RP2 drive and copy the selected UF2 file to "
                "its root. "
                "Wait for the drive to disappear, then reconnect the Feather if needed. "
                "The app will reconnect when the updated firmware starts.",
            )
        self._set_busy(None)

    def _on_operation_error(self, action: str, message: str) -> None:
        self._append_log(f"{action.title()} · {message}")
        if action == "upload" and message == "Upload cancelled":
            self.progress_label.setText("Upload cancelled")
        elif action == "verify":
            QMessageBox.warning(self, "Verification error", message)
        elif action in ("upload", "select", "delete", "firmware_update"):
            QMessageBox.warning(self, "Operation failed", message)
        self._set_busy(None)
        if action == "upload" and message == "Upload cancelled":
            self.progress_label.setText("Upload cancelled")

    def _verify_selected(self) -> None:
        payload_id = self._selected_payload_id()
        if payload_id is None:
            return
        self._set_busy("verify")
        self._on_progress(0, 0, "Verifying payload in flash…")
        self.worker.submit("verify", payload_id=payload_id)

    def _refresh_device(self) -> None:
        if self.busy_action is not None:
            return
        self._set_busy("refresh")
        self.worker.submit("refresh")

    def _select_selected(self) -> None:
        payload_id = self._selected_payload_id()
        if payload_id is None:
            return
        self._set_busy("select")
        self.worker.submit("select", payload_id=payload_id)

    def _delete_selected(self) -> None:
        payload_id = self._selected_payload_id()
        if payload_id is None:
            return
        payload = next((item for item in self.payloads if item["id"] == payload_id), None)
        name = payload["name"] if payload else f"#{payload_id:08X}"
        answer = QMessageBox.question(
            self,
            "Delete payload",
            f"Delete “{name}” from the Feather? This cannot be undone.",
            QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
            QMessageBox.StandardButton.Cancel,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        self._set_busy("delete")
        self.worker.submit("delete", payload_id=payload_id)

    def _choose_upload(self) -> None:
        filename, _ = QFileDialog.getOpenFileName(
            self,
            "Choose payload",
            "",
            "Payload files (*.bin *.payload);;All files (*)",
        )
        if not filename:
            return
        path = Path(filename)
        default_name = path.stem[:63]
        name, accepted = QInputDialog.getText(
            self,
            "Payload name",
            "Name stored on the Feather (printable ASCII, up to 63 characters):",
            text=default_name,
        )
        if not accepted:
            return
        try:
            encoded_name = name.encode("ascii")
        except UnicodeEncodeError:
            QMessageBox.warning(self, "Invalid name", "Use printable ASCII characters only.")
            return
        if (
            not encoded_name
            or len(encoded_name) > 63
            or any(byte < 0x20 or byte > 0x7E for byte in encoded_name)
            or any(character in '\\/:*?"<>|' for character in name)
        ):
            QMessageBox.warning(
                self,
                "Invalid name",
                "Use 1–63 printable ASCII characters. Paths and reserved filename characters are not allowed.",
            )
            return
        if path.stat().st_size <= 0:
            QMessageBox.warning(self, "Empty file", "Choose a non-empty payload file.")
            return
        self._set_busy("upload")
        self.progress_bar.setValue(0)
        self.worker.submit(
            "upload",
            path=str(path),
            name=name,
            select_after_upload=self.select_after_upload.isChecked(),
        )

    def _choose_firmware_update(self) -> None:
        filename, _ = QFileDialog.getOpenFileName(
            self,
            "Choose MRX Loader firmware",
            "",
            "RP2040 UF2 firmware (*.uf2)",
        )
        if not filename:
            return
        try:
            image = inspect_firmware(filename)
        except Uf2Error as error:
            QMessageBox.warning(self, "Invalid firmware image", str(error))
            return

        answer = QMessageBox.question(
            self,
            "Reboot to firmware update mode",
            f"Validated RP2040 firmware image\n"
            f"Size: {format_bytes(image.size)} · UF2 blocks: {image.block_count}\n"
            f"SHA-256: {image.sha256}\n\n"
            "The Feather will restart into BOOTSEL. You must then copy this file "
            "to the RPI-RP2 drive. Continue?",
            QMessageBox.StandardButton.Cancel | QMessageBox.StandardButton.Yes,
            QMessageBox.StandardButton.Cancel,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        self._set_busy("firmware_update")
        self.progress_label.setText("Rebooting Feather into BOOTSEL…")
        self.worker.submit("firmware_update", path=filename)

    def _append_log(self, message: str) -> None:
        self.log_view.append(message)
        scrollbar = self.log_view.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def closeEvent(self, event) -> None:
        if self.worker.isRunning():
            self.worker.request_stop()
            if not self.worker.wait(2500):
                event.ignore()
                self._append_log("Finishing the active serial operation before closing…")
                return
        event.accept()


def main() -> int:
    if not acquire_single_instance():
        return 0
    app = QApplication(sys.argv)
    app.setApplicationName("MRX Loader")
    app.setApplicationVersion(__version__)
    app.setStyle("Fusion")
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor("#090a0f"))
    palette.setColor(QPalette.ColorRole.WindowText, QColor("#f4f2fa"))
    palette.setColor(QPalette.ColorRole.Base, QColor("#101117"))
    palette.setColor(QPalette.ColorRole.Text, QColor("#f4f2fa"))
    palette.setColor(QPalette.ColorRole.Button, QColor("#211e29"))
    palette.setColor(QPalette.ColorRole.ButtonText, QColor("#f4f2fa"))
    palette.setColor(QPalette.ColorRole.Highlight, QColor("#7b25c2"))
    app.setPalette(palette)
    app.setWindowIcon(QIcon(str(app_icon_path())))
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
