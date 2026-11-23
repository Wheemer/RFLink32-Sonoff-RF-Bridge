"""Exercise the production OTA acknowledgement and update paths with faults."""

import subprocess
import tempfile
from pathlib import Path

update = Path("RFLink/13_Update.cpp").read_text()
portal = Path("RFLink/12_Portal_ws.cpp").read_text()
handoff = update[update.index("  void beginHandoff()"):update.index("  bool isPending()")]
reply = portal[portal.index("            const bool replyQueued"):
               portal.index("            activeJob.type = WsJobType::None;",
                            portal.index("            const bool replyQueued"))]

prefix = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
bool updatePending, updateHandoffReady, stopped, prepared, fetchOk, replyOk;
unsigned long now, updateStartMillis;
constexpr unsigned long UPDATE_START_DELAY_MS = 1200;
char pendingUpdateUrl[128], updateLastError[128];
int writes, failWrite, fetches, reboots, sources;
enum class UpdateResult { Fetching, Failed, Staged };
bool persistState(UpdateResult, int32_t, const char *) { return ++writes != failWrite; }
void updatePhaseDetail(char *, size_t, const char *) {}
bool fetchAndApplyUpdate(const char *url, char *, size_t, int32_t &) {
  assert(prepared && stopped && std::strcmp(url, "http://example.test/fw") == 0);
  ++fetches;
  return fetchOk;
}
unsigned long millis() { return now; }
void yield() {}
struct SerialMock {
  void println(const char *) {}
  void print(const char *) {}
} Serial;
#define F(x) x
struct EspMock { void restart() { ++reboots; } } ESP;
namespace RFLink {
enum class RebootSource { SignedUpdate };
void markRebootSource(RebootSource) { ++sources; }
namespace Signal {
struct SignalData { bool readyForDecoder; int Number, Time; } RawSignal;
namespace AsyncSignalScanner {
int nextPulseTimeoutTime_us;
bool isStopped() { return stopped; }
void stopScanning() { stopped = true; }
void startScanning() { stopped = false; }
}
}
namespace Portal { void prepareForUpdate() { prepared = true; } }
namespace UpdateSecurity {
"""

tests = r"""
enum class WsJobType { Update };
struct { WsJobType type; } activeJob{WsJobType::Update};
int client, wsStatusDoc;
bool wsSendJsonToClient(int, int) { return replyOk; }
void reset() {
  updatePending = updateHandoffReady = stopped = prepared = false;
  fetchOk = replyOk = true;
  now = 100;
  writes = failWrite = fetches = reboots = sources = 0;
  std::strcpy(pendingUpdateUrl, "http://example.test/fw");
}
int main() {
  using RFLink::UpdateSecurity::maintain;
  reset();
  replyOk = false;
  sendReply();
  now += 5000;
  maintain();
  assert(writes == 1 && !fetches && !prepared && !reboots && !pendingUpdateUrl[0]);
  for (int fault : {0, 1, 2, 3, 4}) {
    reset();
    failWrite = fault;
    fetchOk = fault != 4;
    sendReply();
    maintain();
    assert(!writes && !prepared);
    now += UPDATE_START_DELAY_MS;
    maintain();
    const bool staged = fault == 0 || fault == 3;
    assert(reboots == staged && sources == staged);
    assert(prepared == (fault != 1));
    assert(fetches == (fault != 1 && fault != 2));
    assert(stopped == staged);
    assert(!updatePending);
  }
}
"""

with tempfile.TemporaryDirectory(prefix="rfbridge-ota-handoff-") as directory:
    path = Path(directory)
    source = path / "check.cpp"
    declarations, main = tests.split("int main()", 1)
    source.write_text(
        f"#include <initializer_list>\n{prefix}{handoff}}}}}\n"
        f"{declarations}\nvoid sendReply() {{\n{reply}}}\nint main(){main}"
    )
    binary = path / "check"
    subprocess.run(
        ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
print("production OTA handoff passed enqueue, persistence, HTTP and reboot fault cases")
