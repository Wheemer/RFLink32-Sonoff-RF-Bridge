"""Exercise production long-pulse deadlines with SDK overhead and clock rollover."""
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
  assert(clockUs == 100000 && yields > 0 && !irq);
  clockUs = 0; yields = 0;
  delayLongPulse(400);
  assert(clockUs == 400 && yields == 0);
  clockUs = UINT32_MAX - 1000; uint32_t start = clockUs;
  delayLongPulse(90000);
  assert(uint32_t(clockUs - start) == 90000 && !irq);
  enabled = 0; clockUs = 0;
  delayLongPulse(4200);
  assert(enabled == 1 && clockUs == 4200 && !irq);
}
"""
with tempfile.TemporaryDirectory(prefix="rfbridge-pulse-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(prefix + function + tests)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("production_pulse_deadline_tests=passed")
