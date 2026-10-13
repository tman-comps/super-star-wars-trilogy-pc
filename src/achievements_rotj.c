// Super Return of the Jedi achievements. Same rules as the other two games' (achievements.c,
// achievements_esb.c): everything is read from game RAM, stage completion is a transition to the
// stage's successor in the game's own scene table ($80:8627), and nothing counts while developer
// features, cheats or rewind are in use.
//   $01BE GameMode  $01C0 SceneID  $01B2 lives  object slots (20, two bytes each):
//   type $0BB8+2n (slot 0 = the hero: 4 Luke, 6 Han, 8 Chewbacca, A Wicket, C/E/10 Leia)  HP $0C58+2n
// Steam names continue after Empire's: ACH_32 .. ACH_47.
#include <stdio.h>
#include <string.h>
#include "snes.h"
#include "steam.h"

#define STEAM_BASE 32
#define SEQ 0x0627
typedef struct { const char* title; const char* desc; } Ach;
enum {
  J_START, J_DANCE, J_RANCOR, J_BARGE, J_BIKES, J_EWOKS, J_GENERATOR, J_FLEET, J_VADER, J_EMPEROR,
  J_REACTOR, J_ENDING, J_ALLIANCE, J_UNTOUCHED, J_SCRAP, J_STEADY, J_COUNT
};
static const Ach k_ach[J_COUNT] = {
  {"Meanwhile On Tatooine", "Start a new adventure in the Dune Sea."},
  {"Boushh", "Get Leia through Jabba's dance hall."},
  {"Rancor Keeper", "Defeat the rancor."},
  {"Sarlacc Pit", "Escape from Jabba's sail barge."},
  {"Speeder Bike Chase", "Complete the speeder bike chase on Endor."},
  {"Honorary Ewok", "Make it through both Ewok villages."},
  {"Shield Down", "Destroy the shield generator."},
  {"It's A Trap", "Survive the space battle."},
  {"Tower Entrance", "Face Darth Vader and live."},
  {"I Am A Jedi", "Defeat the Emperor."},
  {"Main Reactor", "Destroy the Death Star."},
  {"Return Of The Jedi", "Finish the game."},
  {"Rebel Alliance", "Play as Luke, Han, Chewbacca, Leia and Wicket."},
  {"Untouchable", "Complete a side-scrolling stage without taking any damage."},
  {"Imperial Scrap", "Defeat 500 enemies."},
  {"Steady Hands", "Complete the speeder bike chase without losing a life."},
};

static char g_path[1100];
static int g_unlocked[J_COUNT];
static int g_toast_queue[J_COUNT], g_toast_n;
static Snes* s;
static const uint8_t* g_rom;
static int g_prev_mode = -1, g_prev_scene = -1, g_prev_lives = -1, g_prev_hp = -1;
static int g_heroes, g_kills;
static int g_stage_lost, g_stage_hurt;
static uint16_t g_prev_slot_hp[20], g_prev_slot_type[20];

static uint16_t r16(int a) { return s->ram[a] | s->ram[a + 1] << 8; }
static void save(void) {
  FILE* f = fopen(g_path, "w");
  if (!f) return;
  for (int i = 0; i < J_COUNT; i++) if (g_unlocked[i]) fprintf(f, "%d\n", i);
  fprintf(f, "heroes %d\nkills %d\n", g_heroes, g_kills);
  fclose(f);
}
static void unlock(int id) {
  if (g_unlocked[id]) return;
  g_unlocked[id] = 1;
  if (g_toast_n < J_COUNT) g_toast_queue[g_toast_n++] = id;
  steam_unlock(STEAM_BASE + id);
  save();
}

void rotj_ach_init(Snes* snes, const char* base_dir) {
  s = snes;
  g_prev_mode = g_prev_scene = g_prev_lives = g_prev_hp = -1;
  memset(g_unlocked, 0, sizeof g_unlocked); g_toast_n = 0; g_heroes = g_kills = 0;
  snprintf(g_path, sizeof g_path, "%ssrotj_achievements.txt", base_dir);
  FILE* f = fopen(g_path, "r");
  if (!f) return;
  char line[64];
  while (fgets(line, sizeof line, f)) {
    int v;
    if (sscanf(line, "heroes %d", &v) == 1) g_heroes = v;
    else if (sscanf(line, "kills %d", &v) == 1) g_kills = v;
    else if (sscanf(line, "%d", &v) == 1 && v >= 0 && v < J_COUNT) g_unlocked[v] = 1;
  }
  fclose(f);
}
void rotj_ach_set_rom(const uint8_t* rom) { g_rom = rom; }

