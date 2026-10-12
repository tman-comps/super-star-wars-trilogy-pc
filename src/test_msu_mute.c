// Can we silence the SPC music while keeping sound effects? Replaces the "play track" command (06)
// with a candidate command and measures audio RMS in Dune Sea while the player shoots.
//   ssw_msumute <rom> <replace_cmd_hex or -1>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "snes.h"
#include "runner.h"
extern void (*g_snes_write_hook)(Snes*, uint32_t, uint8_t);
static Snes* s; static int repl;
static void hook(Snes* sn, uint32_t adr, uint8_t v) { (void)sn; (void)adr; (void)v; }
// intercept by rewriting the value: we patch snes_write via a pre-hook that can modify the value
extern uint8_t (*g_snes_write_filter)(Snes*, uint32_t, uint8_t);
static uint8_t filt(Snes* sn, uint32_t adr, uint8_t v) {
  if ((adr & 0xffff) == 0x2143 && ((adr >> 16) & 0x7f) < 0x40 && v == 0x06 && repl >= 0) return (uint8_t)repl;
  return v;
}
static int16_t buf[800 * 2];
static double rms_frames(int n, uint16_t btn) {
  double acc = 0; int cnt = 0;
  for (int f = 0; f < n; f++) {
    for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (btn >> i) & 1);
    s->ram[0x0A79] = 32;
    runner_frame(s);
    snes_setSamples(s, buf, 800);
    for (int i = 0; i < 1600; i++) { acc += (double)buf[i] * buf[i]; cnt++; }
  }
  return sqrt(acc / cnt);
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  repl = (int)strtol(argv[2], 0, 16);
  s = snes_init(); snes_loadRom(s, rom, len);
  g_snes_write_filter = filt;
  rms_frames(1500, 0);
  for (int i = 0; i < 6; i++) { rms_frames(4, 1 << BTN_START); rms_frames(120, 0); }
  rms_frames(200, 0);
  double quiet = rms_frames(300, 0);
  double shooting = 0; for (int k = 0; k < 10; k++) shooting += rms_frames(30, k & 1 ? 1 << BTN_Y : 0) / 10;
  printf("replace %s: idle RMS %.0f, while shooting RMS %.0f\n", argv[2], quiet, shooting);
  return 0;
}
