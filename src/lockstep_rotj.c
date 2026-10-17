// Lockstep debugger for Super Return of the Jedi: an interpreted and a recompiled machine side by side.
//   rotj_lockstep <rom> <warp_mode> <warp_scene> <seed> <start_frame> [frames]
// Same warp and input script as rotj_test (verify_rotj.sh). Runs both machines frame by frame to
// start_frame, then instruction by instruction, and reports the first instruction after which the
// CPU registers or WRAM differ, with the recent PC history. Warp mode 00 = no warp.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "runner.h"
#include "recomp_rt.h"
#include "game.h"
int rotj_recomp_run(Cpu* cpu);
extern const uint32_t rotj_g_hook_addrs[];

static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static uint16_t script_buttons(int f, int playing) {   // keep in step with test_rotj.c
  if (!playing) return f >= 900 && f % 100 < 4 ? 1 << BTN_START : 0;
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

static int same(Snes* a, Snes* b) {
  Cpu *x = a->cpu, *y = b->cpu;
  return x->a == y->a && x->x == y->x && x->y == y->y && x->sp == y->sp && x->pc == y->pc && x->k == y->k &&
         x->dp == y->dp && x->db == y->db && x->c == y->c && x->z == y->z && x->n == y->n && x->v == y->v &&
         x->mf == y->mf && x->xf == y->xf && a->cycles == b->cycles && !memcmp(a->ram, b->ram, sizeof a->ram);
}

int main(int argc, char** argv) {
  if (argc < 6) { fprintf(stderr, "usage: %s rom warp_mode warp_scene seed start_frame [frames]\n", argv[0]); return 1; }
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  if (!rom || len != 0x200000) { fprintf(stderr, "need Super Return of the Jedi (USA) Rev 1, headerless\n"); return 1; }
  g_game = &g_rotj; g_recomp_fn = rotj_recomp_run; g_hook_list = rotj_g_hook_addrs;
  uint8_t wm = (uint8_t)strtol(argv[2], 0, 16), ws = (uint8_t)strtol(argv[3], 0, 16);
  if (wm) {   // same warp as rotj_test
    uint8_t* r = rom + g_rotj.seq_off;
    for (int k = 0; r[k] != 0xFF; k += 4)
      if (r[k + 2] == g_rotj.start_mode && r[k + 3] == g_rotj.start_scene) { r[k + 2] = wm; r[k + 3] = ws; }
    uint8_t* p = rom + 0x762C;
    if (p[0] == 0xA9 && p[1] == g_rotj.start_scene && p[7] == 0xA9 && p[8] == g_rotj.start_mode) { p[1] = ws; p[8] = wm; }
  }
  rng = atoi(argv[4]); int start = atoi(argv[5]);
  long steps = argc > 6 ? atol(argv[6]) * 60000L : 50000000L;
  Snes* A = snes_init(); snes_loadRom(A, rom, len);
  Snes* B = snes_init(); snes_loadRom(B, rom, len);
  for (int f = 0; f < start; f++) {
    uint16_t bt = script_buttons(f, game_is_play_mode(game_mode(A->ram)));
    for (int i = 0; i < 12; i++) { snes_setButtonState(A, 1, i, (bt >> i) & 1); snes_setButtonState(B, 1, i, (bt >> i) & 1); }
    if (wm) { A->ram[g_rotj.r_lives] = 3; B->ram[g_rotj.r_lives] = 3; }
    g_use_recomp = 0; runner_frame(A);
    g_use_recomp = 1; runner_frame(B);
  }
  if (!same(A, B)) printf("already differ at start frame\n");
  uint32_t hist[64]; int hn = 0;
  g_recomp_single = 1;
  for (long step = 0; step < steps; step++) {
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
      for (int i = 0; i < 0x20000; i++) if (A->ram[i] != B->ram[i]) printf("  ram %05X: %02X vs %02X\n", i, A->ram[i], B->ram[i]);
      return 1;
    }
    if (A->frames != B->frames) { printf("frame mismatch\n"); return 1; }
  }
  printf("no divergence\n");
  return 0;
}
