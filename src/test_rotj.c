// Headless test driver for Super Return of the Jedi.
//   rotj_test <rom> <frames> <interp|recomp> <seed> [shot_every] [warp_mode warp_scene]
// Plays a deterministic input script (Start every 100 frames outside stages, pseudo-random play in them) and prints one hash
// line per frame of WRAM+VRAM+CGRAM+OAM, so an interpreted and a recompiled run can be diffed
// (verify_rotj.sh). Warp: every new-game route goes to (mode, scene) instead of the first stage.
// Environment:
// The warp needs the Start press during the opening crawl that the default input script makes.
//   CDL=file       interpreter only: write the executed-code log (same format as tools/emu) to file
//   GOD=1          keep the player's health and lives topped up (coverage runs)
//   CLEAR=n        every n frames of play, use the developer stage skip (developer flag on, B+Start)
//   TRACE=1        print every mode/scene change to stderr
//   SHOTS=prefix   screenshot file prefix (default shot)
//   PPUINFO=1      print the windows and the active HDMA channels at the end
//   VRAMDUMP=file  write VRAM and CGRAM at the end, print the BG base addresses
//   RAMDUMP=file   write WRAM at the end
//   RAMLOG=file    append the first 8 KB of WRAM after every frame (for finding RAM variables)
//   WATCHW=addr    print the code address of every CPU write to that WRAM address (hex, bank 7E)
//   CODE=XXBAY     enter a title-screen code (letters A B X Y L R, u/d = up/down) from frame CODE_AT
//   WILD=1         interpreter: stop and print the code path the first time code runs outside ROM
//   CARDS=prefix   screenshots every 10 frames of each hero select card (mode 08), prefix_SS_n.ppm
//   INPUT=file     replay "frame mask" lines (hex SNES button mask) instead of the random play
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
#include "recomp_rt.h"
#include "game.h"
int rotj_recomp_run(Cpu* cpu);
extern const uint32_t rotj_g_hook_addrs[];
extern uint8_t* g_cdl;

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }

