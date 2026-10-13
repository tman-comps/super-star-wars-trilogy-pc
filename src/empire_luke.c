// Empire Luke in Super Star Wars.
//
// Both games draw the player from one "sprite set" block that the game unpacks into WRAM at stage start
// (unpacker $81:85DC in Super Star Wars, $81:87D4 in Empire; same headerless LZSS). Super Star Wars' Luke
// is the block at $85:8002, unpacked to $7F:1820; Empire's Luke (one outfit for the whole game) is at
// $8A:8002. Block layout, identical in both games:
//   +0 word  pool offset (32-byte 4bpp tiles start here)
//   +2 word  number of entries N
//   +4       entries for game pose 1..N: offsets of frames from the block start
//   frames:  parts until $FF: flags (bits 4-5: 0 = one 8x8 tile, 1 = 16x16 from four tiles, 2/3 = a
//            sub-frame; bits 6-7 = flip), dx, dy (signed bytes), then 1 or 4 tile words (offsets into the
//            pool) or a sub-frame number s (= pose s+1)
// The drawer is $81:BFC7 (Super Star Wars) / $81:B5FB (Empire).
//
// We build a new block in Super Star Wars' numbering: each of its 148 poses becomes the matching Empire
// pose (Empire draws Luke as legs + upper body, so most become two sub-frames), the lightsaber poses keep
// Super Star Wars' own frames, and the rest of Empire's frames and tiles that those need are copied in.
// When the game has unpacked its own Luke block, we overwrite it with ours, and Luke's palette (sprite
// palette 7) gets Empire Luke's colours ($96:9D54 in Empire, replacing $90:C782).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "empire_luke.h"
#include "ppu.h"

#define SSW_BLOCK 0x858002
#define ESB_BLOCK 0x8A8002
#define WRAM_BLOCK 0x11820          // $7F:1820
#define WRAM_BLOCK_END 0x1914E      // $7F:914E: the next block starts here
#define SSW_PAL 0x90C782            // Super Star Wars Luke, 16 colours (sprite palette 7)
#define ESB_PAL 0x969D54            // Empire Luke, 16 colours
#define ESB_FACE 0x9BFD7D           // Empire's HUD face: four 8x8 tiles (top-left, top-right, bottom-left, bottom-right)

static uint8_t* g_ssw;  static int g_ssw_n;     // Super Star Wars' Luke block (to recognise it in WRAM)
static uint8_t* g_out;  static int g_out_n;     // ours
static uint8_t g_pal_ssw[32], g_pal_esb[32], g_face[128];
static const uint8_t* g_sswrom; static int g_sswlen;
static int g_ready;

static uint32_t rom_off(uint32_t a) { return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF); }

int eluke_unpack(const uint8_t* rom, int len, uint32_t adr, uint8_t* out, int max) {
  uint32_t p = rom_off(adr); int n = 0;
  for (;;) {
    if (p >= (uint32_t)len) return -1;
    uint8_t fl = rom[p++];
    for (int b = 0; b < 8; b++, fl >>= 1) {
      if (fl & 1) { if (n >= max || p >= (uint32_t)len) return -1; out[n++] = rom[p++]; continue; }
      if (p + 1 >= (uint32_t)len) return -1;
      int d = rom[p] | (rom[p + 1] >> 4) << 8, c = (rom[p + 1] & 15) + 3; p += 2;
      if (!d) return n;
      if (d > n || n + c > max) return -1;
      for (int i = 0; i < c; i++, n++) out[n] = out[n - d];
    }
  }
}

