// Soak test: plays the whole game, new game to credits, with the frontend's optional features
// switched on, and reports crashes, freezes, interpreter fallbacks and widescreen self-check stats.
//   ssw_soak <rom> <seed> <frames_per_stage> <wide_extra> <no_slowdown> <m7k> <restored> [interp]
// Each stage: random play (Luke kept alive), then the game's own pad-2 stage clear.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "ppu.h"
#include "cpu.h"
#include "cart.h"
#include "runner.h"
#include "widescreen.h"
#include "restore.h"
#include "achievements.h"

static Snes* s;
static int wide, slowfix, m7k, restored;
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static long frames, freeze_max, ws_bad, ws_tot, hires_frames;
static uint64_t last_hash; static int same_frames;
static uint32_t wallbuf[512 * 480];
static int stages_seen[256][256];

static uint64_t fnv(const void* p, size_t n, uint64_t h) {
  const uint8_t* b = p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
static void frame(uint16_t p1, uint16_t p2) {
  for (int i = 0; i < 12; i++) { snes_setButtonState(s, 1, i, (p1 >> i) & 1); snes_setButtonState(s, 2, i, (p2 >> i) & 1); }
  s->ram[0x08FB] = 3; if (s->ram[0x0A79] < 8) s->ram[0x0A79] = 8;   // keep Luke alive
  s->ram[0x010C] = 1;                                              // pad-2 stage clear available
  restore_update(restored);
  extern int g_snes_overclock;
  uint16_t m = s->ram[0x04DA] | s->ram[0x04DB] << 8;
  g_snes_overclock = slowfix && (m == 0x04 || m == 0x0E || m == 0x08 || m == 0x10);
  ws_update(wide);
  s->ppu->m7hiresK = m7k; s->ppu->m7frameUsed = false;
  runner_frame(s);
  if (m7k && s->ppu->m7frameUsed) { ppu_renderMode7Hires(s->ppu); hires_frames++; }
  if (!getenv("NOWALLS")) restore_tick();
  restore_draw_walls(wallbuf, 512, 0, 16, 2, 0, 256);
  ach_update(0);
  frames++;
  static long trace_from = -2; if (trace_from == -2) trace_from = getenv("TRACE_FROM") ? atol(getenv("TRACE_FROM")) : -1;
  if (trace_from >= 0 && frames >= trace_from && frames < trace_from + 1500 && frames % 5 == 0) {
    fprintf(stderr, "f%ld m%02X s%02X x%04X y%04X pc%02X:%04X objs", frames, s->ram[0x4DA], s->ram[0x368], s->ram[0xF51] | s->ram[0xF52] << 8, s->ram[0xFC9] | s->ram[0xFCA] << 8, s->cpu->k, s->cpu->pc);
    for (int n = 1; n < 20; n++) if (s->ram[0x9D9 + 2 * n]) fprintf(stderr, " %02X@%04X", s->ram[0x9D9 + 2 * n], s->ram[0xF51 + 2 * n] | s->ram[0xF52 + 2 * n] << 8);
    fprintf(stderr, "\n");
  }
  stages_seen[s->ram[0x04DA]][s->ram[0x0368]] = 1;
  if (m == 0x04 && frames % 30 == 0) { int t; ws_bad += ws_selfcheck(&t); ws_tot += t; }
  // freeze detector: RAM + VRAM unchanged
  uint64_t h = fnv(s->ram, 0x2000, 1469598103934665603ull);
  h = fnv(s->ppu->vram, sizeof(s->ppu->vram), h);
  if (h == last_hash) { if (++same_frames > freeze_max) freeze_max = same_frames; } else same_frames = 0;
  last_hash = h;
}
static void hold(uint16_t p1, uint16_t p2, int n) { for (int i = 0; i < n; i++) frame(p1, p2); }
static void play(int n) {
  static const uint16_t cho[] = {
    1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B), 1 << BTN_Y,
    (1 << BTN_LEFT) | (1 << BTN_Y), (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_A, (1 << BTN_UP) | (1 << BTN_Y),
    (1 << BTN_DOWN) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_X};
  int done = 0;
  while (done < n) { int k = 4 + rnd() % 30; hold(cho[rnd() % 11], 0, k); done += k; }
}
static void skip_screens(int k) {
  for (int i = 0; i < k; i++) { hold(1 << BTN_START, 0, 4); hold(0, 0, 60); hold(1 << BTN_B, 0, 4); hold(0, 0, 60); }
}
int main(int argc, char** argv) {
  if (argc < 8) { fprintf(stderr, "usage: ssw_soak rom seed frames_per_stage wide noslow m7k restored [interp]\n"); return 2; }
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  if (!rom || !runner_check_rom(rom, len)) { fprintf(stderr, "bad rom\n"); return 2; }
  rng = atoi(argv[2]); int per = atoi(argv[3]);
  wide = atoi(argv[4]); slowfix = atoi(argv[5]); m7k = atoi(argv[6]); restored = atoi(argv[7]);
  extern int g_use_recomp; if (argc > 8) g_use_recomp = 0;
  if (getenv("WARP")) { int m, sc; sscanf(getenv("WARP"), "%x %x", &m, &sc); for (int e = 1; e <= 4; e++) { rom[0x1D4A + 4 * e + 2] = m; rom[0x1D4A + 4 * e + 3] = sc; } }
  s = snes_init(); snes_loadRom(s, rom, len);
  snes_setPixelFormat(s, pixelFormatXRGB);
  ws_init(s, rom, len);
  restore_init(s);
  ach_init(s, "/tmp/ssw_soak_");
  ach_set_rom(s->cart->rom);
  s->ppu->m7hiresBuf = calloc((256 + 2 * PPU_WIDE_MAX) * 4 * 896, 4);
  hold(0, 0, 1500);
  skip_screens(4);
  int credits = 0;
  int maxst = getenv("STEPS") ? atoi(getenv("STEPS")) : 60;
  for (int st = 0; st < maxst && !credits; st++) {
    play(per);
    hold(0, 1 << BTN_START, 4);
    skip_screens(5);
    if (getenv("SHOTS")) {
      static uint8_t px[512 * 480 * 4]; snes_setPixels(s, px);
      char nm[256]; snprintf(nm, sizeof nm, "%s_%02d.ppm", getenv("SHOTS"), st);
      FILE* o = fopen(nm, "wb"); fprintf(o, "P6 256 224 255\n");
      for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) { uint8_t* q = px + ((y * 2 + 16) * 512 + x * 2) * 4; fputc(q[2], o); fputc(q[1], o); fputc(q[0], o); }
      fclose(o);
    }
    fprintf(stderr, "step %2d: mode %02X scene %02X  frames %ld  longest freeze %ld\n", st, s->ram[0x4DA], s->ram[0x368], frames, freeze_max);
    if (s->ram[0x4DA] == 0x0A && s->ram[0x368] == 0x1C) credits = 1;
  }
  hold(0, 0, 3000);
  int nst = 0; for (int a = 0; a < 256; a++) for (int b = 0; b < 256; b++) nst += stages_seen[a][b];
  printf("RESULT seed %s wide %d noslow %d m7k %d restored %d %s: frames %ld, reached credits %s, "
         "mode/scene combos %d, longest static stretch %ld frames, interpreter fallbacks %llu, "
         "widescreen self-check %ld/%ld, hi-res frames %ld, scene13 seen %d\n",
         argv[2], wide, slowfix, m7k, restored, g_use_recomp ? "recomp" : "interp", frames, credits ? "yes" : "NO",
         nst, freeze_max, (unsigned long long)runner_misses(), ws_bad, ws_tot, hires_frames, stages_seen[4][0x13]);
  return 0;
}
