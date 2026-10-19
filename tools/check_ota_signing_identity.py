"""Verify that firmware and release signing use the same public key."""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--firmware-source", type=Path, required=True)
    parser.add_argument("--signing-header", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    args = parser.parse_args()

    public_key = subprocess.run(
        ["openssl", "pkey", "-in", str(args.private_key), "-pubout"],
        check=True,
        capture_output=True,
    ).stdout
    expected_key_id = hashlib.sha256(public_key).hexdigest()
    header = args.signing_header.read_text(encoding="utf-8")
    key_match = re.search(
        r"signing_pubkey\[\]\s+PROGMEM\s*=\s*{(?P<body>.*?)};",
        header,
        re.DOTALL,
    )
    if key_match is None:
        raise SystemExit("Embedded OTA signing public key is missing.")
    embedded = bytes(
        int(value, 16)
        for value in re.findall(r"0x([0-9a-fA-F]{2})", key_match.group("body"))
    )
    if not embedded or embedded[-1] != 0:
        raise SystemExit(
            "Embedded OTA signing public key must be NUL-terminated for "
            "BearSSL::PublicKey."
        )
    if embedded[:-1] != public_key:
        raise SystemExit(
            "Embedded OTA signing public key does not match the release signing key."
        )
    firmware = args.firmware.read_bytes()
    if firmware.count(embedded) != 1:
        raise SystemExit(
            "Compiled firmware does not contain exactly one embedded OTA signing key."
        )
    source = args.firmware_source.read_text(encoding="utf-8")
    match = re.search(
        r'UPDATE_SIGNING_KEY_ID\[\]\s*=\s*\n\s*"([0-9a-f]{64})"', source
    )
    if match is None:
        raise SystemExit("Firmware update signing key ID is missing or malformed.")
    if match.group(1) != expected_key_id:
        raise SystemExit(
            "Firmware update signing key ID does not match the release signing key."
        )
    print(f"signing_key_id={expected_key_id}")


if __name__ == "__main__":
    main()
