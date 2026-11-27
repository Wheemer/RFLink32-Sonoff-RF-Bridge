"""Compile and exercise the production pulse parser without ESP hardware."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "RFLink/2_Signal.cpp").read_text()
start = source.index("    const char *skipJsonWhitespace")
end = source.index("    bool getSignalFromJson", start)
functions = source[start:end]
prefix = """
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#include <cassert>
#define RAW_BUFFER_SIZE 512
struct RawSignalStruct { uint16_t Pulses[513]; int Number; uint8_t Multiply; };
"""
tests = r'''
int main() {
  RawSignalStruct s = {};
  int count = 0; unsigned long longest = 0;
  assert(scanJsonPulses("{\"pulses\":[100,200]}", s, false, 1, count, longest));
  assert(count == 2 && longest == 200);
  assert(scanJsonPulses("{\"pulses\":[100,0]}", s, false, 1, count, longest));
  assert(count == 2 && longest == 100);
  assert(!scanJsonPulses("{\"pulses\":[100,0,100,200]}", s, false, 1, count, longest));
  assert(!scanJsonPulses("{\"pulses\":[0,200]}", s, false, 1, count, longest));
  assert(!scanJsonPulses("{\"pulses\":[100,]}", s, false, 1, count, longest));
  assert(!scanJsonPulses("{\"pulses\":[999999999999999999999999]}", s, false, 1, count, longest));
  assert(!scanJsonPulses("{\"pulses\":[16711426,200]}", s, false, 1, count, longest));
  assert(!scanJsonPulses("{\"pulses\":[-1,200]}", s, false, 1, count, longest));
  int value = 0;
  assert(scanJsonPulses("{\"nested\":{\"pulses\":[300,400]},\"pulses\":[100,200]}",
                        s, true, 1, count, longest));
  assert(count == 2 && s.Pulses[1] == 100 && s.Pulses[2] == 200);
  assert(!scanJsonPulses("{\"nested\":{\"pulses\":[300,400]}}", s, false, 1, count, longest));
  assert(!scanJsonPulses("{\"pulses\":[100,200],\"pulses\":[300,400]}", s, false, 1, count, longest));
  assert(parseJsonSignedField("{\"nested\":{\"repeat\":99},\"repeat\":5}",
                              "\"repeat\"", value) && value == 5);
  uint64_t frameUs = 0;
  s.Multiply = 1;
  s.Number = 8;
  for (int i = 1; i <= 8; ++i) s.Pulses[i] = 5000;
  assert(measureRawFrame(s, frameUs) && frameUs == 40000);
  s.Number = 10;
  s.Pulses[9] = 5000;
  s.Pulses[10] = 5001;
  assert(!measureRawFrame(s, frameUs));
  s.Pulses[9] = 5001;
  assert(measureRawFrame(s, frameUs) && frameUs == 50002);
  assert(parseJsonSignedField("{\"repeat\":5}", "\"repeat\"", value) && value == 5);
  assert(!parseJsonSignedField("{\"repeat\":999999999999999999999}", "\"repeat\"", value));
  assert(!parseJsonSignedField("{\"repeat\":2x}", "\"repeat\"", value));
}
'''
with tempfile.TemporaryDirectory(prefix="rfbridge-parser-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(prefix + functions + tests)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("production_raw_parser_host_tests=passed")
