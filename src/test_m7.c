// Mode 7 hi-res test: warps to a Mode 7 stage and writes the normal frame and the K x hi-res frame.
//   ssw_m7 <rom> <mode> <scene> <K> <frames> <prefix>
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
static Snes* s;
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  for (int e = 1; e <= 4; e++) { rom[0x1D4A + 4 * e + 2] = strtol(argv[2], 0, 16); rom[0x1D4A + 4 * e + 3] = strtol(argv[3], 0, 16); }
  int K = atoi(argv[4]), frames = atoi(argv[5]);
  s = snes_init(); snes_loadRom(s, rom, len);
  s->ppu->m7hiresK = K; s->ppu->m7hiresBuf = calloc(256 * K * 224 * K, 4);
  for (int f = 0; f < frames; f++) {
    int b = 0;
    if (f >= 1500 && f < 2300 && (f % 124) < 4) b = 1 << BTN_START;
    if (f >= 2300) b = (f / 40) % 3 ? (1 << BTN_RIGHT) : (1 << BTN_Y);
    for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (b >> i) & 1);
    s->ppu->m7frameUsed = false;
    runner_frame(s);
  }
  ppu_renderMode7Hires(s->ppu);
  char p[256];
  snprintf(p, sizeof p, "%s_hi.ppm", argv[6]); FILE* o = fopen(p, "wb");
  fprintf(o, "P6 %d %d 255\n", 256 * K, 224 * K);
  for (int i = 0; i < 256 * K * 224 * K; i++) { uint32_t c = s->ppu->m7hiresBuf[i]; fputc(c >> 16, o); fputc(c >> 8, o); fputc(c, o); }
  fclose(o);
  snprintf(p, sizeof p, "%s_lo.ppm", argv[6]); o = fopen(p, "wb");
  fprintf(o, "P6 256 224 255\n");
  for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) { uint32_t c = s->ppu->m7out[y][x]; fputc(c >> 16, o); fputc(c >> 8, o); fputc(c, o); }
  fclose(o);
  printf("mode7 used: %d\n", s->ppu->m7frameUsed);
  return 0;
}
