// Plays into Dune Sea and dumps WRAM, VRAM and the BG scroll registers for offline analysis.
//   ssw_dump <rom> <frames_walking> <outdir> [warp_mode warp_scene]
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
static Snes* s;
static void hold(uint16_t m, int n) { for (int f = 0; f < n; f++) { for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (m >> i) & 1); s->ram[0x0A79] = 32; runner_frame(s); } }
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  if (argc > 5) for (int e = 1; e <= 4; e++) { rom[0x1D4A + 4 * e + 2] = strtol(argv[4], 0, 16); rom[0x1D4A + 4 * e + 3] = strtol(argv[5], 0, 16); }
  s = snes_init(); snes_loadRom(s, rom, len);
  hold(0, 1500);
  for (int i = 0; i < 6; i++) { hold(1 << BTN_START, 4); hold(0, 120); }
  hold(0, 200);
  int n = atoi(argv[2]);
  for (int i = 0; i < n; i += 40) { hold(1 << BTN_RIGHT, 30); hold((1 << BTN_RIGHT) | (1 << BTN_B), 10); }
  char p[512];
  snprintf(p, sizeof p, "%s/wram.bin", argv[3]); FILE* f = fopen(p, "wb"); fwrite(s->ram, 1, 0x20000, f); fclose(f);
  snprintf(p, sizeof p, "%s/vram.bin", argv[3]); f = fopen(p, "wb"); fwrite(s->ppu->vram, 1, 0x10000, f); fclose(f);
  snprintf(p, sizeof p, "%s/regs.txt", argv[3]); f = fopen(p, "w");
  for (int l = 0; l < 4; l++) fprintf(f, "%d %d %d %d %d %d\n", s->ppu->bgLayer[l].hScroll, s->ppu->bgLayer[l].vScroll, s->ppu->bgLayer[l].tilemapAdr, s->ppu->bgLayer[l].tilemapWider, s->ppu->bgLayer[l].tilemapHigher, s->ppu->bgLayer[l].tileAdr);
  fclose(f);
  return 0;
}
