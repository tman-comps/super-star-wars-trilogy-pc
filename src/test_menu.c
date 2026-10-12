// Drives the game's debug menu through the port: boot, start game, enable dev flag,
// open menu with pad-2 L+R, press Right N times on pad 1, Start, then report where we ended up.
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
static Snes* s;
static void hold(int pad, uint16_t m, int frames) {
  for (int f = 0; f < frames; f++) {
    for (int i = 0; i < 12; i++) snes_setButtonState(s, pad, i, (m >> i) & 1);
    s->ram[0x10C] = 1;
    runner_frame(s);
  }
  for (int i = 0; i < 12; i++) snes_setButtonState(s, pad, i, 0);
}
static void shot(const char* name) {
  static uint8_t px[512 * 480 * 4];
  snes_setPixels(s, px);
  FILE* o = fopen(name, "wb"); fprintf(o, "P6 256 224 255\n");
  for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) { uint8_t* p = px + ((y * 2 + 16) * 512 + x * 2) * 4; fputc(p[3], o); fputc(p[2], o); fputc(p[1], o); }
  fclose(o);
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  int rights = atoi(argv[2]);
  s = snes_init(); snes_loadRom(s, rom, len);
  hold(1, 0, 1500);
  for (int i = 0; i < 6; i++) { hold(1, 1 << BTN_START, 4); hold(1, 0, 120); }
  hold(1, 0, 300);
  hold(2, (1 << BTN_L) | (1 << BTN_R), 6); hold(2, 0, 40);
  for (int i = 0; i < rights; i++) { hold(1, 1 << BTN_RIGHT, 4); hold(1, 0, 8); }
  shot("menu.ppm");
  printf("area var %d\n", s->ram[0x876]);
  hold(1, 1 << BTN_START, 4); hold(1, 0, 400);
  for (int i = 0; i < 3; i++) { hold(1, 1 << BTN_START, 4); hold(1, 0, 150); }
  shot("after.ppm");
  printf("mode %02X scene %02X\n", s->ram[0x4DA], s->ram[0x368]);
  return 0;
}
