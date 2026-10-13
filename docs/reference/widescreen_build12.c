// REFERENCE ONLY (not compiled): src/widescreen.c from before the trilogy changes (build 12 era).
// Super Star Wars widescreen worked well with this line-matching logic; see claude/handoff-build14.md.
// Widescreen support (experimental).
//
// The game streams its playfield into a tilemap ring and only keeps what's on the 256-pixel screen
// up to date, so the extra columns can't come from VRAM. Instead, whenever the PPU renders a pixel
// outside the original screen, it asks ws_tile() for the tile word. We rebuild it from the level
// data exactly the way the game's column streamer does (validated tile-for-tile against VRAM):
//
//   per streamed layer d (WRAM descriptors, x = d*2):
//     $03FA,x metatile table (bank 7E)      $03E2,x chunk grid (bank 7E, bytes, $FF = empty)
//     $044A,x / $0452,x grid size (chunks)  $0392,x + $03B2,x tile base added to every word
//     $041A,x / $0422,x layer camera X / Y  $03BA,x:$03EA,x ROM header, chunk ptrs at +$14+id*2
//   chunk   = 16x16 metatile cells, row-major words: bit 15 = priority, bits 0-9 = metatile
//   metatile= 4 tile words at table + m*8: TL, TR, BL, BR (metatile 0 = fill with table word 0)
//
// Chunks come from the game's own decompressed-chunk cache when present ($1925 keys,
// [$18FB] + slot*512), else from ROM via our LZSS decoder. Layers the game doesn't stream
// (descriptor width 0) just wrap naturally in VRAM. BG3 (HUD/text) is masked outside the centre.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "ppu.h"
#include "widescreen.h"

static Snes* g_s;
static const uint8_t* g_rom;
static int g_romlen;
int g_ws_mask_bg3 = 1;

static inline uint16_t r16(uint32_t a) { return g_s->ram[a & 0x1ffff] | (g_s->ram[(a + 1) & 0x1ffff] << 8); }
static inline uint32_t off(uint32_t a) { return ((a >> 16) & 0x7f) * 0x8000 + (a & 0x7fff); }

// ---- LZSS (same format as the game's $81:8722; see tools/lzss.py)
static int lzss(const uint8_t* rom, int len, uint32_t o, uint8_t* out, int outcap) {
  if (o + 5 > (uint32_t)len || rom[o] || rom[o + 1]) return -1;
  int size = ((rom[o + 2] | rom[o + 3] << 8) + 1) & ~1;
  int mode = rom[o + 4];
  if (size <= 0 || size > outcap) return -1;
  uint32_t p = o + 5;
  int pos = 0, step = mode == 1 ? 1 : 2;
  memset(out, 0, size);
  for (;;) {
    if (p >= (uint32_t)len) return -1;
    int flags = rom[p++];
    for (int k = 0; k < 8; k++, flags >>= 1) {
      if (p + 2 > (uint32_t)len) return -1;
      if (flags & 1) {
        if (pos >= size) return -1;
        out[pos] = rom[p++];
        pos += step; if (step == 2 && pos >= size) pos = 1;
      } else {
        int b0 = rom[p], b1 = rom[p + 1]; p += 2;
        if (b1 & 0x80) {
          if (!(b1 & 0x40)) return size;
          int cnt = (b1 & 0x3f) + 1, v = b0;
          for (int i = 0; i < cnt; i++) { int q = pos + i * step; if (q >= size) return -1; out[q] = (uint8_t)v++; }
          pos += cnt * step; if (step == 2 && pos >= size) pos = 1;
        } else {
          int dist = b0 | (b1 >> 4) << 8;
          if (!dist) return size;
          int ln = (b1 & 0x0f) + 3;
          if (step == 1) {
            int src = pos - dist;
            if (src < 0 || pos + ln > size) return -1;
            for (int i = 0; i < ln; i++) out[pos + i] = out[src + i];
            pos += ln;
          } else {
            int src = pos - dist * 2;
            if (src < 0) src += size - 1;
            for (int i = 0; i < ln * 2; i += 2) {
              if (pos + i >= size || src + i >= size || src + i < 0) return -1;
              out[pos + i] = out[src + i];
            }
            pos += ln * 2; if (pos >= size) pos = 1;
          }
        }
      }
    }
  }
}

// ---- chunk lookup with a small private cache for chunks the game hasn't cached
#define PCACHE 64
static struct { uint32_t hdr; int id; int ok; uint8_t data[512]; } g_pc[PCACHE];
static int g_pc_next;

