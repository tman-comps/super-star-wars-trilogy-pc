// Achievement logic test: new game, god-mode bot play with pad-2 stage clears (cheating flag
// passed as 0 so detection runs), printing each unlock with the frame it happened on.
#include <stdio.h>
#include <stdlib.h>
#include "snes.h"
#include "runner.h"
#include "achievements.h"
static Snes* s; static int fno; static uint32_t seed = 9;
static void frame(uint16_t p1, uint16_t p2) {
  if ((s->ram[0x4DA] | s->ram[0x4DB] << 8) == 0x0C) p1 = (fno & 8) ? 1 << BTN_START : 0;
  for (int i = 0; i < 12; i++) { snes_setButtonState(s, 1, i, (p1 >> i) & 1); snes_setButtonState(s, 2, i, (p2 >> i) & 1); }
  s->ram[0x0A79] = 32; s->ram[0x08FB] = 3; s->ram[0x010E] = 1;
  for (int n = 1; n < 20; n++) { uint16_t h = s->ram[0xA79 + 2 * n] | s->ram[0xA7A + 2 * n] << 8; if (h > 1 && h < 0x8000) { s->ram[0xA79 + 2 * n] = 1; s->ram[0xA7A + 2 * n] = 0; } }
  runner_frame(s); fno++;
  ach_update(0);
  int id; while ((id = ach_pop_toast()) >= 0) { const char *t, *d; ach_get(id, &t, &d); printf("frame %6d mode %02X scene %02X  UNLOCK: %s\n", fno, s->ram[0x4DA], s->ram[0x368], t); }
}
static void play(int n) {
  static const uint16_t cho[] = {1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), 1 << BTN_Y, (1 << BTN_LEFT) | (1 << BTN_Y), (1 << BTN_B) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B)};
  for (int i = 0; i < n; i += 15) { seed = seed * 1103515245u + 12345u; uint16_t b = cho[(seed >> 16) % 6]; for (int k = 0; k < 15; k++) frame(b, 0); }
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  if (argc > 2) for (int e = 1; e <= 4; e++) { rom[0x1D4A + 4 * e + 2] = 0x04; rom[0x1D4A + 4 * e + 3] = 0x01; }
  s = snes_init(); snes_loadRom(s, rom, len);
  ach_init(s, "achtest_"); ach_set_rom(rom);
  for (int f = 0; f < 1500; f++) frame(0, 0);
  for (int i = 0; i < 6; i++) { for (int f = 0; f < 4; f++) frame(1 << BTN_START, 0); for (int f = 0; f < 120; f++) frame(0, 0); }
  if (argc > 2) {   // jawa check: warped into the Sandcrawler interior
    extern void ach_debug_counts(int*, int*);
    for (int k = 0; k < 10; k++) { play(1200); int a, b; ach_debug_counts(&a, &b); printf("after %d frames: kills %d jawas %d\n", (k + 1) * 1200, a, b); }
    return 0;
  }
  for (int st = 0; st < 16; st++) {
    play(st == 3 ? 6000 : 1500);   // longer in the Sandcrawler interior to count Jawas
    for (int f = 0; f < 4; f++) frame(0, 1 << BTN_START);
    for (int k = 0; k < 5; k++) { for (int f = 0; f < 4; f++) frame(1 << BTN_START, 0); for (int f = 0; f < 100; f++) frame(0, 0); }
    printf("  -- now mode %02X scene %02X, %d/%d unlocked\n", s->ram[0x4DA], s->ram[0x368], ach_unlocked_count(), ach_total());
  }
  return 0;
}
