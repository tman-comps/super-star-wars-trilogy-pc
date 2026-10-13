#ifndef EMPIRE_LUKE_H
#define EMPIRE_LUKE_H
#include <stdint.h>
#include "snes.h"
// Empire Luke in Super Star Wars: Super Empire Strikes Back's Luke (graphics, poses, colours) drawn in
// place of Super Star Wars' Luke. Built at start-up from the player's own two ROMs.
int eluke_init(const uint8_t* ssw_rom, int ssw_len, const uint8_t* esb_rom, int esb_len);   // 1 = ready
int eluke_ready(void);
void eluke_frame(Snes* snes);   // once per frame while Super Star Wars runs with Empire Luke on
// (exposed for the test driver)
int eluke_unpack(const uint8_t* rom, int len, uint32_t snes_adr, uint8_t* out, int max);
#endif
