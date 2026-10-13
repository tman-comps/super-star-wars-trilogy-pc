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
#include "empire_luke.h"
int esb_recomp_run(Cpu* cpu);
extern const uint32_t esb_g_hook_addrs[];
extern void (*g_snes_write_hook)(Snes*, uint32_t, uint8_t);

static Snes* s;
static int g_f;
static long g_watch = -1;
static void hook(Snes* sn, uint32_t adr, uint8_t v) {
  int esb = g_game == &g_esb;
  uint32_t wa = adr & 0xffffff;
  if ((((wa >> 16) & 0x7f) < 0x40 || (wa >> 16) == 0x7E) && (wa & 0xffff) < 0x2000) wa &= 0xffff;   // low WRAM mirrors
  // frame drawer entry: Super Star Wars $81:BFC7/BFD0 (frame [$3E], table $04F8, pos $088A/C),
  // Empire $81:B5FB/B604 (frame [$52], table $07AA, pos $0346/8)
  if (getenv("POSELOG") && g_f >= 2300 && sn->cpu->k == 0x81 &&
      (esb ? sn->cpu->pc == 0xB602 || sn->cpu->pc == 0xB608 : sn->cpu->pc == 0xBFCE || sn->cpu->pc == 0xBFD4)) {
    uint8_t* d = sn->ram + sn->cpu->dp; uint8_t* r = sn->ram; int F = esb ? 0x52 : 0x3E, T = esb ? 0x7AA : 0x4F8, X = esb ? 0x346 : 0x88A;
    unsigned tbl = r[T] | r[T + 1] << 8, fp = d[F] | d[F + 1] << 8;
    if (d[F + 2] == 0x7F && tbl == strtoul(getenv("POSELOG"), 0, 16)) {
      unsigned blk = tbl - 4, rel = fp + 1 - blk; int idx = -1;
      for (int i = 0; i < 400; i++) { unsigned e = r[0x10000 + tbl + 2 * i] | r[0x10000 + tbl + 2 * i + 1] << 8; if (e == rel) { idx = i; break; } }
      int A = esb ? 0xE3 : 0xEE;
      printf("f%d pose %d rel %04X attr %02X%02X x %d y %d\n", g_f, idx, rel, d[A + 1], d[A], (int16_t)(r[X] | r[X + 1] << 8), (int16_t)(r[X + 2] | r[X + 3] << 8));
    }
  }
  if (getenv("DRAWLOG") && g_f == atoi(getenv("DRAWLOG")) && sn->cpu->k == 0x81 && (sn->cpu->pc == 0xBFCE || sn->cpu->pc == 0xBFD4)) {   // frame drawer entry ($81:BFC7 / BFD0)
    uint8_t* d = sn->ram + sn->cpu->dp; uint8_t* r = sn->ram;
    fprintf(stderr, "draw frame %02X:%02X%02X tbl %04X pool %02X%02X x %04X y %04X attr %02X%02X X=%04X\n", d[0x40], d[0x3F], d[0x3E],
            r[0x4F8] | r[0x4F9] << 8, d[0x46], d[0x45], r[0x88A] | r[0x88B] << 8, r[0x88C] | r[0x88D] << 8, d[0xEF], d[0xEE], sn->cpu->x);
  }
  if (getenv("RANGE")) {   // RANGE=lo:hi:frame -> who writes WRAM lo..hi during that frame
    unsigned lo, hi, fr; if (sscanf(getenv("RANGE"), "%x:%x:%u", &lo, &hi, &fr) == 3 && g_f == (int)fr && wa >= lo && wa < hi)
      fprintf(stderr, "range %04lX pc %02X:%04X\n", (long)wa, sn->cpu->k, sn->cpu->pc);
  }
  if (g_watch >= 0 && (long)wa == g_watch && g_f >= (getenv("WFROM") ? atoi(getenv("WFROM")) : 0)) {   // WATCH=7F36AE: who writes this byte
    static int n; uint8_t* d = sn->ram + sn->cpu->dp; if (n++ < 20) fprintf(stderr, "f%d write %06X=%02X pc %02X:%04X src(3E) %02X%02X%02X dst(45) %02X%02X%02X\n", g_f, adr, v, sn->cpu->k, sn->cpu->pc, d[0x40], d[0x3F], d[0x3E], d[0x47], d[0x46], d[0x45]);
  }
  // unpacker ($81:85DC in Super Star Wars, $81:87D4 in Empire): log where each unpack starts

  if (getenv("LZLOG") && sn->cpu->k == 0x81 && (esb ? sn->cpu->pc == 0x87EF || sn->cpu->pc == 0x8826 : sn->cpu->pc == 0x85F7 || sn->cpu->pc == 0x862E)) {
    static uint32_t last; uint8_t* d = sn->ram + sn->cpu->dp; int S = esb ? 0x52 : 0x3E, D = esb ? 0x59 : 0x45;
    uint32_t dst = d[D + 2] << 16 | d[D + 1] << 8 | d[D];
    if (dst < last || dst > last + 20) fprintf(stderr, "f%d unpack src %02X%02X%02X -> %06X\n", g_f, d[S + 2], d[S + 1], d[S], dst);
    last = dst;
  }
  if (getenv("PCLOG") && ((adr >> 16) & 0x7f) < 0x40 && (adr & 0xfff0) == 0x4300 && g_f >= 2600) {   // who sets up DMA
    static int n; if (n++ < 100000) fprintf(stderr, "f%d %04X=%02X pc %02X:%04X\n", g_f, adr & 0xffff, v, sn->cpu->k, sn->cpu->pc);
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

// SEQ="R:60 RB:20 Y:30 U:20 UY:30 ..." = buttons (L R U D B Y A X) held for N frames, after the stage starts
static uint16_t seq_buttons(int t) {
  static char buf[4096]; static struct { uint16_t b; int n; } st[256]; static int ns = -1;
  if (ns < 0) {
    ns = 0; strncpy(buf, getenv("SEQ"), sizeof buf - 1);
    for (char* tok = strtok(buf, " "); tok && ns < 256; tok = strtok(NULL, " ")) {
      char* c = strchr(tok, ':'); if (!c) continue; uint16_t m = 0;
      for (char* q = tok; q < c; q++) switch (*q) { case 'L': m |= 1 << BTN_LEFT; break; case 'R': m |= 1 << BTN_RIGHT; break;
        case 'U': m |= 1 << BTN_UP; break; case 'D': m |= 1 << BTN_DOWN; break; case 'B': m |= 1 << BTN_B; break; case 'Y': m |= 1 << BTN_Y; break;
        case 'A': m |= 1 << BTN_A; break; case 'X': m |= 1 << BTN_X; break; case 'S': m |= 1 << BTN_START; break; case 'E': m |= 1 << BTN_SELECT; break; case 'l': m |= 1 << BTN_L; break; case 'r': m |= 1 << BTN_R; break; }
      st[ns].b = m; st[ns].n = atoi(c + 1); ns++;
    }
  }
  for (int i = 0; i < ns; i++) { if (t < st[i].n) return st[i].b; t -= st[i].n; }
  return 0;
}
static uint16_t buttons(int f) {
  if (getenv("SEQ")) {
    static const int starts[] = {1500, 1564, 1688, 1812, 1936, 2060, 2184};
    for (unsigned i = 0; i < sizeof starts / sizeof *starts; i++) if (f >= starts[i] && f < starts[i] + 4) return 1 << BTN_START;
    return f < 2400 ? 0 : seq_buttons(f - 2400);
  }
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
  if (getenv("WARP")) {   // WARP=MMSS: every route into the first stage goes to mode MM scene SS instead
    unsigned v = (unsigned)strtoul(getenv("WARP"), NULL, 16);
    uint8_t* r = rom + g_game->seq_off;
    for (int k = 0; r[k] != 0xFF; k += 4)
      if (r[k + 2] == g_game->start_mode && r[k + 3] == g_game->start_scene) { r[k + 2] = v >> 8; r[k + 3] = v & 0xFF; }
  }
  if (getenv("ELUKE")) {   // ELUKE=path/to/sesb.sfc: Empire Luke in Super Star Wars
    int el; uint8_t* er = runner_load_file(getenv("ELUKE"), &el);
    fprintf(stderr, "empire luke: %s\n", eluke_init(rom, len, er, el) ? "ready" : "FAILED");
  }
  s = snes_init(); snes_loadRom(s, rom, len);
  g_snes_write_hook = hook;
  for (g_f = 0; g_f < frames; g_f++) {
    uint16_t b = buttons(g_f);
    for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (b >> i) & 1);
    s->ram[g_game->r_lives] = 3;
    if (getenv("POKE")) { unsigned pa, pv; if (sscanf(getenv("POKE"), "%x:%x", &pa, &pv) == 2) s->ram[pa] = pv; }
    if (getenv("SHOTAT")) { int a0 = atoi(getenv("SHOTAT")), st = getenv("SHOTSTEP") ? atoi(getenv("SHOTSTEP")) : 30;
      if (g_f >= a0 && (g_f - a0) % st == 0) { static uint8_t px[512 * 480 * 4]; snes_setPixels(s, px);
        char nm[64]; snprintf(nm, sizeof nm, "s_%05d.ppm", g_f); FILE* o = fopen(nm, "wb"); fprintf(o, "P6 256 224 255\n");
        for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) { uint8_t* p = px + ((y * 2 + 16) * 512 + x * 2) * 4; fputc(p[3], o); fputc(p[2], o); fputc(p[1], o); }
        fclose(o); } }
    runner_frame(s);
    if (getenv("ELUKE")) eluke_frame(s);
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
