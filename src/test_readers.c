// Which code reads which ROM data? Runs the coverage script in interpreter mode and, for every CPU
// read of ROM banks $83-$9F, records (instruction PC -> count, bytes) to find decompressors/loaders.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "runner.h"
extern void (*g_snes_read_hook)(Snes* snes, uint32_t adr);
static Snes* s;
static uint32_t cur_pc;
static uint32_t* hits;   // per PC (24-bit) count
static uint8_t* firstpc_seen;
static void hook(Snes* sn, uint32_t adr) {
  uint32_t b = (adr >> 16) & 0x7f;
  if (b >= 3 && b < 0x20 && (adr & 0xffff) >= 0x8000) hits[cur_pc & 0xffffff]++;
}
static void frame(uint16_t p1) {
  for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (p1 >> i) & 1);
  s->ram[0x0A79] = 32; s->ram[0x08FB] = 3; s->ram[0x010C] = 1;
  // step manually so we know the PC of each instruction
  while (s->inVblank) { cur_pc = (s->cpu->k << 16) | s->cpu->pc; cpu_runOpcode(s->cpu); }
  uint32_t f = s->frames;
  while (!s->inVblank && f == s->frames) { cur_pc = (s->cpu->k << 16) | s->cpu->pc; cpu_runOpcode(s->cpu); }
  extern void snes_catchupApuPublic(Snes*); snes_catchupApuPublic(s);
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  hits = calloc(1 << 24, 4);
  s = snes_init(); snes_loadRom(s, rom, len);
  g_snes_read_hook = hook;
  for (int f = 0; f < 1500; f++) frame(0);
  for (int i = 0; i < 6; i++) { for (int f = 0; f < 4; f++) frame(1 << BTN_START); for (int f = 0; f < 120; f++) frame(0); }
  for (int f = 0; f < 3000; f++) frame((f / 30) % 3 ? 1 << BTN_RIGHT : (1 << BTN_RIGHT) | (1 << BTN_B));
  uint32_t best[40] = {0};
  for (int k = 0; k < 40; k++) {
    uint32_t bi = 0, bv = 0;
    for (uint32_t a = 0; a < (1u << 24); a++) {
      int skip = 0; for (int j = 0; j < k; j++) if (best[j] == a) skip = 1;
      if (!skip && hits[a] > bv) { bv = hits[a]; bi = a; }
    }
    best[k] = bi;
    if (bv) printf("%06X %u\n", bi, bv);
  }
  return 0;
}
