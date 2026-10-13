#ifndef EMPIRE_LUKE_H
#define EMPIRE_LUKE_H
#include <stdint.h>
#include "snes.h"
// Empire Luke in Super Star Wars: Super Empire Strikes Back's Luke (graphics, poses, colours) drawn in
// place of Super Star Wars' Luke. Built at start-up from the player's own two ROMs.
int eluke_init(const uint8_t* ssw_rom, int ssw_len, const uint8_t* esb_rom, int esb_len);   // 1 = ready
int eluke_ready(void);
void eluke_frame(Snes* snes);   // once per frame while Super Star Wars runs with Empire Luke on
// Empire Luke's moves and the Force (side-scrolling stages, while the player is Empire Luke): double jump
// (jump again in mid-air), Force meter, L/R pick a power, X uses it. held = SNES buttons held this frame
// (LakeSnes order: B Y Select Start Up Down Left Right A X L R). Call after the game's frame.
void eluke_play(Snes* snes, uint16_t held);
int eluke_active(Snes* snes);   // Empire Luke is the player right now
int eluke_hud(char* out, int n);   // text for the HUD line ("FORCE 80 HEAL"), 0 = none
// (exposed for the test driver)
int eluke_unpack(const uint8_t* rom, int len, uint32_t snes_adr, uint8_t* out, int max);
#endif
