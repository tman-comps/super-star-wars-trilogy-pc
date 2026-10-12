#ifndef RESTORE_H
#define RESTORE_H
#include "snes.h"
int restore_init(Snes* snes);      // after snes_loadRom; returns 0 if the ROM bytes aren't as expected
void restore_set(int on);
void restore_update(int wanted);   // call once per frame
int restore_available(void);
void restore_tick(void);           // once per frame, after the game's frame
int restore_walls(int* left, int* right, int* top, int* bottom);   // compactor walls (world px), 0 = none
void restore_draw_walls(uint32_t* buf, int stride, int x0, int y0, int scale, int xmin, int xmax);
#endif
