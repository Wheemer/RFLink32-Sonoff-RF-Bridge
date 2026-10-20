"""Generate the PROGMEM gzip asset used by the AP-only Wi-Fi setup portal."""

from __future__ import annotations

import gzip
import sys
from pathlib import Path


def render(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 12):
        rows.append("  " + ", ".join(f"0x{byte:02x}" for byte in data[offset : offset + 12]) + ",")
    return "\n".join(rows)


source = Path(sys.argv[1])
target = Path(sys.argv[2])
compressed = gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(
    "#pragma once\n\n"
    "#include <Arduino.h>\n\n"
    "// Generated from WifiSetupPage.html. Do not edit by hand.\n"
    "static const uint8_t wifiSetupPageGzip[] PROGMEM = {\n"
    + render(compressed)
    + "\n};\n\n"
    f"static constexpr size_t wifiSetupPageGzipLength = {len(compressed)};\n",
    encoding="ascii",
)
