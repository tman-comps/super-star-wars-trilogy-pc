// Logs CPU writes to the APU ports ($2140-$2143) with the instruction PC and current scene,
// printing only changes, to find the music / sound command protocol.
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "runner.h"
extern void (*g_snes_write_hook)(Snes*, uint32_t, uint8_t);
static Snes* s; static uint32_t cur_pc; static uint8_t last[4] = {0xff, 0xff, 0xff, 0xff};
static int frame_no, quiet;
static void hook(Snes* sn, uint32_t adr, uint8_t v) {
  uint32_t a = adr & 0xffff, b = (adr >> 16) & 0x7f;
  if (b < 0x40 && a >= 0x2140 && a <= 0x2143) {
    int p = a - 0x2140;
    if (v != last[p] && !quiet)
      printf("f%6d mode %02X scene %02X pc %06X port %d = %02X\n", frame_no, sn->ram[0x4DA], sn->ram[0x368], cur_pc, p, v);
    last[p] = v;
  }
}
static void frame(uint16_t p1) {
  for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (p1 >> i) & 1);
  for (int ph = 0; ph < 2; ph++) { uint32_t f = s->frames;
    while (ph == 0 ? s->inVblank : (!s->inVblank && f == s->frames)) { cur_pc = (s->cpu->k << 16) | s->cpu->pc; cpu_runOpcode(s->cpu); } }
  extern void snes_catchupApuPublic(Snes*); snes_catchupApuPublic(s);
  frame_no++;
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  s = snes_init(); snes_loadRom(s, rom, len);
  g_snes_write_hook = hook;
  quiet = 0;
  for (int f = 0; f < 1500; f++) frame(0);
  for (int i = 0; i < 6; i++) { for (int f = 0; f < 4; f++) frame(1 << BTN_START); for (int f = 0; f < 120; f++) frame(0); }
  for (int f = 0; f < 600; f++) frame((f / 30) % 2 ? 1 << BTN_RIGHT : (1 << BTN_Y));
  return 0;
}
