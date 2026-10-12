// Port extras: speedrun timer (run + per-stage splits with personal bests) and play statistics.
// Everything is detected from game RAM (addresses per game in game.c), like the achievements.
// A stage counts as completed on a transition to its successor in the game's scene table.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "extras.h"
#include "font8x16.h"
#include "game.h"

static Snes* s;
static const uint8_t* g_rom;
static char g_times_path[1100], g_stats_path[1100];

// best stage times in frames, indexed by mode*256+scene; 0 = none
static uint32_t g_best[65536];
static uint32_t g_best_run;
// stats
static uint64_t g_play_frames;
static uint32_t g_deaths, g_stages_cleared, g_runs_done, g_kills, g_runs_started;

// live timer state
static int g_run_on, g_in_stage;
static uint32_t g_run_frames, g_stage_frames;
static int g_prev_mode = -1, g_prev_scene = -1, g_prev_lives = -1;
static uint16_t g_prev_hp[20], g_prev_type[20];
static int g_split_show;            // frames left to show the last split
static int32_t g_split_diff;        // last completed stage time minus previous best (frames), or 0x7fffffff if new
static uint32_t g_split_time;
static int g_dirty;

static int gameplay(int m) { return game_is_play_mode(m); }

static void save(void) {
  FILE* f = fopen(g_times_path, "w");
  if (f) {
    fprintf(f, "run %u\n", g_best_run);
    for (int i = 0; i < 65536; i++) if (g_best[i]) fprintf(f, "stage %02X%02X %u\n", i >> 8, i & 255, g_best[i]);
    fclose(f);
  }
  f = fopen(g_stats_path, "w");
  if (f) {
    fprintf(f, "play_frames %llu\ndeaths %u\nstages_cleared %u\nruns_started %u\nruns_done %u\nkills %u\n",
            (unsigned long long)g_play_frames, g_deaths, g_stages_cleared, g_runs_started, g_runs_done, g_kills);
    fclose(f);
  }
  g_dirty = 0;
}

void extras_init(Snes* snes, const char* base_dir) {
  if (s && g_dirty) save();   // switching games: keep the previous game's records
  s = snes;
  memset(g_best, 0, sizeof g_best); g_best_run = 0;
  g_play_frames = 0; g_deaths = g_stages_cleared = g_runs_done = g_kills = g_runs_started = 0;
  g_run_on = g_in_stage = 0; g_run_frames = g_stage_frames = 0;
  g_prev_mode = g_prev_scene = g_prev_lives = -1; g_split_show = 0; g_dirty = 0;
  snprintf(g_times_path, sizeof g_times_path, "%s%s_times.txt", base_dir, g_game->key);
  snprintf(g_stats_path, sizeof g_stats_path, "%s%s_stats.txt", base_dir, g_game->key);
  FILE* f = fopen(g_times_path, "r");
  char line[128];
  if (f) {
    while (fgets(line, sizeof line, f)) {
      unsigned a, v;
      if (sscanf(line, "run %u", &v) == 1) g_best_run = v;
      else if (sscanf(line, "stage %4x %u", &a, &v) == 2 && a < 65536) g_best[a] = v;
    }
    fclose(f);
  }
  f = fopen(g_stats_path, "r");
  if (f) {
    while (fgets(line, sizeof line, f)) {
      unsigned long long v;
      char k[64];
      if (sscanf(line, "%63s %llu", k, &v) != 2) continue;
      if (!strcmp(k, "play_frames")) g_play_frames = v;
      else if (!strcmp(k, "deaths")) g_deaths = (uint32_t)v;
      else if (!strcmp(k, "stages_cleared")) g_stages_cleared = (uint32_t)v;
      else if (!strcmp(k, "runs_started")) g_runs_started = (uint32_t)v;
      else if (!strcmp(k, "runs_done")) g_runs_done = (uint32_t)v;
      else if (!strcmp(k, "kills")) g_kills = (uint32_t)v;
    }
    fclose(f);
  }
}
void extras_set_rom(const uint8_t* rom) { g_rom = rom; }

