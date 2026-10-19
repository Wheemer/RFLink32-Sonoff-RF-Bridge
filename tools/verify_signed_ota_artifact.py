"""Verify a signed ESP8266 OTA artifact against the embedded public key."""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import tempfile
from pathlib import Path

RSA_2048_SIGNATURE_BYTES = 256
SIGNATURE_LENGTH_BYTES = 4


def _embedded_public_key(header: Path) -> bytes:
    source = header.read_text(encoding="utf-8")
    match = re.search(
        r"signing_pubkey\[\]\s+PROGMEM\s*=\s*{(?P<body>.*?)};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise SystemExit("Embedded OTA signing public key is missing.")
    key = bytes(
        int(value, 16)
        for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group("body"))
    )
    if not key or key[-1] != 0:
        raise SystemExit("Embedded OTA signing public key is not NUL-terminated.")
    return key[:-1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    parser.add_argument("--signing-header", type=Path, required=True)
    args = parser.parse_args()

    artifact = args.artifact.read_bytes()
    if len(artifact) <= RSA_2048_SIGNATURE_BYTES + SIGNATURE_LENGTH_BYTES:
        raise SystemExit("Signed OTA artifact is too small.")
    signature_length = struct.unpack("<I", artifact[-SIGNATURE_LENGTH_BYTES:])[0]
    if signature_length != RSA_2048_SIGNATURE_BYTES:
        raise SystemExit(f"Unexpected OTA signature length: {signature_length}")

    payload_end = len(artifact) - SIGNATURE_LENGTH_BYTES - signature_length
    with tempfile.TemporaryDirectory() as temporary_directory:
        temporary = Path(temporary_directory)
        public_key = temporary / "public.pem"
        payload = temporary / "payload.bin"
        signature = temporary / "signature.bin"
        public_key.write_bytes(_embedded_public_key(args.signing_header))
        payload.write_bytes(artifact[:payload_end])
        signature.write_bytes(artifact[payload_end:-SIGNATURE_LENGTH_BYTES])
        subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-verify",
                str(public_key),
                "-signature",
                str(signature),
                str(payload),
            ],
            check=True,
        )

    print(f"verified_signed_ota_bytes={len(artifact)}")


if __name__ == "__main__":
    main()
