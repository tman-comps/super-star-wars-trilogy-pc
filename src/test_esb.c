// Headless test driver.
//   ssw_test <rom> <frames> <interp|recomp> <seed> [shot_every] [warp_mode warp_scene]
// Plays a deterministic input script (title skip, then pseudo-random play) and prints one hash
// line per frame of WRAM+VRAM+CGRAM+OAM, so two runs can be diffed. Optionally writes PPM
// screenshots every N frames to shot_XXXXXX.ppm.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
#include "recomp_rt.h"
#include "game.h"
#include "achievements.h"
#include "widescreen.h"
int esb_recomp_run(Cpu* cpu);
extern const uint32_t esb_g_hook_addrs[];

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}

static int g_cur_frame;
static uint8_t apulog_filter(Snes* sn, uint32_t adr, uint8_t v) {
  uint32_t a = adr & 0xffff;
  if (a >= 0x2140 && a <= 0x2143 && (adr >> 16 & 0x7f) < 0x40) {
    static int last_f = -1; static uint8_t pv[4];
    if (v != pv[a - 0x2140] || a != 0x2141) fprintf(stderr, "f%d %04X=%02X  pc %02X%04X\n", g_cur_frame, a, v, sn->cpu->k, sn->cpu->pc);
    pv[a - 0x2140] = v; (void)last_f;
  }
  return v;
}
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }

