// Restored content: scene $13, a Death Star stage that was cut from the game (see CUT_CONTENT.md).
// The ROM still has its scene slot, handler and a 3-record spawn list (Luke's start position and
// two placeholder objects); the level setup record was blanked. restore/build_patch.py builds a
// data patch that gives it a layout made from the Death Star chunk set (including chunks no shipped
// level uses) and puts it between the detention block and the following cutscene.
//
// The patch only writes data: new bytes go into end-of-bank padding, plus the blank setup record,
// the stage's spawn-list pointer and two sequence-table entries (one of them a dead duplicate).
// It is applied to the loaded cartridge copy, so it can be switched on and off at any time. The
// header holds only the port's own bytes; the two records it reuses are copied from the player's ROM.
#include <string.h>
#include "snes.h"
#include "cart.h"
#include "restore.h"
#include "restore_patch.h"
#include "widescreen.h"

#define NPATCH (sizeof k_restore / sizeof k_restore[0])
#define NCOPY (sizeof k_restore_copy / sizeof k_restore_copy[0])
static Snes* s;
static int g_applied, g_ok;
static uint8_t g_saved_p[NPATCH][256], g_saved_c[NCOPY][256], g_copied[NCOPY][256];

static uint8_t* at(unsigned a) {
  unsigned o = ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF);
  return o < s->cart->romSize ? s->cart->rom + o : NULL;
}

int restore_init(Snes* snes) {
  s = snes; g_applied = 0; g_ok = 1;
  for (unsigned i = 0; i < NCOPY && g_ok; i++) {
    uint8_t *d = at(k_restore_copy[i].dst), *src = at(k_restore_copy[i].src);
    if (!d || !src || k_restore_copy[i].len > 256) { g_ok = 0; break; }
    memcpy(g_saved_c[i], d, k_restore_copy[i].len);
    memcpy(g_copied[i], src, k_restore_copy[i].len);    // from the player's own ROM
  }
  for (unsigned i = 0; i < NPATCH && g_ok; i++) {
    uint8_t* p = at(k_restore[i].addr);
    if (!p || k_restore[i].len > 256) { g_ok = 0; break; }
    memcpy(g_saved_p[i], p, k_restore[i].len);
  }
  return g_ok;
}

void restore_set(int on) {
  if (!s || !g_ok || on == g_applied) return;
  if (on) {
    for (unsigned i = 0; i < NCOPY; i++) memcpy(at(k_restore_copy[i].dst), g_copied[i], k_restore_copy[i].len);
    for (unsigned i = 0; i < NPATCH; i++) memcpy(at(k_restore[i].addr), k_restore[i].data, k_restore[i].len);
  } else {   // undo in reverse order
    for (int i = (int)NPATCH - 1; i >= 0; i--) memcpy(at(k_restore[i].addr), g_saved_p[i], k_restore[i].len);
    for (int i = (int)NCOPY - 1; i >= 0; i--) memcpy(at(k_restore_copy[i].dst), g_saved_c[i], k_restore_copy[i].len);
  }
  g_applied = on;
}
void restore_update(int wanted) {
  if (!s) return;
  // a save state taken inside the restored stage needs the patch, whatever the setting
  int in_stage = s->ram[0x04DA] == 0x04 && s->ram[0x04DB] == 0 && s->ram[0x0368] == 0x13;
  restore_set(wanted || in_stage);
}

int restore_available(void) { return g_ok; }

// ---- the compactor
// The original spawn list places two otherwise unused objects (type $FE, very wide collision box)
// at the far left and far right of the room, at floor height. We read them as the compactor walls:
// once Luke moves, they close in on the upper corridor. The way out is down the shaft in the
// middle. Port-side logic: walls push Luke, crush him if they meet while he's still up there.
#define WALL_TOP 0x0400     // upper corridor (chunk row 4)
#define WALL_BOTTOM 0x0500
static int g_wl, g_wr, g_wall_on, g_wall_started;   // inner edges in 1/16 px
static int g_start_x, g_lives = -1;

