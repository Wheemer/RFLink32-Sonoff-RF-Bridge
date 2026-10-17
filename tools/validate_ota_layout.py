"""Reject RF Bridge images that cannot safely bootstrap or stage OTA updates."""

from __future__ import annotations

import argparse
import gzip
from pathlib import Path

FLASH_SECTOR_SIZE = 0x1000
LEGACY_FS_START = 0xBB000
OTA_FS_START = 0xEB000
RSA_2048_SIGNATURE_TRAILER_BYTES = 256 + 4
COMPRESSOR_VARIANCE_GUARD_BYTES = 512


def align_up(value: int) -> int:
    return (value + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)


def align_down(value: int) -> int:
    return value & ~(FLASH_SECTOR_SIZE - 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    args = parser.parse_args()

    firmware = args.firmware.read_bytes()
    firmware_size = len(firmware)
    if firmware_size >= LEGACY_FS_START:
        raise SystemExit(
            "Firmware overlaps the legacy 256 KB filesystem; it cannot safely "
            "copy the existing configuration during a serial bootstrap."
        )

    compressed_size = len(gzip.compress(firmware, compresslevel=9))
    signed_update_size = (
        compressed_size
        + RSA_2048_SIGNATURE_TRAILER_BYTES
        + COMPRESSOR_VARIANCE_GUARD_BYTES
    )
    free_sketch_space = OTA_FS_START - align_up(firmware_size)
    update_capacity = align_down(free_sketch_space - FLASH_SECTOR_SIZE)
    if signed_update_size > update_capacity:
        raise SystemExit(
            "Signed compressed firmware does not fit the OTA staging region: "
            f"{signed_update_size} bytes required, {update_capacity} available."
        )

    print(f"firmware_bytes={firmware_size}")
    print(f"legacy_config_gap_bytes={LEGACY_FS_START - firmware_size}")
    print(f"compressed_firmware_bytes={compressed_size}")
    print(f"signed_update_guarded_bytes={signed_update_size}")
    print(f"ota_staging_capacity_bytes={update_capacity}")
    print(f"ota_staging_margin_bytes={update_capacity - signed_update_size}")


if __name__ == "__main__":
    main()
