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
retry = wifi.split("void maintainStationReconnect()", 1)[1].split(
    "void resetClientWifi()", 1
)[0]
assert "scanNetworks" not in retry
assert "persistent(true)" not in wifi
assert "WiFi.setAutoConnect(true)" not in wifi
assert "WiFi.setAutoReconnect(true)" not in wifi
assert retry.index("RFLink::UpdateSecurity::isPending()") < retry.index("WiFi.disconnect(false, false)")
assert "WiFi.enableSTA" not in retry
assert retry.index("stationAssociated && params::client_dhcp_enabled") < retry.index("wifi_station_dhcpc_stop()")
assert retry.index("stationDhcpRetriedFor != association") < retry.index("wifi_station_dhcpc_start()")
ap_persist = wifi.split("bool persistAccessPointDisabledAfterClientConnect()", 1)[1].split(
    "void shutdownSetupAccessPointIfReady()", 1
)[0]
assert "if(!Config::saveConfigToFlash()) {\n              item->setBoolValue(true);" in ap_persist
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
ws = Path("RFLink/12_Portal_ws.cpp").read_text()
raw_serialization = ws[ws.index("int commandPulseCount"):ws.index("void wsSendHello")]
assert "yield();" not in raw_serialization, "Shared WebSocket output buffer can be overwritten at yield"
assert "if(error || !doc.is<JsonObject>())" in config
assert 'output["uptime"] = micros64() / 1000000ULL;' in Path("RFLink/RFLink.cpp").read_text()
commit = config.index('LittleFS.rename(F("/tmp.json"), configFileName)')
cleanup = config.index("LittleFS.remove(backupFileName)")
assert commit < cleanup, "Preserve the recovery backup until replacement succeeds"
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
