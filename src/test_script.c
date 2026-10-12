// Scriptable driver for quick experiments. Reads commands from stdin:
//   hold <buttons> <frames>     buttons: comma list of B,Y,SEL,START,UP,DOWN,LEFT,RIGHT,A,X,L,R or -
//   pad2 <buttons> <frames>
//   poke <addr hex> <value hex> / poke16 <addr> <value>
//   shot <file.ppm>             dump <file.bin> (WRAM)        print <addr hex> [count]
//   warp <mode hex> <scene hex> (only valid as first command; patches the scene table)
//   god 0|1                     hold HP/lives every frame
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "dma.h"
#include "ppu.h"
#include "runner.h"
#include "restore.h"
#include "widescreen.h"
#include "extras.h"
#include "cart.h"
static Snes* s; static int god;
extern void (*g_snes_write_hook)(Snes*, uint32_t, uint8_t);
static uint8_t apu_p[4];
static void apu_hook(Snes* sn, uint32_t adr, uint8_t v) {
  uint32_t a = adr & 0xffff; if (((adr >> 16) & 0x7f) >= 0x40 || a < 0x2140 || a > 0x2143) return;
  apu_p[a - 0x2140] = v;
  if (a == 0x2143 && (v == 0x0E || v == 0x06)) printf("APU %02X %02X%02X  scene %02X\n", v, apu_p[1], apu_p[0], sn->ram[0x368]);
}
static uint16_t parse(const char* t) {
  static const char* n[] = {"B","Y","SEL","START","UP","DOWN","LEFT","RIGHT","A","X","L","R"};
  uint16_t m = 0; char buf[128]; strncpy(buf, t, 127); buf[127] = 0;
  for (char* p = strtok(buf, ","); p; p = strtok(NULL, ",")) for (int i = 0; i < 12; i++) if (!strcmp(p, n[i])) m |= 1 << i;
  return m;
}
static long spawns, overflow; static int g_extras; static uint8_t prevt[20]; static int pulse;
static void run(uint16_t p1, uint16_t p2, int n) {
  for (int f = 0; f < n; f++) {
    if (pulse && (f & 4)) p1 &= ~(1 << 1);
    for (int i = 0; i < 12; i++) { snes_setButtonState(s, 1, i, (p1 >> i) & 1); snes_setButtonState(s, 2, i, (p2 >> i) & 1); }
    if (god) { s->ram[0x0A79] = s->ram[0x08FF] ? s->ram[0x08FF] : 32; s->ram[0x08FB] = 3; }
    runner_frame(s);
    restore_tick();
    if (g_extras) extras_frame(0);
    if (s->ppu->rangeOver || s->ppu->timeOver) overflow++;
    for (int k = 1; k < 20; k++) { uint8_t t = s->ram[0x9D9 + 2 * k]; if (t && !prevt[k]) spawns++; prevt[k] = t; }
  }
}
static void shot(const char* name) {
  static uint8_t px[512 * 480 * 4];
  snes_setPixels(s, px);
  restore_draw_walls((uint32_t*)px, 512, 0, 16, 2, 0, 256);
  FILE* o = fopen(name, "wb"); fprintf(o, "P6 256 224 255\n");
  for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) { uint8_t* p = px + ((y * 2 + 16) * 512 + x * 2) * 4; fputc(p[2], o); fputc(p[1], o); fputc(p[0], o); }
  fclose(o);
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  char line[4096], cmd[32], a1[128], a2[2048];
  int started = 0;
  while (fgets(line, sizeof line, stdin)) {
    a1[0] = a2[0] = 0;
    if (sscanf(line, "%31s %127s %2047s", cmd, a1, a2) < 1 || cmd[0] == '#') continue;
    if (!strcmp(cmd, "warp") && !started) { for (int e = 1; e <= 4; e++) { rom[0x1D4A + 4 * e + 2] = strtol(a1, 0, 16); rom[0x1D4A + 4 * e + 3] = strtol(a2, 0, 16); } continue; }
    if (!strcmp(cmd, "romw") && !started) {   // romw <snes addr hex> <hex bytes>: patch ROM data before boot
      long a = strtol(a1, 0, 16); long o = ((a >> 16) & 0x7f) * 0x8000 + (a & 0x7fff);
      for (int i = 0; a2[2 * i] && a2[2 * i + 1]; i++) { char b[3] = {a2[2 * i], a2[2 * i + 1], 0}; rom[o + i] = strtol(b, 0, 16); }
      continue;
    }
    if (!started) { s = snes_init(); snes_loadRom(s, rom, len); snes_setPixelFormat(s, pixelFormatXRGB); started = 1; }
    if (!strcmp(cmd, "hold")) run(a1[0] == '-' ? 0 : parse(a1), 0, atoi(a2));
    else if (!strcmp(cmd, "pad2")) run(0, a1[0] == '-' ? 0 : parse(a1), atoi(a2));
    else if (!strcmp(cmd, "poke")) s->ram[strtol(a1, 0, 16)] = strtol(a2, 0, 16);
    else if (!strcmp(cmd, "poke16")) { int a = strtol(a1, 0, 16), v = strtol(a2, 0, 16); s->ram[a] = v; s->ram[a + 1] = v >> 8; }
    else if (!strcmp(cmd, "shot")) shot(a1);
    else if (!strcmp(cmd, "dump")) { FILE* f = fopen(a1, "wb"); fwrite(s->ram, 1, 0x20000, f); fclose(f); }
    else if (!strcmp(cmd, "vdump")) { FILE* f = fopen(a1, "wb"); fwrite(s->ppu->vram, 2, 0x8000, f); fwrite(s->ppu->cgram, 2, 256, f); for (int i = 0; i < 4; i++) fwrite(&s->ppu->bgLayer[i].tileAdr, 2, 1, f); fclose(f); }
    else if (!strcmp(cmd, "god")) god = atoi(a1);
    else if (!strcmp(cmd, "misses")) { uint32_t l[64]; int n = runner_miss_list(l, 64); printf("misses %llu:", (unsigned long long)runner_misses()); for (int i = 0; i < n; i++) printf(" %06X", l[i]); printf("\n"); }
    else if (!strcmp(cmd, "walls")) { int l, r, t, b; int on = restore_walls(&l, &r, &t, &b); printf("walls on=%d l=%X r=%X  player %X,%X\n", on, l, r, s->ram[0xF51] | s->ram[0xF52] << 8, s->ram[0xFC9] | s->ram[0xFCA] << 8); }
    else if (!strcmp(cmd, "extras")) { extras_init(s, a1); extras_set_rom(s->cart->rom); g_extras = 1; }
    else if (!strcmp(cmd, "extras_save")) extras_shutdown();
    else if (!strcmp(cmd, "timershot")) { static uint32_t b[160 * 40]; int on = extras_draw_timer(b, 160, 40); FILE* f = fopen(a1, "wb"); fprintf(f, "P6 160 40 255\n"); for (int i = 0; i < 160 * 40; i++) { uint32_t c = b[i]; fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); } fclose(f); printf("timer on=%d\n", on); }
    else if (!strcmp(cmd, "loadstate")) { int n; uint8_t* d = runner_load_file(a1, &n); if (d) { snes_loadState(s, d, n); free(d); } printf("loaded %s\n", a1); }
    else if (!strcmp(cmd, "vpoke")) s->ppu->vram[strtol(a1, 0, 16) & 0x7fff] = strtol(a2, 0, 16);
    else if (!strcmp(cmd, "hdma")) { for (int i = 0; i < 8; i++) if (s->dma->channel[i].hdmaActive) printf("ch%d b=21%02X a=%02X:%04X mode %d ind %d indBank %02X\n", i, s->dma->channel[i].bAdr, s->dma->channel[i].aBank, s->dma->channel[i].aAdr, s->dma->channel[i].mode, s->dma->channel[i].indirect, s->dma->channel[i].indBank); }
    else if (!strcmp(cmd, "bgregs")) for (int i = 0; i < 4; i++) printf("bg%d h %d v %d map %04X tiles %04X\n", i + 1, s->ppu->bgLayer[i].hScroll, s->ppu->bgLayer[i].vScroll, s->ppu->bgLayer[i].tilemapAdr, s->ppu->bgLayer[i].tileAdr);
    else if (!strcmp(cmd, "spawns")) { printf("spawns %ld\n", spawns); spawns = 0; }
    else if (!strcmp(cmd, "pulse")) pulse = atoi(a1);
    else if (!strcmp(cmd, "interp")) { extern int g_dsp_interp; g_dsp_interp = atoi(a1); }
    else if (!strcmp(cmd, "wav")) {   // wav <file> <frames>: record audio at 32 kHz
      int n = atoi(a2); FILE* f = fopen(a1, "wb"); static int16_t buf[2 * 800]; long tot = 0;
      fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\x7d\0\0\0\xf4\x01\0\x04\0\x10\0data\0\0\0\0", 1, 44, f);
      for (int i = 0; i < n; i++) { run(0, 0, 1); snes_setSamples(s, buf, 534); fwrite(buf, 4, 534, f); tot += 534 * 4; }
      fseek(f, 40, SEEK_SET); fwrite(&tot, 4, 1, f); long r = tot + 36; fseek(f, 4, SEEK_SET); fwrite(&r, 4, 1, f); fclose(f);
    }
    else if (!strcmp(cmd, "kon")) { extern uint32_t g_dsp_kon[8]; printf("kon"); for (int i = 0; i < 8; i++) { printf(" %u", g_dsp_kon[i]); g_dsp_kon[i] = 0; } printf("\n"); }
    else if (!strcmp(cmd, "overflow")) { printf("overflow frames %ld\n", overflow); overflow = 0; }
    else if (!strcmp(cmd, "nolimit")) s->ppu->noSpriteLimit = atoi(a1);
    else if (!strcmp(cmd, "apulog")) g_snes_write_hook = atoi(a1) ? apu_hook : NULL;
    else if (!strcmp(cmd, "restore")) { printf("restore ok=%d\n", restore_init(s)); restore_set(atoi(a1)); ws_init(s, rom, len); }
    else if (!strcmp(cmd, "print")) { int a = strtol(a1, 0, 16), n = a2[0] ? atoi(a2) : 2; printf("%04X:", a); for (int i = 0; i < n; i++) printf(" %02X", s->ram[a + i]); printf("   mode %02X scene %02X\n", s->ram[0x4DA], s->ram[0x368]); }
  }
  return 0;
}
