// MSU-1 end-to-end test: boots, goes through the crawl into Dune Sea with msu/ test tracks
// installed, and reports per second whether our track is playing and the audio level.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "snes.h"
#include "runner.h"
#include "msu.h"
extern int msu_current_track(void);
static Snes* s;
static int16_t buf[800 * 2];
static void run(int n, uint16_t b, int report) {
  static int fno = 0; static double acc; static int playing;
  for (int f = 0; f < n; f++) {
    for (int i = 0; i < 12; i++) snes_setButtonState(s, 1, i, (b >> i) & 1);
    runner_frame(s);
    snes_setSamples(s, buf, 800);
    msu_mix(buf, 800, 48000, 100);
    for (int i = 0; i < 1600; i++) acc += (double)buf[i] * buf[i];
    playing += msu_playing();
    if (++fno % 60 == 0) {
      if (report) printf("t=%3ds mode %02X scene %02X  msu track %2d playing %2d/60 frames  rms %6.0f\n", fno / 60, s->ram[0x4DA], s->ram[0x368], msu_current_track(), playing, sqrt(acc / (60 * 1600)));
      acc = 0; playing = 0;
    }
  }
}
int main(int argc, char** argv) {
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  s = snes_init(); snes_loadRom(s, rom, len);
  printf("mapped songs: %d\n", msu_init(argv[2], "msu"));
  msu_enable(1);
  run(1500, 0, 0);
  run(4, 1 << BTN_START, 1); run(60, 0, 1);
  run(4, 1 << BTN_START, 1); run(600, 0, 1);    // crawl plays (track 4)
  for (int i = 0; i < 4; i++) { run(4, 1 << BTN_START, 1); run(120, 0, 1); }
  run(900, 1 << BTN_RIGHT, 1);                  // Dune Sea (track 5)
  return 0;
}
