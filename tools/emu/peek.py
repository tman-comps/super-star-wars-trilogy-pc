"""Warp to (mode, scene), run N frames with light input, save screenshots."""
import os, sys
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu
rom, mode, scene, out = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), sys.argv[4]
d = bytearray(open(rom, 'rb').read())
for ent in (1, 2, 3, 4):
    d[0x1D4A + 4 * ent + 2] = mode; d[0x1D4A + 4 * ent + 3] = scene
tmp = os.path.join(tempfile.gettempdir(), f'peek_{os.getpid()}.sfc'); open(tmp, 'wb').write(d)
e = Emu(tmp)
e.run(1500); e.press('START', 4, 60)
for _ in range(5): e.press('START', 4, 120)
for i in range(4):
    e.run(150, ['RIGHT'] if i % 2 else [])
    e.screenshot(f'{out}_{i}.png')
print(mode, scene, 'mode now', hex(e.r16(0x4DA)), 'scene now', hex(e.r16(0x368)))
os.remove(tmp)