// Super Star Wars pose -> Empire poses drawn for it (first is on top; dy moves that one up/down), KEEP =
// Super Star Wars' own frame, 0 = nothing. Both in the games' own numbering (entry at block+2+2*pose). Found
// by logging what each game draws for every action (src/test_luke.c) and checking the pictures.
enum { KEEP = -1 };
static int16_t k_map[149][2];
static int8_t k_dy[149][2];
static void set(int k, int a, int b, int dyb) { k_map[k][0] = a; k_map[k][1] = b; k_dy[k][1] = dyb; }
static void build_map(void) {
  memset(k_dy, 0, sizeof k_dy);
  for (int k = 1; k <= 148; k++) set(k, KEEP, 0, 0);     // lightsaber poses stay Super Star Wars'
  for (int k = 1; k <= 8; k++) set(k, 21 + k, 13 + k, 0);  // run: upper body + legs
  for (int k = 9; k <= 16; k++) set(k, 0, 0, 0);           // blaster in hand while running: in Empire's body
  set(25, 93, 0, 0);                                       // take off
  for (int k = 26; k <= 28; k++) set(k, 5, 3, -9);         // jump: upper body + bent legs
  for (int k = 29; k <= 32; k++) set(k, 0, 0, 0);
  set(37, 76, 0, 0); set(38, 75, 0, 0);                    // land, crouch
  set(39, 0, 0, 0); set(40, 0, 0, 0);
  for (int k = 43; k <= 45; k++) set(k, 103, 0, 0);        // hit
  for (int k = 46; k <= 48; k++) set(k, 0, 0, 0);
  for (int k = 52; k <= 56; k++) set(k, k - 6, 0, 0);      // knocked down, dying
  for (int k = 57; k <= 61; k++) set(k, 0, 0, 0);
  set(67, 72, 0, 0); set(68, 73, 0, 0);                    // turn, stand
  set(69, 7, 113, 0);                                      // stand and shoot (blaster): upper body + standing legs
  for (int k = 70; k <= 72; k++) set(k, 0, 0, 0);
  // lightsaber swings (Super Star Wars' blade is drawn on top): Empire's swing upper bodies holding the hilt
  static const int16_t swing[11] = {136, 136, 137, 138, 139, 127, 128, 139, 139, 136, 136};
  for (int k = 76; k <= 86; k++) set(k, swing[k - 76], 113, 0);                // standing: + standing legs
  for (int k = 87; k <= 97; k++) set(k, swing[k - 87], 3, -9);                 // jumping: + bent legs
  for (int k = 98; k <= 109; k++) set(k, k - 98 < 11 ? swing[k - 98] : 136, 2, -3);   // crouching: + crouched legs
  for (int k = 124; k <= 132; k++) set(k, 152 + (k - 124 < 7 ? k - 124 : 7), 0, 0);   // somersault
}

typedef struct { const uint8_t* b; int n; } Blk;
static int w16(const uint8_t* b, int i) { return b[i] | b[i + 1] << 8; }
static int frame_len(const Blk* B, int fo) {
  int p = fo;
  while (p < B->n && B->b[p] != 0xFF) { int t = B->b[p] & 0x30; p += 3 + (t >= 0x20 ? 2 : t == 0x10 ? 8 : 2); }
  return p < B->n ? p + 1 - fo : -1;
}

// frames to copy: (source 0 = Super Star Wars, 1 = Empire, pose) -> new pose number
#define MAXF 400
static struct { int src, pose, newpose, fo, len; } g_f[MAXF];
static int g_nf;
static int want(int src, int pose) {
  for (int i = 0; i < g_nf; i++) if (g_f[i].src == src && g_f[i].pose == pose) return i;
  if (g_nf >= MAXF) return -1;
  g_f[g_nf].src = src; g_f[g_nf].pose = pose; return g_nf++;
}

