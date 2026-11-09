"""Exercise production capture-retention allocation and ownership paths."""
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/12_Portal_ws.cpp").read_text()
start = source.index("        bool broadcastRawRfEvent(")
end = source.index("        void cleanupWebSocketClients()", start)
function = source[start:end]
prefix = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
using byte = unsigned char;
char wsOutbound[2300];
char *pendingLearnEvent = nullptr;
uint32_t pendingLearnSequence = 0;
bool allocationFails = false;
unsigned int allocations = 0;
void *captureMalloc(size_t bytes) {
  allocations++;
  return allocationFails ? nullptr : std::malloc(bytes);
}
#define malloc captureMalloc
struct {
  unsigned int clients = 0;
  unsigned int sent = 0;
  bool availableForWriteAll() { return true; }
  unsigned int count() { return clients; }
  void textAll(const char *) { sent++; }
} apiWebSocket;
bool buildRawRfEventJson(uint32_t, unsigned long, int, byte, byte, byte, int,
                        bool, const uint16_t *, bool) {
  strcpy(wsOutbound, "{\"type\":\"rf_event\",\"event\":{\"command\":\"test\"}}");
  return true;
}
'''
tests = r'''
int main() {
  uint16_t pulses[3] = {0,100,200};
  allocationFails = true;
  assert(!broadcastRawRfEvent(1,0,2,0,0,1,0,false,pulses));
  assert(pendingLearnEvent == nullptr && pendingLearnSequence == 0);
  allocationFails = false;
  assert(broadcastRawRfEvent(2,0,2,0,0,1,0,false,pulses));
  assert(pendingLearnEvent && pendingLearnSequence == 2 && apiWebSocket.sent == 0);
  assert(strstr(pendingLearnEvent, "\"learn_capture\":true"));
  unsigned int count = allocations;
  assert(!broadcastRawRfEvent(3,0,2,0,0,1,0,false,pulses));
  assert(allocations == count && pendingLearnSequence == 2);
  free(pendingLearnEvent);
  pendingLearnEvent = nullptr;
  apiWebSocket.clients = 1;
  assert(broadcastRawRfEvent(4,0,2,0,0,1,0,false,pulses));
  assert(apiWebSocket.sent == 1 && pendingLearnSequence == 4);
  free(pendingLearnEvent);
}
'''
with tempfile.TemporaryDirectory(prefix="rfbridge-lowmem-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(prefix + function + tests)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(path / "test.cpp"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("production_learn_low_memory_tests=passed")