static const uint8_t* chunk_data(int x, int id) {
  uint16_t key = (uint16_t)((x << 8) | id);
  for (int i = 0; i < 20; i++)
    if (r16(0x1925 + i * 2) == key) return &g_s->ram[(r16(0x18FB) + i * 512) & 0xffff];
  uint32_t hdr = ((uint32_t)g_s->ram[0x3BA + x] << 16) | r16(0x3EA + x);
  for (int i = 0; i < PCACHE; i++) if (g_pc[i].hdr == hdr && g_pc[i].id == id) return g_pc[i].ok ? g_pc[i].data : NULL;
  int slot = g_pc_next++ % PCACHE;
  g_pc[slot].hdr = hdr; g_pc[slot].id = id; g_pc[slot].ok = 0;
  uint32_t p = off(hdr) + 0x14 + id * 2;
  if (p + 2 > (uint32_t)g_romlen) return NULL;
  uint32_t ptr = (hdr & 0xff0000) | (g_rom[p] | g_rom[p + 1] << 8) | 0x8000;
  uint8_t tmp[0x10000];
  int n = lzss(g_rom, g_romlen, off(ptr), tmp, sizeof tmp);
  if (n >= 512) { memcpy(g_pc[slot].data, tmp, 512); g_pc[slot].ok = 1; }
  return g_pc[slot].ok ? g_pc[slot].data : NULL;   // other format: not supported yet -> VRAM
}

// world-space tile word for streamed layer descriptor x, or -1
static int level_tile(int x, int tx, int ty) {
  int w = r16(0x44A + x), h = r16(0x452 + x);
  int mx = tx >> 1, my = ty >> 1, cx = mx >> 4, cy = my >> 4;
  if (tx < 0 || ty < 0 || cx >= w || cy >= h) return -1;
  uint16_t meta = r16(0x3FA + x), grid = r16(0x3E2 + x);
  uint16_t base = r16(0x392 + x) + r16(0x3B2 + x);
  int id = g_s->ram[(grid + cy * w + cx) & 0xffff];
  if (id == 0xff) return -1;
  const uint8_t* c = chunk_data(x, id);
  if (!c) return -1;
  int o = ((my & 15) * 16 + (mx & 15)) * 2;
  uint16_t cell = c[o] | c[o + 1] << 8;
  int m = cell & 0x3ff;
  if (m == 0) return (uint16_t)(r16(meta) + base);
  uint16_t v = r16(meta + m * 8 + (ty & 1) * 4 + (tx & 1) * 2);
  return (uint16_t)((v + base) | ((cell & 0x8000) >> 2));
}

// nearest value congruent to v (mod m) around target
static int unwrap(int v, int m, int target) {
  int k = (target - v + m / 2);
  k = (k >= 0 ? k / m : -((-k + m - 1) / m));
  return v + k * m;
}

static uint16_t vram_tile(int layer, int lx, int ly) {
  BgLayer* b = &g_s->ppu->bgLayer[layer];
  uint16_t adr = b->tilemapAdr + (((ly >> 3) & 0x1f) << 5 | ((lx >> 3) & 0x1f));
  if ((lx & 0x100) && b->tilemapWider) adr += 0x400;
  if ((ly & 0x100) && b->tilemapHigher) adr += b->tilemapWider ? 0x800 : 0x400;
  return g_s->ppu->vram[adr & 0x7fff];
}

// per-layer, per-line trust: does rebuilding the line's real screen edges from level data reproduce
// VRAM? (Fails on lines where mid-frame effects move a layer away from its camera.)
static int g_trust[4][240];       // 0 = unknown, 1 = trusted, 2 = not trusted
static uint32_t g_trust_frame[4][240], g_frame_no;

static int line_trusted(int layer, int x, int lyline) {
  int y = g_s->ppu->curScreenY;
  if (y < 0 || y >= 240) return 0;
  if (g_trust_frame[layer][y] == g_frame_no) return g_trust[layer][y] == 1;
  g_trust_frame[layer][y] = g_frame_no;
  BgLayer* b = &g_s->ppu->bgLayer[layer];
  int camx = r16(0x41A + x), camy = r16(0x422 + x);
  int ly = (y + b->vScroll) & 0x3ff;
  int wy = unwrap(ly, 1024, camy + y);
  int ok = 1, checked = 0;
  for (int sx = 0; sx < 256; sx += 248) {
    for (int dx = 0; dx < 8; dx += 8) {
      int lx = (sx + dx + b->hScroll) & 0x3ff;
      int wx = unwrap(lx, 1024, camx + sx + dx);
      int t = level_tile(x, wx >> 3, wy >> 3);
      if (t < 0) continue;
      checked++;
      if ((uint16_t)t != vram_tile(layer, lx, ly)) ok = 0;
    }
  }
  g_trust[layer][y] = (ok && checked) ? 1 : 2;
  (void)lyline;
  return g_trust[layer][y] == 1;
}