static int build(const Blk* S, const Blk* E) {
  const Blk* B[2] = {S, E};
  int sN = w16(S->b, 2), eN = w16(E->b, 2);
  if (sN != 148 || eN < 160 || eN > 200) return 0;
  build_map();
  g_nf = 0;
  for (int k = 1; k <= 148; k++) {
    if (k_map[k][0] == KEEP) want(0, k);
    else for (int j = 0; j < 2; j++) if (k_map[k][j] > 0) want(1, k_map[k][j]);
  }
  for (int i = 0; i < g_nf; i++) {   // pull in sub-frames
    const Blk* b = B[g_f[i].src];
    int n = g_f[i].src ? eN : sN;
    if (g_f[i].pose < 1 || g_f[i].pose > n) return 0;
    int fo = w16(b->b, 2 + 2 * g_f[i].pose), L = frame_len(b, fo);
    if (fo <= 0 || L < 1) return 0;
    g_f[i].fo = fo; g_f[i].len = L;
    for (int p = fo; b->b[p] != 0xFF;) {
      int t = b->b[p] & 0x30; p += 3;
      if (t >= 0x20) { if (want(g_f[i].src, w16(b->b, p) + 1) < 0) return 0; p += 2; }
      else p += t == 0x10 ? 8 : 2;
    }
  }
  int N = 148 + g_nf, hdr = 4 + 2 * N;
  for (int i = 0; i < g_nf; i++) g_f[i].newpose = 149 + i;
  int cap = WRAM_BLOCK_END - WRAM_BLOCK;
  uint8_t* out = calloc(1, cap); if (!out) return 0;
  uint8_t* tiles = calloc(1, cap); int nt = 0;
  // tile dedupe: (src, word) -> new word; small linear table is enough (a few hundred tiles)
  static struct { int src, word, nw; } tm[2048]; int ntm = 0;
  int pos = hdr, ok = 1;
  #define PUT16(at, v) do { out[at] = (uint8_t)(v); out[(at) + 1] = (uint8_t)((v) >> 8); } while (0)
  int* entry = calloc(N + 1, sizeof(int));
  for (int i = 0; i < g_nf && ok; i++) {
    const Blk* b = B[g_f[i].src];
    int P = w16(b->b, 0), fo = g_f[i].fo, L = g_f[i].len;
    if (pos + L > cap) { ok = 0; break; }
    memcpy(out + pos, b->b + fo, L);
    for (int p = fo; b->b[p] != 0xFF;) {
      int t = b->b[p] & 0x30; p += 3;
      int cnt = t >= 0x20 ? 0 : t == 0x10 ? 4 : 1;
      if (t >= 0x20) { int j = want(g_f[i].src, w16(b->b, p) + 1); PUT16(pos + p - fo, g_f[j].newpose - 1); p += 2; continue; }
      for (int k = 0; k < cnt; k++, p += 2) {
        int word = w16(b->b, p), nw = -1;
        for (int m = 0; m < ntm; m++) if (tm[m].src == g_f[i].src && tm[m].word == word) { nw = tm[m].nw; break; }
        if (nw < 0) {
          if (ntm >= 2048 || P + word + 32 > b->n || nt + 32 > cap) { ok = 0; break; }
          nw = nt; memcpy(tiles + nt, b->b + P + word, 32); nt += 32;
          tm[ntm].src = g_f[i].src; tm[ntm].word = word; tm[ntm].nw = nw; ntm++;
        }
        PUT16(pos + p - fo, nw);
      }
    }
    entry[g_f[i].newpose] = pos; pos += L;
  }
  int empty = pos; if (pos < cap) out[pos++] = 0xFF;
  for (int k = 1; k <= 148 && ok; k++) {
    if (k_map[k][0] == KEEP) { entry[k] = entry[g_f[want(0, k)].newpose]; continue; }
    if (k_map[k][0] == 0) { entry[k] = empty; continue; }
    if (pos + 11 > cap) { ok = 0; break; }
    entry[k] = pos;
    for (int j = 0; j < 2; j++) if (k_map[k][j] > 0) {
      out[pos++] = 0x20; out[pos++] = 0; out[pos++] = (uint8_t)k_dy[k][j];
      int np = g_f[want(1, k_map[k][j])].newpose - 1; PUT16(pos, np); pos += 2;
    }
    out[pos++] = 0xFF;
  }
  if (ok && pos + nt > cap) ok = 0;
  if (ok) {
    PUT16(0, pos); PUT16(2, N);
    for (int k = 1; k <= N; k++) PUT16(2 + 2 * k, entry[k]);
    memcpy(out + pos, tiles, nt);
    g_out = out; g_out_n = pos + nt;
  } else free(out);
  free(tiles); free(entry);
  return ok;
}

int eluke_init(const uint8_t* ssw_rom, int ssw_len, const uint8_t* esb_rom, int esb_len) {
  g_ready = 0;
  if (!ssw_rom || !esb_rom) return 0;
  static uint8_t sb[0x10000], eb[0x10000];
  int sn = eluke_unpack(ssw_rom, ssw_len, SSW_BLOCK, sb, sizeof sb);
  int en = eluke_unpack(esb_rom, esb_len, ESB_BLOCK, eb, sizeof eb);
  if (sn != WRAM_BLOCK_END - WRAM_BLOCK || en < 0x1000) return 0;
  free(g_ssw); g_ssw = malloc(sn); memcpy(g_ssw, sb, sn); g_ssw_n = sn;
  Blk S = {sb, sn}, E = {eb, en};
  free(g_out); g_out = NULL;
  if (!build(&S, &E)) return 0;
  memcpy(g_pal_ssw, ssw_rom + rom_off(SSW_PAL), 32);
  g_sswrom = ssw_rom; g_sswlen = ssw_len;
  memcpy(g_pal_esb, esb_rom + rom_off(ESB_PAL), 32);
  memcpy(g_face, esb_rom + rom_off(ESB_FACE), 128);
  g_ready = 1;
  return 1;
}

