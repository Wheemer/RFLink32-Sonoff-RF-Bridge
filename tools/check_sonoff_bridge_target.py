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
    update = ROOT / "RFLink" / "13_Update.cpp"
    serial = ROOT / "RFLink" / "3_Serial.h"

    for setting in (
        "[env:sonoff_bridge]",
        "-D TARGET_BOARD_ESP8285",
        "-D RFLINK_USE_HARDCODED_RADIO_PINS_ONLY",
        "-D RFLINK_NO_RADIOLIB_SUPPORT",
        "-D RFLINK_SONOFF_BRIDGE_CONTINUOUS_RX",
    ):
        require(platformio, setting)

    for setting in (
        "#define PIN_RF_RX_DATA 4",
        "#define PIN_RF_TX_DATA 5",
        "#define PULLUP_RF_RX_DATA false",
    ):
        require(radio, setting)

    if "RFLINK_SONOFF_BRIDGE_RX_PULLUP" in platformio.read_text(encoding="utf-8"):
        raise SystemExit("Direct-hacked Sonoff receiver must not enable an ESP8285 pull-up")

    require(api, "#define RFLINK_API_PORT 8080")
    require(serial, "#define INPUT_COMMAND_SIZE 2000")
    if "#define SERIAL_ENABLED" in api.read_text(encoding="utf-8"):
        raise SystemExit("Sonoff target must not compile the retired runtime serial transport")
    require(websocket, "char command[INPUT_COMMAND_SIZE] = {0};")
    require(websocket, "if(info->index + len >= sizeof(activeJob.command))")
    require(wifi, "volatile bool stationGotIpPending = false;")
    require(wifi, "volatile bool stationDisconnectedPending = false;")
    require(wifi, "void processStationGotIp()")
    require(wifi, "void processStationDisconnected()")
    if wifi.read_text(encoding="utf-8").count("stationGotIpPending = true;") != 2:
        raise SystemExit("Both WiFi got-IP callbacks must defer work to the main loop")
    if wifi.read_text(encoding="utf-8").count("RFLink::Portal::notifyNetworkReady();") != 1:
        raise SystemExit("WiFi callbacks must not run API listener work directly")
    if wifi.read_text(encoding="utf-8").count("stationDisconnectedPending = true;") != 2:
        raise SystemExit("Both WiFi disconnect callbacks must defer work to the main loop")

    for call in (
        "void delayLongPulse(unsigned long duration_us)",
        "delayLongPulse(high_us);",
        "delayLongPulse(low_us);",
        "const uint32_t started_us = micros();",
        "runtime::txLastElapsedMs = (micros() - started_us) / 1000UL;",
        "uint64_t estimated_us = 0;",
        "if(estimated_ms > 120000ULL)",
        "RawSignal.Number++;\n\n        if (RawSignal.Number >= RAW_BUFFER_SIZE)",
        "(long)(micros() - AsyncSignalScanner::nextPulseTimeoutTime_us) >= 0",
        "stopScanning();\n        params::async_mode_enabled = false;",
        "if (!scanningStopped)\n          return;",
    ):
        require(signal, call)

    signal_source = signal.read_text(encoding="utf-8")
    if "static unsigned long lastChangedState_us" in signal_source:
        raise SystemExit("Async scanner must reset the timestamp used by its ISR")
    if "firstPulse && pulseLength_us < SIGNAL_MIN_PREAMBLE_US" in signal_source:
        raise SystemExit("Sonoff async capture must retain upstream first-pulse semantics")
    for required in (
        "volatile unsigned long int lastChangedState_us = 0;",
        "volatile unsigned long int nextPulseTimeoutTime_us = 0;",
        "volatile bool scanningStopped = true;",
        "RawSignal.Time = 1;",
        "RawSignal.Time = millis();",
    ):
        require(signal, required)
    isr_start = signal_source.index("void IRAM_ATTR RX_pin_changed_state()")
    isr_end = signal_source.index("void onPulseTimerTimeout()", isr_start)
    if "millis()" in signal_source[isr_start:isr_end]:
        raise SystemExit("GPIO ISR must not call millis()")

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
    require(websocket, "const uint32_t startedUs = micros();")
    require(websocket, "activeJob.elapsedMs = (micros() - startedUs) / 1000UL;")
    if "ESP.getResetReason()" in websocket.read_text(encoding="utf-8"):
        raise SystemExit("WebSocket command telemetry must not allocate reset-reason Strings")
    require(core, "const __FlashStringHelper *resetReasonText(uint32_t reason)")
    require(core, "const rst_info *resetInfo = ESP.getResetInfoPtr();")
    require(core, "RFLink::Signal::getStatusJsonString(output);")
    require(signal, "#ifdef RFLINK_SONOFF_BRIDGE_CONTINUOUS_RX")
    require(signal, "params::async_mode_enabled = true;")
    require(signal, "refreshParametersFromConfig(false);")
    require(signal, 'signal[F("async_receiver_enabled")] = params::async_mode_enabled;')
    require(signal, 'signal[F("async_receiver_scanning")] = !AsyncSignalScanner::isStopped();')
    if "RFLink::Signal::AsyncSignalScanner::enableAsyncReceiver();\n            RFLink::armRawRfLearning" in websocket.read_text(encoding="utf-8"):
        raise SystemExit("Learn must observe the continuous receiver, not enable it")

    core_source = core.read_text(encoding="utf-8")
    update_source = update.read_text(encoding="utf-8")
    if core_source.count("ESP.restart();") != 2 or update_source.count("ESP.restart();") != 1:
        raise SystemExit("Unexpected software restart path requires explicit review")
    for setting in (
        "RFLink::markRebootSource(RFLink::RebootSource::LegacyCommand);",
        "markRebootSource(scheduledRebootSource);",
    ):
        require(core, setting)
    require(update, "RFLink::markRebootSource(RFLink::RebootSource::SignedUpdate);")
    for setting in (
        'constexpr char OTA_STATE_FILE_A[] = "/ota-state-a.bin";',
        'constexpr char OTA_STATE_FILE_B[] = "/ota-state-b.bin";',
        "bool persistState(UpdateResult result, int32_t errorCode, const char *detail)",
        "if(!persistState(UpdateResult::Scheduled, 0, \"\"))",
        "updatePhaseDetail(phaseDetail, sizeof(phaseDetail), \"OTA handoff starting\");",
        "RFLink::Portal::prepareForUpdate();",
        "updatePhaseDetail(phaseDetail, sizeof(phaseDetail), \"OTA HTTP updater starting\");",
        "persistState(UpdateResult::Failed, errorCode, updateLastError);",
        "if(!persistState(UpdateResult::Staged, 0, \"Firmware staged; restarting\"))",
        "updater.closeConnectionsOnUpdate(true);",
    ):
        require(update, setting)
    for setting in (
        'output["update_last_result"] = RFLink::UpdateSecurity::lastResult();',
        'output["update_last_error_code"] = RFLink::UpdateSecurity::lastErrorCode();',
        'output["update_last_attempt"] = RFLink::UpdateSecurity::lastAttempt();',
    ):
        require(core, setting)
if __name__ == "__main__":
    main()
