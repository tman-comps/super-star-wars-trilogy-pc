"""Merge executed-code logs (16 MB, one byte per 24-bit address, written by rotj_test CDL=... or
tools/emu) into tracer entry hints for any LoROM game.
usage: cdl_to_hints.py rom.sfc out_hints.txt log.cdl [log.cdl ...]
Also writes out_hints.txt.romcdl (the merged log per ROM offset), named in a "cdl" line.
Every instruction start that was executed becomes an "entry <pc24> <m> <x>" line for tools/trace.py,
once per width state seen. Only addresses and flags are written, never ROM bytes."""
import sys
import numpy as np

rom_len = len(open(sys.argv[1], "rb").read())
rom_len -= rom_len % 0x8000
NB = rom_len // 0x8000
acc = np.zeros(0x1000000, np.uint8)
for f in sys.argv[3:]:
    acc |= np.fromfile(f, np.uint8)
rom = np.zeros(NB * 0x8000, np.uint8)
for b in range(NB):
    for hb in (b, 0x80 | b):
        rom[b * 0x8000:(b + 1) * 0x8000] |= acc[(hb << 16) + 0x8000:(hb << 16) + 0x10000]
out = []
for off in np.nonzero(rom & 0x0f)[0]:
    f = int(rom[off]); b = off // 0x8000
    pc = ((0x80 | b) << 16) | 0x8000 | (off % 0x8000)
    for m, mb in ((1, 1), (0, 2)):
        for x, xb in ((1, 4), (0, 8)):
            if f & mb and f & xb:
                out.append(f"entry {pc:06X} {m} {x}")
rom.tofile(sys.argv[2] + ".romcdl")   # per ROM offset, for the tracer's "cdl" hint (stops jump tables at executed code)
out.insert(0, f"cdl {sys.argv[2]}.romcdl")
open(sys.argv[2], "w").write("\n".join(out) + "\n")
print(len(out), "entries from", len(sys.argv) - 3, "logs; executed bytes in RAM:",
      int(np.count_nonzero(acc[0x7E0000:0x800000] & 0x0f)) + int(np.count_nonzero(acc[0:0x2000] & 0x0f)))