int eluke_ready(void) { return g_ready; }

void eluke_frame(Snes* s) {
  if (!g_ready) return;
  uint8_t* r = s->ram;
  // the game has just unpacked its own Luke: put ours in its place (Han / Chewie blocks don't match)
  if (r[WRAM_BLOCK] == g_ssw[0] && r[WRAM_BLOCK + 1] == g_ssw[1] && !memcmp(r + WRAM_BLOCK, g_ssw, g_ssw_n))
  {
    memcpy(r + WRAM_BLOCK, g_out, g_out_n);
    memset(r + 0x2000, 0xFF, 0x500);   // the drawer's tile cache ($7E:2000, cleared the same way by $81:C26D)
  }
  // palette 7 (colours 1-15): the game's CGRAM copy at $7E:1D9F and the two fade buffers, and CGRAM itself
  static const uint32_t bufs[] = {0x1F81, 0x26E2, 0x28E2};
  for (int i = 0; i < 3; i++)
    if (!memcmp(r + bufs[i], g_pal_ssw + 2, 30)) memcpy(r + bufs[i], g_pal_esb + 2, 30);
  uint16_t* cg = s->ppu->cgram + 0xF0;
  int same = 1;
  for (int i = 1; i < 16 && same; i++) same = cg[i] == (g_pal_ssw[2 * i] | g_pal_ssw[2 * i + 1] << 8);
  if (same) for (int i = 1; i < 16; i++) cg[i] = g_pal_esb[2 * i] | g_pal_esb[2 * i + 1] << 8;
  // the HUD face (sprite 0 at 16,8 in palette 7) is drawn with Luke's palette: show Empire's face in it
  Ppu* p = s->ppu;
  if (p->oam[0] == (16 | 8 << 8) && ((p->oam[1] >> 9) & 7) == 7 && (p->highOam[0] & 2)) {
    int t = p->oam[1] & 0x1FF, base = p->objTileAdr1;
    if (t & 0x100) base = p->objTileAdr2 - 0x1000;   // second name table (its gap is already in objTileAdr2)
    static const int sub[4] = {0, 1, 16, 17};
    for (int k = 0; k < 4; k++) {
      uint16_t* v = p->vram + ((base + ((t + sub[k]) & 0x1FF) * 16) & 0x7FFF);
      for (int i = 0; i < 16; i++) v[i] = g_face[32 * k + 2 * i] | g_face[32 * k + 2 * i + 1] << 8;
    }
  }
}

// ---------------------------------------------------------------- moves and the Force
// Super Star Wars object tables (word per slot, slot 0 = player): HP $0A79, script wait $0C59, X $0F51,
// X speed $0F79, Y $0FC9, Y speed $0FF1 (1/256 px per frame; a jump starts at -1792, gravity +96 a frame).
// Luke already slides (Down + jump while running) and somersaults (Up + jump) in Super Star Wars.
#define R_HP 0x0A79
#define R_WAIT 0x0C59
#define R_X 0x0F51
#define R_VX 0x0F79
#define R_Y 0x0FC9
#define R_VY 0x0FF1
#define SLOTS 20
enum { B_B = 1 << 0, B_UP = 1 << 4, B_LEFT = 1 << 6, B_RIGHT = 1 << 7, B_X = 1 << 9, B_L = 1 << 10, B_R = 1 << 11 };
// L/R go through the 8 Force powers and the thermal detonators (item 8, 9 per stage, no Force needed)
#define NSEL 9
static const char* const k_power[NSEL] = {"HEAL", "ELEVATION", "SLOWDOWN", "SABER CONTROL", "SABER DEFLECT", "FREEZE",
                                          "INVISIBLE", "MIND CONTROL", "THERMAL DETONATOR"};
