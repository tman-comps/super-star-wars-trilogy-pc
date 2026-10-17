// Routes the achievement calls to the running game's list (game.h): Super Star Wars (achievements.c),
// Super Empire Strikes Back (achievements_esb.c) or Super Return of the Jedi (achievements_rotj.c).
#include <string.h>
#include "achievements.h"
#include "game.h"

void ssw_ach_init(Snes*, const char*); void ssw_ach_set_rom(const uint8_t*); void ssw_ach_update(int);
int ssw_ach_total(void); int ssw_ach_unlocked_count(void); int ssw_ach_get(int, const char**, const char**);
int ssw_ach_pop_toast(void); void ssw_ach_debug_counts(int*, int*);
void esb_ach_init(Snes*, const char*); void esb_ach_set_rom(const uint8_t*); void esb_ach_update(int);
int esb_ach_total(void); int esb_ach_unlocked_count(void); int esb_ach_get(int, const char**, const char**);
int esb_ach_pop_toast(void);
void rotj_ach_init(Snes*, const char*); void rotj_ach_set_rom(const uint8_t*); void rotj_ach_update(int);
int rotj_ach_total(void); int rotj_ach_unlocked_count(void); int rotj_ach_get(int, const char**, const char**);
int rotj_ach_pop_toast(void);

static int which(void) { return !g_game ? 0 : !strcmp(g_game->key, "esb") ? 1 : !strcmp(g_game->key, "srotj") ? 2 : 0; }
void ach_init(Snes* snes, const char* base_dir) { switch (which()) { case 1: esb_ach_init(snes, base_dir); break; case 2: rotj_ach_init(snes, base_dir); break; default: ssw_ach_init(snes, base_dir); } }
void ach_set_rom(const uint8_t* rom) { switch (which()) { case 1: esb_ach_set_rom(rom); break; case 2: rotj_ach_set_rom(rom); break; default: ssw_ach_set_rom(rom); } }
void ach_update(int cheating) { switch (which()) { case 1: esb_ach_update(cheating); break; case 2: rotj_ach_update(cheating); break; default: ssw_ach_update(cheating); } }
int ach_total(void) { switch (which()) { case 1: return esb_ach_total(); case 2: return rotj_ach_total(); default: return ssw_ach_total(); } }
int ach_unlocked_count(void) { switch (which()) { case 1: return esb_ach_unlocked_count(); case 2: return rotj_ach_unlocked_count(); default: return ssw_ach_unlocked_count(); } }
int ach_get(int i, const char** t, const char** d) { switch (which()) { case 1: return esb_ach_get(i, t, d); case 2: return rotj_ach_get(i, t, d); default: return ssw_ach_get(i, t, d); } }
int ach_pop_toast(void) { switch (which()) { case 1: return esb_ach_pop_toast(); case 2: return rotj_ach_pop_toast(); default: return ssw_ach_pop_toast(); } }
int ach_steam_id(int i) { return which() * 16 + i; }
void ach_debug_counts(int* kills, int* jawas) { if (which()) { *kills = *jawas = 0; } else ssw_ach_debug_counts(kills, jawas); }
