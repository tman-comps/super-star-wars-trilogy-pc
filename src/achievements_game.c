// Routes the achievement calls to the running game's list (game.h): Super Star Wars (achievements.c)
// or Super Empire Strikes Back (achievements_esb.c).
#include <string.h>
#include "achievements.h"
#include "game.h"

void ssw_ach_init(Snes*, const char*); void ssw_ach_set_rom(const uint8_t*); void ssw_ach_update(int);
int ssw_ach_total(void); int ssw_ach_unlocked_count(void); int ssw_ach_get(int, const char**, const char**);
int ssw_ach_pop_toast(void); void ssw_ach_debug_counts(int*, int*);
void esb_ach_init(Snes*, const char*); void esb_ach_set_rom(const uint8_t*); void esb_ach_update(int);
int esb_ach_total(void); int esb_ach_unlocked_count(void); int esb_ach_get(int, const char**, const char**);
int esb_ach_pop_toast(void);

static int esb(void) { return g_game && !strcmp(g_game->key, "esb"); }
void ach_init(Snes* snes, const char* base_dir) { if (esb()) esb_ach_init(snes, base_dir); else ssw_ach_init(snes, base_dir); }
void ach_set_rom(const uint8_t* rom) { if (esb()) esb_ach_set_rom(rom); else ssw_ach_set_rom(rom); }
void ach_update(int cheating) { if (esb()) esb_ach_update(cheating); else ssw_ach_update(cheating); }
int ach_total(void) { return esb() ? esb_ach_total() : ssw_ach_total(); }
int ach_unlocked_count(void) { return esb() ? esb_ach_unlocked_count() : ssw_ach_unlocked_count(); }
int ach_get(int i, const char** t, const char** d) { return esb() ? esb_ach_get(i, t, d) : ssw_ach_get(i, t, d); }
int ach_pop_toast(void) { return esb() ? esb_ach_pop_toast() : ssw_ach_pop_toast(); }
int ach_steam_id(int i) { return esb() ? 16 + i : i; }
void ach_debug_counts(int* kills, int* jawas) { if (esb()) { *kills = *jawas = 0; } else ssw_ach_debug_counts(kills, jawas); }
