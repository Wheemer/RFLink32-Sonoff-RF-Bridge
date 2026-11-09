"""Execute the production raw emitter and check every GPIO edge, not TX counters."""
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/2_Signal.cpp").read_text()
start = source.index("    void delayLongPulse(")
end = source.index("    const char *skipJsonWhitespace", start)
prefix = r'''
#include <cstdint>
#include <cassert>
#include <vector>
#include <utility>
#include <stdexcept>
using byte = unsigned char;
uint32_t clockUs = 0, sdkUs = 0;
bool irq = false;
void (*timerFn)() = nullptr;
uint32_t timerAt = 0;
bool timerEnabled = false;
bool timerNmi = false;
constexpr int TIM_DIV16 = 1, TIM_EDGE = 0, TIM_SINGLE = 0;
#define IRAM_ATTR
#define microsecondsToClockCycles(us) (static_cast<uint32_t>(us) * 80U)
void timer1_attachInterrupt(void (*fn)()) { timerFn = fn; }
void timer1_detachInterrupt() { timerFn = nullptr; }
void timer1_enable(int, int, int) { timerEnabled = true; }
void timer1_disable() { timerEnabled = false; }
void timer1_write(uint32_t ticks) { timerAt = clockUs + ((ticks + 4) / 5); }
void ETS_FRC_TIMER1_INTR_ATTACH(void*, void*) {}
void ETS_FRC_TIMER1_NMI_INTR_ATTACH(void (*fn)()) {
 timerFn = fn; timerNmi = fn != nullptr;
#ifdef TEST_NORMAL_IRQ
 timerNmi = false;
#endif
}
void timer1_isr_init() {}
std::vector<std::pair<int, uint32_t>> edges;
uint32_t micros() { return clockUs; }
unsigned long millis() { return clockUs / 1000; }
void delayMicroseconds(unsigned long us) { clockUs += us; }
void delay(unsigned long ms) { clockUs += ms * 1000; }
void interrupts() { irq = true; }
void noInterrupts() { irq = false; }
void yield() {
 assert(irq);
 if(!timerFn || !timerEnabled) { clockUs += sdkUs; return; }
 const uint32_t end = clockUs + (sdkUs ? sdkUs : 1);
 if(!timerNmi) {
  // Model SDK critical sections deferring the ordinary interrupt until resume.
  clockUs = end;
  if(timerEnabled && static_cast<int32_t>(timerAt - end) <= 0) timerFn();
  return;
 }
 while(timerEnabled && static_cast<int32_t>(timerAt - end) <= 0) {
  clockUs = timerAt;
  timerFn();
 }
 clockUs = end;
}
struct {
 void wdtFeed() {}
 uint32_t getCycleCount() { throw std::runtime_error("CCOUNT is not a wall clock"); }
} ESP;
constexpr int HIGH = 1, LOW = 0;
void digitalWrite(int, int value) { edges.emplace_back(value, clockUs); }
struct GpioRegister {
 int level;
 void operator=(uint32_t) { edges.emplace_back(level, clockUs); }
} GPOS{HIGH}, GPOC{LOW};
namespace Radio {
enum class States { Radio_TX, Radio_RX };
namespace pins { constexpr int TX_DATA = 5; }
void set_Radio_mode(States) {}
}
namespace runtime {
unsigned long txStartedCount, txLastStartedMs, txLastFinishedMs, txLastElapsedMs;
unsigned long txLastPulses, txLastRepeats, txLastDelay, txLastMultiply;
unsigned long txFinishedCount;
char txLastError[48];
}
struct RawSignalStruct {
 int Number = 0; uint16_t Repeats = 0, Delay = 80;
 byte Multiply = 1; uint16_t Pulses[513] = {};
};
'''
tests = r'''
int main() {
 RawSignalStruct s;
 for(int frame = 0; frame < 3; frame++) {
  for(int bit = 0; bit < 24; bit++) {
   s.Pulses[++s.Number] = bit % 2 ? 919 : 295;
   s.Pulses[++s.Number] = bit % 2 ? 361 : 985;
  }
  s.Pulses[++s.Number] = 295; s.Pulses[++s.Number] = 9917;
 }
 for(uint32_t overhead : {0U, 250U, 3000U}) {
#ifdef TEST_NORMAL_IRQ
  if(overhead != 0) continue;
#endif
  clockUs = UINT32_MAX - 1000; sdkUs = overhead; edges.clear(); irq = true;
  const uint32_t start = clockUs;
  RawSendRF(&s);
  assert(edges.size() == static_cast<unsigned>(s.Number));
  uint32_t expected = edges.front().second;
  assert(uint32_t(expected - start) <= 1);
  for(int i = 0; i < s.Number; i++) {
   assert(edges[i].first == (i % 2 ? LOW : HIGH));
   assert(edges[i].second == expected);
   expected += s.Pulses[i + 1];
  }
  assert(static_cast<int32_t>(clockUs - expected) >= 0 && irq);
 }
 // A scheduler suspension longer than a sync gap still corrupts the waveform.
 sdkUs = 12000; clockUs = 0; edges.clear(); irq = true; RawSendRF(&s);
 uint32_t expected = edges.front().second; unsigned mismatches = 0;
 for(int i = 0; i < s.Number; i++) {
  if(edges[i].second != expected) mismatches++;
  expected += s.Pulses[i + 1];
 }
#ifdef ESP8266
#ifdef TEST_NORMAL_IRQ
 assert(mismatches > 0);
 return 0;
#endif
 assert(mismatches == 0);
 assert(timerFn == nullptr);
 // Long-mark signals retain the existing path.
 s.Pulses[1] = 9147;
 assert(!useTimedShortMarks(&s));
 s.Pulses[1] = 295;
 assert(useTimedShortMarks(&s));
 // Long recorded tails must not divert otherwise short-mark frames to bitbang.
 s.Pulses[s.Number - 1] = 11067; s.Pulses[s.Number] = 49260;
 assert(useTimedShortMarks(&s));
 clockUs = 0; edges.clear(); RawSendRF(&s);
 assert(edges.size() == static_cast<unsigned>(s.Number));
 assert(edges.back().second - edges[edges.size() - 2].second == 11067);
 assert(clockUs - edges.back().second >= 49260);
 s.Pulses[s.Number - 1] = 10738;
 clockUs = 0; edges.clear(); RawSendRF(&s);
 assert(edges.back().second - edges[edges.size() - 2].second == 10738);
 s.Pulses[s.Number] = 1000;
 assert(!useTimedShortMarks(&s));
 s.Pulses[s.Number] = 49260;
 s.Pulses[1] = 9147;
 assert(!useTimedShortMarks(&s));
 s.Pulses[1] = 295;
 // Respect repeat metadata and repeat delays without extra command repetitions.
 s.Number = 2; s.Pulses[1] = 300; s.Pulses[2] = 0;
 s.Repeats = 2; s.Delay = 80; clockUs = 0; edges.clear();
 RawSendRF(&s);
 assert(edges.size() == 6);
 const uint32_t origin = edges[0].second;
 assert(edges[1].second - origin == 300);
 assert(edges[2].second - origin == 80300 && edges[3].second - origin == 80600);
 assert(edges[4].second - origin == 160600 && edges[5].second - origin == 160900);
 // The timer register is only 23 bits: long spaces require early callbacks.
 s.Number = 2; s.Pulses[1] = 8; s.Pulses[2] = 50000;
 s.Multiply = 40; s.Repeats = 0; clockUs = UINT32_MAX - 1000; edges.clear();
 RawSendRF(&s);
 assert(edges.size() == 2);
 assert(uint32_t(edges[1].second - edges[0].second) == 320);
 assert(uint32_t(clockUs - edges[1].second) >= 2000000);
 assert(!timerEnabled && timerFn == nullptr);
#else
 assert(mismatches > 0);
#endif
}
'''
with tempfile.TemporaryDirectory(prefix="rfbridge-edges-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(prefix + source[start:end] + tests)
    for defines in ([], ["-DESP8266"], ["-DESP8266", "-DTEST_NORMAL_IRQ"]):
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                        *defines, str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
print("raw_edges=legacy_and_normal_irq_corruption_reproduced; nmi_edges_exact_under_sdk_stalls")
