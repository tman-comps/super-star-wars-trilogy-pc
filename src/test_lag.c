// Lag measurement: plays a stage with a fixed input script and reports how far the stage timer
// (game-logic seconds) advanced over N frames, with and without overclock.
//   ssw_lag <rom> <mode> <scene> <frames> <overclock 0/1>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "runner.h"
extern int g_snes_overclock;
static Snes* s;
static void hold(uint16_t m, int n) { for (int f = 0; f < n; f++) {
  uint16_t mm = m;
  if ((s->ram[0x4DA] | s->ram[0x4DB] << 8) == 0x0C) mm = (f & 8) ? 1 << BTN_START : 0;
  for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (mm >> i) & 1);
  s->ram[0x0A79] = 32; s->ram[0x08FB] = 3; runner_frame(s); } }
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  for (int e = 1; e <= 4; e++) { rom[0x1D4A + 4 * e + 2] = strtol(argv[2], 0, 16); rom[0x1D4A + 4 * e + 3] = strtol(argv[3], 0, 16); }
  int frames = atoi(argv[4]);
  s = snes_init(); snes_loadRom(s, rom, len);
  hold(0, 1500);
  for (int i = 0; i < 6; i++) { hold(1 << BTN_START, 4); hold(0, 120); }
  hold(0, 300);
  g_snes_overclock = atoi(argv[5]);
  int t0 = s->ram[0x96F] | s->ram[0x970] << 8;
  uint32_t seed = 5;
  for (int f = 0; f < frames; f += 20) {
    seed = seed * 1103515245u + 12345u;
    static const uint16_t cho[] = {1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_Y), (1 << BTN_LEFT) | (1 << BTN_Y), (1 << BTN_B) | (1 << BTN_Y)};
    hold(cho[(seed >> 16) % 5], 20);
  }
  int t1 = s->ram[0x96F] | s->ram[0x970] << 8;
  printf("timer %d -> %d : %d game seconds in %.1f real seconds\n", t0, t1, t0 - t1, frames / 60.1);
  return 0;
}
