"""Static walker for Super Star Wars' two script VMs, seeded by emulator fetch logs.

VM0  object/AI scripts   dispatcher $80:D41E, opcode table $80:D35B (63 ops)
     the script pointer is 24-bit in $4B; jumps replace only the low 16 bits (same bank)
VM1  level spawn/trigger lists   dispatcher $81:8FE9, opcode table $81:8D8D, always bank $82
     records: <id byte> ops... terminated by 19 ED / 1A ED; list ends with 18 ED

usage: scripts.py rom.sfc out.json log.scr.json...
"""
import json
import os
import sys

ROM = open(sys.argv[1], "rb").read()
OUT = sys.argv[2]
LOGS = sys.argv[3:]


def off(a):
    if (a & 0xFFFF) < 0x8000 or (a >> 16) in (0x7E, 0x7F):
        return None
    return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)


def snes(o):
    return 0x800000 | ((o // 0x8000) << 16) | 0x8000 | (o % 0x8000)


def w(o):
    return ROM[o] | ROM[o + 1] << 8


# ---- VM0 spec: op -> (length, kind, name)
# kinds: seq (fall through), end (stop), jmp (word target), jcc (word target + fall through),
#        call (word target + fall through), rand (4 word targets), loop (word target + fall through),
#        nat (word native addr, bank $80), natl (24-bit native addr)
V0 = {0x00: (1, "seq", "yield"), 0x0C: (1, "end", "kill"), 0x15: (3, "jmp", "jmp"),
      0x16: (4, "loop", "loop"), 0x17: (3, "jcc", "jcc17"), 0x18: (3, "jcc", "jcc18"),
      0x19: (3, "jcc", "jcc19"), 0x1A: (3, "jcc", "jcc1A"), 0x1B: (3, "jcc", "jz_f4"),
      0x1C: (3, "jcc", "jnz_f4"), 0x1D: (3, "jcc", "jpl_f4"), 0x1E: (3, "jcc", "jmi_f4"),
      0x1F: (9, "rand", "jrand"), 0x20: (3, "call", "call"), 0x21: (1, "end", "ret"),
      0x22: (3, "nat", "native"), 0x23: (4, "natl", "native_long"), 0x24: (2, "seq", "wait")}
for op, n in {0x01: 3, 0x02: 3, 0x03: 3, 0x04: 3, 0x05: 1, 0x06: 1, 0x07: 1, 0x08: 1, 0x09: 3,
              0x0A: 4, 0x0B: 6, 0x0D: 2, 0x0E: 2, 0x0F: 1, 0x10: 1, 0x11: 2, 0x13: 1, 0x14: 9,
              0x25: 3, 0x26: 3, 0x27: 3, 0x28: 3, 0x29: 3, 0x2A: 1, 0x2B: 1, 0x2C: 2, 0x2D: 3,
              0x2E: 2, 0x2F: 5, 0x30: 11, 0x31: 6, 0x32: 9, 0x33: 3, 0x34: 3, 0x35: 9, 0x36: 9,
              0x37: 3, 0x38: 5, 0x39: 3, 0x3A: 3, 0x3B: 3, 0x3C: 3, 0x3D: 3, 0x3E: 3}.items():
    V0[op] = (n, "seq", f"op{op:02X}")

# ---- VM1 spec
V1 = {0x18: (2, "end", "end_list"), 0x19: (2, "rec_end", "end_rec"), 0x1A: (2, "rec_end", "spawn_end"),
      0x0C: (4, "natl", "native_long"), 0x06: (1, "seq", "nop"), 0x16: (1, "seq", "op16")}
for op, n in {0x00: 2, 0x01: 7, 0x02: 5, 0x07: 5, 0x03: 5, 0x04: 2, 0x05: 9, 0x08: 5, 0x09: 2, 0x0A: 2, 0x0B: 3, 0x0D: 2,
              0x0E: 5, 0x0F: 5, 0x10: 2, 0x11: 3, 0x12: 2, 0x13: 3, 0x14: 3, 0x15: 3, 0x17: 5}.items():
    V1[op] = (n, "seq", f"op{op:02X}")

CDL = open("build/rom.cdl", "rb").read() if os.path.exists("build/rom.cdl") else bytes(len(ROM))


def executed(o, n):
    return any(CDL[i] & 0x1F for i in range(o, o + n))


ops = {}        # rom offset -> {vm, op, len, name, targets:[snes], native:snes|None}
natives = {}    # snes addr -> kind
unknown = {}    # rom offset -> (vm, op)
seeds = {0: set(), 1: set()}
for f in LOGS:
    d = json.load(open(f))
    for vm, a, c in d["fetch"]:
        seeds[vm].add(a)


def walk0(start):
    work = [start]
    while work:
        a = work.pop()
        while True:
            o = off(a)
            if o is None or o in ops:
                break
            op = ROM[o]
            if op not in V0:
                unknown[o] = (0, op)
                break
            n, kind, name = V0[op]
            if executed(o, n):
                unknown[o] = (0, op)
                break
            bank = a & 0xFF0000
            ent = {"vm": 0, "op": op, "len": n, "name": name, "targets": [], "native": None}
            if kind in ("jmp", "jcc", "call", "loop"):
                ent["targets"] = [bank | w(o + 1)]
            elif kind == "rand":
                ent["targets"] = [bank | w(o + 1 + 2 * i) for i in range(4)]
            elif kind == "nat":
                ent["native"] = 0x800000 | w(o + 1)
            elif kind == "natl":
                ent["native"] = ROM[o + 1] | ROM[o + 2] << 8 | ROM[o + 3] << 16
            ops[o] = ent
            if ent["native"] is not None:
                natives[ent["native"]] = kind
            work.extend(ent["targets"])
            if kind in ("end", "jmp", "rand"):
                break
            if kind == "natl" and ent["native"] == (a + n) & 0xFFFFFF:
                break   # script hands control to inline native code right after the op
            a = bank | ((a + n) & 0xFFFF)


def walk1(start):
    a = start
    while True:
        o = off(a)
        if o is None or o in ops:
            return
        op = ROM[o]
        if op not in V1:
            unknown[o] = (1, op)
            # resync like the game does: scan for the record terminator
            p = o
            while p < min(len(ROM) - 1, o + 0x400) and not (ROM[p + 1] == 0xED and ROM[p] in (0x19, 0x1A)):
                p += 1
            if p == o or not (ROM[p + 1] == 0xED and ROM[p] in (0x19, 0x1A)) or p in ops:
                return
            a = snes(p)
            continue
        n, kind, name = V1[op]
        if executed(o, n):
            return
        ent = {"vm": 1, "op": op, "len": n, "name": name, "targets": [], "native": None}
        if kind == "natl":
            ent["native"] = ROM[o + 1] | ROM[o + 2] << 8 | ROM[o + 3] << 16
            natives[ent["native"]] = kind
        ops[o] = ent
        if kind == "end":
            return
        a += n
        if kind == "rec_end":
            # next record starts with an id byte, then ops
            ro = off(a)
            if ro is None or ro in ops:
                return
            if ROM[ro] == 0x18 and ROM[ro + 1] == 0xED:   # end of list
                ops[ro] = {"vm": 1, "op": 0x18, "len": 2, "name": "end_list", "targets": [], "native": None}
                return
            ops[ro] = {"vm": 1, "op": None, "len": 1, "name": "record", "targets": [], "native": None}
            a += 1


for a in sorted(seeds[0]):
    walk0(a)
# VM1: walk back from each seed to its list start is unknown; walk forward from seeds
for a in sorted(seeds[1]):
    walk1(a)

vm_bytes = sum(e["len"] for e in ops.values())
print("script ops:", len(ops), "bytes:", vm_bytes, "natives:", len(natives), "unknown-op stops:", len(unknown))
for o, (vm, op) in sorted(unknown.items())[:20]:
    print(f"  unknown VM{vm} op {op:02X} at {snes(o):06X}")
json.dump({"ops": {str(k): v for k, v in ops.items()},
           "natives": {f"{k:06X}": v for k, v in natives.items()}}, open(OUT, "w"))