static int g_frame, g_watch = -1;
static uint8_t watch_filter(Snes* sn, uint32_t adr, uint8_t v) {
  uint32_t a = (adr >> 16) == 0x7E ? (adr & 0xffff) : ((adr >> 16 & 0x7f) < 0x40 && (adr & 0xffff) < 0x2000) ? (adr & 0xffff) : 0x1000000;
  if ((int)a == g_watch || (int)a == g_watch + 1)
  {
    fprintf(stderr, "f%d write %04X=%02X  pc %02X%04X  A=%04X X=%04X Y=%04X stack", g_frame, a, v, sn->cpu->k, sn->cpu->pc, sn->cpu->a, sn->cpu->x, sn->cpu->y);
    for (int i = 1; i <= 12; i++) fprintf(stderr, " %02X", sn->ram[(sn->cpu->sp + i) & 0x1fff]);
    fprintf(stderr, "\n");
  }
  return v;
}
static uint16_t* g_input; static int g_input_n;
static uint16_t script_buttons(int f, int playing) {
  if (g_input) return f < g_input_n ? g_input[f] : 0;
  const char* code = getenv("CODE");   // CODE=XXBAY: a title-screen code entered from frame CODE_AT (default 700)
  int at = getenv("CODE_AT") ? atoi(getenv("CODE_AT")) : 700;
  if (code && f < at + 12 * (int)strlen(code) + 200) {
    int k = (f - at) / 12;
    if (f < at || k >= (int)strlen(code) || (f - at) % 12 >= 4) return 0;
    switch (code[k]) { case 'A': return 1 << BTN_A; case 'B': return 1 << BTN_B; case 'X': return 1 << BTN_X; case 'Y': return 1 << BTN_Y;
      case 'L': return 1 << BTN_L; case 'R': return 1 << BTN_R; case 'u': return 1 << BTN_UP; case 'd': return 1 << BTN_DOWN; }
    return 0;
  }
  // title -> menu -> opening crawl skip, then random play once a stage runs (Start would pause it)
  if (!playing) return f >= 900 && f % 100 < 4 ? 1 << BTN_START : 0;
  static uint16_t cur = 0; static int left = 0;
  if (left-- <= 0) {
    static const uint16_t cho[] = {
      1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B), 1 << BTN_Y,
      (1 << BTN_LEFT) | (1 << BTN_Y), 1 << BTN_B, (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_A, 1 << BTN_UP, 1 << BTN_DOWN,
      (1 << BTN_RIGHT) | (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_X, 1 << BTN_L, 1 << BTN_R, (1 << BTN_UP) | (1 << BTN_Y)};
    int k = getenv("GOD") ? 15 : 10;
    cur = cho[rnd() % k];
    left = 4 + rnd() % 36;
  }
  return cur;
}

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: %s rom frames interp|recomp seed [shot_every] [warp_mode warp_scene]\n", argv[0]); return 1; }
  int len;
  uint8_t* rom = runner_load_file(argv[1], &len);
  if (!rom || (len != 0x200000 && len != 0x200200)) { fprintf(stderr, "need Super Return of the Jedi (USA) Rev 1\n"); return 1; }
  g_game = &g_rotj;
  g_recomp_fn = rotj_recomp_run; g_hook_list = rotj_g_hook_addrs;
  int frames = atoi(argv[2]);
  g_use_recomp = strcmp(argv[3], "recomp") == 0;
  rng = (uint32_t)atoi(argv[4]);
  int shot = argc > 5 ? atoi(argv[5]) : 0;
  int off = len == 0x200200 ? 0x200 : 0;
  if (argc > 7) {   // warp: every route into the first stage goes to (mode, scene) instead
    uint8_t wm = (uint8_t)strtol(argv[6], 0, 16), ws = (uint8_t)strtol(argv[7], 0, 16);
    uint8_t* r = rom + off + g_rotj.seq_off;   // the scene table (opening crawl played out)
    for (int k = 0; r[k] != 0xFF; k += 4)
      if (r[k + 2] == g_rotj.start_mode && r[k + 3] == g_rotj.start_scene) { r[k + 2] = wm; r[k + 3] = ws; }
    // Start during the opening crawl: lda #$0012 / jsl set_scene / lda #$0014 / jmp enter_mode at $80:F62C
    uint8_t* p = rom + off + 0x762C;
    if (p[0] == 0xA9 && p[1] == g_rotj.start_scene && p[7] == 0xA9 && p[8] == g_rotj.start_mode) { p[1] = ws; p[8] = wm; }
  }
  if (getenv("INPUT")) {
    FILE* in = fopen(getenv("INPUT"), "r"); int f; unsigned m;
    g_input = calloc(frames + 1, 2); g_input_n = frames;
    while (in && fscanf(in, "%d %x", &f, &m) == 2) if (f >= 0 && f < frames) g_input[f] = (uint16_t)m;
    if (in) fclose(in);
  }
  if (getenv("CDL")) g_cdl = calloc(1, 1 << 24);
  int god = getenv("GOD") != NULL, clear = getenv("CLEAR") ? atoi(getenv("CLEAR")) : 0;
  const char* shots = getenv("SHOTS") ? getenv("SHOTS") : "shot";
  Snes* snes = snes_init();
  snes_loadRom(snes, rom + off, len - off);
  if (getenv("WATCHW")) { extern uint8_t (*g_snes_write_filter)(Snes*, uint32_t, uint8_t); g_watch = (int)strtol(getenv("WATCHW"), 0, 16); g_snes_write_filter = watch_filter; }
  int play = 0, pm = -1, ps = -1;
  for (int f = 0; f < frames; f++) {
  static uint8_t pixels[512 * 480 * 4];
    int m = game_mode(snes->ram), sc = game_scene(snes->ram);
    int playing = game_is_play_mode(m);
    uint16_t b = script_buttons(f, playing);
    for (int i = 0; i < 12; i++) snes_setButtonState(snes, 1, i, (b >> i) & 1);
    play = playing ? play + 1 : 0;
    uint16_t b2 = 0;
    // the developer stage skip: hold B and press Start on controller 1 (Empire used controller 2 Start)
    if (clear && playing && play % clear >= clear - 12) b = play % clear >= clear - 4 ? (1 << BTN_B) | (1 << BTN_START) : 0;
    for (int i = 0; i < 12; i++) snes_setButtonState(snes, 1, i, (b >> i) & 1);
    if (clear) { snes->ram[g_rotj.r_debug] = 1; snes->ram[g_rotj.r_debug + 1] = 0; }
    for (int i = 0; i < 12; i++) snes_setButtonState(snes, 2, i, (b2 >> i) & 1);
    if (god && playing) {
      if (g_rotj.r_lives) snes->ram[g_rotj.r_lives] = 3;
      if (g_rotj.r_hp && snes->ram[g_rotj.r_hp] && snes->ram[g_rotj.r_hp] < 0x80) snes->ram[g_rotj.r_hp] = 0x20;
    }
    if (argc > 7 && g_rotj.r_lives) snes->ram[g_rotj.r_lives] = 3;
    g_frame = f;
    if (getenv("WILD")) {   // interpreter: stop at the first instruction fetched outside ROM (other than the $0100 vectors), print the path there
      static uint32_t hist[256]; static int hn;
      uint32_t fr = snes->frames; int phase = 0;
      for (;;) {
        if (phase == 0 && !snes->inVblank) { phase = 1; fr = snes->frames; }
        if (phase == 1 && (snes->inVblank || fr != snes->frames)) break;
        uint32_t a = snes->cpu->k << 16 | snes->cpu->pc;
        if (f > 10 && ((getenv("WILDSP") && snes->cpu->sp < 0x1E00) || (a & 0xffff) < 0x8000) && ((a & 0xffff) < 0x100 || (a & 0xffff) > 0x107) && (a >> 16 & 0x7f) < 0x40) {
          fprintf(stderr, "f%d: code outside ROM at %06X (S=%04X mode %02X scene %02X); before it:\n", f, a, snes->cpu->sp, game_mode(snes->ram), game_scene(snes->ram));
          for (int k = 255; k >= 0; k--) if (k < hn) fprintf(stderr, " %06X", hist[(hn - 1 - k) & 255]);
          fprintf(stderr, "\n"); return 2;
        }
        hist[hn++ & 255] = a;
        runner_step(snes);
      }
      extern void snes_catchupApuPublic(Snes* snes); snes_catchupApuPublic(snes);
    } else
    runner_frame(snes);
    m = game_mode(snes->ram); sc = game_scene(snes->ram);
    {   // CARDS=prefix: a screenshot of every stage card / hero select (mode 08), 90 frames in
      static int card_at = -1;
      static int card_n, card_k;
      if (getenv("CARDS") && (m != pm || sc != ps) && m == 0x08 && sc != 0x31) { card_at = f + 10; card_n = 0; card_k++; }
      if (f == card_at && card_n < 30 && m == 0x08 && sc != 0x31) {
        card_at += 10; card_n++;
        snes_setPixels(snes, pixels);
        char name[256]; snprintf(name, sizeof name, "%s_%02X_%d.ppm", getenv("CARDS"), sc, card_n);
        FILE* o = fopen(name, "wb"); fprintf(o, "P6 256 224 255\n");
        for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) { uint8_t* q = pixels + ((y * 2 + 16) * 512 + x * 2) * 4; fputc(q[3], o); fputc(q[2], o); fputc(q[1], o); }
        fclose(o);
      }
    }
    if (getenv("TRACE") && (m != pm || sc != ps)) { fprintf(stderr, "f%d mode %02X scene %02X\n", f, m, sc); pm = m; ps = sc; }
    if (getenv("RAMLOG")) { static FILE* rl; if (!rl) rl = fopen(getenv("RAMLOG"), "wb"); fwrite(snes->ram, 1, 0x2000, rl); }
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, snes->ram, sizeof(snes->ram));
    h = fnv(h, snes->ppu->vram, sizeof(snes->ppu->vram));
    h = fnv(h, snes->ppu->cgram, sizeof(snes->ppu->cgram));
    h = fnv(h, snes->ppu->oam, sizeof(snes->ppu->oam));
    printf("%d %016llx\n", f, (unsigned long long)h);
    if (shot && f % shot == shot - 1) {
      snes_setPixels(snes, pixels);
      char name[256]; snprintf(name, sizeof name, "%s_%06d.ppm", shots, f);
      FILE* o = fopen(name, "wb");
      fprintf(o, "P6 256 224 255\n");
      for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) {
        uint8_t* p = pixels + ((y * 2 + 16) * 512 + x * 2) * 4;   // 512x480 output, 2x scaled, 16 lines of overscan offset
        fputc(p[3], o); fputc(p[2], o); fputc(p[1], o);
      }
      fclose(o);
    }
  }
  if (getenv("PPUINFO")) {   // windows and HDMA channels at the end
    Ppu* p = snes->ppu;
    fprintf(stderr, "bgmode %d bg3 main %d big %d inidisp-shadow? brightness %d\n", p->mode, p->layer[2].mainScreenEnabled, p->bgLayer[2].bigTiles, p->brightness);
    fprintf(stderr, "win1 %d-%d win2 %d-%d\nhdma:", p->window1left, p->window1right, p->window2left, p->window2right);
    for (int i = 0; i < 8; i++) if (snes->dma->channel[i].hdmaActive) fprintf(stderr, " ch%d b=21%02X a=%02X:%04X mode %d ind %d indbank %02X", i, snes->dma->channel[i].bAdr, snes->dma->channel[i].aBank, snes->dma->channel[i].aAdr, snes->dma->channel[i].mode, snes->dma->channel[i].indirect, snes->dma->channel[i].indBank);
    fprintf(stderr, "\n");
  }
  if (getenv("VRAMDUMP")) { FILE* o = fopen(getenv("VRAMDUMP"), "wb"); fwrite(snes->ppu->vram, 2, 0x8000, o); fwrite(snes->ppu->cgram, 2, 256, o); fclose(o);
    fprintf(stderr, "bg mode %d; bg tileAdr/tilemapAdr:", snes->ppu->mode); for (int i = 0; i < 4; i++) fprintf(stderr, " %04X/%04X", snes->ppu->bgLayer[i].tileAdr, snes->ppu->bgLayer[i].tilemapAdr); fprintf(stderr, "\n"); }
  if (getenv("RAMDUMP")) { FILE* o = fopen(getenv("RAMDUMP"), "wb"); fwrite(snes->ram, 1, 0x20000, o); fclose(o); }
  if (g_cdl) { FILE* o = fopen(getenv("CDL"), "wb"); fwrite(g_cdl, 1, 1 << 24, o); fclose(o); }
  uint32_t miss[64]; int n = runner_miss_list(miss, 64);
  fprintf(stderr, "misses: %llu total, %d unique\n", (unsigned long long)runner_misses(), n);
  for (int i = 0; i < n && i < 20; i++) fprintf(stderr, "  %06X\n", miss[i]);
  return 0;
}
