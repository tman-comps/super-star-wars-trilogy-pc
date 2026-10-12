// Lockstep debugger: runs an interpreted and a recompiled machine side by side.
//   ssw_lockstep <rom> <warp_mode> <warp_scene> <seed> <start_frame>
// Runs both to start_frame (frame-level), then single-steps both and reports the first
// instruction after which CPU registers or WRAM differ, with the recent PC history.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "runner.h"

static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static uint16_t cur; static int left;
static uint16_t script_buttons(int f) {
  static const int starts[] = {1500, 1564, 1688, 1812, 1936, 2060, 2184};
  for (unsigned i = 0; i < 7; i++) if (f >= starts[i] && f < starts[i] + 4) return 1 << BTN_START;
  if (f < 2300) return 0;
  if (left-- <= 0) {
    static const uint16_t cho[] = {
      1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B), 1 << BTN_Y,
      (1 << BTN_LEFT) | (1 << BTN_Y), 1 << BTN_B, (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_A, 1 << BTN_UP, 1 << BTN_DOWN};
    cur = cho[rnd() % 10]; left = 4 + rnd() % 36;
  }
  return cur;
}

static int same(Snes* a, Snes* b) {
  Cpu *x = a->cpu, *y = b->cpu;
  return x->a == y->a && x->x == y->x && x->y == y->y && x->sp == y->sp && x->pc == y->pc && x->k == y->k &&
         x->dp == y->dp && x->db == y->db && x->c == y->c && x->z == y->z && x->n == y->n && x->v == y->v &&
         x->mf == y->mf && x->xf == y->xf && a->cycles == b->cycles && !memcmp(a->ram, b->ram, sizeof a->ram);
}

int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  for (int e = 1; e <= 4; e++) { rom[0x1D4A + 4 * e + 2] = strtol(argv[2], 0, 16); rom[0x1D4A + 4 * e + 3] = strtol(argv[3], 0, 16); }
  rng = atoi(argv[4]); int start = atoi(argv[5]);
  Snes* A = snes_init(); snes_loadRom(A, rom, len);
  Snes* B = snes_init(); snes_loadRom(B, rom, len);
  for (int f = 0; f < start; f++) {
    uint16_t bt = script_buttons(f);
    for (int i = 0; i < 12; i++) { snes_setButtonState(A, 1, i, (bt >> i) & 1); snes_setButtonState(B, 1, i, (bt >> i) & 1); }
    A->ram[0x08FB] = 3; B->ram[0x08FB] = 3;
    A->ram[0x08FB] = 3; B->ram[0x08FB] = 3;
    g_use_recomp = 0; runner_frame(A);
    g_use_recomp = 1; runner_frame(B);
  }
  if (!same(A, B)) { printf("already differ at start frame\n"); }
  uint32_t hist[64]; int hn = 0;
  g_recomp_single = 1;
  for (long step = 0; step < 50000000; step++) {
    uint32_t pc = (A->cpu->k << 16) | A->cpu->pc;
    hist[hn++ % 64] = pc;
    g_use_recomp = 0; runner_step(A);
    g_use_recomp = 1; runner_step(B);
    if (!same(A, B)) {
      printf("diverged after instruction at %06X (step %ld)\n", pc, step);
      for (int i = 0; i < 64 && i < hn; i++) printf("  %06X\n", hist[(hn - 64 + i + 6400) % 64]);
      Cpu *x = A->cpu, *y = B->cpu;
      printf("interp: A=%04X X=%04X Y=%04X S=%04X PC=%02X:%04X D=%04X DB=%02X m=%d x=%d cyc=%llu\n", x->a, x->x, x->y, x->sp, x->k, x->pc, x->dp, x->db, x->mf, x->xf, (unsigned long long)A->cycles);
      printf("recomp: A=%04X X=%04X Y=%04X S=%04X PC=%02X:%04X D=%04X DB=%02X m=%d x=%d cyc=%llu\n", y->a, y->x, y->y, y->sp, y->k, y->pc, y->dp, y->db, y->mf, y->xf, (unsigned long long)B->cycles);
      for (int i = 0; i < 0x20000; i++) if (A->ram[i] != B->ram[i]) { printf("  ram %05X: %02X vs %02X\n", i, A->ram[i], B->ram[i]); }
      return 1;
    }
    if (A->frames != B->frames) { printf("frame mismatch\n"); return 1; }
  }
  printf("no divergence\n");
  return 0;
}
