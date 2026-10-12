// Super Empire Strikes Back achievements. Same rules as Super Star Wars' (achievements.c): everything
// is read from game RAM, stage completion is a transition to the stage's successor in the game's own
// scene table ($80:8D4E), and nothing counts while developer features, cheats or rewind are in use.
//   $01B6 GameMode  $01B8 SceneID  $01AC lives  object slots (20, two bytes each):
//   type $0C56+2n (slot 0 = the hero: 4 Luke, 6 Han, 8 Chewbacca)  HP $0CF6+2n
// Steam names continue after Super Star Wars': ACH_16 .. ACH_31.
#include <stdio.h>
#include <string.h>
#include "snes.h"
#include "steam.h"

#define STEAM_BASE 16
typedef struct { const char* title; const char* desc; } Ach;
enum {
  E_START, E_WAMPA, E_SPEEDER, E_HOTH, E_EVAC, E_ASTEROIDS, E_DAGOBAH, E_BESPIN, E_CARBON, E_VADER,
  E_ENDING, E_TRIO, E_UNTOUCHED, E_SCRAP, E_TOWCABLE, E_ODDS, E_COUNT
};
static const Ach k_ach[E_COUNT] = {
  {"Ice Planet", "Start a new adventure on Hoth."},
  {"Cold Reception", "Escape from the wampa's cave."},
  {"Rogue Squadron", "Complete the snowspeeder mission."},
  {"Walkers Down", "Win the battle of Hoth."},
  {"Evacuation", "Get the Millennium Falcon out of Echo Base."},
  {"Never Tell Me The Odds", "Make it through the asteroid field."},
  {"Do Or Do Not", "Complete your training on Dagobah."},
  {"Cloud City", "Arrive at Bespin."},
  {"In Carbonite", "Survive the carbon-freezing chamber."},
  {"The Dark Side", "Face Darth Vader and live."},
  {"The Empire Strikes Back", "Finish the game."},
  {"Rebel Trio", "Play as Luke, Han and Chewbacca."},
  {"Untouchable", "Complete a side-scrolling stage without taking any damage."},
  {"Imperial Scrap", "Defeat 500 enemies."},
  {"Tow Cables", "Complete the snowspeeder mission without losing a life."},
  {"Flying Casual", "Make it through the asteroid field without losing a life."},
};

static char g_path[1100];
static int g_unlocked[E_COUNT];
static int g_toast_queue[E_COUNT], g_toast_n;
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
  for (int i = 0; i < E_COUNT; i++) if (g_unlocked[i]) fprintf(f, "%d\n", i);
  fprintf(f, "heroes %d\nkills %d\n", g_heroes, g_kills);
  fclose(f);
}
static void unlock(int id) {
  if (g_unlocked[id]) return;
  g_unlocked[id] = 1;
  if (g_toast_n < E_COUNT) g_toast_queue[g_toast_n++] = id;
  steam_unlock(STEAM_BASE + id);
  save();
}

void esb_ach_init(Snes* snes, const char* base_dir) {
  s = snes;
  g_prev_mode = g_prev_scene = g_prev_lives = g_prev_hp = -1;
  memset(g_unlocked, 0, sizeof g_unlocked); g_toast_n = 0; g_heroes = g_kills = 0;
  snprintf(g_path, sizeof g_path, "%sesb_achievements.txt", base_dir);
  FILE* f = fopen(g_path, "r");
  if (!f) return;
  char line[64];
  while (fgets(line, sizeof line, f)) {
    int v;
    if (sscanf(line, "heroes %d", &v) == 1) g_heroes = v;
    else if (sscanf(line, "kills %d", &v) == 1) g_kills = v;
    else if (sscanf(line, "%d", &v) == 1 && v >= 0 && v < E_COUNT) g_unlocked[v] = 1;
  }
  fclose(f);
}
void esb_ach_set_rom(const uint8_t* rom) { g_rom = rom; }

static int successor(int mode, int scene, int* nm, int* ns) {
  for (int o = 0x0D4E; g_rom[o] != 0xFF; o += 4)
    if (g_rom[o] == mode && (g_rom[o + 1] == 0xFF || g_rom[o + 1] == scene)) { *nm = g_rom[o + 2]; *ns = g_rom[o + 3]; return 1; }
  return 0;
}
static int play_mode(int m) { return m == 0x04 || m == 0x0E || m == 0x12; }

static void stage_completed(int mode, int scene) {
  int st = mode << 8 | scene;
  if (st == 0x0405) unlock(E_WAMPA);
  if (st == 0x0E0A) { unlock(E_SPEEDER); if (!g_stage_lost) unlock(E_TOWCABLE); }
  if (st == 0x0415) unlock(E_HOTH);
  if (st == 0x0408) unlock(E_EVAC);
  if (st == 0x1200) { unlock(E_ASTEROIDS); if (!g_stage_lost) unlock(E_ODDS); }
  if (st == 0x040E) unlock(E_DAGOBAH);
  if (st == 0x0417) unlock(E_CARBON);
  if (st == 0x0416) unlock(E_VADER);
  if (mode == 0x04 && !g_stage_hurt) unlock(E_UNTOUCHED);
}

void esb_ach_update(int cheating) {
  if (!s || !g_rom) return;
  int mode = r16(0x01B6), scene = s->ram[0x01B8];
  int lives = s->ram[0x01AC], hp = r16(0x0CF6);
  if (mode != g_prev_mode || scene != g_prev_scene) {
    int nm, ns;
    if (g_prev_mode >= 0 && !cheating && successor(g_prev_mode, g_prev_scene, &nm, &ns) && nm == mode && ns == scene)
      stage_completed(g_prev_mode, g_prev_scene);
    if (!cheating) {
      if (mode == 0x04 && scene == 0x01 && !play_mode(g_prev_mode)) unlock(E_START);
      if (mode == 0x04 && scene == 0x0F) unlock(E_BESPIN);
      if (mode == 0x0A && scene == 0x1D && g_prev_mode != -1) unlock(E_ENDING);
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
    int hero = r16(0x0C56);
    if (hero == 4) g_heroes |= 1; else if (hero == 6) g_heroes |= 2; else if (hero == 8) g_heroes |= 4;
    if (g_heroes == 7) unlock(E_TRIO);
  }
  if (play_mode(mode)) {   // kills: an object slot whose HP drops from >0 to 0 while it keeps its type
    for (int n = 1; n < 20; n++) {
      uint16_t t = r16(0x0C56 + 2 * n), h = r16(0x0CF6 + 2 * n);
      if (g_prev_slot_type[n] == t && g_prev_slot_hp[n] > 0 && g_prev_slot_hp[n] < 0x8000 && h == 0) {
        if (++g_kills >= 500) unlock(E_SCRAP);
        if (g_kills % 50 == 0) save();
      }
      g_prev_slot_type[n] = t; g_prev_slot_hp[n] = h;
    }
  }
}

int esb_ach_total(void) { return E_COUNT; }
int esb_ach_unlocked_count(void) { int n = 0; for (int i = 0; i < E_COUNT; i++) n += g_unlocked[i]; return n; }
int esb_ach_get(int i, const char** title, const char** desc) { *title = k_ach[i].title; *desc = k_ach[i].desc; return g_unlocked[i]; }
int esb_ach_pop_toast(void) {
  if (!g_toast_n) return -1;
  int id = g_toast_queue[0];
  memmove(g_toast_queue, g_toast_queue + 1, --g_toast_n * sizeof(int));
  return id;
}
void esb_ach_save(void) { save(); }
int esb_ach_kills(void) { return g_kills; }
