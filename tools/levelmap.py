"""Rebuild playfield tilemap words from level data, the way the game's column streamer does.

layer descriptor (x = layer*2) in WRAM: $03FA metatile table, $03E2 chunk grid, $044A/$0452 grid
size in chunks, $0392+$03B2 tile base, $03BA:$03EA ROM header (chunk pointers at +$14+id*2).
Chunk = 16x16 metatile cells, row-major words: bit15 = priority, low 10 bits = metatile.
Metatile = 4 tile words: [top-left, top-right, bottom-left, bottom-right].
"""
import sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from lzss import decompress


def off(a):
    return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)


class Layer:
    def __init__(self, rom, wram, x):
        r16 = lambda a: wram[a] | wram[a + 1] << 8
        self.rom, self.wram = rom, wram
        self.meta = 0x00000 | r16(0x3FA + x)        # bank $7E
        self.grid = r16(0x3E2 + x)
        self.w, self.h = r16(0x44A + x), r16(0x452 + x)
        self.base = (r16(0x392 + x) + r16(0x3B2 + x)) & 0xFFFF
        self.hdr = (wram[0x3BA + x] << 16) | r16(0x3EA + x)
        self.cache = {}

    def chunk(self, cid):
        if cid not in self.cache:
            p = off(self.hdr) + 0x14 + cid * 2
            ptr = (self.hdr & 0xFF0000) | (self.rom[p] | self.rom[p + 1] << 8) | 0x8000
            data, _, _ = decompress(self.rom, off(ptr))
            self.cache[cid] = data
        return self.cache[cid]

    def tile(self, tx, ty):
        mx, my = tx >> 1, ty >> 1
        cx, cy = mx >> 4, my >> 4
        if cx >= self.w or cy >= self.h or cx < 0 or cy < 0:
            cid = 0xFF
        else:
            cid = self.wram[self.grid + cy * self.w + cx]
        if cid == 0xFF:
            return None
        c = self.chunk(cid)
        o = ((my & 15) * 16 + (mx & 15)) * 2
        cell = c[o] | c[o + 1] << 8
        m = cell & 0x3FF
        prio = (cell & 0x8000) >> 2
        if m == 0:
            v = self.wram[self.meta] | self.wram[self.meta + 1] << 8
            return (v + self.base) & 0xFFFF
        e = self.meta + m * 8 + (ty & 1) * 4 + (tx & 1) * 2
        v = self.wram[e] | self.wram[e + 1] << 8
        return ((v + self.base) | prio) & 0xFFFF
