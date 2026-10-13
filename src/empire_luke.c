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
  set(69, 7, 113, 0);                                      // stand and shoot: upper body + standing legs
  for (int k = 70; k <= 72; k++) set(k, 0, 0, 0);
  for (int k = 76; k <= 86; k++) set(k, 7, 113, 0);        // aim forward
  set(80, 11, 113, 0); set(81, 13, 113, 0); set(82, 13, 113, 0); set(83, 9, 113, 0);   // diagonal up, up, diagonal down
  for (int k = 87; k <= 97; k++) set(k, 7, 3, -9);         // aim while jumping
  for (int k = 98; k <= 109; k++) set(k, 76, 0, 0);        // aim while crouching
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
