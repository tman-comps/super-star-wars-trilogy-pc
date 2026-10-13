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
#include "game.h"
#include "recomp_rt.h"

static Snes* g_s;
static const uint8_t* g_rom;
static int g_romlen;

// Per game: the engine is shared, the RAM layout moved. Empire's layer descriptors are Super Star Wars'
// moved by $29E-$2AE and its chunk cache by +$514 (found by matching the engine's code, tools/xmatch.py).
typedef struct {
  uint16_t a[10];                  // layer-descriptor arrays, in Super Star Wars order: $0392 $03B2 $03BA
                                   // $03E2 $03EA $03FA $041A $0422 $044A $0452
  uint16_t cache_keys, cache_base; // decompressed-chunk cache: 20 key words, pointer to the 512-byte slots
  uint32_t vis1, vis2;             // visibility compares (cmp #$0100), hooked: A += E
  uint32_t despawn; uint8_t despawn_dp;   // despawn compare, hooked: [dp + n] += E
  uint32_t spawn[4];               // spawn-window operands: #$FFF0, #$0110, #$FF40, #$01C0
  uint8_t m7_modes[2];             // game modes whose Mode 7 plane may be widened
  uint32_t bank;                   // WRAM bank offset of the grid, metatile table and chunk cache (0 = $7E, $10000 = $7F)
  uint8_t grid_words;              // grid cells: 0 = bytes ($FF empty), 1 = words (low byte $FF empty; the
                                   // whole word is the chunk-cache key, chunk number = low byte)
  uint8_t prio_xor;                // the cell's priority flag: 0 = OR'd into the tile (SSW), 1 = XOR'd (Empire)
  uint8_t keep43[4][2];            // side-scrolling stages shown 4:3 (a layer the level data can't rebuild)
} WsDef;
static const WsDef k_ws_ssw = {{0x392, 0x3B2, 0x3BA, 0x3E2, 0x3EA, 0x3FA, 0x41A, 0x422, 0x44A, 0x452}, 0x1925, 0x18FB, 0x80B692, 0x80B69E, 0x80E6BF, 0x34,
                               {0x8190D5, 0x8190DA, 0x819109, 0x819110}, {0x08, 0x0E}, 0, 0, 0, {{0}}};
static const WsDef k_ws_esb = {{0x630, 0x658, 0x660, 0x688, 0x690, 0x6A0, 0x6C8, 0x6D0, 0x6F8, 0x700}, 0x1E39, 0x1E0F, 0x80A559, 0x80A565, 0x80DF0A, 0x48,
                               {0x819142, 0x819147, 0x81916F, 0x819176}, {0x0E, 0x0E}, 0x10000, 1, 1,
                               {{0x04, 0x15}, {0x04, 0x0D}}};   // battle of Hoth (snowfield), Dagobah dark cave (fog layer)
static const WsDef* W = &k_ws_ssw;
static int g_obj_e, g_obj_sw = -1;
int g_ws_spawn_wide = 0;   // see ws_objects()
static uint16_t D(uint16_t ssw_adr) {   // this game's address of Super Star Wars' descriptor array
  static const uint16_t k[10] = {0x392, 0x3B2, 0x3BA, 0x3E2, 0x3EA, 0x3FA, 0x41A, 0x422, 0x44A, 0x452};
  for (int i = 0; i < 10; i++) if (k[i] == ssw_adr) return W->a[i];
  return ssw_adr;
}
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

