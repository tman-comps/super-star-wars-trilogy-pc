// Coverage driver running on the port's own core (interpreter mode, exact input).
//   ssw_cover <rom> <out_prefix> <seed> <frames_per_stage>
// 1) visits all 15 debug-menu areas, playing each in god mode then clearing it
// 2) a full run from a new game to the credits, clearing each stage with the pad-2 debug key
// Writes <out>.cdl (16 MB) and <out>.scr.json (script fetches), like tools/emu/cover.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "runner.h"
extern uint8_t* g_cdl; extern uint32_t* g_fetch_log; extern uint32_t g_fetch_n, g_fetch_cap;
static Snes* s;
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static FILE* fj; static int first = 1;
static void drain(void) {
  for (uint32_t i = 0; i < g_fetch_n; i += 2) {
    fprintf(fj, "%s[%u,%u,1]", first ? "" : ",", g_fetch_log[i], g_fetch_log[i + 1]);
    first = 0;
  }
  g_fetch_n = 0;
}
static void god(void) {
  s->ram[0x0A79] = 32; s->ram[0x08FB] = 3; s->ram[0x010C] = 1;
  for (int x = 2; x < 0x50; x += 2) {
    uint16_t v = s->ram[0x0A79 + x] | s->ram[0x0A7A + x] << 8;
    if (v > 1 && v < 0x8000) { s->ram[0x0A79 + x] = 1; s->ram[0x0A7A + x] = 0; }
  }
}
static void frame(uint16_t p1, uint16_t p2, int g) {
  for (int i = 0; i < 12; i++) { snes_setButtonState(s, 1, i, (p1 >> i) & 1); snes_setButtonState(s, 2, i, (p2 >> i) & 1); }
  if (g) god();
  runner_frame(s);
  if (g_fetch_n > g_fetch_cap - 4096) drain();
}
static void hold(uint16_t p1, uint16_t p2, int n, int g) { for (int i = 0; i < n; i++) frame(p1, p2, g); }
static void play(int n) {
  static const uint16_t cho[] = {
    1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B), 1 << BTN_Y,
    (1 << BTN_LEFT) | (1 << BTN_Y), (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_A, (1 << BTN_UP) | (1 << BTN_Y),
    (1 << BTN_DOWN) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_X, 1 << BTN_L, 1 << BTN_R};
  int done = 0;
  while (done < n) { int k = 4 + rnd() % 30; hold(cho[rnd() % 13], 0, k, 1); done += k; }
}
static void skip_screens(int k) {  // get through text, tallies and hero select
  for (int i = 0; i < k; i++) { hold(1 << BTN_START, 0, 4, 1); hold(0, 0, 60, 1); hold(1 << BTN_B, 0, 4, 1); hold(0, 0, 60, 1); }
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  rng = atoi(argv[3]); int per = atoi(argv[4]);
  g_cdl = calloc(1, 1 << 24);
  g_fetch_cap = 1 << 22; g_fetch_log = malloc(g_fetch_cap * 4);
  g_use_recomp = 0;
  char path[512]; snprintf(path, sizeof path, "%s.scr.json", argv[2]);
  fj = fopen(path, "w"); fprintf(fj, "{\"pairs\": [], \"fetch\": [");
  s = snes_init(); snes_loadRom(s, rom, len);
  hold(0, 0, 1500, 0);
  skip_screens(4);
  hold(0, 0, 300, 1);
  // part 1: every debug-menu area
  for (int area = 0; area < 15; area++) {
    hold(0, (1 << BTN_L) | (1 << BTN_R), 6, 1); hold(0, 0, 40, 1);
    for (int i = 0; i < 20; i++) { hold(1 << BTN_LEFT, 0, 3, 1); hold(0, 0, 6, 1); }   // back to area 0
    for (int i = 0; i < area; i++) { hold(1 << BTN_RIGHT, 0, 3, 1); hold(0, 0, 6, 1); }
    hold(1 << BTN_START, 0, 4, 1); hold(0, 0, 300, 1);
    skip_screens(2);
    play(per);
    hold(0, 1 << BTN_START, 4, 1);      // stage clear
    skip_screens(4);
    play(per / 4);
    fprintf(stderr, "area %d done: mode %02X scene %02X\n", area, s->ram[0x4DA], s->ram[0x368]);
  }
  // part 2: soft reset, new game, straight through to the credits
  snes_reset(s, false);
  hold(0, 0, 1500, 0);
  skip_screens(4);
  for (int st = 0; st < 45; st++) {
    play(per / 2);
    hold(0, 1 << BTN_START, 4, 1);
    skip_screens(5);
    fprintf(stderr, "run step %d: mode %02X scene %02X\n", st, s->ram[0x4DA], s->ram[0x368]);
  }
  hold(0, 0, 6000, 0);   // let the ending/credits play out
  drain();
  fprintf(fj, "]}\n"); fclose(fj);
  snprintf(path, sizeof path, "%s.cdl", argv[2]);
  FILE* o = fopen(path, "wb"); fwrite(g_cdl, 1, 1 << 24, o); fclose(o);
  return 0;
}
