"""Run the production retry scheduler against a mocked station."""
import re
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/10_Wifi.cpp").read_text()
constants = "\n".join(re.findall(r"const unsigned long STATION_RECONNECT_[A-Z_]+ = \d+;", source))
scheduler = source.split("void clearStationReconnectSchedule()", 1)[1].split("void configureClientWifi()", 1)[0]
retry = source.split("void maintainStationReconnect()", 1)[1].split("void resetClientWifi()", 1)[0]
callbacks = source.split("WiFiEventHandler e1;", 1)[1].split("#endif", 1)[0]
mock = r"""
#include <cassert>
#include <cstdint>
#include <string>
#define ESP8266
uint32_t now, stationReconnectAt;
uint32_t stationAssociationStartedAt;
uint32_t stationAssociatedAt, stationAssociatedCount, stationLocalDisconnectAt;
uint32_t stationFailureReason, stationFailureAt, stationLocalLeaveCount;
uint32_t stationDhcpRetriedFor, stationDhcpRestartCount;
uint32_t stationDisconnectCount, stationDisconnectAt, stationDisconnectReason;
bool stationAssociated, stationLocalDisconnectPending, stationRetryAfterDisconnect;
bool stationGotIpPending, stationDisconnectedPending;
using WiFiEventHandler = int;
struct WiFiEventStationModeConnected {};
struct WiFiEventStationModeGotIP {};
struct WiFiEventStationModeDisconnected { int reason; };
constexpr int REASON_ASSOC_LEAVE = 8;
enum { STATION_IDLE, STATION_CONNECTING, STATION_CONNECT_FAIL };
int stationStatus = STATION_IDLE;
int wifi_station_get_connect_status() { return stationStatus; }
int disconnects, joins, dhcpStops, dhcpStarts;
bool ota;
namespace params {
bool client_enabled = true, client_dhcp_enabled = true;
std::string client_ssid = "test";
}
namespace RFLink { namespace UpdateSecurity { bool isPending() { return ota; } } }
void wifi_station_dhcpc_stop() { assert(stationAssociated); ++dhcpStops; }
void wifi_station_dhcpc_start() { assert(stationAssociated); ++dhcpStarts; }
struct {
  bool connected = true;
  bool isConnected() { return connected; }
  bool disconnect(bool disable, bool erase) {
    assert(!connected && !disable && !erase);
    ++disconnects;
    return true;
  }
} WiFi;
uint32_t millis() { return now; }
void startClientAssociation() {
  stationAssociationStartedAt = now;
  ++joins;
}
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
    assert(joins == count && !disconnects && !dhcpStarts);
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

  params::client_ssid = "test";
  stationStatus = STATION_CONNECTING;
  now = UINT32_MAX - 100;
  stationAssociationStartedAt = now;
  maintainStationReconnect();
  for (uint32_t elapsed = 15000; elapsed < 60000; elapsed += 5000) {
    now = stationAssociationStartedAt + elapsed;
    maintainStationReconnect();
    assert(joins == 20 && stationReconnectAt - now == 5000);
  }
  now = stationAssociationStartedAt + 60000;
  maintainStationReconnect();
  assert(joins == 20 && disconnects == 1);
  eventHandler_WiFiStationDisconnected({8});
  assert(stationLocalLeaveCount == 1 && !stationFailureReason);
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(joins == 21 && disconnects == 1);

  // A terminal failure need not wait for the pending-join deadline.
  stationStatus = STATION_CONNECT_FAIL;
  eventHandler_WiFiStationDisconnected({202});
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(joins == 22 && disconnects == 1 && stationFailureReason == 202);

  stationStatus = STATION_CONNECTING;
  now = stationReconnectAt - 5000;
  eventHandler_WiFiStationConnected({});
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(joins == 22 && !dhcpStarts);
  now = stationAssociatedAt + 15000;
  maintainStationReconnect();
  assert(joins == 22 && disconnects == 1 && dhcpStarts == 1 && dhcpStops == 1);
  ota = true;
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(dhcpStarts == 1 && disconnects == 1);
  ota = false;
  maintainStationReconnect();
  assert(dhcpStarts == 1 && disconnects == 2 && joins == 22);
  eventHandler_WiFiStationDisconnected({8});
  assert(stationFailureReason == 202 && stationDisconnectReason == 8);
  assert(stationLocalLeaveCount == 2);
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(joins == 23 && disconnects == 2);
  eventHandler_WiFiStationConnected({});
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(dhcpStarts == 2 && disconnects == 2);
  WiFi.connected = true;
  maintainStationReconnect();
  assert(!stationReconnectAt && disconnects == 2);

  WiFi.connected = false;
  params::client_dhcp_enabled = false;
  eventHandler_WiFiStationConnected({});
  maintainStationReconnect();
  now = stationReconnectAt;
  maintainStationReconnect();
  assert(dhcpStarts == 2);
}
"""
with tempfile.TemporaryDirectory(prefix=".station-recovery-", dir=Path.cwd()) as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(f"{mock}\n{constants}\nvoid clearStationReconnectSchedule(){scheduler}"
                   f"\nvoid maintainStationReconnect(){retry}\n{callbacks}\n{tests}")
    subprocess.run(["g++", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("station recovery: association, DHCP-only retry, delayed reconnect, reason preservation and OTA exclusion")
