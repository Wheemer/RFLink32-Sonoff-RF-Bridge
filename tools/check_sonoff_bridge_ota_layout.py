# ruff: noqa: F821
"""Fail Sonoff RF Bridge builds that cannot preserve config or stage OTA."""

from __future__ import annotations

import gzip

from SCons.Script import Import

Import("env")

FLASH_SECTOR_SIZE = 0x1000
LEGACY_FS_START = 0xBB000
CURRENT_FS_START = 0xEB000
TARGET_FS_START = 0xF3000
RSA_2048_SIGNATURE_TRAILER_BYTES = 256 + 4
COMPRESSOR_VARIANCE_GUARD_BYTES = 512
MINIMUM_OTA_MARGIN_BYTES = 16 * 1024


def _align_up(value: int) -> int:
    return (value + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)


def _align_down(value: int) -> int:
    return value & ~(FLASH_SECTOR_SIZE - 1)


def validate_ota_layout(source, target, env):
    firmware_path = str(target[0])
    with open(firmware_path, "rb") as firmware_file:
        firmware = firmware_file.read()

    firmware_size = len(firmware)
    if firmware_size >= LEGACY_FS_START:
        raise RuntimeError(
            "Sonoff RF Bridge image overlaps the legacy filesystem; "
            "configuration migration would be unsafe."
        )

    compressed_size = len(gzip.compress(firmware, compresslevel=9))
    signed_update_size = (
        compressed_size
        + RSA_2048_SIGNATURE_TRAILER_BYTES
        + COMPRESSOR_VARIANCE_GUARD_BYTES
    )
    free_sketch_space = TARGET_FS_START - _align_up(firmware_size)
    update_capacity = _align_down(free_sketch_space - FLASH_SECTOR_SIZE)
    staging_margin = update_capacity - signed_update_size
    if staging_margin < MINIMUM_OTA_MARGIN_BYTES:
        raise RuntimeError(
            "Sonoff RF Bridge signed compressed image does not leave the required "
            f"{MINIMUM_OTA_MARGIN_BYTES}-byte OTA margin: {signed_update_size} required, "
            f"{update_capacity} available."
        )

    print(
        "Sonoff RF Bridge OTA layout: "
        f"firmware={firmware_size}, signed_update={signed_update_size}, "
        f"staging_margin={staging_margin} bytes"
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", validate_ota_layout)