static int successor(int mode, int scene, int* nm, int* ns) {
  for (int o = SEQ; g_rom[o] != 0xFF; o += 4)
    if (g_rom[o] == mode && (g_rom[o + 1] == 0xFF || g_rom[o + 1] == scene)) { *nm = g_rom[o + 2]; *ns = g_rom[o + 3]; return 1; }
  return 0;
}
// side-scrolling 04, reactor run 0A, space battle 0C, speeder bikes 12, landspeeder / Death Star surface 14
static int play_mode(int m) { return m == 0x04 || m == 0x0A || m == 0x0C || m == 0x12 || m == 0x14; }

static void stage_completed(int mode, int scene) {
  int st = mode << 8 | scene;
  if (st == 0x0407) unlock(J_DANCE);
  if (st == 0x0408) unlock(J_RANCOR);
  if (st == 0x0406) unlock(J_BARGE);
  if (st == 0x1211) { unlock(J_BIKES); if (!g_stage_lost) unlock(J_STEADY); }
  if (st == 0x040B) unlock(J_EWOKS);
  if (st == 0x0409) unlock(J_GENERATOR);
  if (st == 0x0C10) unlock(J_FLEET);
  if (st == 0x040C) unlock(J_VADER);
  if (st == 0x040D) unlock(J_EMPEROR);
  if (st == 0x0A02) unlock(J_REACTOR);
  if (mode == 0x04 && !g_stage_hurt) unlock(J_UNTOUCHED);
}

void rotj_ach_update(int cheating) {
  if (!s || !g_rom) return;
  int mode = r16(0x01BE), scene = s->ram[0x01C0];
  int lives = s->ram[0x01B2], hp = r16(0x0C58);
  if (mode != g_prev_mode || scene != g_prev_scene) {
    int nm, ns;
    if (g_prev_mode >= 0 && !cheating && successor(g_prev_mode, g_prev_scene, &nm, &ns) && nm == mode && ns == scene)
      stage_completed(g_prev_mode, g_prev_scene);
    if (!cheating) {
      if (mode == 0x14 && scene == 0x12 && !play_mode(g_prev_mode)) unlock(J_START);
      // the credits after the celebration, not the title code that shows them
      if (mode == 0x06 && scene == 0x17 && g_prev_mode == 0x06 && g_prev_scene == 0x30) unlock(J_ENDING);
    }
    g_stage_lost = 0; g_stage_hurt = 0;
    g_prev_mode = mode; g_prev_scene = scene; g_prev_hp = -1;
    memset(g_prev_slot_hp, 0, sizeof g_prev_slot_hp);
  }
  if (cheating) { g_prev_lives = lives; return; }
  if (g_prev_lives >= 0 && lives < g_prev_lives && lives != 0xFF) g_stage_lost = 1;
  g_prev_lives = lives;
  if (play_mode(mode) && g_prev_hp >= 0 && hp < g_prev_hp && hp < 0x8000) g_stage_hurt = 1;
  g_prev_hp = hp;
  if (mode == 0x04) {
    int hero = r16(0x0BB8);
    if (hero == 4) g_heroes |= 1; else if (hero == 6) g_heroes |= 2; else if (hero == 8) g_heroes |= 4;
    else if (hero == 0x0A) g_heroes |= 8; else if (hero == 0x0C || hero == 0x0E || hero == 0x10) g_heroes |= 16;
    if (g_heroes == 31) unlock(J_ALLIANCE);
  }
  if (play_mode(mode)) {   // kills: an object slot whose HP drops from >0 to 0 while it keeps its type
    for (int n = 1; n < 20; n++) {
      uint16_t t = r16(0x0BB8 + 2 * n), h = r16(0x0C58 + 2 * n);
      if (g_prev_slot_type[n] == t && g_prev_slot_hp[n] > 0 && g_prev_slot_hp[n] < 0x8000 && h == 0) {
        if (++g_kills >= 500) unlock(J_SCRAP);
        if (g_kills % 50 == 0) save();
      }
      g_prev_slot_type[n] = t; g_prev_slot_hp[n] = h;
    }
  }
}

int rotj_ach_total(void) { return J_COUNT; }
int rotj_ach_unlocked_count(void) { int n = 0; for (int i = 0; i < J_COUNT; i++) n += g_unlocked[i]; return n; }
int rotj_ach_get(int i, const char** title, const char** desc) { *title = k_ach[i].title; *desc = k_ach[i].desc; return g_unlocked[i]; }
int rotj_ach_pop_toast(void) {
  if (!g_toast_n) return -1;
  int id = g_toast_queue[0];
  memmove(g_toast_queue, g_toast_queue + 1, --g_toast_n * sizeof(int));
  return id;
}
int rotj_ach_kills(void) { return g_kills; }
