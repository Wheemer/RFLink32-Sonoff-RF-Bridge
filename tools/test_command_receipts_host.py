"""Exercise the production receipt cache without allocating a WebSocket."""
import subprocess
import tempfile
from pathlib import Path

test = r"""
#include <cassert>
#include "CommandReceipts.h"
int main() {
  RFLink::CommandReceipts cache;
  const char *first = "0123456789abcdef";
  assert(!cache.find(first));
  assert(!cache.valid("short") && !cache.valid("0123456789abcdeg"));
  cache.remember(first, true, 190);
  cache.remember(first, false, 0);
  assert(cache.find(first)->ok && cache.find(first)->elapsedMs == 190);
  for (const char *token : {"0000000000000001", "0000000000000002",
                           "0000000000000003", "0000000000000004"})
    cache.remember(token, false, 42);
  assert(!cache.find(first));
  const auto *last = cache.find("0000000000000004");
  assert(last && !last->ok && last->elapsedMs == 42);
}
"""
source = Path("RFLink/12_Portal_ws.cpp").read_text()
finish = source.split("void wsFinishJob()", 1)[1].split("void wsHandleMessage", 1)[0]
assert finish.index("commandReceipts.remember") < finish.index("const bool replyQueued")
handler = source.split("void wsHandleMessage", 1)[1].split("void onWsEvent", 1)[0]
assert handler.index("commandReceipts.find") < handler.index("activeJob.type = WsJobType::Command")
with tempfile.TemporaryDirectory(prefix=".receipt-test-", dir=Path.cwd()) as directory:
    binary = Path(directory) / "test"
    subprocess.run(
        ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-include", "initializer_list",
         "-I", "RFLink", "-x", "c++", "-", "-o", str(binary)],
        input=test, text=True, check=True,
    )
    subprocess.run([str(binary)], check=True)
print("command receipts: stored before reply, duplicate lookup, rejected result, bounded cache")
