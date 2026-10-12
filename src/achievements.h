#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H
#include "snes.h"
void ach_init(Snes* snes, const char* base_dir);
void ach_set_rom(const uint8_t* rom);
void ach_update(int cheating);          // call once per frame
int ach_total(void);
int ach_unlocked_count(void);
int ach_get(int i, const char** title, const char** desc);   // returns 1 if unlocked
int ach_pop_toast(void);                // next newly unlocked id, or -1
int ach_steam_id(int i);                // Steam achievement number of the running game's achievement i
#endif
