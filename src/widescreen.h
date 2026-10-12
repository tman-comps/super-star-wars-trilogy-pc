#ifndef WIDESCREEN_H
#define WIDESCREEN_H
#include <stdint.h>
#include "snes.h"
extern int g_ws_mask_bg3;
void ws_init(Snes* s, const uint8_t* rom, int romlen);
int ws_update(int extra_wanted);   // returns the extra pixels per side actually active this frame
int ws_selfcheck(int* total);
int ws_world_pixel(int wx, int wy);   // playfield colour at a world position, -1 = none
#endif
