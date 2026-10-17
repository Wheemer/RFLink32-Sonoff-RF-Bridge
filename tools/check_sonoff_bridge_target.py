"""Reject builds that drift away from the modified Sonoff RF Bridge hardware."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(path: Path, text: str) -> None:
    if text not in path.read_text(encoding="utf-8"):
        raise SystemExit(f"{path.relative_to(ROOT)} is missing required target setting: {text}")


def main() -> None:
    platformio = ROOT / "platformio.ini"
    radio = ROOT / "RFLink" / "1_Radio.h"
    api = ROOT / "RFLink" / "RFLink.h"
    signal = ROOT / "RFLink" / "2_Signal.cpp"

    for setting in (
        "[env:sonoff_bridge]",
        "-D TARGET_BOARD_ESP8285",
        "-D RFLINK_USE_HARDCODED_RADIO_PINS_ONLY",
        "-D RFLINK_NO_RADIOLIB_SUPPORT",
    ):
        require(platformio, setting)

    for setting in (
        "#define PIN_RF_RX_DATA 4",
        "#define PIN_RF_TX_DATA 5",
    ):
        require(radio, setting)

    require(api, "#define RFLINK_API_PORT 8080")

    for call in (
        "void delayLongPulse(unsigned long duration_us)",
        "delayLongPulse(high_us);",
        "delayLongPulse(low_us);",
        "uint64_t estimated_us = 0;",
        "if(estimated_ms > 120000ULL)",
    ):
        require(signal, call)


if __name__ == "__main__":
    main()
