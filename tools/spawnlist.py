"""Decode a scene's spawn list (VM1). usage: spawnlist.py rom scene_hex"""
import sys
rom = open(sys.argv[1], 'rb').read()
def off(a): return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)
def w(a): return rom[off(a)] | rom[off(a) + 1] << 8
LEN = {0x00: 2, 0x01: 7, 0x02: 5, 0x07: 5, 0x03: 5, 0x04: 2, 0x05: 9, 0x08: 5, 0x09: 2, 0x0A: 2, 0x0B: 3, 0x0D: 2,
       0x0E: 5, 0x0F: 5, 0x10: 2, 0x11: 3, 0x12: 2, 0x13: 3, 0x14: 3, 0x15: 3, 0x17: 5, 0x0C: 4, 0x06: 1, 0x16: 1}
def zones(scene):
    p = 0x820000 | w(0x82B0F5 + scene * 2) | 0x8000
    out = []
    while w(p) != 0xFFFF:
        out.append((w(p), w(p + 2), w(p + 4), w(p + 6), 0x820000 | w(p + 8)))
        p += 10
    cps = []; q = p + 2
    while (w(q) & 0xFF) == 7 and w(q) != 0xFFFF:
        cps.append((w(q + 1), w(q + 3))); q += 5
    return p, out, cps
def records(a):
    recs = []
    while w(a) != 0xED18:
        idx = rom[off(a)]; a += 1; ops = []
        while True:
            op = rom[off(a)]
            if op in (0x19, 0x1A) and rom[off(a) + 1] == 0xED: a += 2; break
            n = LEN.get(op)
            if n is None: ops.append(('??', rom[off(a):off(a) + 8].hex())); return recs + [(idx, ops)], a
            ops.append((f"{op:02X}", rom[off(a) + 1:off(a) + n].hex())); a += n
        recs.append((idx, ops))
    return recs, a
if __name__ == '__main__':
    sc = int(sys.argv[2], 16)
    p, zs, cps = zones(sc)
    print("checkpoints", [(hex(x), hex(y)) for x, y in cps])
    for z in zs:
        print(f"zone x {z[0]:04X}-{z[1]:04X} y {z[2]:04X}-{z[3]:04X} script {z[4]:06X}")
        recs, end = records(z[4])
        for idx, ops in recs: print(f"   [{idx:02X}] " + ' '.join(f"{o}:{a}" for o, a in ops))