static uint16_t script_buttons(int f) {
  const char* code = getenv("CODE");   // CODE=XBBY... entered on the title from frame CODE_AT, nothing else
  if (code) {
    int at = getenv("CODE_AT") ? atoi(getenv("CODE_AT")) : 1300, k = (f - at) / 12;
    if (f < at || k >= (int)strlen(code) || (f - at) % 12 >= 4) return 0;
    switch (code[k]) { case 'A': return 1 << BTN_A; case 'B': return 1 << BTN_B; case 'X': return 1 << BTN_X; case 'Y': return 1 << BTN_Y;
      case 'S': return 1 << BTN_START; case 'L': return 1 << BTN_L; case 'R': return 1 << BTN_R; case 'u': return 1 << BTN_UP; case 'd': return 1 << BTN_DOWN; }
    return 0;
  }
  // title -> menu -> crawl skips, then random play
  static const int starts[] = {1500, 1564, 1688, 1812, 1936, 2060, 2184};
  for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
    if (f >= starts[i] && f < starts[i] + 4) return 1 << BTN_START;
  if (f < 2300) return 0;
  if (getenv("P1S")) { int at = atoi(getenv("P1S")); return f >= at && f < at + 4 ? 1 << BTN_START : 0; }
  if (getenv("WALK")) return (f >= 2800 ? 1 << BTN_RIGHT : 0) | (getenv("JUMP") && f >= 2900 && f < 2930 ? 1 << BTN_B : 0);
  static uint16_t cur = 0; static int left = 0;
  if (left-- <= 0) {
    static const uint16_t cho[] = {
      1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B), 1 << BTN_Y,
      (1 << BTN_LEFT) | (1 << BTN_Y), 1 << BTN_B, (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_A, 1 << BTN_UP, 1 << BTN_DOWN};
    cur = cho[rnd() % 10];
    left = 4 + rnd() % 36;
  }
  return cur;
}

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: %s rom frames interp|recomp seed [shot_every]\n", argv[0]); return 1; }
  int len;
  uint8_t* rom = runner_load_file(argv[1], &len);
  if (!rom || len != 0x180000) { fprintf(stderr, "need Super Empire Strikes Back (USA) Rev 1\n"); return 1; }
  g_recomp_fn = esb_recomp_run; g_hook_list = esb_g_hook_addrs;
  int frames = atoi(argv[2]);
  g_use_recomp = strcmp(argv[3], "recomp") == 0;
  rng = (uint32_t)atoi(argv[4]);
  int shot = argc > 5 ? atoi(argv[5]) : 0;
  if (argc > 7) {  // warp: every route into the first stage goes to (mode, scene) instead
    int off = len == 0x100200 ? 0x200 : 0;
    for (int e = 1; e <= 4; e++) {
      rom[off + 0x0D4E + 4 * e + 2] = (uint8_t)strtol(argv[6], 0, 16);
      rom[off + 0x0D4E + 4 * e + 3] = (uint8_t)strtol(argv[7], 0, 16);
    }
  }
  g_game = &g_esb;
  Snes* snes = snes_init();
  snes_loadRom(snes, rom, len);
  if (getenv("APULOG")) { extern uint8_t (*g_snes_write_filter)(Snes*, uint32_t, uint8_t); g_snes_write_filter = apulog_filter; }
  int ws = getenv("WS") ? atoi(getenv("WS")) : 0;
  if (ws) ws_init(snes, rom, len);
  if (getenv("ACH")) { ach_init(snes, getenv("ACH")); ach_set_rom(snes->cart->rom); }
  static uint8_t pixels[512 * 480 * 4];
  for (int f = 0; f < frames; f++) {
    uint16_t b = script_buttons(f);
    for (int i = 0; i < 12; i++) snes_setButtonState(snes, 1, i, (b >> i) & 1);
    if (getenv("P2")) {   // P2=frame:mask -> debug flag on, controller 2 holds mask for 4 frames from that frame
      int at = atoi(getenv("P2")); unsigned mk = (unsigned)strtoul(strchr(getenv("P2"), ':') + 1, 0, 16);
      snes->ram[0x010C] = 1; snes->ram[0x010D] = 0;
      for (int i = 0; i < 12; i++) snes_setButtonState(snes, 2, i, f >= at && f < at + 4 && ((mk >> i) & 1));
    }
    if (argc > 7) snes->ram[0x01AC] = 3;
    if (getenv("POKE")) { unsigned pa, pv; if (sscanf(getenv("POKE"), "%x:%x", &pa, &pv) == 2) snes->ram[pa] |= pv; }   // keep lives topped up on warped test runs
    g_cur_frame = f;
    if (ws) ws_update(ws);
    runner_frame(snes);
    if (ws && getenv("WSHOT") && f == atoi(getenv("WSHOT"))) {   // wide frame as PPM
      int W = 256 + 2 * PPU_WIDE_MAX, e = snes->ppu->wideExtra;
      FILE* o = fopen("wide.ppm", "wb"); fprintf(o, "P6 %d 224 255\n", 256 + 2 * e);
      for (int y = 0; y < 224; y++) for (int x = -e; x < 256 + e; x++) { uint32_t c = snes->ppu->wideBuffer[y * W + x + PPU_WIDE_MAX]; fputc(c >> 16, o); fputc(c >> 8, o); fputc(c, o); }
      fclose(o);
    }
    if (ws && f % 100 == 99 && f > 2300) { int tot, bad = ws_selfcheck(&tot); fprintf(stderr, "f%d mode %02X scene %02X selfcheck %d/%d bad, wideExtra %d\n", f, snes->ram[0x1B6], snes->ram[0x1B8], bad, tot, snes->ppu->wideExtra); }
    if (getenv("ACH")) { ach_update(0); int id; while ((id = ach_pop_toast()) >= 0) { const char *t, *d; ach_get(id, &t, &d); fprintf(stderr, "f%d UNLOCK %s\n", f, t); } }
    if (getenv("WATCH") && f >= 2790 && f % 5 == 0) { fprintf(stderr, "f%d", f); for (int a = 0x11CE; a < 0x1420; a += 0x28) fprintf(stderr, " %04X", snes->ram[a] | snes->ram[a+1] << 8); fprintf(stderr, " hp %d\n", snes->ram[0xCF6]); }
    if (getenv("DUMPAT")) { char nm[64]; int k = atoi(getenv("DUMPAT")); if (f == k || f == k + 60) { snprintf(nm, sizeof nm, "ram_%d.bin", f); FILE* o = fopen(nm, "wb"); fwrite(snes->ram, 1, 0x2000, o); fclose(o); } }
    { static int pm=-1, ps=-1; int m=snes->ram[0x1B6], sc=snes->ram[0x1B8];
      if (getenv("TRACE") && (m!=pm||sc!=ps)) { fprintf(stderr,"f%d mode %02X scene %02X\n",f,m,sc); pm=m; ps=sc; } }
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, snes->ram, sizeof(snes->ram));
    h = fnv(h, snes->ppu->vram, sizeof(snes->ppu->vram));
    h = fnv(h, snes->ppu->cgram, sizeof(snes->ppu->cgram));
    h = fnv(h, snes->ppu->oam, sizeof(snes->ppu->oam));
    printf("%d %016llx\n", f, (unsigned long long)h);
    if (shot && f % shot == shot - 1) {
      snes_setPixels(snes, pixels);
      char name[64]; snprintf(name, sizeof name, "shot_%06d.ppm", f);
      FILE* o = fopen(name, "wb");
      fprintf(o, "P6 256 224 255\n");
      for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) {
        uint8_t* p = pixels + ((y * 2 + 16) * 512 + x * 2) * 4;   // 512x480 output, 2x scaled, 16 lines of overscan offset
        fputc(p[3], o); fputc(p[2], o); fputc(p[1], o);
      }
      fclose(o);
    }
  }
  if (getenv("PPUINFO")) {
    Ppu* p = snes->ppu;
    fprintf(stderr, "win1 %d-%d win2 %d-%d clip %d prevent %d sub %d half %d fixed %d,%d,%d math", p->window1left, p->window1right, p->window2left, p->window2right, p->clipMode, p->preventMathMode, p->subtractColor, p->halfColor, p->fixedColorR, p->fixedColorG, p->fixedColorB);
    for (int i = 0; i < 6; i++) fprintf(stderr, " %d", p->mathEnabled[i]);
    fprintf(stderr, "\nwinlayers:"); for (int i = 0; i < 6; i++) fprintf(stderr, " [%d%d%d%d]", p->windowLayer[i].window1enabled, p->windowLayer[i].window2enabled, p->windowLayer[i].window1inversed, p->windowLayer[i].window2inversed);
    fprintf(stderr, "\nhdma:"); for (int i = 0; i < 8; i++) if (snes->dma->channel[i].hdmaActive) fprintf(stderr, " ch%d b=21%02X a=%02X:%04X mode %d ind %d", i, snes->dma->channel[i].bAdr, snes->dma->channel[i].aBank, snes->dma->channel[i].aAdr, snes->dma->channel[i].mode, snes->dma->channel[i].indirect);
    fprintf(stderr, "\n");
  }
  if (getenv("VRAMDUMP")) { FILE* o = fopen(getenv("VRAMDUMP"), "wb"); fwrite(snes->ppu->vram, 2, 0x8000, o); fwrite(snes->ppu->cgram, 2, 256, o); fclose(o);
    fprintf(stderr, "bg mode %d; bg tileAdr/tilemapAdr:", snes->ppu->mode); for (int i = 0; i < 4; i++) fprintf(stderr, " %04X/%04X", snes->ppu->bgLayer[i].tileAdr, snes->ppu->bgLayer[i].tilemapAdr); fprintf(stderr, "\n"); }
  if (getenv("RAMDUMP")) { FILE* o = fopen(getenv("RAMDUMP"), "wb"); fwrite(snes->ram, 1, 0x20000, o); fclose(o); }
  uint32_t miss[64]; int n = runner_miss_list(miss, 64);
  fprintf(stderr, "misses: %llu total, %d unique\n", (unsigned long long)runner_misses(), n);
  for (int i = 0; i < n && i < 20; i++) fprintf(stderr, "  %06X\n", miss[i]);
  return 0;
}
