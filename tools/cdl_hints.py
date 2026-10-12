"""Turn build/rom.cdl executed-instruction marks into tracer entry hints."""
import numpy as np
rom = np.fromfile('build/rom.cdl', np.uint8)
out = []
for off in np.nonzero(rom & 0x0f)[0]:
    f = int(rom[off]); b = off // 0x8000
    pc = ((0x80 | b) << 16) | 0x8000 | (off % 0x8000)
    for m, mb in ((1, 1), (0, 2)):
        for x, xb in ((1, 4), (0, 8)):
            if f & mb and f & xb:
                out.append(f"entry {pc:06X} {m} {x}")
open('build/cdl_hints.txt', 'w').write("\n".join(out) + "\n")
print(len(out), 'hints')