static int g_kind;   // 1 = side-scrolling stage (mode $04), 2 = Mode 7 stage (modes $08, $0E; the trench run keeps
                     // 4:3 because its cockpit is a wrapping background layer)
static int ws_tile(void* ctx, int layer, int lx, int ly) {
  (void)ctx;
  if (layer == 2 && g_ws_mask_bg3) return -2;           // HUD/text layer: keep it 4:3
  if (g_kind != 1) return -1;                             // Mode 7 stages: other layers wrap in VRAM
  int x = layer * 2;
  if (r16(0x44A + x) == 0) return -1;                     // not a streamed layer: VRAM wraps fine
  if (!line_trusted(layer, x, ly)) return -1;
  int sx = g_s->ppu->curScreenX, sy = g_s->ppu->curScreenY;
  int camx = r16(0x41A + x), camy = r16(0x422 + x);
  int wx = unwrap(lx, 1024, camx + sx);
  int wy = unwrap(ly, 1024, camy + sy);
  int t = level_tile(x, wx >> 3, wy >> 3);
  return t;
}

void ws_init(Snes* s, const uint8_t* rom, int romlen) {
  g_s = s; g_rom = rom; g_romlen = romlen;
  s->ppu->wideBuffer = calloc((256 + 2 * PPU_WIDE_MAX) * 240, 4);
  s->ppu->wideTileHook = ws_tile;
  s->ppu->wideCtx = s;
}

// ---- objects in the extra area
// The game decides "on screen" for a 256-pixel view in three places. With widescreen on we widen
// all three by the extra width E on each side, so enemies are drawn, spawned and kept alive across
// the whole wide view instead of popping in at the old screen edge:
//   1. visibility ($80:B663): screen x + extent < $100. Hooks before both compares add E to A,
//      and the compared value becomes $100 + 2E (immediate operand, read from the cartridge).
//   2. despawn distance ($80:E6A6): |screen x| < margin (+$100 on the right). A hook adds E to
//      the margin in $34 before the compare.
//   3. spawn window for spawn-list records ($81:90D5.. and $81:9107): objects spawn just outside
//      the view; the window edges move out by E (immediate operands).
// With E = 0 every operand is back to its original value and the hooks are off.
#include "recomp_rt.h"
#include "cart.h"
static int g_obj_e;
static void set_imm16(uint32_t adr, uint16_t v) {   // operand bytes of the instruction at adr
  uint32_t o = ((adr >> 16) & 0x7F) * 0x8000 + ((adr + 1) & 0x7FFF);
  g_s->cart->rom[o] = v & 0xFF; g_s->cart->rom[o + 1] = v >> 8;
}
static void ws_hook(Cpu* cpu, uint32_t adr) {
  int e = g_obj_e;
  if (adr == 0x80B692 || adr == 0x80B69E) cpu->a = (uint16_t)(cpu->a + e);
  else if (adr == 0x80E6BF) {
    uint16_t d = (uint16_t)(cpu->dp + 0x34);
    uint16_t v = (uint16_t)((g_s->ram[d] | g_s->ram[(uint16_t)(d + 1)] << 8) + e);
    g_s->ram[d] = v & 0xFF; g_s->ram[(uint16_t)(d + 1)] = v >> 8;
  }
}
static void ws_objects(int e) {
  if (e == g_obj_e) return;
  g_obj_e = e;
  set_imm16(0x80B692, 0x0100 + 2 * e);
  set_imm16(0x80B69E, 0x0100 + 2 * e);
  set_imm16(0x8190D5, 0xFFF0 - e);
  set_imm16(0x8190DA, 0x0110 + e);
  set_imm16(0x819109, 0xFF40 - e);
  set_imm16(0x819110, 0x01C0 + e);
  g_recomp_hook = ws_hook;
  g_hook_enable = e > 0;
}
int g_ws_objects = 1;
int g_ws_mode7 = 1;     // widen the Mode 7 stages too   // widen object visibility/spawning along with the picture

