"""Reject a raw RF Bridge OTA bootstrap that cannot fit the live staging area."""

from __future__ import annotations

import argparse
from pathlib import Path

CURRENT_RF_BRIDGE_STAGING_CAPACITY_BYTES = 425_984
RSA_2048_SIGNATURE_TRAILER_BYTES = 260
MINIMUM_OTA_MARGIN_BYTES = 16 * 1024


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    image = firmware.read_bytes()
    if not image or image[0] != 0xE9:
        raise SystemExit("Bootstrap must be a raw ESP8266 firmware image.")
    signed_size = len(image) + RSA_2048_SIGNATURE_TRAILER_BYTES
    margin = CURRENT_RF_BRIDGE_STAGING_CAPACITY_BYTES - signed_size
    if margin < MINIMUM_OTA_MARGIN_BYTES:
        raise SystemExit(
            "Bootstrap signed image does not leave the required OTA staging margin."
        )
    print(f"bootstrap_raw_bytes={len(image)}")
    print(f"bootstrap_signed_bytes={signed_size}")
    print(f"ota_staging_margin_bytes={margin}")


if __name__ == "__main__":
    main()
