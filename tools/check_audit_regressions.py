"""Structural regression checks; these do not replace hardware fault injection."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source(name):
    return (ROOT / "RFLink" / name).read_text()


wifi = source("10_Wifi.cpp")
disconnect = wifi.split("void processStationDisconnected()", 1)[1].split(
    "void startMdnsDiscovery()", 1
)[0]
assert "clearStationReconnectSchedule();" not in disconnect
assert "if(stationReconnectAt == 0)" in disconnect
assert "STATION_RECONNECT_INITIAL_MS = 15000" in wifi
assert "stationReconnectAttempts < 255" in wifi
retry = wifi.split("void maintainStationReconnect()", 1)[1].split(
    "void resetClientWifi()", 1
)[0]
assert "scanNetworks" not in retry
assert "persistent(true)" not in wifi
ws = source("12_Portal_ws.cpp")
assert "!jobExecuting" in ws
assert ws.index("jobExecuting = true;", ws.index("void processWebSocketJobs")) < ws.index(
    "::RFLink::executeCliCommand(activeJob.command)"
)
assert "Learn duration exceeds 300 seconds" in ws
assert "wsFrameReceived != info->len" in ws
assert "info->index != wsFrameReceived" in ws
assert "info->num != wsFrameNumber" in ws
assert "measureJson(doc) >= sizeof(wsOutbound)" in ws
config = source("11_Config.cpp")
assert "bytes_written != expected_bytes" in config
assert "Recovered configuration from interrupted save backup" in config
assert "Filesystem mount failed; preserving existing flash data" in config
assert "words[index] != 0xFFFFFFFFUL" in config
ota = source("13_Update.cpp")
failure = ota.split('"Could not persist OTA updater state"', 1)[1].split(
    "int32_t errorCode", 1
)[0]
assert "startScanning()" in failure
signal = source("2_Signal.cpp")
assert "errno == ERANGE" in signal
assert "pulse > 65535UL * 255UL" in signal
assert "pulse == 0 && ((count % 2) != 0" in signal
assert "if(seconds > 300)" in source("RFLink.cpp")
print("audit_structural_regressions=passed (hardware behavior not verified)")
