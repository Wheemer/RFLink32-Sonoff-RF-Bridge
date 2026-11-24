"""Run the production station retry loop with a simulated ESP8266."""

import re
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/10_Wifi.cpp").read_text()
constants = "\n".join(
    re.findall(r"^\s*(?:constexpr|const) [^;]+ STATION_[A-Z_]+[^;]*;", source, re.MULTILINE)
)
scheduler = source[source.index("        void clearStationReconnectSchedule()"):
                   source.index("        void configureClientWifi()")]
retry = source[source.index("        void maintainStationReconnect()"):
               source.index("        void resetClientWifi()")]
mock = r"""
#include <cassert>
#include <cstdint>
#include <string>
#define ESP8266
#define F(x) x
uint32_t now;
unsigned long stationReconnectAt;
uint8_t stationReconnectAttempts;
bool stationCyclePending, stationCycleUsed, stationStableTimerArmed, ota;
bool stationAssociated, stationDhcpRetried;
unsigned dhcpStops, dhcpStarts;
void wifi_station_dhcpc_stop() { ++dhcpStops; }
void wifi_station_dhcpc_start() { ++dhcpStarts; }
uint32_t stationConnectedAt;
#define WIFI_STA 1
uint32_t stationCycles, joins, configs;
namespace params {
bool client_enabled = true, client_dhcp_enabled = true;
std::string client_ssid = "test";
}
namespace RFLink { namespace UpdateSecurity { bool isPending() { return ota; } } }
struct {
  bool connected = false, station = true, ap = true, allowEnable = true;
  bool isConnected() { return connected; }
  int getMode() { return (station ? WIFI_STA : 0) | (ap ? 2 : 0); }
  bool enableSTA(bool enable) {
    if (enable && !allowEnable) return false;
    station = enable;
    return true;
  }
} WiFi;
uint32_t millis() { return now; }
void configureClientWifi() { assert(WiFi.station); ++configs; }
void startClientAssociation() { assert(WiFi.station); ++joins; }
"""
tests = r"""
int main() {
  maintainStationReconnect();
  assert(stationReconnectAt == 15000 && joins == 0);
  for (uint32_t expected = 1; expected <= 3; ++expected) {
    now = stationReconnectAt - 1;
    maintainStationReconnect();
    assert(joins == expected - 1);
    ++now;
    maintainStationReconnect();
    assert(joins == expected && WiFi.station && WiFi.ap);
  }
  now = stationReconnectAt;
  ota = true;
  maintainStationReconnect();
  assert(!stationCycles && WiFi.station);
  ota = false;
  maintainStationReconnect();
  assert(stationCycles == 1 && !WiFi.station && WiFi.ap);
  now += 999;
  maintainStationReconnect();
  assert(!WiFi.station && joins == 3);
  ++now;
  WiFi.allowEnable = false;
  maintainStationReconnect();
  assert(stationCyclePending && !WiFi.station && joins == 3);
  WiFi.allowEnable = true;
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(!stationCyclePending && WiFi.station && WiFi.ap && joins == 4 && configs == 1);
  for (int i = 0; i != 12; ++i) {
    now = stationReconnectAt;
    maintainStationReconnect();
    assert(stationCycles == 1 && WiFi.station);
  }
  assert(joins == 16);
  WiFi.connected = true;
  maintainStationReconnect();
  assert(!stationReconnectAt && !stationReconnectAttempts);
  now += STATION_STABLE_MS - 1;
  maintainStationReconnect();
  assert(stationCycleUsed);
  WiFi.connected = false;
  maintainStationReconnect();
  assert(stationCycleUsed && !stationStableTimerArmed);
  WiFi.connected = true;
  maintainStationReconnect();
  now += STATION_STABLE_MS;
  maintainStationReconnect();
  assert(!stationCycleUsed);
  WiFi.connected = false;
  params::client_ssid.clear();
  maintainStationReconnect();
  assert(!stationReconnectAt && joins == 16);
  params::client_ssid = "test";
  now = UINT32_MAX - 100;
  maintainStationReconnect();
  assert(static_cast<uint32_t>(stationReconnectAt) == 14899);
  now = 14898;
  maintainStationReconnect();
  assert(joins == 16);
  ++now;
  maintainStationReconnect();
  assert(joins == 17);
  stationAssociated = true;
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(stationDhcpRetried && dhcpStops == 1 && dhcpStarts == 1 && joins == 17);
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(dhcpStops == 1 && dhcpStarts == 1 && joins == 18);
  params::client_dhcp_enabled = false;
  stationDhcpRetried = false;
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(dhcpStarts == 1 && joins == 19);
}
"""
with tempfile.TemporaryDirectory(prefix=".station-recovery-", dir=Path.cwd()) as directory:
    path = Path(directory)
    cpp = path / "test.cpp"
    binary = path / "test"
    cpp.write_text(f"{mock}\n{constants}\n{scheduler}\n{retry}\n{tests}")
    subprocess.run(["g++", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("production station recovery: retries, AP preservation and OTA exclusion passed")
