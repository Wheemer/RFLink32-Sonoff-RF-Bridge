"""Exercise the production bounded JSON output helpers on the host."""
import subprocess
import tempfile
from pathlib import Path

source = Path("RFLink/12_Portal_ws.cpp").read_text()
helpers = source[source.index("          bool appendJsonText"):
                 source.index("          void writeRuntimeDebugJson")]
prefix = "#include <cstdio>\n#include <cstring>\n#include <cassert>\n"
tests = r'''
int main() {
  char out[100] = {}; char *p = out; size_t n = sizeof(out);
  assert(appendJsonEscaped(p, n, "a\"\\\r\n\t\x01"));
  assert(strcmp(out, "\"a\\\"\\\\\\r\\n\\t\\u0001\"") == 0);
  struct { char tiny[3]; char sentinel; } bounded = {{0}, 'Z'};
  p = bounded.tiny; n = sizeof(bounded.tiny);
  assert(!appendJsonEscaped(p, n, "long"));
  assert(bounded.sentinel == 'Z');
  char exact[4] = {}; p = exact; n = sizeof(exact);
  assert(appendJsonEscaped(p, n, "x"));
  assert(strcmp(exact, "\"x\"") == 0);
}
'''
with tempfile.TemporaryDirectory(prefix="rfbridge-json-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(prefix + helpers + tests)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("production_json_builder_host_tests=passed")
