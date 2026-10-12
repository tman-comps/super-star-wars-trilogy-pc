#ifndef RUNNER_H
#define RUNNER_H
#include <stdint.h>
#include "snes.h"

extern int g_use_recomp;   // 1 = recompiled CPU, 0 = reference interpreter
void runner_frame(Snes* snes);
extern void (*g_runner_after_vblank)(void);   // called between vblank (the game's uploads) and the picture
void runner_step(Snes* snes);
extern int g_recomp_single;
uint64_t runner_misses(void);
int runner_miss_list(uint32_t* out, int max);
uint8_t* runner_load_file(const char* path, int* len);
int runner_check_rom(const uint8_t* d, int len);

enum { BTN_B = 0, BTN_Y, BTN_SELECT, BTN_START, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_X, BTN_L, BTN_R };

#endif
