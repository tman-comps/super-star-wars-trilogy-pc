"""Recursive-descent 65816 tracer for LoROM SNES images.

Tracks M/X width flags through REP/SEP/PHP/PLP/XCE, follows calls,
jumps and branches, and resolves (abs,X) jump tables.  A routine may be
reached with several flag states; that's fine as long as every
instruction encodes to the same length under each.  Real width
conflicts are reported.

usage: trace.py rom.sfc out.json [hints.txt]
hints.txt lines:  entry <pc24> <m> <x>
                  table <pc24> <count> <m> <x>     (word pointers, same bank)
                  ltable <pc24> <count> <m> <x>    (3-byte long pointers)
                  notable <pc24>                   (suppress auto-table)
                  data <pc24> <len>                (bytes that are never code)
"""
import json
import sys
from collections import deque

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from op65816 import OPS, oplen  # noqa: E402

ROM = open(sys.argv[1], "rb").read()
OUT = sys.argv[2]
HINTS = sys.argv[3] if len(sys.argv) > 3 else None
NBANKS = len(ROM) // 0x8000


def to_off(a):
    bank, addr = a >> 16, a & 0xFFFF
    if bank in (0x7E, 0x7F) or addr < 0x8000:
        return None
    b = bank & 0x7F
    if b >= NBANKS:
        return None
    return b * 0x8000 + (addr - 0x8000)


def rd(off, n):
    return int.from_bytes(ROM[off:off + n], "little")


code = {}        # off -> dict(len, m, x, pb, states)
owner = {}
labels = {}
conflicts = []
indirect = []
tables = {}      # off -> dict(pb, n, m, x, e, kind, fixed)
callers = {}
notable = set()
datarange = set()
seen = set()
q = deque()


def push(pc, m, x, e, stack, why, frm=None):
    off = to_off(pc)
    if off is None or off in datarange:
        return
    if why != "loc" or off not in labels:
        labels[off] = why if off not in labels or labels[off] == "loc" else labels[off]
    if frm is not None:
        callers.setdefault(off, set()).add(frm)
    q.append((pc, m, x, e, stack))


def vec(a):
    return rd(to_off(0x00FF00 | a), 2)


CDL = None
hint_offs = set()
if HINTS:
    for line in open(HINTS):
        f = line.split("#")[0].split()
        if not f:
            continue
        k = f[0]
        if k == "entry":
            pc = int(f[1], 16)
            hint_offs.add(to_off(pc))
            q.append((pc, int(f[2]), int(f[3]), 0, ()))
        elif k == "label":
            push(int(f[1], 16), int(f[2]), int(f[3]), 0, (), "manual")
        elif k == "cdl":
            CDL = open(f[1], "rb").read()
        elif k in ("table", "ltable"):
            pc = int(f[1], 16)
            tables[to_off(pc)] = {"pb": pc >> 16, "n": 0, "want": int(f[2]), "m": int(f[3]),
                                  "x": int(f[4]), "e": 0, "kind": k, "fixed": True}
        elif k == "notable":
            notable.add(to_off(int(f[1], 16)))
        elif k == "data":
            o = to_off(int(f[1], 16))
            datarange.update(range(o, o + int(f[2], 16)))


# vectors (COP/BRK vectors are unused by this game and point into the middle of code)
push(vec(0xFC), 1, 1, 1, (), "reset")
for name, a in [("nmi", 0xEA), ("irq", 0xEE)]:
    v = vec(a)
    if v >= 0x8000:
        push(v, 1, 1, 0, (), name)


def resolve_table(pb, arg, m, x, e):
    toff = to_off(pb | arg)
    if toff is None or toff in tables or toff in notable:
        return
    tables[toff] = {"pb": pb >> 16, "n": 0, "m": m, "x": x, "e": e, "kind": "table", "fixed": False}


def table_bytes():
    s = set()
    for o, t in tables.items():
        w = 3 if t["kind"] == "ltable" else 2
        s.update(range(o, o + w * t["n"]))
    return s


def grow_tables():
    grew = False
    for toff, t in tables.items():
        w = 3 if t["kind"] == "ltable" else 2
        if t["fixed"]:
            while t["n"] < t["want"]:
                o = toff + w * t["n"]
                v = rd(o, w)
                pc = v if w == 3 else (t["pb"] << 16) | v
                t["n"] += 1
                grew = True
                push(pc, t["m"], t["x"], t["e"], (), "tbl")
            continue
        others = table_bytes() - set(range(toff, toff + w * t["n"]))
        while t["n"] < 256:
            o = toff + w * t["n"]
            if o in owner or o + 1 in owner or o in datarange:
                break
            if CDL is not None and (CDL[o] & 0x1f or CDL[o + 1] & 0x1f):
                break
            if t["n"] > 0 and (o in labels or o in tables or o in others or o + 1 in others):
                break
            v = rd(o, 2)
            if v < 0x8000:
                break
            tgt = to_off((t["pb"] << 16) | v)
            if tgt is None or tgt in datarange:
                break
            if tgt in owner and owner[tgt] != tgt:
                break
            t["n"] += 1
            grew = True
            push((t["pb"] << 16) | v, t["m"], t["x"], t["e"], (), "tbl")
    return grew


