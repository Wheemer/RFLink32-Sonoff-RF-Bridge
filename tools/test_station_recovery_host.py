"""Run the production retry scheduler against a mocked station."""
import re
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/10_Wifi.cpp").read_text()
constants = "\n".join(re.findall(r"const unsigned long STATION_RECONNECT_[A-Z_]+ = \d+;", source))
scheduler = source.split("void clearStationReconnectSchedule()", 1)[1].split("void configureClientWifi()", 1)[0]
retry = source.split("void maintainStationReconnect()", 1)[1].split("void resetClientWifi()", 1)[0]
mock = r"""
#include <cassert>
#include <cstdint>
#include <string>
#define ESP8266
uint32_t now, stationReconnectAt;
int disconnects, joins;
bool ota;
namespace params { bool client_enabled = true; std::string client_ssid = "test"; }
namespace RFLink { namespace UpdateSecurity { bool isPending() { return ota; } } }
struct {
  bool connected = true;
  bool isConnected() { return connected; }
  void disconnect(bool disable, bool erase) {
    assert(!connected && !disable && !erase && disconnects == joins);
    ++disconnects;
  }
} WiFi;
uint32_t millis() { return now; }
void startClientAssociation() { assert(disconnects == joins + 1); ++joins; }
"""
tests = r"""
int main() {
  maintainStationReconnect();
  assert(!stationReconnectAt && !joins);
  WiFi.connected = false;
  now = UINT32_MAX - 100;
  maintainStationReconnect();
  assert(stationReconnectAt == 14899);
  now = 14898;
  maintainStationReconnect();
  assert(!joins);
  ++now;
  ota = true;
  maintainStationReconnect();
  assert(!joins);
  ota = false;
  for (int count = 1; count <= 20; ++count) {
    now = stationReconnectAt;
    maintainStationReconnect();
    assert(joins == count && disconnects == count);
    assert(stationReconnectAt - now == 30000);
    maintainStationReconnect();
    assert(joins == count);
  }
  WiFi.connected = true;
  maintainStationReconnect();
  assert(!stationReconnectAt && joins == 20);
  WiFi.connected = false;
  params::client_ssid.clear();
  maintainStationReconnect();
  assert(!stationReconnectAt && joins == 20);
}
"""
with tempfile.TemporaryDirectory(prefix=".station-recovery-", dir=Path.cwd()) as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(f"{mock}\n{constants}\nvoid clearStationReconnectSchedule(){scheduler}"
                   f"\nvoid maintainStationReconnect(){retry}\n{tests}")
    subprocess.run(["g++", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("production station recovery: bounded retry cadence, credential preservation and OTA exclusion passed")
