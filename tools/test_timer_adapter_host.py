"""Compile the actual NMI adapter, including its CPU-cycle/timer-tick conversion."""
import json
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "RFLink/2_Signal.cpp").read_text()
adapter = source[source.index("      struct RfPinWriter"):
                 source.index("      bool useCycleTimer")]
fixtures = {"generic": {"pulses": [32, 1444, 295, 9917, 11067, 49260], "repeat": 2, "delay": 80}}
if path := os.environ.get("RFBRIDGE_WAVEFORMS"):
    fixtures.update(json.loads(Path(path).read_text()))

code = r"""
#include <cstdint>
#include <cassert>
#include <vector>
#include <utility>
#define IRAM_ATTR
#include "RfPulseTrain.h"
uint64_t timeCycles;
uint32_t ticks;
bool disabled;
std::vector<std::pair<bool, uint64_t>> edges;
struct Register {
  bool high;
  void operator=(uint32_t mask) { assert(mask == 32); edges.emplace_back(high, timeCycles); }
};
Register GPOS{true}, GPOC{false};
namespace Radio { namespace pins { constexpr int TX_DATA = 5; } }
struct Esp { uint32_t getCycleCount() { return timeCycles; } } ESP;
RfPulseTrain pulseTrain;
uint8_t timerCycleShift;
void timer1_disable() { disabled = true; }
void timer1_write(uint32_t value) { assert(value && value <= 0x7fffff); ticks = value; }
"""
code += adapter
code += r"""
void test(std::vector<uint16_t> p, int repeat, int gap, int cpu, int late) {
  p.insert(p.begin(), 0);
  pulseTrain = RfPulseTrain{};
  pulseTrain.pulses = p.data(); pulseTrain.count = p.size() - 1;
  pulseTrain.repeats = repeat; pulseTrain.gapUs = gap * 1000; pulseTrain.cyclesPerUs = cpu;
  timerCycleShift = cpu == 160 ? 1 : 0;
  timeCycles = UINT32_MAX - 1000; edges.clear(); disabled = false;
  unsigned callbacks = 0;
  while(!disabled) {
    assert(++callbacks < 100000);
    rfTimerInterrupt();
    timeCycles += uint64_t(ticks) * cpu / 80 + late * cpu;
  }
  assert(pulseTrain.done && edges.size() == (p.size() - 1) * (repeat + 1));
  for(std::size_t i = 0; i + 1 < edges.size(); ++i) {
    assert(edges[i].first == ((i % (p.size() - 1)) % 2 == 0));
    uint64_t wanted = uint64_t(p[i % (p.size() - 1) + 1]) * cpu;
    if((i + 1) % (p.size() - 1) == 0) wanted += uint64_t(gap) * 1000 * cpu;
    const auto width = edges[i + 1].second - edges[i].second;
    assert(width >= wanted);
    assert(width <= wanted + uint64_t(late * 2 + 4) * cpu);
  }
}
int main() {
"""
for payload in fixtures.values():
    pulses = ",".join(str(p) for p in payload["pulses"])
    for cpu in (80, 160):
        for late in (0, 5, 20, 100, 1000):
            code += (
                f"test({{{pulses}}},{payload.get('repeat', 0)},"
                f"{payload.get('delay', 0)},{cpu},{late});\n"
            )
code += "}"
with tempfile.TemporaryDirectory(prefix="rfbridge-timer-adapter-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(code)
    subprocess.run(
        ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
         "-I", str(root / "RFLink"), str(path / "test.cpp"), "-o", str(path / "test")],
        check=True,
    )
    subprocess.run([str(path / "test")], check=True)
print(f"production_nmi_adapter=passed fixtures={list(fixtures)} cpu=80/160 latency=0/5/20/100/1000us")
