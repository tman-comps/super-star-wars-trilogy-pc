"""Empire coverage of the developer features on controller 2 (enabled by $010C):
L+R debug menu (navigated, values changed, warps taken), Start = clear stage, A+B+X+Y+Select power-ups,
hold A+B+X+Y+Select = invincibility / coordinates, hold A+B+X+Y + Start = sound test.
usage: cover_debug.py rom.sfc mode scene seed out_prefix [shots_prefix]"""
import os, random, sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu

rom, mode, scene, seed, outp = sys.argv[1:6]
shots = sys.argv[6] if len(sys.argv) > 6 else None
mode, scene, seed = int(mode, 16), int(scene, 16), int(seed)
data = bytearray(open(rom, 'rb').read())
SEQ = 0x0D4E
for ent in (1, 2, 3, 4):
    data[SEQ + 4 * ent + 2] = mode
    data[SEQ + 4 * ent + 3] = scene
tmp = os.path.join(tempfile.gettempdir(), f'esbdbg_{os.getpid()}.sfc')
open(tmp, 'wb').write(data)
e = Emu(tmp)
rng = random.Random(seed)

def dbg(m):
    m.w8(0x010C, 1); m.w8(0x010D, 0); m.w8(0x01AC, 3)
e.run(1500)
for _ in range(8):
    e.press('START', 4, 120)
e.per_frame = [dbg]
e.run(300)
k = 0
def shot():
    global k
    if shots:
        e.screenshot(f'{shots}_{k:02d}.png'); k += 1
def pad2(bs, n=4, after=30):
    e.buttons2 = set(bs); e.run(n); e.buttons2 = set(); e.run(after)

for rnd in range(6):
    # debug menu: open, walk every row both ways, then warp
    pad2(['L', 'R'], 4, 60); shot()
    for _ in range(rng.randint(20, 60)):
        e.press(rng.choice(['UP', 'DOWN', 'LEFT', 'RIGHT', 'LEFT', 'RIGHT', 'A', 'B', 'X', 'Y']), 3, rng.randint(4, 12))
    shot()
    e.press('START', 4, 400); shot()
    # play a little, power-ups, invincibility, sound test, clear stage
    e.run(200, ['RIGHT', 'Y'])
    pad2(['A', 'B', 'X', 'Y', 'SELECT'], 4, 60)
    e.buttons2 = {'A', 'B', 'X', 'Y', 'SELECT'}
    e.run(200, ['RIGHT', 'B']); shot()
    e.buttons2 = set(); e.run(30)
    e.buttons2 = {'A', 'B', 'X', 'Y'}; e.run(10); e.buttons2 = {'A', 'B', 'X', 'Y', 'START'}; e.run(6)
    e.buttons2 = {'A', 'B', 'X', 'Y'}; e.run(60); e.buttons2 = set(); shot()
    for _ in range(rng.randint(10, 30)):
        e.press(rng.choice(['UP', 'DOWN', 'LEFT', 'RIGHT', 'A', 'B', 'START']), 3, 8)
    e.run(120)
    pad2(['START'], 4, 600); shot()
    e.run(300, ['RIGHT', 'Y'])
open(outp + '.cdl', 'wb').write(bytes(e.cdl))
print('done', outp, 'mode %02X scene %02X' % (e.wram[0x1B6], e.wram[0x1B8]))
os.remove(tmp)
