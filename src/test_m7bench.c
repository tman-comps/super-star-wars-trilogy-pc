#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  for (int e = 1; e <= 4; e++) { rom[0x1D4A + 4 * e + 2] = 0x0E; rom[0x1D4A + 4 * e + 3] = 0x04; }
  Snes* s = snes_init(); snes_loadRom(s, rom, len);
  int K = argc > 2 ? atoi(argv[2]) : 4; s->ppu->m7hiresK = K; s->ppu->m7hiresBuf = calloc(256 * K * 224 * K, 4);
  for (int f = 0; f < 3000; f++) { int b = (f >= 1500 && f < 2300 && (f % 124) < 4) ? 1 << BTN_START : 0; for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (b >> i) & 1); runner_frame(s); }
  clock_t t = clock();
  for (int i = 0; i < 60; i++) ppu_renderMode7Hires(s->ppu);
  printf("%.2f ms per hi-res frame\n", (clock() - t) * 1000.0 / CLOCKS_PER_SEC / 60);
  return 0;
}
