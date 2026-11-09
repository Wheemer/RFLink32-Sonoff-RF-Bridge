"""Reject SDK/flash references in the compiled RF NMI, not just its source."""
import subprocess
from pathlib import Path

objdump = Path.home() / ".platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-objdump"
build = Path(".pio/build/sonoff_bridge")
obj = build / "src/2_Signal.cpp.o"
symbols = subprocess.check_output([str(objdump), "-t", str(obj)], text=True)
row = next(line.split() for line in symbols.splitlines() if line.endswith("rfTimerInterruptEv"))
section = row[3]
assert section.startswith(".iram."), section
relocations = subprocess.check_output(
    [str(objdump), "-r", "-j", section, str(obj)], text=True,
)
allowed = {
    ".bss._ZN6RFLink6Signal12_GLOBAL__N_115timerCycleShiftE",
    ".data._ZN6RFLink6Signal12_GLOBAL__N_110pulseTrainE",
    "_ZN6RFLink5Radio4pins7TX_DATAE",
    "timer1_disable",
    "timer1_write",
}
for line in relocations.splitlines():
    columns = line.split()
    if len(columns) < 3 or not columns[1].startswith("R_XTENSA_"):
        continue
    target = columns[2]
    assert target.startswith(section) or target in allowed, f"Unsafe NMI reference: {target}"
linked = subprocess.check_output(
    [str(objdump), "-t", str(build / "firmware.elf")], text=True,
)
for name in ("timer1_disable", "timer1_write"):
    row = next(line.split() for line in linked.splitlines() if line.endswith(" " + name))
    assert 0x40100000 <= int(row[0], 16) < 0x40200000, (name, row)
print("compiled_rf_nmi=IRAM_and_DRAM_only; no_SDK_calls_or_runtime_division")
