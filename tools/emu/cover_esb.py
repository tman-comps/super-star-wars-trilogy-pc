"""Empire coverage run: warp to (mode, scene) through the scene table, play with a god-mode bot,
dump the code/data log and the script-VM opcode fetch log.
usage: cover_esb.py rom.sfc mode scene frames seed out_prefix [shots_prefix]"""
import json, os, random, sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu

rom, mode, scene, frames, seed, outp = sys.argv[1:7]
shots = sys.argv[7] if len(sys.argv) > 7 else None
mode, scene, frames, seed = int(mode, 16), int(scene, 16), int(frames), int(seed)
data = bytearray(open(rom, 'rb').read())
SEQ = 0x0D4E                      # $80:8D4E
for ent in (1, 2, 3, 4):
    data[SEQ + 4 * ent + 2] = mode
    data[SEQ + 4 * ent + 3] = scene
tmp = os.path.join(tempfile.gettempdir(), f'esbwarp_{os.getpid()}.sfc')
open(tmp, 'wb').write(data)
e = Emu(tmp)
VMS = {0: 0x80CA04, 1: 0x81902A, 2: 0x81835C}   # lda [$5F],y opcode fetch sites
for i, pc in VMS.items():
    e.watch(i, pc, 0x5F)
HP, LIVES = 0x0CF6, 0x01AC

def god(m):
    m.w8(HP, 32); m.w8(LIVES, 3)
    for x in range(2, 0x50, 2):
        v = m.r16(HP + x)
        if 1 < v < 0x8000:
            m.w16(HP + x, 1)

fetch = {}; pairs = set()
def drain():
    prev = None
    for vm, a in e.take_log():
        fetch[(vm, a)] = fetch.get((vm, a), 0) + 1
        if prev and prev[0] == vm and 0 < a - prev[1] <= 24:
            pairs.add((vm, prev[1], a))
        prev = (vm, a)

e.run(1500)
for _ in range(8):
    e.press('START', 4, 120)
drain()
e.per_frame = [god]
rng = random.Random(seed)
CHO = [['RIGHT'], ['RIGHT', 'Y'], ['RIGHT', 'B'], ['LEFT', 'Y'], ['UP'], ['DOWN', 'Y'], ['Y'],
       ['B', 'Y'], ['A'], ['RIGHT', 'B', 'Y'], ['X'], ['L'], ['R'], ['UP', 'Y'], ['LEFT'], ['B'],
       ['UP', 'RIGHT', 'Y'], ['DOWN'], ['RIGHT', 'A'], ['LEFT', 'B'], ['RIGHT', 'Y'], ['RIGHT', 'B', 'Y'], ['B', 'B']]
done = 0; k = 0
while done < frames:
    n = rng.randint(4, 40)
    e.run(n, rng.choice(CHO))
    done += n
    if done % 600 < n: drain()
    if done % 4000 < n:
        if shots:
            e.screenshot(f'{shots}_{k:02d}.png'); k += 1
        e.press('START', 3, 20); e.press('START', 3, 20)
drain()
open(outp + '.cdl', 'wb').write(bytes(e.cdl))
json.dump({'fetch': [[vm, a, c] for (vm, a), c in fetch.items()], 'pairs': sorted(pairs)}, open(outp + '.scr.json', 'w'))
print('done', outp, 'mode %02X scene %02X' % (e.wram[0x1B6], e.wram[0x1B8]))
os.remove(tmp)
