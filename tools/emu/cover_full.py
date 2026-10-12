"""Full start-to-credits run: new game, god mode, play each stage for a while, then use the
game's own pad-2 stage-clear debug feature to advance. Saves CDL + script log + screenshots.
usage: cover_full.py rom out_prefix play_frames_per_stage seed"""
import json, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu
rom, outp, per, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
e = Emu(rom)
e.watch(0, 0x80D421, 0x4B); e.watch(1, 0x818FEC, 0x4B)
fetch = {}; pairs = set()
def drain():
    prev = None
    for vm, a in e.take_log():
        fetch[(vm, a)] = fetch.get((vm, a), 0) + 1
        if prev and prev[0] == vm and 0 < a - prev[1] <= 24: pairs.add((vm, prev[1], a))
        prev = (vm, a)
def god(m):
    m.w8(0x0A79, 32); m.w8(0x08FB, 3); m.w16(0x010E, 1)
    for x in range(2, 0x50, 2):
        v = m.r16(0x0A79 + x)
        if 1 < v < 0x8000: m.w16(0x0A79 + x, 1)
e.run(1500); e.press("START", 4, 60)
for _ in range(5): e.press("START", 4, 120)
e.per_frame = [god]
rng = random.Random(seed)
CHO = [["RIGHT"], ["RIGHT", "Y"], ["RIGHT", "B"], ["LEFT", "Y"], ["UP", "Y"], ["Y"], ["B", "Y"], ["RIGHT", "B", "Y"], ["A"], ["DOWN", "Y"]]
seen = []; k = 0
for stage in range(60):
    sc = (e.r16(0x04DA), e.r16(0x0368))
    seen.append(sc)
    done = 0
    while done < per:
        n = rng.randint(4, 30); e.run(n, rng.choice(CHO)); done += n
    e.screenshot(f"{outp}_{k:02d}.png"); k += 1
    drain()
    e.buttons2 = {"START"}; e.run(4); e.buttons2 = set()
    for _ in range(6): e.press("START", 3, 90)   # skip text / tallies
    if len(seen) > 4 and seen[-1] == seen[-2] == seen[-3]:
        e.press("START", 3, 300)
drain()
open(outp + ".cdl", "wb").write(bytes(e.cdl))
json.dump({"fetch": [[vm, a, c] for (vm, a), c in fetch.items()], "pairs": sorted(pairs)}, open(outp + ".scr.json", "w"))
print("scenes visited:", " ".join(f"{m:02X}/{s:02X}" for m, s in seen))
