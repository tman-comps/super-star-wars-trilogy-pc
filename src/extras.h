#ifndef EXTRAS_H
#define EXTRAS_H
#include <stdint.h>
#include "snes.h"
void extras_init(Snes* snes, const char* base_dir);
void extras_set_rom(const uint8_t* rom);           // live cartridge copy (scene table)
void extras_frame(int assisted);                    // once per emulated frame; assisted = no records
void extras_shutdown(void);
int extras_draw_timer(uint32_t* buf, int w, int h); // ARGB overlay, transparent background
int extras_stat_lines(char lines[][48], int max);
uint32_t extras_best_stage(int mode, int scene);
void extras_format_time(char* out, int n, uint32_t frames);
int extras_timer_text(char* run, int rn, char* st, int sn, uint32_t* run_col, uint32_t* st_col, int* split);
#endif
