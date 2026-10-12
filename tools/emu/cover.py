"""Warp to a (mode, scene) via a patched copy of the sequence table, play it
with a random bot in "god mode", and dump the code/data log plus a log of
script-VM opcode fetches.

usage: cover.py rom.sfc mode scene frames seed out_prefix [shots_prefix]
writes out_prefix.cdl and out_prefix.scr.json
"""
import json
import tempfile
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu  # noqa: E402

rom, mode, scene, frames, seed, outp = sys.argv[1:7]
shots = sys.argv[7] if len(sys.argv) > 7 else None
mode, scene, frames, seed = int(mode, 16), int(scene, 16), int(frames), int(seed)
data = bytearray(open(rom, "rb").read())


def off(a):
    return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)


SEQ = 0x1D4A
for ent in (1, 2, 3, 4):        # every route into the first stage
    data[SEQ + 4 * ent + 2] = mode
    data[SEQ + 4 * ent + 3] = scene
for a in (0x808FBC, 0x80EC0B):  # infinite lives: dec $08FB -> lda $08FB
    assert data[off(a)] == 0xCE
    data[off(a)] = 0xAD
tmp = os.path.join(tempfile.gettempdir(), f'warp_{os.getpid()}.sfc')
open(tmp, "wb").write(data)
e = Emu(tmp)

# script VM opcode fetch sites: lda [$4B],y
VMS = {0: 0x80D421, 1: 0x818FEC}
for i, pc in VMS.items():
    e.watch(i, pc, 0x4B)

HP = 0x0A79          # object hit points, 2 bytes per slot, slot 0 = player


def god(m):
    m.w8(HP, 32)
    for x in range(2, 0x50, 2):
        v = m.r16(HP + x)
        if 1 < v < 0x8000:
            m.w16(HP + x, 1)


fetch = {}           # (vm, addr) -> count
pairs = set()        # (vm, addr, next_addr) consecutive fetches within one run


def drain():
    prev = None
    for vm, a in e.take_log():
        fetch[(vm, a)] = fetch.get((vm, a), 0) + 1
        if prev and prev[0] == vm and 0 < a - prev[1] <= 24:
            pairs.add((vm, prev[1], a))
        prev = (vm, a)


e.run(1500)
e.press("START", 4, 60)
for _ in range(5):
    e.press("START", 4, 120)
drain()
e.per_frame = [god]
rng = random.Random(seed)
CHO = [["RIGHT"], ["RIGHT", "Y"], ["RIGHT", "B"], ["LEFT", "Y"], ["UP"], ["DOWN", "Y"], ["Y"],
       ["B", "Y"], ["A"], ["RIGHT", "B", "Y"], ["X"], ["L"], ["R"], ["UP", "Y"], ["LEFT"], ["B"],
       ["UP", "RIGHT", "Y"], ["DOWN"], ["RIGHT", "A"], ["LEFT", "B"], ["RIGHT", "Y"], ["RIGHT", "B", "Y"]]
done = 0
k = 0
while done < frames:
    n = rng.randint(4, 40)
    e.run(n, rng.choice(CHO))
    done += n
    if done % 600 < n:
        drain()
    if done % 6000 < n:
        if shots:
            e.screenshot(f"{shots}_{k:02d}.png"); k += 1
        e.press("START", 3, 20)   # continue screens / skip text; pause+unpause otherwise
        e.press("START", 3, 20)
drain()
open(outp + ".cdl", "wb").write(bytes(e.cdl))
json.dump({"fetch": [[vm, a, c] for (vm, a), c in fetch.items()], "pairs": sorted(pairs)},
          open(outp + ".scr.json", "w"))
os.remove(tmp)