def run():
    while q:
        pc, m, x, e, stack = q.popleft()
        while True:
            off = to_off(pc)
            if off is None or off in datarange:
                break
            key = (off, m, x)
            if key in seen:
                break
            op = ROM[off]
            mn, mode = OPS[op]
            n = oplen(op, m, x)
            if off in code:
                c = code[off]
                if n != c["len"]:
                    conflicts.append([off, pc, [c["m"], c["x"]], [m, x]])
                    break
                c["states"].append([m, x])
            else:
                if off in owner or any(o in owner for o in range(off + 1, off + n)):
                    conflicts.append([off, pc, "overlap", owner.get(off)])
                    break
                code[off] = {"len": n, "m": m, "x": x, "pb": pc >> 16, "states": [[m, x]]}
                for o in range(off, off + n):
                    owner[o] = off
            seen.add(key)
            pb = pc & 0xFF0000
            nxt = pb | ((pc + n) & 0xFFFF)
            arg = rd(off + 1, n - 1) if n > 1 else 0

            if mn == "rep":
                if not e:
                    if arg & 0x20: m = 0
                    if arg & 0x10: x = 0
            elif mn == "sep":
                if arg & 0x20: m = 1
                if arg & 0x10: x = 1
            elif mn == "php":
                stack = (stack + ((m, x),))[-8:]
            elif mn == "plp":
                if stack:
                    m, x = stack[-1]
                    stack = stack[:-1]
            elif mn == "xce":
                prev = ROM[off - 1] if off else 0
                if prev == 0x18:
                    e = 0
                elif prev == 0x38:
                    e = 1; m = 1; x = 1

            if mode in ("rel", "rel16"):
                if mn == "per":
                    pc = nxt
                    continue
                disp = arg - 0x100 if (mode == "rel" and arg >= 0x80) else arg
                if mode == "rel16" and arg >= 0x8000:
                    disp = arg - 0x10000
                push(pb | ((pc + n + disp) & 0xFFFF), m, x, e, stack, "loc")
                if mn in ("bra", "brl"):
                    break
                pc = nxt
                continue
            if mn == "jsr" and mode == "abs":
                push(pb | arg, m, x, e, (), "sub", off)
                pc = nxt
                continue
            if mn == "jsl":
                push(arg, m, x, e, (), "long", off)
                pc = nxt
                continue
            if mn == "jmp" and mode == "abs":
                push(pb | arg, m, x, e, stack, "loc")
                break
            if mn == "jml" and mode == "absl":
                push(arg, m, x, e, stack, "loc")
                break
            if mode in ("absi", "absxi", "absil"):
                indirect.append({"off": off, "pc": pc, "op": mn, "mode": mode, "arg": arg, "m": m, "x": x})
                if mode == "absxi":
                    resolve_table(pb, arg, m, x, e)
                if mn == "jsr":
                    pc = nxt
                    continue
                break
            if mn in ("rts", "rtl", "rti", "stp", "brk", "cop"):
                break
            pc = nxt


while True:
    run()
    if not grow_tables() and not q:
        break

# hint-only entry points get a label only if nothing falls through into them
ENDERS = {"rts", "rtl", "rti", "jmp", "jml", "bra", "brl", "stp", "brk"}
ends_at = {}
for o, c in code.items():
    ends_at[o + c["len"]] = o
for o in hint_offs:
    if o is None or o not in code or o in labels:
        continue
    p = ends_at.get(o)
    if p is None or OPS[ROM[p]][0] in ENDERS or (OPS[ROM[p]][0] == "jmp"):
        labels[o] = "loc"

# dedupe indirect list
ind = {}
for i in indirect:
    ind[i["off"]] = i
print("instructions:", len(code))
print("bytes covered:", len(owner), "of", len(ROM))
print("tables:", len(tables), "entries:", sum(t["n"] for t in tables.values()))
print("conflicts:", len(conflicts))
print("indirect jumps:", len(ind))
json.dump({
    "code": {str(k): v for k, v in code.items()},
    "labels": {str(k): v for k, v in labels.items()},
    "conflicts": conflicts,
    "indirect": list(ind.values()),
    "tables": {str(k): v for k, v in tables.items()},
    "callers": {str(k): sorted(v) for k, v in callers.items()},
}, open(OUT, "w"))
