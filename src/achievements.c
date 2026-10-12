// Super Star Wars achievements (the public ach_* calls are routed here by achievements_game.c).
// Port achievements: our own list, inspired by the goals of the PS4/Vita trophy set.
// Everything is detected from game RAM each frame (see sym/ram.txt in the decomp project):
//   $04DA GameMode  $0368 SceneID  $08FB Lives  $0A79 player HP  $09D9 player type (Luke $22,
//   Han $24, Chewbacca $26)  $0985 weapon (4 = Plasma)  $011C difficulty (2 = Jedi)
//   object slots: type $09D9+2n, HP $0A79+2n   boss: $09A3 BossSlot (slot*2)
// Stage completion = a transition from a stage to its successor in the game's own scene table
// ($80:9D4A), so deaths and restarts don't count as completions.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "achievements.h"
#include "steam.h"

typedef struct { const char* title; const char* desc; } Ach;
enum {
  A_START, A_DUNESEA, A_SANDCRAWLER, A_MOSEISLEY, A_DEATHSTAR, A_RESCUE, A_ENDING, A_JEDIMASTER,
  A_PLASMA, A_TRIO, A_HANCANTINA, A_CHEWBOSSES, A_JAWAS, A_UNTOUCHED, A_TRENCH, A_ACE, A_COUNT
};
static const Ach k_ach[A_COUNT] = {
  {"A Long Time Ago", "Start a new adventure on Tatooine."},
  {"Dune Sea Survivor", "Complete the Dune Sea."},
  {"Junk Collector", "Fight your way through the Sandcrawler."},
  {"Wretched Hive", "Arrive in Mos Eisley."},
  {"That's No Moon", "Reach the Death Star."},
  {"Rescue Mission", "Get the princess out of the detention block."},
  {"The Force Will Be With You", "Destroy the Death Star."},
  {"Jedi Master", "Destroy the Death Star on Jedi difficulty without losing a life."},
  {"Heavy Artillery", "Power your blaster all the way up to Plasma."},
  {"Three of a Kind", "Play as Luke, Han and Chewbacca."},
  {"Shoot First", "Clear the Cantina as Han Solo."},
  {"Laugh It Up, Fuzzball", "Defeat three bosses as Chewbacca."},
  {"Jawa Hunter", "Defeat 20 Jawas in a single trip through the Sandcrawler."},
  {"Untouchable", "Complete the Dune Sea without taking any damage."},
  {"Stay On Target", "Complete the trench run without losing a life."},
  {"Ace Pilot", "Destroy 20 enemies in the trench run."},
};

static char g_path[1100];
static int g_unlocked[A_COUNT];
static int g_toast_queue[A_COUNT], g_toast_n;
static Snes* s;

// progress
static int g_prev_mode = -1, g_prev_scene = -1;
static int g_heroes;                  // bit 0 Luke, 1 Han, 2 Chewbacca
static int g_chew_bosses;
static int g_run_active, g_run_lost, g_run_jedi;
static int g_stage_lost, g_stage_hurt, g_stage_kills, g_stage_jawas;
static int g_prev_lives = -1, g_prev_hp = -1;
static uint16_t g_prev_slot_hp[20], g_prev_slot_type[20];
static int g_boss_counted;

static uint16_t r16(int a) { return s->ram[a] | s->ram[a + 1] << 8; }

static void save(void) {
  FILE* f = fopen(g_path, "w");
  if (!f) return;
  for (int i = 0; i < A_COUNT; i++) if (g_unlocked[i]) fprintf(f, "%d\n", i);
  fprintf(f, "heroes %d\nchewbosses %d\n", g_heroes, g_chew_bosses);
  fclose(f);
}
static void unlock(int id) {
  if (g_unlocked[id]) return;
  g_unlocked[id] = 1;
  if (g_toast_n < A_COUNT) g_toast_queue[g_toast_n++] = id;
  steam_unlock(id);   // no-op unless Steam is available
  save();
}

void ssw_ach_init(Snes* snes, const char* base_dir) {
  s = snes;
  memset(g_unlocked, 0, sizeof g_unlocked); g_toast_n = 0; g_heroes = g_chew_bosses = 0;
  g_prev_mode = g_prev_scene = g_prev_lives = g_prev_hp = -1; g_run_active = 0;
  snprintf(g_path, sizeof g_path, "%sssw_achievements.txt", base_dir);
  FILE* f = fopen(g_path, "r");
  if (!f) return;
  char line[64];
  while (fgets(line, sizeof line, f)) {
    int v;
    if (sscanf(line, "heroes %d", &v) == 1) g_heroes = v;
    else if (sscanf(line, "chewbosses %d", &v) == 1) g_chew_bosses = v;
    else if (sscanf(line, "%d", &v) == 1 && v >= 0 && v < A_COUNT) g_unlocked[v] = 1;
  }
  fclose(f);
}

// successor of (mode, scene) in the game's scene table at $80:9D4A (ROM offset $1D4A)
static int successor(const uint8_t* rom, int mode, int scene, int* nm, int* ns) {
  for (int o = 0x1D4A; rom[o] != 0xFF; o += 4)
    if (rom[o] == mode && (rom[o + 1] == 0xFF || rom[o + 1] == scene)) { *nm = rom[o + 2]; *ns = rom[o + 3]; return 1; }
  return 0;
}
static const uint8_t* g_rom;
void ssw_ach_set_rom(const uint8_t* rom) { g_rom = rom; }

