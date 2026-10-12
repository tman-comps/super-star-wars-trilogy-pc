// Who writes the BG1 tilemap? Logs, per instruction PC, writes to VMADD ($2116/7), VMDATA ($2118/9)
// and DMA starts ($420B) while walking right through Dune Sea. Prints the top PCs.
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "runner.h"
extern void (*g_snes_write_hook)(Snes*, uint32_t, uint8_t);
static Snes* s; static uint32_t cur_pc;
static uint32_t* cnt;
static uint8_t lo; static uint32_t hist[256];
static void hook(Snes* sn, uint32_t adr, uint8_t v) {
  uint32_t a = adr & 0xffff, b = (adr >> 16) & 0x7f;
  if (b >= 0x40) return;
  if (a == 0x2116 && 0) {}
  if (a == 0x2116) lo = v;
  if (a == 0x2117) { uint16_t va = lo | v << 8; hist[va >> 8]++; if (va >= 0x0800 && va < 0x1000) cnt[cur_pc & 0xffffff]++; }
}
static void frame(uint16_t p1) {
  for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (p1 >> i) & 1);
  s->ram[0x0A79] = 32;
  for (int ph = 0; ph < 2; ph++) { uint32_t f = s->frames;
    while (ph == 0 ? s->inVblank : (!s->inVblank && f == s->frames)) { cur_pc = (s->cpu->k << 16) | s->cpu->pc; cpu_runOpcode(s->cpu); } }
  extern void snes_catchupApuPublic(Snes*); snes_catchupApuPublic(s);
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  cnt = calloc(1 << 24, 4);
  s = snes_init(); snes_loadRom(s, rom, len);
  for (int f = 0; f < 1500; f++) frame(0);
  for (int i = 0; i < 6; i++) { for (int f = 0; f < 4; f++) frame(1 << BTN_START); for (int f = 0; f < 120; f++) frame(0); }
  for (int f = 0; f < 200; f++) frame(0);
  g_snes_write_hook = hook;
  for (int f = 0; f < 600; f++) frame(1 << BTN_RIGHT);
  for (int i = 0; i < 256; i++) if (hist[i]) printf("vram %02X00: %u\n", i, hist[i]);
  for (int k = 0; k < 25; k++) {
    uint32_t bi = 0, bv = 0;
    for (uint32_t a = 0; a < (1u << 24); a++) if (cnt[a] > bv) { bv = cnt[a]; bi = a; }
    if (!bv) break;
    printf("%06X %u\n", bi, bv); cnt[bi] = 0;
  }
  return 0;
}
