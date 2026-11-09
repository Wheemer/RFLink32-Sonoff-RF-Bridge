"""Compare the complete sender and scheduling trace against pre-audit 88c7bf2."""
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/2_Signal.cpp").read_text()
emitter = source[source.index("    void delayLongPulse("):source.index("    const char *skipJsonWhitespace")]
stock = Path("tools/fixtures/rawsend_88c7bf2.cpp").read_text()
reference_hash = "83b1cdf3cac3179fab0e6523583a23353cd26d6c375d06cc6f1fb9afbb004180"
assert hashlib.sha256(stock.strip().encode()).hexdigest() == reference_hash
assert emitter.strip() == stock.strip(), "Production sender differs from pre-audit reference"
print("preaudit_sender_sha256=" + hashlib.sha256(stock.strip().encode()).hexdigest())
parser = source[source.index("    const char *skipJsonWhitespace"):
                source.index("    bool getSignalFromJson")]
prefix = r'''
#include <cstdint>
#include <cassert>
#include <vector>
#include <utility>
#include <tuple>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#define RAW_BUFFER_SIZE 512
using byte = unsigned char;
uint32_t clockUs = 0;
bool irq = true;
std::vector<std::pair<int, uint32_t>> edges;
std::vector<std::tuple<int, uint32_t, uint32_t>> trace;
unsigned yieldLag = 0, yields = 0;
void record(int kind, uint32_t arg = 0) { trace.emplace_back(kind, clockUs, arg); }
uint32_t micros() { return clockUs; }
unsigned long millis() { return clockUs / 1000; }
void delayMicroseconds(unsigned long us) { record(2, us); clockUs += us; }
void delay(unsigned long ms) { assert(irq); record(3, ms); clockUs += ms * 1000; }
void interrupts() { irq = true; record(4); }
void noInterrupts() { irq = false; record(5); }
void yield() { assert(irq); record(6); clockUs += yieldLag + (++yields % 3) * 17; }
struct Esp { void wdtFeed() { assert(irq); record(7); } } ESP;
constexpr int HIGH = 1, LOW = 0;
void digitalWrite(int, int value) {
 assert(!irq); edges.emplace_back(value, clockUs); record(value);
}
namespace Radio {
enum class States { Radio_TX, Radio_RX };
namespace pins { constexpr int TX_DATA = 5; }
void set_Radio_mode(States mode) { assert(irq); record(8, static_cast<int>(mode)); }
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
 clockUs = UINT32_MAX - 1000; edges.clear(); trace.clear(); irq = true; yields = 0;
 StockRawSendRF(&s);
 const auto expected = edges; const auto finished = clockUs; const auto schedule = trace;
 clockUs = UINT32_MAX - 1000; edges.clear(); trace.clear(); irq = true; yields = 0;
 RawSendRF(&s);
 assert(edges == expected && clockUs == finished && irq);
 assert(trace == schedule);
 assert(edges.size() == static_cast<unsigned>(s.Number) * (s.Repeats + 1));
}
int main() { for(unsigned lag : {0U, 100U, 1000U, 12000U}) {
 yieldLag = lag;
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
 s.Number = 2;
 for(unsigned us : {0U, 999U, 1000U, 4999U, 5000U, 5001U, 15999U, 16000U, 16001U}) {
  s.Pulses[1] = 295; s.Pulses[2] = us; compare(s);
 }
}}
'''
with tempfile.TemporaryDirectory(prefix="rfbridge-stock-edges-") as directory:
    path = Path(directory)
    if fixture_path := os.environ.get("RFBRIDGE_WAVEFORMS"):
        extra = ""
        for fixture in json.loads(Path(fixture_path).read_text()).values():
            pulses = fixture["pulses"]
            extra += "{\n" + f"s.Number={len(pulses)}; s.Multiply=1; "
            extra += f"s.Repeats={fixture.get('repeat', 0)}; s.Delay={fixture.get('delay', 0)};\n"
            extra += "".join(f"s.Pulses[{i}]={v};" for i, v in enumerate(pulses, 1))
            extra += "const auto saved = s; int count = 0; unsigned long longest = 0;\n"
            payload = json.dumps(json.dumps(fixture, separators=(",", ":")))
            extra += f"assert(scanJsonPulses({payload}, s, true, 1, count, longest));\n"
            extra += "assert(count == s.Number);\n"
            extra += "for(int i = 1; i <= count; ++i) assert(s.Pulses[i] == saved.Pulses[i]);\n"
            extra += "compare(s);\n}\n"
        tests = tests.rsplit("}}", 1)[0] + extra + "}}"
    code = (prefix + parser + emitter + "\n#define RawSendRF StockRawSendRF\n"
            "#define delayLongPulse StockDelayLongPulse\n" + stock
            + "\n#undef RawSendRF\n#undef delayLongPulse\n" + tests)
    (path / "test.cpp").write_text(code)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("raw_edges_and_scheduling=identical_to_88c7bf2 including SDK yields and watchdog feeds")
