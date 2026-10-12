// Overclock safety test: boots normally, then keeps overclock on only in gameplay modes and clears
// stages with the pad-2 debug key, checking that every following screen still renders.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
extern int g_snes_overclock;
static Snes* s; static int oc;
static uint8_t px[512 * 480 * 4];
static int gm(void) { return s->ram[0x4DA] | s->ram[0x4DB] << 8; }
static void hold(uint16_t m, uint16_t m2, int n) { for (int f = 0; f < n; f++) {
  uint16_t mm = m;
  if (gm() == 0x0C) mm = (f & 8) ? 1 << BTN_START : 0;
  for (int i = 0; i < 12; i++) { snes_setButtonState(s, 1, i, (mm >> i) & 1); snes_setButtonState(s, 2, i, (m2 >> i) & 1); }
  int g = gm();
  g_snes_overclock = oc && (g == 0x04 || g == 0x0E || g == 0x08 || g == 0x10);
  s->ram[0x0A79] = 32; s->ram[0x08FB] = 3; s->ram[0x010C] = 1; runner_frame(s); } }
static double bright(void) { snes_setPixels(s, px); double t = 0; for (int i = 0; i < 512 * 480 * 4; i += 4) t += px[i + 1] + px[i + 2] + px[i + 3]; return t / (512 * 480 * 3); }
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  oc = atoi(argv[2]);
  s = snes_init(); snes_loadRom(s, rom, len);
  hold(0, 0, 1500);
  for (int i = 0; i < 6; i++) { hold(1 << BTN_START, 0, 4); hold(0, 0, 120); }
  for (int st = 0; st < 12; st++) {
    hold(1 << BTN_RIGHT, 0, 400);
    printf("step %2d mode %02X scene %02X brightness %.1f\n", st, gm(), s->ram[0x368], bright());
    hold(0, 1 << BTN_START, 4);
    for (int k = 0; k < 5; k++) { hold(1 << BTN_START, 0, 4); hold(0, 0, 100); }
  }
  return 0;
}
