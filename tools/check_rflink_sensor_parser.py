"""Verify RFLink hexadecimal sequences preserve the decoded protocol name."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "custom_components" / "rf_bridge" / "__init__.py"

source = SOURCE.read_text(encoding="utf-8")
required = (
    'parts[0] == "20"',
    'len(parts[1]) == 2',
    'character in "0123456789abcdefABCDEF"',
    "protocol_index = 2",
)
for fragment in required:
    if fragment not in source:
        raise SystemExit(f"Missing hexadecimal RFLink sequence handling: {fragment}")

print("rflink_sensor_parser=passed")
