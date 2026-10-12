// Renders widescreen frames headlessly: walks through a stage and writes wide PPMs.
//   ssw_wide <rom> <extra> <frames> <out_prefix> [warp_mode warp_scene]
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
#include "widescreen.h"
static Snes* s; static int extra; static int m7k;
static long ck_bad, ck_tot;
static long pop_in, pop_edge, g_spawns, g_alive_sum, g_alive_n;     // objects becoming visible inside the wide view vs at its edges
static uint16_t prevflag[20], prevtype[20];
static void popstat(void) {
  if ((s->ram[0x4DA] | s->ram[0x4DB] << 8) != 0x0004) return;
  int cam = s->ram[0x8E3] | s->ram[0x8E4] << 8, e = s->ppu->wideExtra;
  for (int n = 1; n < 20; n++) {
    uint16_t t = s->ram[0x9D9 + 2 * n] | s->ram[0x9DA + 2 * n] << 8;
    uint16_t f = s->ram[0x1159 + 2 * n] | s->ram[0x115A + 2 * n] << 8;
    int sx = (int16_t)((s->ram[0xF51 + 2 * n] | s->ram[0xF52 + 2 * n] << 8) - cam);
    bool vis = t && !(f & 0x40), was = prevtype[n] == t && prevtype[n] && !(prevflag[n] & 0x40);
    if (vis && !was) { if (sx > -e + 24 && sx < 256 + e - 24) pop_in++; else pop_edge++; }
    prevflag[n] = f; prevtype[n] = t;
  }
}
static void hold(uint16_t m, int n) {
  for (int f = 0; f < n; f++) {
    uint16_t mm = m;
    static int hs = 0;
    if ((s->ram[0x4DA] | s->ram[0x4DB] << 8) == 0x000C) {   // hero select: cycle through buttons until one confirms
      hs++;
      static const uint16_t tryb[] = {1 << BTN_START, 1 << BTN_A, 1 << BTN_B, 1 << BTN_Y, 1 << BTN_X, 1 << BTN_SELECT};
      mm = ((hs / 8) & 1) ? tryb[(hs / 16) % 6] : 0;
    }
    for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (mm >> i) & 1);
    s->ram[0x0A79] = 32; ws_update(extra); if (m7k) s->ppu->m7frameUsed = false; runner_frame(s); popstat();
    { static uint16_t pt[20]; int alive = 0; for (int k = 1; k < 20; k++) { uint16_t t = s->ram[0x9D9 + 2 * k] | s->ram[0x9DA + 2 * k] << 8; if (t && !pt[k]) g_spawns++; if (t) alive++; pt[k] = t; }
      if (s->ram[0x4DA] == 4) { g_alive_sum += alive; g_alive_n++; } }
    if ((s->ram[0x4DA] | s->ram[0x4DB] << 8) == 0x0004 && (f % 10) == 0) { int t; ck_bad += ws_selfcheck(&t); ck_tot += t; }
  }
}
static void shot_hires(const char* name) {
  Ppu* p = s->ppu; if (!p->m7frameUsed) return;
  ppu_renderMode7Hires(p);
  int W = (256 + 2 * p->wideExtra) * m7k;
  char n2[300]; snprintf(n2, sizeof n2, "%s.hi.ppm", name);
  FILE* o = fopen(n2, "wb"); fprintf(o, "P6 %d %d 255\n", W, 224 * m7k);
  for (int i = 0; i < W * 224 * m7k; i++) { uint32_t c = p->m7hiresBuf[i]; fputc(c >> 16, o); fputc(c >> 8, o); fputc(c, o); }
  fclose(o);
}
static void shot(const char* name) {
  int W = 256 + 2 * PPU_WIDE_MAX, e = s->ppu->wideExtra;
  FILE* o = fopen(name, "wb"); fprintf(o, "P6 %d 224 255\n", 256 + 2 * extra);
  for (int y = 0; y < 224; y++) for (int x = -extra; x < 256 + extra; x++) {
    uint32_t c = (x < -e || x >= 256 + e) ? 0 : s->ppu->wideBuffer[y * W + x + PPU_WIDE_MAX];
    fputc(c >> 16, o); fputc(c >> 8, o); fputc(c, o);
  }
  fclose(o);
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  extra = atoi(argv[2]); int frames = atoi(argv[3]);
  uint8_t* rom2 = malloc(len); memcpy(rom2, rom, len);
  if (argc > 6) for (int e = 1; e <= 4; e++) { rom2[0x1D4A + 4 * e + 2] = strtol(argv[5], 0, 16); rom2[0x1D4A + 4 * e + 3] = strtol(argv[6], 0, 16); }
  s = snes_init(); snes_loadRom(s, rom2, len);
  if (getenv("M7K")) { m7k = atoi(getenv("M7K")); s->ppu->m7hiresK = m7k; s->ppu->m7hiresBuf = calloc((256 + 2 * PPU_WIDE_MAX) * 4 * 896, 4); }
  ws_init(s, rom, len);
  extern int g_snes_overclock; if (getenv("OVERCLOCK")) g_snes_overclock = 1;
  extern int g_ws_objects; if (getenv("WS_NOOBJ")) g_ws_objects = 0;
  extern int g_ws_spawn_wide; if (getenv("WS_SPAWNWIDE")) g_ws_spawn_wide = 1;
  extern int g_use_recomp; if (getenv("INTERP")) g_use_recomp = 0;
  hold(0, 1500);
  for (int i = 0; i < 6; i++) { hold(1 << BTN_START, 4); hold(0, 120); }
  char name[256];
  for (int k = 0; k < frames / 300; k++) {
    hold(1 << BTN_RIGHT, 250); hold((1 << BTN_RIGHT) | (1 << BTN_B), 20); hold(0, 30);
    snprintf(name, sizeof name, "%s_%02d.ppm", argv[4], k); shot(name); if (m7k) shot_hires(name);
  }
  if (getenv("WS_REGS")) {
    Ppu* p = s->ppu;
    fprintf(stderr, "mode %d forcedBlank %d brightness %d\n", p->mode, p->forcedBlank, p->brightness);
    for (int l = 0; l < 5; l++) fprintf(stderr, "layer %d main %d sub %d mainWin %d subWin %d\n", l, p->layer[l].mainScreenEnabled, p->layer[l].subScreenEnabled, p->layer[l].mainScreenWindowed, p->layer[l].subScreenWindowed);
    fprintf(stderr, "win1 %d-%d win2 %d-%d clipMode %d\n", p->window1left, p->window1right, p->window2left, p->window2right, p->clipMode);
    for (int l = 0; l < 6; l++) fprintf(stderr, " wl%d en1 %d inv1 %d en2 %d inv2 %d logic %d\n", l, p->windowLayer[l].window1enabled, p->windowLayer[l].window1inversed, p->windowLayer[l].window2enabled, p->windowLayer[l].window2inversed, p->windowLayer[l].maskLogic);
    for (int x = 0; x < 8; x += 2) fprintf(stderr, " desc %d w %d\n", x, s->ram[0x44A + x]);
  }
  fprintf(stderr, "appear: %ld inside the view, %ld at the edges\n", pop_in, pop_edge);
  fprintf(stderr, "spawns %ld, objects alive on average %.2f\n", g_spawns, g_alive_n ? (double)g_alive_sum / g_alive_n : 0.0);
  fprintf(stderr, "selfcheck: %ld mismatches of %ld tiles\n", ck_bad, ck_tot);
  return 0;
}