static int successor(int mode, int scene, int* nm, int* ns) {
  if (!g_rom) return 0;
  for (int o = (int)g_game->seq_off; g_rom[o] != 0xFF; o += 4)
    if (g_rom[o] == mode && (g_rom[o + 1] == 0xFF || g_rom[o + 1] == scene)) { *nm = g_rom[o + 2]; *ns = g_rom[o + 3]; return 1; }
  return 0;
}

void extras_frame(int assisted) {
  if (!s) return;
  uint8_t* r = s->ram;
  int mode = game_mode(r), scene = game_scene(r);
  if (mode != g_prev_mode || scene != g_prev_scene) {
    int nm, ns;
    // stage completed: the game moved on to the stage's successor
    if (g_in_stage && successor(g_prev_mode, g_prev_scene, &nm, &ns) && nm == mode && ns == scene) {
      int key = g_prev_mode << 8 | g_prev_scene;
      g_split_time = g_stage_frames;
      g_split_diff = g_best[key] ? (int32_t)g_stage_frames - (int32_t)g_best[key] : 0x7fffffff;
      if (!assisted && (!g_best[key] || g_stage_frames < g_best[key])) g_best[key] = g_stage_frames;
      g_split_show = 240;
      g_stages_cleared++;
      g_dirty = 1;
    }
    // new game: the first stage entered from the title side
    if (mode == g_game->start_mode && scene == g_game->start_scene && !gameplay(g_prev_mode)) {
      g_run_on = 1; g_run_frames = 0; g_runs_started++; g_dirty = 1;
    }
    // the credits: run complete
    if (mode == g_game->end_mode && scene == g_game->end_scene && g_run_on) {
      g_run_on = 0;
      if (!assisted && (!g_best_run || g_run_frames < g_best_run)) g_best_run = g_run_frames;
      g_runs_done++; g_dirty = 1;
    }
    g_in_stage = gameplay(mode);
    if (g_in_stage) g_stage_frames = 0;
    g_prev_mode = mode; g_prev_scene = scene;
    memset(g_prev_hp, 0, sizeof g_prev_hp);
  }
  if (g_run_on) g_run_frames++;
  if (g_in_stage) {
    g_stage_frames++;
    g_play_frames++;
    int lives = r[g_game->r_lives];
    if (g_prev_lives >= 0 && lives < g_prev_lives && lives != 0xFF) { g_deaths++; g_dirty = 1; }
    g_prev_lives = lives;
    for (int n = 1; n < 20 && g_game->r_objtype; n++) {
      int ta = g_game->r_objtype + 2 * n, ha = g_game->r_objhp + 2 * n;
      uint16_t t = r[ta] | r[ta + 1] << 8, h = r[ha] | r[ha + 1] << 8;
      if (g_prev_type[n] == t && g_prev_hp[n] > 0 && g_prev_hp[n] < 0x8000 && h == 0) g_kills++;
      g_prev_type[n] = t; g_prev_hp[n] = h;
    }
  } else g_prev_lives = -1;
  if (g_split_show > 0) g_split_show--;
  if (g_dirty && (g_play_frames % 600 == 0 || !g_in_stage)) save();
}

void extras_shutdown(void) { save(); }

// m:ss.cc from frames (NTSC 60.0988 fps)
static void fmt_time(char* out, size_t n, uint32_t frames, int sign) {
  double t = frames / 60.0988;
  int cs = (int)(t * 100 + 0.5);
  int m = cs / 6000, sec = (cs / 100) % 60, c = cs % 100;
  if (m >= 60) snprintf(out, n, "%s%d:%02d:%02d.%02d", sign < 0 ? "-" : sign > 0 ? "+" : "", m / 60, m % 60, sec, c);
  else snprintf(out, n, "%s%d:%02d.%02d", sign < 0 ? "-" : sign > 0 ? "+" : "", m, sec, c);
}

static void text(uint32_t* buf, int w, int h, int x, int y, const char* str, uint32_t col) {
  for (; *str; str++, x += 8) {
    int c = (unsigned char)*str;
    if (c < 32 || c > 126) continue;
    const unsigned char* g = g_font8x16[c - 32];
    for (int ry = 0; ry < 16; ry++) for (int b = 0; b < 8; b++) {
      int px = x + b, py = y + ry;
      if (px < 0 || px >= w || py < 0 || py >= h) continue;
      if (g[ry] & (0x80 >> b)) buf[py * w + px] = 0xff000000u | col;
      else if ((ry > 0 && (g[ry - 1] & (0x80 >> b))) || (b > 0 && (g[ry] & (0x80 >> (b - 1)))))
        if (!(buf[py * w + px] >> 24)) buf[py * w + px] = 0xc0000000u;   // drop shadow
    }
  }
}

