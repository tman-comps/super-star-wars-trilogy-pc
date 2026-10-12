"""Static recompiler: 65816 (traced code map) -> C, one function per ROM bank.

Each traced instruction becomes a labelled C block that performs exactly the bus accesses
LakeSnes's interpreter would (same order, same timing), so the result is cycle-identical to the
interpreter. Operand widths (M/X) are still evaluated at runtime, so a wrong static flag guess
can only cost a trip through the dispatcher, never wrong behaviour.

Control flow:
  * fall-through / static branch & jump targets inside the bank -> direct goto
  * anything else (RTS/RTL/RTI, jump tables, script native calls, other banks) -> dispatcher
  * dispatcher miss (untraced code, code in RAM) -> returns RECOMP_MISS; the runner
    interprets one instruction and re-enters

usage: recomp.py rom.sfc map.json lakesnes/cpu.c outdir [prefix] [hooks.txt]
prefix (default empty) is put in front of every exported symbol, so several games can be linked
into one program (e.g. "esb_" -> esb_recomp_run, esb_recomp_bank00, esb_g_hook_addrs).
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from op65816 import OPS  # noqa: E402

ROM = open(sys.argv[1], "rb").read()
M = json.load(open(sys.argv[2]))
CPU_C = open(sys.argv[3]).read()
OUT = sys.argv[4]
PFX = sys.argv[5] if len(sys.argv) > 5 else ""
HOOKFILE = sys.argv[6] if len(sys.argv) > 6 else None
os.makedirs(OUT, exist_ok=True)

# ---- pull opcode bodies and helpers out of LakeSnes cpu.c
body_start = CPU_C.index("static void cpu_doOpcode(Cpu* cpu, uint8_t opcode) {")
helpers_start = CPU_C.index("static uint8_t cpu_read(Cpu* cpu, uint32_t adr) {")
helpers = CPU_C[helpers_start:body_start]
switch_src = CPU_C[body_start:]
cases = {}
for m in re.finditer(r"\n    case 0x([0-9a-f]{2}): \{ // ([^\n]*)\n(.*?)\n      break;\n    \}", switch_src, re.S):
    cases[int(m.group(1), 16)] = (m.group(2), m.group(3))
assert len(cases) == 256, len(cases)

decls = "\n".join(l for l in CPU_C[:helpers_start].split("\n") if l.startswith("static ") and l.endswith(");"))
open(os.path.join(OUT, "cpu_helpers.inc"), "w").write(
    "// extracted from LakeSnes cpu.c by tools/recomp.py -- do not edit\n"
    "#pragma GCC diagnostic ignored \"-Wunused-function\"\n" + decls.replace("static void cpu_doOpcode(Cpu* cpu, uint8_t opcode);", "") + "\n" + helpers)

code = {int(k): v for k, v in M["code"].items()}
# hook points (sym/hooks.txt): a call into the port before these instructions, when enabled
HOOKS = set()
_hp = HOOKFILE or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sym", "hooks.txt")
if os.path.exists(_hp):
    for line in open(_hp):
        line = line.split("#")[0].strip()
        if line:
            HOOKS.add(int(line, 16))
labels = {int(k) for k in M["labels"]}
NB = len(ROM) // 0x8000


def addr16(off):
    return 0x8000 | (off % 0x8000)


banks = {}
for off in code:
    banks.setdefault(off // 0x8000, []).append(off)

ENDERS = {"rts", "rtl", "rti", "jmp", "jml", "bra", "brl", "stp", "brk", "cop"}
index = []
for b, offs in sorted(banks.items()):
    offs.sort()
    starts = set(offs)
    L = []
    L.append('#include "recomp_rt.h"')
    L.append('#include "cpu_helpers.inc"')
    L.append(f"int {PFX}recomp_bank{b:02X}(Cpu* cpu) {{")
    L.append("  goto dispatch;")
    for off in offs:
        c = code[off]
        op = ROM[off]
        mn, mode = OPS[op]
        pc = addr16(off)
        nxt = (pc + c["len"]) & 0xFFFF
        comment, body = cases[op]
        L.append(f"L_{pc:04X}: // {comment}")
        if (0x800000 | (b << 16) | pc) in HOOKS:
            L.append(f"  if (g_hook_enable) g_recomp_hook(cpu, 0x{0x800000 | (b << 16) | pc:06X});")
        L.append(f"  cpu_read(cpu, (cpu->k << 16) | 0x{pc:04X}); cpu->pc = 0x{(pc + 1) & 0xFFFF:04X};")
        L.append("  {" + body.replace("\n      ", "\n    ") + "\n  }")
        L.append("  if (RECOMP_STOP(cpu)) return RECOMP_EVENT;")
        tgts = []
        if mn not in ENDERS and nxt - 0x8000 + b * 0x8000 in starts and nxt >= 0x8000:
            tgts.append(nxt)
        if mode in ("rel", "rel16") and mn != "per":
            a = int.from_bytes(ROM[off + 1:off + c["len"]], "little")
            disp = a - 0x100 if (mode == "rel" and a >= 0x80) else (a - 0x10000 if (mode == "rel16" and a >= 0x8000) else a)
            t = (pc + c["len"] + disp) & 0xFFFF
            if t >= 0x8000 and (b * 0x8000 + t - 0x8000) in starts:
                tgts.append(t)
        if mn in ("jmp", "jsr") and mode == "abs":
            t = int.from_bytes(ROM[off + 1:off + 3], "little")
            if t >= 0x8000 and (b * 0x8000 + t - 0x8000) in starts:
                tgts.append(t)
        if mn in ("bra", "brl", "jmp") or mn not in ENDERS:
            for t in dict.fromkeys(tgts):
                L.append(f"  if (cpu->pc == 0x{t:04X}) goto L_{t:04X};")
        L.append("  goto dispatch;")
    L.append("dispatch:")
    L.append(f"  if ((cpu->k & 0x7f) != 0x{b:02X}) return RECOMP_OTHER_BANK;")
    L.append("  switch (cpu->pc) {")
    for off in offs:
        pc = addr16(off)
        L.append(f"    case 0x{pc:04X}: goto L_{pc:04X};")
    L.append("    default: return RECOMP_MISS;")
    L.append("  }")
    L.append("}")
    open(os.path.join(OUT, f"recomp_bank{b:02X}.c"), "w").write("\n".join(L) + "\n")
    index.append(b)

D = ['#include "recomp_rt.h"']
D.append(f"const uint32_t {PFX}g_hook_addrs[] = {{" + ", ".join(f"0x{h:06X}" for h in sorted(HOOKS)) + (", " if HOOKS else "") + "0};")
for b in index:
    D.append(f"int {PFX}recomp_bank{b:02X}(Cpu* cpu);")
D.append(f"int {PFX}recomp_run(Cpu* cpu) {{")
D.append("  for (;;) {")
D.append("    int r;")
D.append("    switch (cpu->k & 0x7f) {")
for b in index:
    D.append(f"      case 0x{b:02X}: r = {PFX}recomp_bank{b:02X}(cpu); break;")
D.append("      default: return RECOMP_MISS;")
D.append("    }")
D.append("    if (r != RECOMP_OTHER_BANK) return r;")
D.append("  }")
D.append("}")
open(os.path.join(OUT, "recomp_dispatch.c"), "w").write("\n".join(D) + "\n")
print("banks", [f"{b:02X}" for b in index], "instructions", len(code))