// Called once per frame: enable the wide renderer only where it's known to work (side-scrolling
// stages, mode $04), otherwise render 4:3 inside the wide frame (pillarbox).
int ws_update(int extra_wanted) {
  g_frame_no++;
  uint16_t mode = r16(0x04DA);
  // Mode 7 stages: the plane simply continues past the old edges (the PPU's Mode 7 fetch takes any x)
  g_kind = (mode == 0x0004 && g_s->ppu->mode != 7) ? 1 : (mode == 0x0008 || mode == 0x000E) && g_ws_mode7 ? 2 : 0;
  int extra = g_kind ? extra_wanted : 0;
  if (extra > PPU_WIDE_MAX) extra = PPU_WIDE_MAX;
  if (extra != g_s->ppu->wideExtra) {
    // clear the side bars when switching so stale pixels don't linger
    memset(g_s->ppu->wideBuffer, 0, (256 + 2 * PPU_WIDE_MAX) * 240 * 4);
  }
  g_s->ppu->wideExtra = extra;
  ws_objects(g_ws_objects && g_kind == 1 ? extra : 0);
  return extra;
}

// Debug: rebuild the tiles of the original 256-pixel screen from level data and compare them with
// what the game actually put in VRAM. Returns mismatches; *total gets the number compared.
int ws_selfcheck(int* total) {
  Ppu* p = g_s->ppu;
  int bad = 0, tot = 0;
  for (int layer = 0; layer < 3; layer++) {
    int x = layer * 2;
    if (r16(0x44A + x) == 0) continue;
    BgLayer* b = &p->bgLayer[layer];
    for (int sy = 0; sy < 224; sy += 8) for (int sx = 0; sx < 256; sx += 8) {
      int lx = (sx + b->hScroll) & 0x3ff, ly = (sy + b->vScroll) & 0x3ff;
      uint16_t adr = b->tilemapAdr + (((ly >> 3) & 0x1f) << 5 | ((lx >> 3) & 0x1f));
      if ((lx & 0x100) && b->tilemapWider) adr += 0x400;
      if ((ly & 0x100) && b->tilemapHigher) adr += b->tilemapWider ? 0x800 : 0x400;
      uint16_t vram = p->vram[adr & 0x7fff];
      int camx = r16(0x41A + x), camy = r16(0x422 + x);
      int wx = unwrap(lx, 1024, camx + sx), wy = unwrap(ly, 1024, camy + sy);
      int t = level_tile(x, wx >> 3, wy >> 3);
      if (t < 0) continue;
      tot++;
      if (t != vram) { bad++; if (getenv("WS_VERBOSE") && bad < 12) fprintf(stderr, "  L%d s(%d,%d) w(%d,%d) cam(%d,%d) vram %04X calc %04X\n", layer, sx, sy, wx, wy, camx, camy, vram, t); }
    }
  }
  if (total) *total = tot;
  return bad;
}

// Colour of the playfield (BG2) at a world position, from the level data and the current VRAM and
// palette; -1 if transparent or outside the map. Used to draw port-side scenery (the restored
// stage's compactor walls) with the game's own tiles.
int ws_world_pixel(int wx, int wy) {
  int t = level_tile(2, wx >> 3, wy >> 3);
  if (t < 0) return -1;
  Ppu* p = g_s->ppu;
  int num = t & 0x3ff, pal = (t >> 10) & 7, px = wx & 7, py = wy & 7;
  if (t & 0x4000) px = 7 - px;
  if (t & 0x8000) py = 7 - py;
  uint16_t a = (p->bgLayer[1].tileAdr + num * 16 + py) & 0x7fff;
  uint16_t w0 = p->vram[a], w1 = p->vram[(a + 8) & 0x7fff];
  int b = 7 - px;
  int ci = ((w0 >> b) & 1) | ((w0 >> (8 + b)) & 1) << 1 | ((w1 >> b) & 1) << 2 | ((w1 >> (8 + b)) & 1) << 3;
  if (!ci) return -1;
  uint16_t c = p->cgram[pal * 16 + ci];
  int r = c & 31, g = (c >> 5) & 31, bl = (c >> 10) & 31, br = p->brightness;
  return ((((r << 3) | (r >> 2)) * br / 15) << 16) | ((((g << 3) | (g >> 2)) * br / 15) << 8) | (((bl << 3) | (bl >> 2)) * br / 15);
}
