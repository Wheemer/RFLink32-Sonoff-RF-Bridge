"""Protect the pre-1.0.34 pulse scheduling contract with simulated SDK overhead."""
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/2_Signal.cpp").read_text()
function = source[source.index("    void delayLongPulse("):
                  source.index("    void RawSendRF(", source.index("    void delayLongPulse("))]
prefix = """
#include <cstdint>
#include <cassert>
uint32_t clockUs = 0;
unsigned int yields = 0;
bool irq = false;
unsigned int enabled = 0;
uint32_t micros() { return clockUs; }
void delayMicroseconds(unsigned long us) { clockUs += us; }
void interrupts() { irq = true; enabled++; }
void noInterrupts() { irq = false; }
void yield() { assert(irq); clockUs += 250; yields++; }
struct { void wdtFeed() {} } ESP;
"""
tests = """
int main() {
  delayLongPulse(100000);
  assert(clockUs == 101750 && yields == 7 && !irq);
  clockUs = 0; yields = 0;
  delayLongPulse(400);
  assert(clockUs == 400 && yields == 0);
  clockUs = UINT32_MAX - 1000; uint32_t start = clockUs;
  delayLongPulse(90000);
  assert(uint32_t(clockUs - start) == 91500 && !irq);
  enabled = 0; clockUs = 0;
  delayLongPulse(4200);
  assert(enabled == 0 && clockUs == 4200 && !irq);
  clockUs = 0; yields = 0;
  delayLongMark(9147);
  assert(clockUs == 9147 && yields == 0 && !irq);
  clockUs = 0; yields = 0;
  delayLongPulse(9884);
  assert(clockUs == 10134 && yields == 1 && !irq);
}
"""
with tempfile.TemporaryDirectory(prefix="rfbridge-pulse-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(prefix + function + tests)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("baseline_pulse_scheduling_tests=passed")