static const uint8_t k_cost[NSEL] = {30, 0, 25, 20, 20, 30, 40, 35, 0};   // Elevation: 1 per 4 frames while held
static struct {
  int force;          // 0..100 (x16 inside: g.f16)
  int f16;
  int sel;
  int air, ground, dj_used;
  int slow, freeze, invis, deflect, mind, saber;   // frames left
  int hp_max;
  int16_t keep_vx[SLOTS], keep_vy[SLOTS];
  int was_frozen;
  int bombs, facing;   // thermal detonators left; last direction pressed (1 right, -1 left)
  uint16_t last;
} g;

static int16_t rw(const uint8_t* r, int a) { return (int16_t)(r[a] | r[a + 1] << 8); }
static void ww(uint8_t* r, int a, int v) { r[a] = (uint8_t)v; r[a + 1] = (uint8_t)(v >> 8); }

int eluke_active(Snes* s) {
  return g_ready && g_out && !memcmp(s->ram + WRAM_BLOCK, g_out, 64);   // our sprite set is the player's
}

// enemies: objects 1-19 that have hit points (shots, effects and pickups don't)
static int is_enemy(const uint8_t* r, int slot) { return slot > 0 && rw(r, R_HP + 2 * slot) > 0; }

// Send object `slot` an event the way $80:CF91 does: its type's definition ($82:FDCD + type) holds at +$2F a
// 24-bit pointer to [event][script word] rows ($FF ends); the matching script starts at once. $44 = defeated.
static int send_event(uint8_t* r, int slot, int ev) {
  int type = rw(r, 0x09D9 + 2 * slot);
  if (!g_sswrom || type <= 0 || type > 0xFE) return 0;
  uint32_t t = rom_off(0x82FDCD) + type;
  int def = g_sswrom[t] | g_sswrom[t + 1] << 8;
  if (def < 0x8000) return 0;
  uint32_t d = rom_off(0x820000 | def);
  uint32_t st = g_sswrom[d + 0x2F] | g_sswrom[d + 0x30] << 8 | g_sswrom[d + 0x31] << 16;
  if ((st & 0xFFFF) < 0x8000 || rom_off(st) + 3 * 64 > (uint32_t)g_sswlen) return 0;
  for (uint32_t o = rom_off(st); g_sswrom[o] != 0xFF; o += 3)
    if (g_sswrom[o] == ev) {
      ww(r, 0x0B69 + 2 * slot, ev);
      ww(r, 0x0B91 + 2 * slot, g_sswrom[o + 1] | g_sswrom[o + 2] << 8);
      ww(r, 0x0BB9 + 2 * slot, st >> 16);
      ww(r, 0x0C09 + 2 * slot, 0); ww(r, R_WAIT + 2 * slot, 0);
      return 1;
    }
  return 0;
}

static void hold_enemies(uint8_t* r, int on) {
  for (int i = 1; i < SLOTS; i++) {
    if (!is_enemy(r, i)) continue;
    if (on) {
      if (!g.was_frozen) { g.keep_vx[i] = rw(r, R_VX + 2 * i); g.keep_vy[i] = rw(r, R_VY + 2 * i); }
      if (rw(r, R_WAIT + 2 * i) < 2) ww(r, R_WAIT + 2 * i, 2);   // their scripts wait
      ww(r, R_VX + 2 * i, 0); ww(r, R_VY + 2 * i, 0);
    } else if (g.was_frozen) {
      ww(r, R_VX + 2 * i, g.keep_vx[i]); ww(r, R_VY + 2 * i, g.keep_vy[i]);
    }
  }
  g.was_frozen = on;
}

