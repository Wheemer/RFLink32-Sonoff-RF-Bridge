"""Exercise the production timer state machine, optionally with private commands."""
import json
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "RFLink/2_Signal.cpp").read_text()
parser = source[source.index("    const char *skipJsonWhitespace"):
                source.index("    bool getSignalFromJson")]
selector = source[source.index("      bool useCycleTimer"):
                  source.index("      void sendCycleTimed")]
fixtures = {
    "short": {"pulses": [295, 985, 295, 9917, 328, 49260], "repeat": 2, "delay": 80},
    "tail": {"pulses": [32, 1444, 985, 328, 11067, 49260], "repeat": 0},
    "zero": {"pulses": [9147, 500, 500, 0], "repeat": 7, "delay": 100},
    "long": {"pulses": [16000000, 100], "repeat": 0},
}
private = os.environ.get("RFBRIDGE_WAVEFORMS")
if private:
    fixtures.update(json.loads(Path(private).read_text()))

prefix = r"""
#include <cassert>
#include <cstdint>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#include <string>
#define IRAM_ATTR
#define RAW_BUFFER_SIZE 512
#include "RfPulseTrain.h"
struct RawSignalStruct {
  uint16_t Pulses[513] = {};
  int Number = 0;
  unsigned Multiply = 1;
};
"""
prefix += parser + selector
prefix += r"""
using Edge = std::pair<bool, uint64_t>;
void test(const std::vector<uint32_t>& input, int repeat, int gap, int cpu, int lag, int timer) {
  uint32_t multiply = 1;
  for(auto p : input)
    while((p + multiply / 2) / multiply > 65535) ++multiply;
  std::vector<uint16_t> stored(1, 0);
  for(auto p : input) stored.push_back((p + multiply / 2) / multiply);
  std::string json = "{\"pulses\":[";
  for(auto p : input) json += std::to_string(p) + ",";
  json.back() = ']'; json += "}";
  RawSignalStruct parsed;
  int count = 0; unsigned long longest = 0;
  assert(scanJsonPulses(json.c_str(), parsed, true, multiply, count, longest));
  parsed.Number = count; parsed.Multiply = multiply;
  for(int i = 1; i <= count; ++i) assert(parsed.Pulses[i] == stored[i]);
  if(timer >= 0) assert(useCycleTimer(&parsed) == bool(timer));
  RfPulseTrain s;
  s.pulses = parsed.Pulses; s.count = input.size(); s.repeats = repeat;
  s.gapUs = gap * 1000; s.multiply = multiply; s.cyclesPerUs = cpu;
  uint64_t time = UINT32_MAX - 1000;
  std::vector<Edge> edges;
  unsigned calls = 0;
  while(!s.done) {
    assert(++calls < 100000);
    auto next = s.step(static_cast<uint32_t>(time), [&](bool high) {
      edges.emplace_back(high, time);
    });
    // Other core waveforms may invoke our callback early; it must not advance.
    if(!s.done && next > 2) {
      const auto count = edges.size();
      s.step(static_cast<uint32_t>(time + next / 2), [&](bool high) {
        edges.emplace_back(high, time + next / 2);
      });
      assert(edges.size() == count);
    }
    time += next + lag * cpu;
  }
  assert(edges.size() == input.size() * (repeat + 1));
  for(std::size_t i = 0; i < edges.size(); ++i) {
    assert(edges[i].first == ((i % input.size()) % 2 == 0));
    if(i + 1 == edges.size()) break;
    uint64_t expected = uint64_t(stored[i % input.size() + 1]) * multiply * cpu;
    if((i + 1) % input.size() == 0) expected += uint64_t(gap) * 1000 * cpu;
    const auto actual = edges[i + 1].second - edges[i].second;
    assert(actual >= expected);
    // Chunks use absolute deadlines, so lateness must not accumulate per chunk.
    assert(actual <= expected + uint64_t(lag) * cpu * 2);
  }
  assert(s.maxLateCycles <= static_cast<unsigned>(lag * cpu * 2 + 1));
}
int main() {
"""
calls = []
for name, payload in fixtures.items():
    pulses = ",".join(str(p) for p in payload["pulses"])
    for cpu in (80, 160):
        for lag in (0, 5, 20, 100, 1000):
            calls.append(
                f"test({{{pulses}}}, {payload.get('repeat', 0)}, "
                f"{payload.get('delay', 0)}, {cpu}, {lag}, "
                f"{0 if name == 'fit_start' else 1 if name.startswith('hood_') else -1});"
            )
with tempfile.TemporaryDirectory(prefix="rfbridge-core-timer-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(prefix + "\n".join(calls) + "\n}")
    subprocess.run(
        ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(root / "RFLink"),
         str(path / "test.cpp"), "-o", str(path / "test")], check=True,
    )
    subprocess.run([str(path / "test")], check=True)
print(f"core_timer_state_machine=passed fixtures={list(fixtures)} cases={len(calls)}")
print("Injected lateness is measured, not proof of acceptable physical RF tolerance.")
