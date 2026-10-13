// Where does the player's sprite art come from? Boots either game (Super Star Wars or Empire, by ROM
// size), starts a new game, then runs / jumps / attacks and logs every DMA into sprite VRAM:
//   f<frame> src <bank:addr> len <bytes> vram <word addr> anim <player anim word>
// plus a summary of the sources per destination. ssw_luke <rom> [frames]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "ppu.h"
#include "dma.h"
#include "runner.h"
#include "recomp_rt.h"
#include "game.h"
int esb_recomp_run(Cpu* cpu);
extern const uint32_t esb_g_hook_addrs[];
extern void (*g_snes_write_hook)(Snes*, uint32_t, uint8_t);

static Snes* s;
static int g_f;
static long g_watch = -1;
static void hook(Snes* sn, uint32_t adr, uint8_t v) {
  if (g_watch >= 0 && (long)(adr & 0xffffff) == g_watch) {   // WATCH=7F36AE: who writes this byte
    static int n; if (n++ < 20) fprintf(stderr, "f%d write %06X=%02X pc %02X:%04X\n", g_f, adr, v, sn->cpu->k, sn->cpu->pc);
  }
  if (((adr >> 16) & 0x7f) >= 0x40 || (adr & 0xffff) != 0x420B || g_f < 2300) return;
  for (int c = 0; c < 8; c++) {
    if (!(v >> c & 1)) continue;
    DmaChannel* ch = &sn->dma->channel[c];
    if (ch->bAdr != 0x18 || ch->fromB) continue;
    uint16_t va = sn->ppu->vramPointer;
    printf("f%d src %02X:%04X len %u vram %04X obj %04X\n", g_f, ch->aBank, ch->aAdr, ch->size ? ch->size : 0x10000, va, sn->ppu->objTileAdr1);
  }
}

static uint16_t buttons(int f) {
  static const int starts[] = {1500, 1564, 1688, 1812, 1936, 2060, 2184};
  for (unsigned i = 0; i < sizeof starts / sizeof *starts; i++) if (f >= starts[i] && f < starts[i] + 4) return 1 << BTN_START;
  if (f < 2600) return 0;
  int t = f - 2600;   // a little routine: run, jump, attack, crouch, run left
  if (t < 120) return 1 << BTN_RIGHT;
  if (t < 160) return (1 << BTN_RIGHT) | (1 << BTN_B);
  if (t < 260) return (t / 8 % 2) << BTN_Y;
  if (t < 300) return 1 << BTN_DOWN;
  if (t < 340) return (1 << BTN_DOWN) | ((t / 8 % 2) << BTN_Y);
  if (t < 440) return 1 << BTN_LEFT;
  if (t < 480) return (1 << BTN_B) | ((t / 6 % 2) << BTN_Y);
  return (t / 30 % 2) << BTN_RIGHT;
}

int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  int frames = argc > 2 ? atoi(argv[2]) : 3300;
  if (len == 0x180000) { g_game = &g_esb; g_recomp_fn = esb_recomp_run; g_hook_list = esb_g_hook_addrs; }
  else g_game = &g_ssw;
  g_use_recomp = !getenv("INTERP");
  if (getenv("WATCH")) g_watch = strtol(getenv("WATCH"), 0, 16);
  s = snes_init(); snes_loadRom(s, rom, len);
  g_snes_write_hook = hook;
  for (g_f = 0; g_f < frames; g_f++) {
    uint16_t b = buttons(g_f);
    for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (b >> i) & 1);
    s->ram[g_game->r_lives] = 3;
    runner_frame(s);
    if (getenv("DUMP") && g_f == atoi(getenv("DUMP"))) {
      FILE* o = fopen("wram.bin", "wb"); fwrite(s->ram, 1, 0x20000, o); fclose(o);
      o = fopen("vram.bin", "wb"); fwrite(s->ppu->vram, 2, 0x8000, o); fclose(o);
      o = fopen("oam.bin", "wb"); fwrite(s->ppu->oam, 2, 0x100, o); fwrite(s->ppu->highOam, 1, 0x20, o); fclose(o);
      o = fopen("cgram.bin", "wb"); fwrite(s->ppu->cgram, 2, 0x100, o); fclose(o);
    }
    if (getenv("SHOT") && g_f % 300 == 299) {
      static uint8_t px[512 * 480 * 4]; snes_setPixels(s, px);
      char nm[64]; snprintf(nm, sizeof nm, "luke_%d.ppm", g_f); FILE* o = fopen(nm, "wb");
      fprintf(o, "P6 256 224 255\n");
      for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) { uint8_t* p = px + ((y * 2 + 16) * 512 + x * 2) * 4; fputc(p[3], o); fputc(p[2], o); fputc(p[1], o); }
      fclose(o);
    }
  }
  return 0;
}
