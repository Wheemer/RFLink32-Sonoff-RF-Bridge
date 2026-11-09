"""Compare the production emitter against the original ed800fe implementation."""
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/2_Signal.cpp").read_text()
emitter = source[source.index("    void RawSendRF("):source.index("    const char *skipJsonWhitespace")]
stock = Path("tools/fixtures/rawsend_ed800fe.cpp").read_text()
prefix = r'''
#include <cstdint>
#include <cassert>
#include <vector>
#include <utility>
using byte = unsigned char;
uint32_t clockUs = 0;
bool irq = true;
std::vector<std::pair<int, uint32_t>> edges;
uint32_t micros() { return clockUs; }
unsigned long millis() { return clockUs / 1000; }
void delayMicroseconds(unsigned long us) { assert(!irq); clockUs += us; }
void delay(unsigned long ms) { assert(irq); clockUs += ms * 1000; }
void interrupts() { irq = true; }
void noInterrupts() { irq = false; }
constexpr int HIGH = 1, LOW = 0;
void digitalWrite(int, int value) { assert(!irq); edges.emplace_back(value, clockUs); }
namespace Radio {
enum class States { Radio_TX, Radio_RX };
namespace pins { constexpr int TX_DATA = 5; }
void set_Radio_mode(States) { assert(irq); }
}
namespace runtime {
unsigned long txStartedCount, txLastStartedMs, txLastFinishedMs, txLastElapsedMs;
unsigned long txLastPulses, txLastRepeats, txLastDelay, txLastMultiply, txFinishedCount;
char txLastError[48];
}
struct RawSignalStruct {
 int Number = 0; uint16_t Repeats = 0, Delay = 80;
 byte Multiply = 1; uint16_t Pulses[513] = {};
};
'''
tests = r'''
void compare(RawSignalStruct &s) {
 clockUs = UINT32_MAX - 1000; edges.clear(); irq = true;
 StockRawSendRF(&s);
 const auto expected = edges; const auto finished = clockUs;
 clockUs = UINT32_MAX - 1000; edges.clear(); irq = true;
 RawSendRF(&s);
 assert(edges == expected && clockUs == finished && irq);
 assert(edges.size() == static_cast<unsigned>(s.Number) * (s.Repeats + 1));
}
int main() {
 RawSignalStruct s;
 for(int count : {104, 156, 180, 318, 512}) {
  s.Number = count;
  for(int i = 1; i <= count; i++) s.Pulses[i] = i % 2 ? 295 : 985;
  for(int i = 50; i < count; i += 50) s.Pulses[i] = 9917;
  s.Pulses[count] = 49260; s.Repeats = 0; compare(s);
  s.Multiply = 2; compare(s); s.Multiply = 1;
 }
 s.Number = 104; s.Pulses[1] = 9147; s.Pulses[104] = 0;
 s.Repeats = 7; s.Delay = 100; compare(s);
}
'''
with tempfile.TemporaryDirectory(prefix="rfbridge-stock-edges-") as directory:
    path = Path(directory)
    code = prefix + emitter + "\n#define RawSendRF StockRawSendRF\n" + stock + "\n#undef RawSendRF\n" + tests
    (path / "test.cpp").write_text(code)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("raw_edges=identical_to_ed800fe_including_long_marks_gaps_repeats_and_wrap")
