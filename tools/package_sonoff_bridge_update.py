"""Build and verify the exact signed OTA artifact for the modified RF Bridge."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

FLASH_SECTOR_SIZE = 0x1000
OTA_FS_START = 0xF3000
RSA_2048_SIGNATURE_BYTES = 256
RSA_2048_SIGNATURE_TRAILER_BYTES = RSA_2048_SIGNATURE_BYTES + 4
MINIMUM_OTA_MARGIN_BYTES = 16 * 1024


def align_up(value: int) -> int:
    return (value + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)


def align_down(value: int) -> int:
    return value & ~(FLASH_SECTOR_SIZE - 1)


def update_capacity(firmware_size: int) -> int:
    free_sketch_space = OTA_FS_START - align_up(firmware_size)
    return align_down(free_sketch_space - FLASH_SECTOR_SIZE)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--signing-tool", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--version")
    parser.add_argument("--artifact-url")
    parser.add_argument("--release-url")
    parser.add_argument("--release-notes", type=Path)
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    private_key = args.private_key.resolve()
    signing_tool = args.signing_tool.resolve()
    output_dir = args.output_dir.resolve()
    if not firmware.is_file() or not private_key.is_file() or not signing_tool.is_file():
        raise SystemExit("Firmware, private key, and signing tool must be existing files.")

    output_dir.mkdir(parents=True, exist_ok=True)
    compressed = output_dir / f"{firmware.name}.gz"
    signed = output_dir / f"{firmware.name}.gz.signed"

    with compressed.open("wb") as output_file:
        subprocess.run(["gzip", "-9", "-n", "-c", str(firmware)], check=True, stdout=output_file)
    subprocess.run(
        [
            "python3",
            str(signing_tool),
            "--mode",
            "sign",
            "--privatekey",
            str(private_key),
            "--bin",
            str(compressed),
            "--out",
            str(signed),
        ],
        check=True,
    )

    signed_size = signed.stat().st_size
    staging_capacity = update_capacity(firmware.stat().st_size)
    staging_margin = staging_capacity - signed_size
    if staging_margin < MINIMUM_OTA_MARGIN_BYTES:
        raise SystemExit(
            "Exact signed artifact does not leave the required "
            f"{MINIMUM_OTA_MARGIN_BYTES}-byte OTA staging margin."
        )

    with signed.open("rb") as signed_file:
        signed_file.seek(-4, 2)
        signature_size = struct.unpack("<I", signed_file.read(4))[0]
    if signature_size != RSA_2048_SIGNATURE_BYTES:
        raise SystemExit(f"Unexpected signature length: {signature_size}")
    if signed_size != compressed.stat().st_size + RSA_2048_SIGNATURE_TRAILER_BYTES:
        raise SystemExit("Signed artifact does not have the expected gzip-plus-signature layout.")

    if bool(args.version) != bool(args.artifact_url):
        raise SystemExit("--version and --artifact-url must be provided together.")
    if args.version:
        if not all(character.isalnum() or character in ".-_" for character in args.version):
            raise SystemExit("Version may only contain letters, numbers, dots, dashes, and underscores.")
        if not args.artifact_url.startswith("https://"):
            raise SystemExit("Artifact URL must use HTTPS.")
        notes = None
        if args.release_notes:
            notes = args.release_notes.read_text(encoding="utf-8")
        manifest = {
            "target": "modified_sonoff_rf_bridge",
            "version": args.version,
            "url": args.artifact_url,
            "sha256": hashlib.sha256(signed.read_bytes()).hexdigest(),
        }
        if args.release_url:
            manifest["release_url"] = args.release_url
        if notes:
            manifest["release_notes"] = notes
        manifest_path = output_dir / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"manifest={manifest_path}")

    print(f"compressed={compressed}")
    print(f"signed={signed}")
    print(f"signed_bytes={signed_size}")
    print(f"ota_staging_capacity_bytes={staging_capacity}")
    print(f"ota_staging_margin_bytes={staging_margin}")


if __name__ == "__main__":
    main()