// Draws the timer into a w x h ARGB overlay (transparent background). Returns 1 if anything is shown.
int extras_draw_timer(uint32_t* buf, int w, int h) {
  memset(buf, 0, (size_t)w * h * 4);
  if (!g_run_on && !g_in_stage && !g_split_show) return 0;
  char t[48], u[48];
  fmt_time(t, sizeof t, g_run_frames, 0);
  int len = (int)strlen(t);
  text(buf, w, h, w - 4 - len * 8, 2, t, g_run_on ? 0xffffff : 0x9aa4b0);
  if (g_split_show) {
    if (g_split_diff == 0x7fffffff) { fmt_time(u, sizeof u, g_split_time, 0); strcat(u, " new"); }
    else fmt_time(u, sizeof u, (uint32_t)abs(g_split_diff), g_split_diff < 0 ? -1 : g_split_diff > 0 ? 1 : 0);
    uint32_t col = g_split_diff == 0x7fffffff ? 0xffe060 : g_split_diff <= 0 ? 0x60e070 : 0xf06060;
    text(buf, w, h, w - 4 - (int)strlen(u) * 8, 20, u, col);
  } else if (g_in_stage) {
    fmt_time(u, sizeof u, g_stage_frames, 0);
    text(buf, w, h, w - 4 - (int)strlen(u) * 8, 20, u, 0xb8c4d0);
  }
  return 1;
}

int extras_stat_lines(char lines[][48], int max) {
  int n = 0;
  char t[32];
  uint64_t sec = (uint64_t)(g_play_frames / 60.0988);
  if (n < max) snprintf(lines[n++], 48, "Play time  %llu:%02llu:%02llu", (unsigned long long)(sec / 3600), (unsigned long long)(sec / 60 % 60), (unsigned long long)(sec % 60));
  if (n < max) snprintf(lines[n++], 48, "Games started  %u", g_runs_started);
  if (n < max) snprintf(lines[n++], 48, "Games finished %u", g_runs_done);
  if (g_best_run) { fmt_time(t, sizeof t, g_best_run, 0); if (n < max) snprintf(lines[n++], 48, "Best full run  %s", t); }
  if (n < max) snprintf(lines[n++], 48, "Stages cleared %u", g_stages_cleared);
  if (n < max) snprintf(lines[n++], 48, "Enemies defeated %u", g_kills);
  if (n < max) snprintf(lines[n++], 48, "Lives lost     %u", g_deaths);
  return n;
}

uint32_t extras_best_stage(int mode, int scene) { return g_best[(mode & 255) << 8 | (scene & 255)]; }
void extras_format_time(char* out, int n, uint32_t frames) { fmt_time(out, n, frames, 0); }

// texts for the HUD timer: run time, and either the current stage time or the last split
int extras_timer_text(char* run, int rn, char* st, int sn, uint32_t* run_col, uint32_t* st_col, int* split) {
  if (!g_run_on && !g_in_stage && !g_split_show) return 0;
  fmt_time(run, rn, g_run_frames, 0);
  *run_col = g_run_on ? 0xffffff : 0x9aa4b0;
  *split = g_split_show > 0;
  if (g_split_show) {
    if (g_split_diff == 0x7fffffff) { char t[24]; fmt_time(t, sizeof t, g_split_time, 0); snprintf(st, sn, "%s NEW", t); *st_col = 0xffe060; }
    else { fmt_time(st, sn, (uint32_t)abs(g_split_diff), g_split_diff < 0 ? -1 : g_split_diff > 0 ? 1 : 0); *st_col = g_split_diff <= 0 ? 0x60e070 : 0xf06060; }
  } else if (g_in_stage) { fmt_time(st, sn, g_stage_frames, 0); *st_col = 0xb8c4d0; }
  else st[0] = 0;
  return 1;
}
