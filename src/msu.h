#ifndef MSU_H
#define MSU_H
#include <stdint.h>
int msu_init(const char* base_dir, const char* folder);   // loads <folder>/msu.txt; returns songs that have a PCM
void msu_enable(int on);
int msu_playing(void);
void msu_mix(int16_t* out, int n, int out_rate, int volume_pct);
#endif
