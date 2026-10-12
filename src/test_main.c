// Headless test driver.
//   ssw_test <rom> <frames> <interp|recomp> <seed> [shot_every] [warp_mode warp_scene]
// Plays a deterministic input script (title skip, then pseudo-random play) and prints one hash
// line per frame of WRAM+VRAM+CGRAM+OAM, so two runs can be diffed. Optionally writes PPM
// screenshots every N frames to shot_XXXXXX.ppm.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}

static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }

static uint16_t script_buttons(int f) {
  // title -> menu -> crawl skips, then random play
  static const int starts[] = {1500, 1564, 1688, 1812, 1936, 2060, 2184};
  for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
    if (f >= starts[i] && f < starts[i] + 4) return 1 << BTN_START;
  if (f < 2300) return 0;
  static uint16_t cur = 0; static int left = 0;
  if (left-- <= 0) {
    static const uint16_t cho[] = {
      1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B), 1 << BTN_Y,
      (1 << BTN_LEFT) | (1 << BTN_Y), 1 << BTN_B, (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_A, 1 << BTN_UP, 1 << BTN_DOWN};
    cur = cho[rnd() % 10];
    left = 4 + rnd() % 36;
  }
  return cur;
}

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: %s rom frames interp|recomp seed [shot_every]\n", argv[0]); return 1; }
  int len;
  uint8_t* rom = runner_load_file(argv[1], &len);
  if (!rom || !runner_check_rom(rom, len)) { fprintf(stderr, "need Super Star Wars (USA) Rev 1\n"); return 1; }
  int frames = atoi(argv[2]);
  g_use_recomp = strcmp(argv[3], "recomp") == 0;
  rng = (uint32_t)atoi(argv[4]);
  int shot = argc > 5 ? atoi(argv[5]) : 0;
  if (argc > 7) {  // warp: every route into the first stage goes to (mode, scene) instead
    int off = len == 0x100200 ? 0x200 : 0;
    for (int e = 1; e <= 4; e++) {
      rom[off + 0x1D4A + 4 * e + 2] = (uint8_t)strtol(argv[6], 0, 16);
      rom[off + 0x1D4A + 4 * e + 3] = (uint8_t)strtol(argv[7], 0, 16);
    }
  }
  Snes* snes = snes_init();
  snes_loadRom(snes, rom, len);
  static uint8_t pixels[512 * 480 * 4];
  for (int f = 0; f < frames; f++) {
    uint16_t b = script_buttons(f);
    for (int i = 0; i < 12; i++) snes_setButtonState(snes, 1, i, (b >> i) & 1);
    if (argc > 7) snes->ram[0x08FB] = 3;   // keep lives topped up on warped test runs
    if (argc > 7) snes->ram[0x08FB] = 3;   // keep lives topped up on warped test runs
    runner_frame(snes);
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, snes->ram, sizeof(snes->ram));
    h = fnv(h, snes->ppu->vram, sizeof(snes->ppu->vram));
    h = fnv(h, snes->ppu->cgram, sizeof(snes->ppu->cgram));
    h = fnv(h, snes->ppu->oam, sizeof(snes->ppu->oam));
    printf("%d %016llx\n", f, (unsigned long long)h);
    if (shot && f % shot == shot - 1) {
      snes_setPixels(snes, pixels);
      char name[64]; snprintf(name, sizeof name, "shot_%06d.ppm", f);
      FILE* o = fopen(name, "wb");
      fprintf(o, "P6 256 224 255\n");
      for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) {
        uint8_t* p = pixels + ((y * 2 + 16) * 512 + x * 2) * 4;   // 512x480 output, 2x scaled, 16 lines of overscan offset
        fputc(p[3], o); fputc(p[2], o); fputc(p[1], o);
      }
      fclose(o);
    }
  }
  uint32_t miss[64]; int n = runner_miss_list(miss, 64);
  fprintf(stderr, "misses: %llu total, %d unique\n", (unsigned long long)runner_misses(), n);
  for (int i = 0; i < n && i < 20; i++) fprintf(stderr, "  %06X\n", miss[i]);
  return 0;
}
