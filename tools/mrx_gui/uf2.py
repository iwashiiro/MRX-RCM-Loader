"""Basic safety checks for the RP2040 UF2 firmware update workflow."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct

UF2_BLOCK_SIZE = 512
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_NOT_MAIN_FLASH = 0x00000001
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
RP2040_FLASH_BASE = 0x10000000
FIRMWARE_PARTITION_SIZE = 1024 * 1024
MAX_UF2_FILE_SIZE = 4 * 1024 * 1024


class Uf2Error(ValueError):
    """The selected file is not a safe MRX Loader RP2040 firmware image."""


@dataclass(frozen=True)
class FirmwareImage:
    path: Path
    size: int
    sha256: str
    block_count: int


def inspect_firmware(path: str | Path) -> FirmwareImage:
    """Validate UF2 structure, RP2040 family, and MRX 1 MiB flash bounds."""
    image_path = Path(path)
    try:
        size = image_path.stat().st_size
    except OSError as error:
        raise Uf2Error(f"Cannot read the selected firmware file: {error}") from error
    if size == 0 or size > MAX_UF2_FILE_SIZE or size % UF2_BLOCK_SIZE:
        raise Uf2Error("UF2 size is invalid; expected a non-empty, 512-byte-aligned file under 4 MiB")

    try:
        data = image_path.read_bytes()
    except OSError as error:
        raise Uf2Error(f"Cannot read the selected firmware file: {error}") from error
    if len(data) != size:
        raise Uf2Error("The selected firmware file changed while it was being read")

    count = size // UF2_BLOCK_SIZE
    seen_blocks: set[int] = set()
    flash_ranges: list[tuple[int, int]] = []
    for index in range(count):
        start = index * UF2_BLOCK_SIZE
        block = data[start : start + UF2_BLOCK_SIZE]
        magic0, magic1, flags, address, payload_size, block_no, total_blocks, family_id = (
            struct.unpack_from("<8I", block)
        )
        end_magic = struct.unpack_from("<I", block, 508)[0]
        if (magic0, magic1, end_magic) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            raise Uf2Error(f"UF2 block {index} has invalid magic values")
        if flags & UF2_FLAG_NOT_MAIN_FLASH:
            raise Uf2Error("UF2 contains non-flash blocks and is not accepted")
        if not flags & UF2_FLAG_FAMILY_ID_PRESENT or family_id != RP2040_FAMILY_ID:
            raise Uf2Error(f"UF2 block {index} is not identified for the RP2040 family")
        if not 1 <= payload_size <= 476:
            raise Uf2Error(f"UF2 block {index} has an invalid payload size")
        if total_blocks != count or block_no >= count or block_no in seen_blocks:
            raise Uf2Error("UF2 block numbering is inconsistent or duplicated")
        seen_blocks.add(block_no)

        flash_end = address + payload_size
        partition_end = RP2040_FLASH_BASE + FIRMWARE_PARTITION_SIZE
        if address < RP2040_FLASH_BASE or flash_end > partition_end or flash_end <= address:
            raise Uf2Error("UF2 writes outside the MRX Loader 1 MiB firmware partition")
        flash_ranges.append((address, flash_end))

    if len(seen_blocks) != count:
        raise Uf2Error("UF2 is missing one or more data blocks")
    flash_ranges.sort()
    for previous, current in zip(flash_ranges, flash_ranges[1:]):
        if current[0] < previous[1]:
            raise Uf2Error("UF2 contains overlapping flash blocks")
    if not flash_ranges or flash_ranges[0][0] != RP2040_FLASH_BASE:
        raise Uf2Error("UF2 does not include the RP2040 boot image at flash address 0x10000000")

    return FirmwareImage(
        path=image_path,
        size=size,
        sha256=hashlib.sha256(data).hexdigest(),
        block_count=count,
    )