int restore_walls(int* left, int* right, int* top, int* bottom) {
  if (!g_wall_on) return 0;
  *left = g_wl >> 4; *right = g_wr >> 4; *top = WALL_TOP; *bottom = 0x04B4;   // drawn down to the floor
  return 1;
}

void restore_tick(void) {
  if (!s) return;
  uint8_t* r = s->ram;
  int in_stage = g_applied && r[0x04DA] == 0x04 && r[0x04DB] == 0 && r[0x0368] == 0x13;
  if (!in_stage) { g_wall_on = g_wall_started = 0; g_lives = -1; return; }
  int px = r[0x0F51] | r[0x0F52] << 8, py = r[0x0FC9] | r[0x0FCA] << 8;
  if (r[0x08FB] != g_lives) { g_lives = r[0x08FB]; g_wall_on = 0; }   // lost a life: walls reset
  if (!g_wall_on) {   // (re)start of the stage
    g_wall_on = 1; g_wall_started = 0; g_start_x = px;
    g_wl = 0x0030 << 4; g_wr = 0x0900 << 4;
  }
  if (!g_wall_started && (px > g_start_x + 24 || px < g_start_x - 24)) g_wall_started = 1;
  if (r[0x0A79] == 0) return;                       // dying: freeze
  int upper = py >= WALL_TOP && py < WALL_BOTTOM;
  if (g_wall_started && g_wr - g_wl > 0) { g_wl += 6; g_wr -= 6; }   // ~0.4 px per frame each
  if (g_wr < g_wl) g_wr = g_wl = (g_wl + g_wr) / 2;
  if (!upper) return;
  int lo = (g_wl >> 4) + 10, hi = (g_wr >> 4) - 10;
  if (hi - lo < 4) { r[0x0A79] = 0; return; }       // crushed
  static int touch;
  if (px < lo || px > hi) {                         // pinned against a wall: pushed, and it hurts
    px = px < lo ? lo : hi;
    r[0x0F51] = px & 0xff; r[0x0F52] = px >> 8;
    if (++touch % 24 == 0) r[0x0A79] = r[0x0A79] > 2 ? r[0x0A79] - 2 : 0;
  } else touch = 0;
}

// restored stage: the compactor walls (port-side scenery drawn with the level's own wall tiles).
// Screen pixel (sx, sy) lands at buf[(y0 + sy*scale) * stride + x0 + sx*scale]; sx covers [xmin, xmax).
void restore_draw_walls(uint32_t* buf, int stride, int x0, int y0, int scale, int xmin, int xmax) {
  int lx, rx, top, bot;
  if (!restore_walls(&lx, &rx, &top, &bot)) return;
  uint8_t* r = s->ram;
  int camx = r[0x08E3] | r[0x08E4] << 8, camy = r[0x08E5] | r[0x08E6] << 8;
  for (int sy = 26; sy < 224; sy++) {
    int wy = camy + sy;
    if (wy < top || wy >= bot) continue;
    for (int sx = xmin; sx < xmax; sx++) {
      int wx = camx + sx, d;
      if (wx <= lx) d = lx - wx;
      else if (wx >= rx) d = wx - rx;
      else continue;
      int tx = wx <= lx ? (wx - lx) & 255 : (wx - rx) & 255;
      int c = ws_world_pixel(tx, (wy - top) & 255);
      if (c < 0) c = 0x202830;
      if (d < 2) c = 0xd8e0f0;                               // lit inner edge
      else if (d < 6) c = (c >> 1) & 0x7f7f7f;              // shadow line
      for (int j = 0; j < scale; j++) for (int i = 0; i < scale; i++)
        buf[(y0 + sy * scale + j) * stride + x0 + sx * scale + i] = (uint32_t)c;
    }
  }
}