void eluke_play(Snes* s, uint16_t held) {
  uint8_t* r = s->ram;
  uint16_t pressed = held & ~g.last;
  g.last = held;
  if (!eluke_active(s)) { memset(&g, 0, sizeof g); g.last = held; return; }
  if (!g.f16 && !g.force) { g.f16 = 100 * 16; g.bombs = 9; g.facing = 1; }   // full meter when he first appears (New Game+ Luke)
  if (held & B_RIGHT) g.facing = 1;
  if (held & B_LEFT) g.facing = -1;
  int hp = rw(r, R_HP);
  if (hp > g.hp_max) g.hp_max = hp;

  // double jump: jump again once while in the air (not straight after take-off)
  int vy = rw(r, R_VY);
  if (vy) { g.air++; g.ground = 0; } else if (++g.ground >= 3) { g.air = 0; g.dj_used = 0; }
  if ((pressed & B_B) && g.air > 6 && !g.dj_used && !(held & B_UP)) { ww(r, R_VY, -1856); g.dj_used = 1; }

  // the Force: L/R choose, X uses (Elevation while held); the meter refills slowly
  if (pressed & B_L) g.sel = (g.sel + NSEL - 1) % NSEL;
  if (pressed & B_R) g.sel = (g.sel + 1) % NSEL;
  if (g.f16 < 100 * 16) g.f16 += 1;   // about 1 point every 16 frames
  int force = g.f16 / 16;
  if ((pressed & B_X) && g.sel == 8 && g.bombs > 0) {   // thermal detonator: blasts the enemies ahead
    g.bombs--;
    int px = rw(r, R_X) + 64 * g.facing, py = rw(r, R_Y);
    for (int i = 1; i < SLOTS; i++) {
      if (!is_enemy(r, i)) continue;
      int dx = rw(r, R_X + 2 * i) - px, dy = rw(r, R_Y + 2 * i) - py;
      if (dx > -72 && dx < 72 && dy > -80 && dy < 40) {
        int h = rw(r, R_HP + 2 * i) - 8;
        if (h > 0) ww(r, R_HP + 2 * i, h);
        else if (rw(r, 0x0B69 + 2 * i) < 0x44) { ww(r, R_HP + 2 * i, 0); send_event(r, i, 0x44); }
      }
    }
  } else if ((pressed & B_X) && g.sel != 1 && g.sel != 8 && force >= k_cost[g.sel]) {
    g.f16 -= k_cost[g.sel] * 16;
    switch (g.sel) {
      case 0: ww(r, R_HP, hp + 10 > g.hp_max ? g.hp_max : hp + 10); break;   // heal
      case 2: g.slow = 6 * 60; break;
      case 3: g.saber = 60; break;
      case 4: g.deflect = 6 * 60; break;
      case 5: g.freeze = 5 * 60; break;
      case 6: g.invis = 8 * 60; break;
      case 7: g.mind = 8 * 60; break;
    }
  }
  if (g.sel == 1 && (held & B_X) && g.f16 >= 16) {   // elevation: float up
    g.f16 -= 4;
    ww(r, R_VY, vy > -320 ? -320 : vy);
  }
  // effects
  if (g.invis || g.deflect || g.mind) { if (hp < g.hp_max && rw(r, R_HP) < hp) ww(r, R_HP, hp); }
  if (g.invis || g.deflect || g.freeze || g.mind) ww(r, R_HP, rw(r, R_HP) < hp ? hp : rw(r, R_HP));   // no damage
  int hold = g.freeze || g.mind || (g.slow && (s->frames & 1));
  hold_enemies(r, hold);
  if (g.saber) {   // saber control: the thrown saber hits enemies around Luke
    if (g.saber % 10 == 0) {
      int px = rw(r, R_X), py = rw(r, R_Y);
      for (int i = 1; i < SLOTS; i++) {
        if (!is_enemy(r, i)) continue;
        int dx = rw(r, R_X + 2 * i) - px, dy = rw(r, R_Y + 2 * i) - py;
        if (dx > -112 && dx < 112 && dy > -64 && dy < 32) {
          int h = rw(r, R_HP + 2 * i) - 4;
          if (h > 0) ww(r, R_HP + 2 * i, h);
          else if (rw(r, 0x0B69 + 2 * i) < 0x44) { ww(r, R_HP + 2 * i, 0); send_event(r, i, 0x44); }   // defeated
        }
      }
    }
  }
  if (g.slow) g.slow--;
  if (g.freeze) g.freeze--;
  if (g.invis) g.invis--;
  if (g.deflect) g.deflect--;
  if (g.mind) g.mind--;
  if (g.saber) g.saber--;
}

int eluke_hud(char* out, int n) {
  if (!g_ready || !g.f16) return 0;
  if (g.sel == 8) snprintf(out, n, "FORCE %d DETONATORS %d", g.f16 / 16, g.bombs);
  else snprintf(out, n, "FORCE %d %s", g.f16 / 16, k_power[g.sel]);
  return 1;
}
