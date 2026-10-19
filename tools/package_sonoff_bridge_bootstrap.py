"""Sign and validate the raw OTA bootstrap for the modified RF Bridge."""

from __future__ import annotations

import argparse
import struct
import subprocess
from pathlib import Path

from package_sonoff_bridge_update import (
    MINIMUM_OTA_MARGIN_BYTES,
    RSA_2048_SIGNATURE_BYTES,
    RSA_2048_SIGNATURE_TRAILER_BYTES,
)

# The bridge currently runs the full RFLink image. Its OTA partition starts
# below that image's aligned end, so bootstrap admissibility must be measured
# against the live full-image staging capacity, not the smaller bootstrap.
CURRENT_RF_BRIDGE_STAGING_CAPACITY_BYTES = 425_984


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--signing-tool", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    private_key = args.private_key.resolve()
    signing_tool = args.signing_tool.resolve()
    output = args.output.resolve()
    if not firmware.is_file() or not private_key.is_file() or not signing_tool.is_file():
        raise SystemExit("Firmware, private key, and signing tool must be existing files.")

    raw = firmware.read_bytes()
    if not raw or raw[0] != 0xE9:
        raise SystemExit("Bootstrap must be a raw ESP8266 firmware image, not a gzip payload.")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "python3",
            str(signing_tool),
            "--mode",
            "sign",
            "--privatekey",
            str(private_key),
            "--bin",
            str(firmware),
            "--out",
            str(output),
        ],
        check=True,
    )

    signed = output.read_bytes()
    if len(signed) != len(raw) + RSA_2048_SIGNATURE_TRAILER_BYTES:
        raise SystemExit("Bootstrap signed artifact has an unexpected size.")
    signature_size = struct.unpack("<I", signed[-4:])[0]
    if signature_size != RSA_2048_SIGNATURE_BYTES:
        raise SystemExit("Bootstrap signed artifact has an unexpected signature length.")
    margin = CURRENT_RF_BRIDGE_STAGING_CAPACITY_BYTES - len(signed)
    if margin < MINIMUM_OTA_MARGIN_BYTES:
        raise SystemExit("Bootstrap does not leave the required OTA staging margin.")

    print(f"signed={output}")
    print(f"signed_bytes={len(signed)}")
    print(f"ota_staging_capacity_bytes={CURRENT_RF_BRIDGE_STAGING_CAPACITY_BYTES}")
    print(f"ota_staging_margin_bytes={margin}")


if __name__ == "__main__":
    main()
