"""Render level map chunks using a scene's WRAM+VRAM dump (from ssw_script 'dump' + 'vdump').
usage: chunkview.py rom wram.bin vram.bin layer out.png [ids...|full]"""
import sys
from PIL import Image
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from levelmap import Layer

def load(rom, wf, vf, layer):
    wram = open(wf, 'rb').read(); v = open(vf, 'rb').read()
    vram = [v[2*i] | v[2*i+1] << 8 for i in range(0x8000)]
    cg = [v[0x10000+2*i] | v[0x10000+2*i+1] << 8 for i in range(256)]
    tadr = [v[0x10200+2*i] | v[0x10201+2*i] << 8 for i in range(4)]
    L = Layer(rom, wram, layer * 2)
    pal = [((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31) for c in cg]
    return L, vram, pal, tadr

def draw_tile(img, px, py, word, vram, pal, tbase):
    num = word & 0x3FF; p = (word >> 10) & 7; hf = word & 0x4000; vf = word & 0x8000
    a = (tbase + num * 16) & 0x7FFF
    for r in range(8):
        w0 = vram[(a + r) & 0x7FFF]; w1 = vram[(a + r + 8) & 0x7FFF]
        for c in range(8):
            b = 7 - c
            ci = ((w0 >> b) & 1) | ((w0 >> (8 + b)) & 1) << 1 | ((w1 >> b) & 1) << 2 | ((w1 >> (8 + b)) & 1) << 3
            if not ci: continue
            x = 7 - c if hf else c; y = 7 - r if vf else r
            img.putpixel((px + x, py + y), pal[p * 16 + ci])

def chunk_words(L, cid):
    c = L.chunk(cid); out = {}
    for my in range(16):
        for mx in range(16):
            o = (my * 16 + mx) * 2; cell = c[o] | c[o + 1] << 8; m = cell & 0x3FF
            for k in range(4):
                if m == 0: v = L.wram[L.meta] | L.wram[L.meta + 1] << 8
                else:
                    e = L.meta + m * 8 + k * 2; v = L.wram[e] | L.wram[e + 1] << 8
                out[(mx * 2 + (k & 1), my * 2 + (k >> 1))] = (v + L.base) & 0xFFFF
    return out

def render_chunk(L, vram, pal, tbase, cid, bg=(24, 24, 40)):
    img = Image.new('RGB', (128 * 2, 128 * 2), bg)
    for (tx, ty), w in chunk_words(L, cid).items():
        draw_tile(img, tx * 8, ty * 8, w, vram, pal, tbase)
    return img

if __name__ == '__main__':
    rom = open(sys.argv[1], 'rb').read()
    layer = int(sys.argv[4])
    L, vram, pal, tadr = load(rom, sys.argv[2], sys.argv[3], layer)
    tb = tadr[layer]
    ids = [int(x, 0) for x in sys.argv[6:]]
    tiles = [render_chunk(L, vram, pal, tb, i) for i in ids]
    sheet = Image.new('RGB', (len(tiles) * 264, 256), (0, 0, 0))
    for i, t in enumerate(tiles): sheet.paste(t, (i * 264, 0))
    sheet.save(sys.argv[5])
