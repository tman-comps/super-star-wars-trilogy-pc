// Dumps BG layer configuration and renders each BG tilemap (full 512/256 wide) at a gameplay frame,
// to judge how much valid background exists beyond the visible 256 pixels (widescreen feasibility).
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
static Snes* s;
static void hold(int pad, uint16_t m, int n) { for (int f = 0; f < n; f++) { for (int i = 0; i < 12; i++) snes_setButtonState(s, pad, i, (m >> i) & 1); runner_frame(s); } }
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  int extra = atoi(argv[2]);
  s = snes_init(); snes_loadRom(s, rom, len);
  hold(1, 0, 1500);
  for (int i = 0; i < 6; i++) { hold(1, 1 << BTN_START, 4); hold(1, 0, 120); }
  hold(1, 1 << BTN_RIGHT, extra);
  Ppu* p = s->ppu;
  printf("mode %d\n", p->mode);
  for (int l = 0; l < 4; l++) {
    BgLayer* b = &p->bgLayer[l];
    printf("BG%d: map %04X wide %d high %d hscroll %d vscroll %d tiles %04X big %d\n", l + 1, b->tilemapAdr, b->tilemapWider, b->tilemapHigher, b->hScroll, b->vScroll, b->tileAdr, b->bigTiles);
  }
  return 0;
}
