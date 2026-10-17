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
    display = ROOT / "RFLink" / "4_Display.cpp"
    websocket = ROOT / "RFLink" / "12_Portal_ws.cpp"
    wifi = ROOT / "RFLink" / "10_Wifi.cpp"
    core = ROOT / "RFLink" / "RFLink.cpp"

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
    require(wifi, "bool stationGotIpPending = false;")
    require(wifi, "bool stationDisconnectedPending = false;")
    require(wifi, "void processStationGotIp()")
    require(wifi, "void processStationDisconnected()")
    if wifi.read_text(encoding="utf-8").count("stationGotIpPending = true;") != 2:
        raise SystemExit("Both WiFi got-IP callbacks must defer work to the main loop")
    if wifi.read_text(encoding="utf-8").count("stationDisconnectedPending = true;") != 2:
        raise SystemExit("Both WiFi disconnect callbacks must defer work to the main loop")

    for call in (
        "void delayLongPulse(unsigned long duration_us)",
        "delayLongPulse(high_us);",
        "delayLongPulse(low_us);",
        "uint64_t estimated_us = 0;",
        "if(estimated_ms > 120000ULL)",
        "const bool firstPulse = RawSignal.Number == 0;",
        "if (firstPulse && pulseLength_us < SIGNAL_MIN_PREAMBLE_US)",
        "(long)(micros() - AsyncSignalScanner::nextPulseTimeoutTime_us) >= 0",
        "stopScanning();\n        params::async_mode_enabled = false;",
        "if (!scanningStopped)\n          return;",
    ):
        require(signal, call)

    if "static unsigned long lastChangedState_us" in signal.read_text(encoding="utf-8"):
        raise SystemExit("Async scanner must reset the timestamp used by its ISR")

    display_source = display.read_text(encoding="utf-8")
    if "strcat(pbuffer" in display_source:
        raise SystemExit("Display output must not append to pbuffer without bounds")
    for setting in (
        "static void appendToPrintBuffer(const char *input)",
        "displayMessageOverflowed = true;",
        "pbuffer[0] = 0;\n  displayMessageOverflowed = false;",
    ):
        require(display, setting)

    require(websocket, "activeJob.type == WsJobType::Command &&\n                   activeJob.clientId == client->id()")
    require(websocket, "RFLink::resetReasonText(")
    if "ESP.getResetReason()" in websocket.read_text(encoding="utf-8"):
        raise SystemExit("WebSocket command telemetry must not allocate reset-reason Strings")
    require(core, "const __FlashStringHelper *resetReasonText(uint32_t reason)")
    require(core, "const rst_info *resetInfo = ESP.getResetInfoPtr();")


if __name__ == "__main__":
    main()
