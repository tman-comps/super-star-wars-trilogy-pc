"""Merge emulator logs into a per-ROM-offset map (build/rom.cdl)."""
import glob
import sys
import numpy as np
acc = np.zeros(0x1000000, np.uint8)
files = sys.argv[1:] or glob.glob('build/cdl/*.cdl')
for f in files:
    acc |= np.fromfile(f, np.uint8)
NB = 0x30   # 1.5 MB
rom = np.zeros(NB * 0x8000, np.uint8)
for b in range(NB):
    for hb in (b, 0x80 | b):
        rom[b * 0x8000:(b + 1) * 0x8000] |= acc[(hb << 16) + 0x8000:(hb << 16) + 0x10000]
rom.tofile('build/rom.cdl')
ex = (rom & 0x1f) != 0
print('files', len(files), 'exec starts', ex.sum(), 'cpu-read', ((rom & 0x40) != 0).sum(), 'dma-read', ((rom & 0x80) != 0).sum(), 'untouched', (rom == 0).sum())
for b in range(NB):
    s = slice(b * 0x8000, (b + 1) * 0x8000)
    print(f'{0x80+b:02X} exec {ex[s].sum():5d} cpu {((rom[s]&0x40)!=0).sum():5d} dma {((rom[s]&0x80)!=0).sum():5d} untouched {(rom[s]==0).sum():5d}')