static void stage_completed(int mode, int scene) {
  if (mode == 0x04 && scene == 0x10) { unlock(A_DUNESEA); if (!g_stage_hurt) unlock(A_UNTOUCHED); }
  if (mode == 0x04 && scene == 0x01) { unlock(A_SANDCRAWLER); }
  if (mode == 0x04 && scene == 0x0C && r16(0x09D9) == 0x24) unlock(A_HANCANTINA);
  if (mode == 0x04 && scene == 0x11) unlock(A_RESCUE);
  if (mode == 0x10 && scene == 0x06) { if (!g_stage_lost) unlock(A_TRENCH); }
}

void ssw_ach_update(int cheating) {
  if (!s) return;
  int mode = r16(0x04DA), scene = r16(0x0368);
  int lives = s->ram[0x08FB], hp = s->ram[0x0A79];
  if (cheating) { g_run_active = 0; }
  // scene transitions
  if (mode != g_prev_mode || scene != g_prev_scene) {
    int nm, ns;
    if (g_prev_mode >= 0 && g_rom && successor(g_rom, g_prev_mode, g_prev_scene, &nm, &ns) && nm == mode && ns == scene && !cheating)
      stage_completed(g_prev_mode, g_prev_scene);
    if (mode == 0x04 && scene == 0x10 && g_prev_mode != 0x04) {     // new game starts in the Dune Sea
      if (!cheating) unlock(A_START);
      g_run_active = !cheating; g_run_lost = 0; g_run_jedi = s->ram[0x011C] == 2;
    }
    if (mode == 0x04 && scene == 0x0D && !cheating) unlock(A_MOSEISLEY);
    if (mode == 0x04 && scene == 0x00 && !cheating) unlock(A_DEATHSTAR);
    if (mode == 0x0A && scene == 0x1C && g_prev_mode != -1 && !cheating) {
      unlock(A_ENDING);
      if (g_run_active && g_run_jedi && !g_run_lost) unlock(A_JEDIMASTER);
    }
    // per-stage counters reset whenever the scene changes
    g_stage_lost = 0; g_stage_hurt = 0; g_stage_kills = 0; g_stage_jawas = 0;
    g_prev_mode = mode; g_prev_scene = scene;
    g_prev_hp = -1;
    memset(g_prev_slot_hp, 0, sizeof g_prev_slot_hp);
  }
  if (cheating) { g_prev_lives = lives; return; }
  // lives and damage
  if (g_prev_lives >= 0 && lives < g_prev_lives && lives != 0xFF) { g_run_lost = 1; g_stage_lost = 1; }
  g_prev_lives = lives;
  if (mode == 0x04 && g_prev_hp >= 0 && hp < g_prev_hp) g_stage_hurt = 1;
  g_prev_hp = hp;
  // gameplay-only checks
  if (mode == 0x04) {
    uint16_t pt = r16(0x09D9);
    if (pt == 0x22) g_heroes |= 1; else if (pt == 0x24) g_heroes |= 2; else if (pt == 0x26) g_heroes |= 4;
    if (g_heroes == 7) unlock(A_TRIO);
    if (s->ram[0x0985] == 4) unlock(A_PLASMA);
  }
  // kills: an object slot whose HP drops from >0 to 0
  for (int n = 1; n < 20; n++) {
    uint16_t t = r16(0x09D9 + 2 * n), h = r16(0x0A79 + 2 * n);
    if (g_prev_slot_type[n] == t && g_prev_slot_hp[n] > 0 && g_prev_slot_hp[n] < 0x8000 && h == 0) {
      g_stage_kills++;
      if (mode == 0x04 && scene == 0x01 && t == 0x7C && ++g_stage_jawas >= 20) unlock(A_JAWAS);
      if (mode == 0x10 && scene == 0x06 && g_stage_kills >= 20) unlock(A_ACE);
    }
    g_prev_slot_type[n] = t; g_prev_slot_hp[n] = h;
  }
  // bosses defeated as Chewbacca
  int bs = r16(0x09A3);
  if (bs > 0 && bs < 0x28 && !(bs & 1)) {
    uint16_t bh = r16(0x0A79 + bs);
    if (bh == 0 && !g_boss_counted) {
      g_boss_counted = 1;
      if (r16(0x09D9) == 0x26 && ++g_chew_bosses >= 3) unlock(A_CHEWBOSSES);
      save();
    } else if (bh > 0) g_boss_counted = 0;
  } else g_boss_counted = 0;
}

int ssw_ach_total(void) { return A_COUNT; }
int ssw_ach_unlocked_count(void) { int n = 0; for (int i = 0; i < A_COUNT; i++) n += g_unlocked[i]; return n; }
int ssw_ach_get(int i, const char** title, const char** desc) { *title = k_ach[i].title; *desc = k_ach[i].desc; return g_unlocked[i]; }
int ssw_ach_pop_toast(void) {
  if (!g_toast_n) return -1;
  int id = g_toast_queue[0];
  memmove(g_toast_queue, g_toast_queue + 1, --g_toast_n * sizeof(int));
  return id;
}

void ssw_ach_debug_counts(int* kills, int* jawas) { *kills = g_stage_kills; *jawas = g_stage_jawas; }