static const uint8_t* chunk_data(int x, int cellv) {
  int id = cellv & 0xff;
  uint16_t key = W->grid_words ? (uint16_t)(cellv & 0x0fff) : (uint16_t)((x << 8) | id);
  for (int i = 0; i < 20; i++)
    if (r16(W->cache_keys + i * 2) == key) return &g_s->ram[W->bank + ((r16(W->cache_base) + i * 512) & 0xffff)];
  uint32_t hdr = ((uint32_t)g_s->ram[D(0x3BA) + x] << 16) | r16(D(0x3EA) + x);
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
  int w = r16(D(0x44A) + x), h = r16(D(0x452) + x);
  int mx = tx >> 1, my = ty >> 1, cx = mx >> 4, cy = my >> 4;
  if (tx < 0 || ty < 0 || cx >= w || cy >= h) return -1;
  uint16_t meta = r16(D(0x3FA) + x), grid = r16(D(0x3E2) + x);
  uint16_t base = r16(D(0x392) + x) + r16(D(0x3B2) + x);
  int id = W->grid_words ? r16(W->bank + ((grid + 2 * (cy * w + cx)) & 0xffff))
                         : g_s->ram[(grid + cy * w + cx) & 0xffff];
  if ((id & 0xff) == 0xff) return -1;
  const uint8_t* c = chunk_data(x, id);
  if (!c) return -1;
  int o = ((my & 15) * 16 + (mx & 15)) * 2;
  uint16_t cell = c[o] | c[o + 1] << 8;
  int m = cell & 0x3ff;
  if (m == 0) return (uint16_t)(r16(W->bank + meta) + base);
  uint16_t v = r16(W->bank + ((meta + m * 8 + (ty & 1) * 4 + (tx & 1) * 2) & 0xffff));
  uint16_t pr = (cell & 0x8000) >> 2;
  return (uint16_t)(W->prio_xor ? (uint16_t)(v + base) ^ pr : (uint16_t)(v + base) | pr);
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
static int g_trust[4][240];       // per layer and line: descriptor offset that rebuilds it (0, 2, 4, 6), or -1
static uint32_t g_trust_frame[4][240], g_frame_no;

// Does streamed descriptor x reproduce this line of BG layer `layer` on the real screen? Sampled
// across the line; all rebuilt tiles must equal VRAM. (Fails on lines where mid-frame effects move a
// layer away from its camera, and for a descriptor that drives a different layer.)
static int desc_matches_line(int layer, int x, int y) {
  if (r16(D(0x44A) + x) == 0) return 0;
  BgLayer* b = &g_s->ppu->bgLayer[layer];
  int camx = r16(D(0x41A) + x), camy = r16(D(0x422) + x);
  int ly = (y + b->vScroll) & 0x3ff;
  int wy = unwrap(ly, 1024, camy + y);
  int checked = 0, first = -1, varied = 0;
  for (int sx = 0; sx < 256; sx += 8) {
    int lx = (sx + b->hScroll) & 0x3ff;
    int wx = unwrap(lx, 1024, camx + sx);
    int t = level_tile(x, wx >> 3, wy >> 3);
    if (t < 0) continue;
    checked++;
    if ((uint16_t)t != vram_tile(layer, lx, ly)) return 0;
    if (first < 0) first = t; else if (t != first) varied = 1;
  }
  return checked >= 2 && varied;   // a row of one repeated tile proves nothing (blank sky, a stage card)
}
// Super Star Wars (the build-12 test, which was right on every stage): rebuild only the line's two
// screen-edge tiles from the layer's own descriptor and compare them with VRAM. The strict test above
// rejects too many good lines here: the game redraws moving scenery (the Sandcrawler) into the
// playfield tilemap at runtime, so a few tiles in the middle of a line never match the level data.
static int ssw_line_trusted(int layer, int y) {
  int x = layer * 2;
  if (r16(D(0x44A) + x) == 0) return 0;
  BgLayer* b = &g_s->ppu->bgLayer[layer];
  int camx = r16(D(0x41A) + x), camy = r16(D(0x422) + x);
  int ly = (y + b->vScroll) & 0x3ff;
  int wy = unwrap(ly, 1024, camy + y);
  int checked = 0;
  for (int sx = 0; sx < 256; sx += 248) {
    int lx = (sx + b->hScroll) & 0x3ff;
    int t = level_tile(x, unwrap(lx, 1024, camx + sx) >> 3, wy >> 3);
    if (t < 0) continue;
    checked++;
    if ((uint16_t)t != vram_tile(layer, lx, ly)) return 0;
  }
  return checked > 0;
}
// Which descriptor draws this line of the layer? Super Star Wars always pairs descriptor n with BG n;
// Empire sometimes doesn't (e.g. BG2/BG3 from descriptors 1/2 with BG1 as the HUD), so the others
// are tried too, the layer's own first.
static int line_desc(int layer) {
  int y = g_s->ppu->curScreenY;
  if (y < 0 || y >= 240) return -1;
  if (g_trust_frame[layer][y] == g_frame_no) return g_trust[layer][y];
  g_trust_frame[layer][y] = g_frame_no;
  int found = -1;
  if (!W->grid_words) found = ssw_line_trusted(layer, y) ? layer * 2 : -1;
  else {   // Empire: the layer's own descriptor first, then the others
    if (desc_matches_line(layer, layer * 2, y)) found = layer * 2;
    for (int x = 0; x < 8 && found < 0; x += 2) if (x != layer * 2 && desc_matches_line(layer, x, y)) found = x;
  }
  g_trust[layer][y] = found;
  return found;
}

static int g_kind;   // 1 = side-scrolling stage (mode $04), 2 = Mode 7 stage (modes $08, $0E; the trench run keeps
                     // 4:3 because its cockpit is a wrapping background layer)
static int ws_tile(void* ctx, int layer, int lx, int ly) {
  (void)ctx; (void)ly;
  if (layer == 2 && g_ws_mask_bg3 && !W->grid_words) return -2;   // Super Star Wars' BG3 is always the HUD: keep it 4:3
  if (g_kind != 1) return layer == 2 && g_ws_mask_bg3 ? -2 : -1;   // Mode 7 stages: other layers wrap in VRAM
  int x = line_desc(layer);
  if (x < 0) return layer == 2 && g_ws_mask_bg3 ? -2 : -1;   // HUD/text (BG3 kept 4:3), or not streamed: VRAM wraps
  int sx = g_s->ppu->curScreenX, sy = g_s->ppu->curScreenY;
  int camx = r16(D(0x41A) + x), camy = r16(D(0x422) + x);
  int wx = unwrap(lx, 1024, camx + sx);
  int wy = unwrap(ly, 1024, camy + sy);
  int t = level_tile(x, wx >> 3, wy >> 3);
  // Outside the map or an empty cell. Empire: nothing there. Super Star Wars: the wrapped VRAM copy,
  // as in build 12 (its levels keep scenery in the tilemap that the level data leaves empty).
  if (t < 0) return W->grid_words ? -2 : -1;
  return t;
}

void ws_init(Snes* s, const uint8_t* rom, int romlen) {
  g_s = s; g_rom = rom; g_romlen = romlen;
  W = !strcmp(g_game->key, "esb") ? &k_ws_esb : &k_ws_ssw;
  g_obj_e = 0; g_obj_sw = -1; g_hook_enable = 0;
  memset(g_pc, 0, sizeof g_pc); memset(g_trust_frame, 0, sizeof g_trust_frame);
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
static void set_imm16(uint32_t adr, uint16_t v) {   // operand bytes of the instruction at adr
  uint32_t o = ((adr >> 16) & 0x7F) * 0x8000 + ((adr + 1) & 0x7FFF);
  g_s->cart->rom[o] = v & 0xFF; g_s->cart->rom[o + 1] = v >> 8;
}
static void ws_hook(Cpu* cpu, uint32_t adr) {
  int e = g_obj_e;
  if (adr == W->vis1 || adr == W->vis2) cpu->a = (uint16_t)(cpu->a + e);
  else if (adr == W->despawn) {
    uint16_t d = (uint16_t)(cpu->dp + W->despawn_dp);
    uint16_t v = (uint16_t)((g_s->ram[d] | g_s->ram[(uint16_t)(d + 1)] << 8) + e);
    g_s->ram[d] = v & 0xFF; g_s->ram[(uint16_t)(d + 1)] = v >> 8;
  }
}
// Spawning: by default enemies spawn exactly where the original spawns them (just outside the 4:3
// view), so the number of enemies -- and the difficulty -- is the original's; they are only drawn and
// kept alive in the extra area. g_ws_spawn_wide moves the spawn window out to the wide view too
// (enemies appear earlier; noticeably harder, as tested in RC1).
static void ws_objects(int e) {
  int sw = g_ws_spawn_wide ? e : 0;
  if (e == g_obj_e && sw == g_obj_sw) return;
  g_obj_e = e; g_obj_sw = sw;
  set_imm16(W->vis1, 0x0100 + 2 * e);
  set_imm16(W->vis2, 0x0100 + 2 * e);
  set_imm16(W->spawn[0], 0xFFF0 - sw);
  set_imm16(W->spawn[1], 0x0110 + sw);
  set_imm16(W->spawn[2], 0xFF40 - sw);
  set_imm16(W->spawn[3], 0x01C0 + sw);
  g_recomp_hook = ws_hook;
  g_hook_enable = e > 0;
}
int g_ws_objects = 1;
int g_ws_mode7 = 1;     // widen the Mode 7 stages too   // widen object visibility/spawning along with the picture

// Called once per frame: enable the wide renderer only where it's known to work (side-scrolling
// stages, mode $04), otherwise render 4:3 inside the wide frame (pillarbox).
int ws_update(int extra_wanted) {
  g_frame_no++;
  uint16_t mode = r16(g_game->r_mode);
  // Mode 7 stages: the plane simply continues past the old edges (the PPU's Mode 7 fetch takes any x)
  g_kind = (mode == 0x0004 && g_s->ppu->mode != 7) ? 1 : (mode == W->m7_modes[0] || mode == W->m7_modes[1]) && g_ws_mode7 ? 2 : 0;
  if (g_kind == 1 && W->grid_words) {   // Empire: a screen no layer of which comes from the level data (stage cards, fades): keep it 4:3
    int any = 0;
    for (int l = 0; l < 3 && !any; l++) for (int y = 48; y < 224 && !any; y += 40)
      for (int x = 0; x < 8 && !any; x += 2) if (desc_matches_line(l, x, y)) any = 1;
    if (!any) g_kind = 0;
  }
  if (g_kind == 1 && !W->grid_words) {   // Super Star Wars' stage cards ("LUKE READY!") swap the tile graphics
    // under the level's tilemaps; they're the only side-scrolling screens without the HUD on BG3's top rows
    BgLayer* b3 = &g_s->ppu->bgLayer[2];
    uint16_t first = g_s->ppu->vram[b3->tilemapAdr & 0x7fff]; int varied = 0;
    for (int i = 1; i < 4 * 32 && !varied; i++) if (g_s->ppu->vram[(b3->tilemapAdr + i) & 0x7fff] != first) varied = 1;
    if (!varied) g_kind = 0;
  }
  int scene = g_s->ram[g_game->r_scene];
  for (int i = 0; i < 4; i++) if (W->keep43[i][0] && W->keep43[i][0] == mode && W->keep43[i][1] == scene) g_kind = 0;
  // Super Star Wars: past either end of the level map there is nothing to show (the game never
  // scrolls there), so those columns are black, like the edge of the world
  g_s->ppu->wideClipL = -PPU_WIDE_MAX; g_s->ppu->wideClipR = 256 + PPU_WIDE_MAX;
  if (g_kind == 1 && !W->grid_words) {
    int l = 0, r = 0, any = 0;
    for (int x = 0; x < 6; x += 2) {
      int w = r16(D(0x44A) + x); if (!w) continue;
      int cam = r16(D(0x41A) + x);
      if (!any || -cam < l) l = -cam;
      if (!any || w * 256 - cam > r) r = w * 256 - cam;
      any = 1;
    }
    if (any) { g_s->ppu->wideClipL = l; g_s->ppu->wideClipR = r; }
  }
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
    if (r16(D(0x44A) + x) == 0) continue;
    BgLayer* b = &p->bgLayer[layer];
    for (int sy = 0; sy < 224; sy += 8) for (int sx = 0; sx < 256; sx += 8) {
      int lx = (sx + b->hScroll) & 0x3ff, ly = (sy + b->vScroll) & 0x3ff;
      uint16_t adr = b->tilemapAdr + (((ly >> 3) & 0x1f) << 5 | ((lx >> 3) & 0x1f));
      if ((lx & 0x100) && b->tilemapWider) adr += 0x400;
      if ((ly & 0x100) && b->tilemapHigher) adr += b->tilemapWider ? 0x800 : 0x400;
      uint16_t vram = p->vram[adr & 0x7fff];
      int camx = r16(D(0x41A) + x), camy = r16(D(0x422) + x);
      int wx = unwrap(lx, 1024, camx + sx), wy = unwrap(ly, 1024, camy + sy);
      int t = level_tile(x, wx >> 3, wy >> 3);
      if (t < 0) continue;
      tot++;
      if (t != vram) { bad++; if (getenv("WS_VERBOSE") && bad < 12) fprintf(stderr, "  L%d s(%d,%d) w(%d,%d) cam(%d,%d) vram %04X calc %04X\n", layer, sx, sy, wx, wy, camx, camy, vram, t); }
    }
  }
  if (total) *total = tot;
  if (getenv("WS_MATRIX")) {   // debug: how well each descriptor matches each BG layer
    for (int l = 0; l < 3; l++) fprintf(stderr, "  BG%d scroll %d,%d map %04X wide %d high %d\n", l + 1, p->bgLayer[l].hScroll, p->bgLayer[l].vScroll, p->bgLayer[l].tilemapAdr, p->bgLayer[l].tilemapWider, p->bgLayer[l].tilemapHigher);
    fprintf(stderr, "  ram 0600-071F:"); for (int a2 = 0x600; a2 < 0x720; a2 += 2) fprintf(stderr, "%s%04X", (a2 & 0xF) ? " " : "\n   ", r16(a2)); fprintf(stderr, "\n");
    for (int x = 0; x < 6; x += 2) {
      if (r16(D(0x44A) + x) == 0) continue;
      fprintf(stderr, "  desc %d:", x / 2);
      for (int layer = 0; layer < 3; layer++) {
        BgLayer* b = &p->bgLayer[layer]; int ok = 0, n = 0;
        for (int sy = 32; sy < 224; sy += 8) for (int sx = 0; sx < 256; sx += 8) {
          int lx = (sx + b->hScroll) & 0x3ff, ly = (sy + b->vScroll) & 0x3ff;
          uint16_t adr = b->tilemapAdr + (((ly >> 3) & 0x1f) << 5 | ((lx >> 3) & 0x1f));
          if ((lx & 0x100) && b->tilemapWider) adr += 0x400;
          if ((ly & 0x100) && b->tilemapHigher) adr += b->tilemapWider ? 0x800 : 0x400;
          int camx = r16(D(0x41A) + x), camy = r16(D(0x422) + x);
          int t = level_tile(x, unwrap(lx, 1024, camx + sx) >> 3, unwrap(ly, 1024, camy + sy) >> 3);
          if (t < 0) continue;
          n++; if ((uint16_t)t == p->vram[adr & 0x7fff]) ok++;
        }
        fprintf(stderr, "  BG%d %d/%d", layer + 1, ok, n);
      }
      fprintf(stderr, "   cam %d,%d size %dx%d\n", r16(D(0x41A) + x), r16(D(0x422) + x), r16(D(0x44A) + x), r16(D(0x452) + x));
    }
  }
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
