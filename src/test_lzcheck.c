// Captures real decompressions: when the game's LZSS loader ($81:8722) reaches its end ($81:887A),
// dump source pointer and the output buffer, so tools/lzss.py can be checked against the game.
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "runner.h"
#include "ppu.h"
static Snes* s;
static uint32_t src_ptr; static int n_dumps;
static FILE* out;
static uint8_t rd(uint32_t a) { return snes_read(s, a); }
static void stepf(uint16_t p1) {
  for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (p1 >> i) & 1);
  s->ram[0x0A79] = 32; s->ram[0x08FB] = 3;
  for (int phase = 0; phase < 2; phase++) {
    uint32_t f = s->frames;
    while (phase == 0 ? s->inVblank : (!s->inVblank && f == s->frames)) {
      uint32_t pc = ((s->cpu->k << 16) | s->cpu->pc) & 0x7fffff;
      if (pc == 0x018722) src_ptr = s->ram[0x3E] | s->ram[0x3F] << 8 | s->ram[0x40] << 16;
      if (pc == 0x01887A && n_dumps < 400) {
        uint32_t start = (s->ram[0x85] | s->ram[0x86] << 8) | (s->ram[0x44] << 16);
        uint32_t size = s->ram[0x87] | s->ram[0x88] << 8;
        fprintf(out, "%06X %06X %u ", src_ptr, start, size);
        for (uint32_t i = 0; i < size; i++) fprintf(out, "%02X", rd((start & 0xff0000) | ((start + i) & 0xffff)));
        fprintf(out, "\n"); n_dumps++;
      }
      cpu_runOpcode(s->cpu);
    }
  }
  extern void snes_catchupApuPublic(Snes*); snes_catchupApuPublic(s);
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  s = snes_init(); snes_loadRom(s, rom, len);
  out = fopen(argv[2], "w");
  for (int f = 0; f < 1500; f++) stepf(0);
  for (int i = 0; i < 6; i++) { for (int f = 0; f < 4; f++) stepf(1 << BTN_START); for (int f = 0; f < 120; f++) stepf(0); }
  for (int f = 0; f < 4000; f++) stepf((f / 30) % 3 ? 1 << BTN_RIGHT : (1 << BTN_RIGHT) | (1 << BTN_B));
  fclose(out);
  FILE* v = fopen("vram.bin", "wb"); fwrite(s->ppu->vram, 1, 0x10000, v); fclose(v);
  v = fopen("wram.bin", "wb"); fwrite(s->ram, 1, 0x20000, v); fclose(v);
  fprintf(stderr, "%d decompressions captured\n", n_dumps);
  return 0;
}
