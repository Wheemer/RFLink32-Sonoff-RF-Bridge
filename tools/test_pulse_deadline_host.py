"""Exercise production pulse deadlines with variable SDK scheduling overhead."""
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/2_Signal.cpp").read_text()
function = source[source.index("    void delayLongPulse("):
                  source.index("    void RawSendRF(", source.index("    void delayLongPulse("))]
prefix = """
#include <cstdint>
#include <cassert>
#include <initializer_list>
uint32_t clockUs = 0;
unsigned int yields = 0;
bool irq = false;
unsigned int enabled = 0;
uint32_t sdkUs = 250;
uint32_t micros() { return clockUs; }
void delayMicroseconds(unsigned long us) { clockUs += us; }
void interrupts() { irq = true; enabled++; }
void noInterrupts() { irq = false; }
void yield() { assert(irq); clockUs += sdkUs; yields++; }
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
  assert(enabled == 0 && clockUs == 4200 && !irq);
  clockUs = 0; yields = 0;
  delayLongMark(9147);
  assert(clockUs == 9147 && yields == 0 && !irq);
  clockUs = 0; yields = 0;
  delayLongPulse(9884);
  assert(clockUs == 9884 && yields == 1 && !irq);
  for(uint32_t overhead : {0U, 250U, 1000U, 3000U, 9000U}) {
    sdkUs = overhead;
    for(uint32_t gap : {9852U, 9884U, 9917U, 49260U, 109718U}) {
      clockUs = UINT32_MAX - 1000; start = clockUs; yields = 0;
      delayLongPulse(gap);
      assert(uint32_t(clockUs - start) == gap && yields > 0 && !irq);
    }
  }
  // SDK overrun cannot be undone; do not add another full gap afterward.
  sdkUs = 12000; clockUs = 0;
  delayLongPulse(9917);
  assert(clockUs == 12000 && !irq);
  // Repeated 24-bit OOK frames must not accumulate SDK time in their gaps.
  sdkUs = 3000; clockUs = 0;
  uint32_t expected = 0;
  for(int frame = 0; frame < 32; frame++) {
    for(int bit = 0; bit < 24; bit++) {
      delayLongMark(295); delayLongPulse(985); expected += 1280;
    }
    delayLongMark(295); delayLongPulse(9917); expected += 10212;
    assert(clockUs == expected && !irq);
  }
}
"""
with tempfile.TemporaryDirectory(prefix="rfbridge-pulse-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(prefix + function + tests)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("pulse_deadline_tests=passed")
