// Super Star Wars native port - SDL2 frontend.
//
// Usage: ssw [path/to/Super Star Wars (USA) (Rev 1).sfc]
// With no argument it looks for ssw.sfc next to the executable / in the working directory.
//
// Keyboard: arrows = d-pad, Z = B (jump), X = A, A = Y (shoot), S = X, Q/W = L/R,
//           Enter = Start, Right Shift = Select
//           Esc = menu (settings, level select, save/load, quit)
//           F5 = save state, F7 = load state, F11/Alt+Enter = fullscreen, Tab (hold) = fast forward
//           F1 = developer features, F2 = level select (debug menu), F3 = stage clear,
//           F4 = lightsaber start, F6 = hero select, F12 = recompiled/interpreted CPU
// Gamepad:  SNES-style layout (bottom = B, right = A, left = Y, top = X); Guide or Back+Start = menu
// Settings are saved to ssw.ini next to the executable.
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "snes.h"
#include "runner.h"
#include "font8x16.h"
#include "widescreen.h"
#include "msu.h"
#include "achievements.h"
#include "restore.h"
#include "steam.h"
#include "extras.h"
#include "crashlog.h"
#include "game.h"
#include "rom_check.h"
#include "recomp_rt.h"
#include "cart.h"
#include "ppu.h"

#define SAMPLE_RATE 48000

static Snes* g_snes;
static uint8_t g_pixels[512 * 480 * 4];
static int16_t g_audio[SAMPLE_RATE / 50 * 2];
static uint8_t* g_state;
static int g_state_len;
static SDL_Window* g_win;
static char g_ini_path[1024], g_state_path[1024], g_cont_path[1024];

// ---------------------------------------------------------------- settings
enum { ASPECT_4_3, ASPECT_8_7, ASPECT_STRETCH, ASPECT_COUNT };
static const char* k_aspect_names[] = {"4:3 (TV)", "8:7 (square pixels)", "Stretch"};
enum { WIDE_OFF, WIDE_16_10, WIDE_16_9, WIDE_21_9, WIDE_COUNT };
static const char* k_wide_names[] = {"Off", "16:10", "16:9", "21:9"};
static struct {
  int aspect, integer_scale, fullscreen, volume, smooth;
  int dev, saber, hero;
  int wide, unlocked;
  int no_slowdown;
  int msu;
  int m7;    // 0 original, 1 = 2x, 2 = 4x
  int restored;   // restored cut stage (scene $13)
  int rumble;     // gamepad rumble when Luke gets hit
  int crt;        // 0 off, 1 scanlines, 2 full CRT (scanlines + shadow mask + vignette)
  int nolimit;    // no sprite flicker (draw every sprite on a line)
  int interp;     // sound: 0 original (gaussian), 1 crisp (cubic)
  int color;      // 0 original, 1 vivid, 2 soft
  int music_vol;  // orchestral music volume (percent of the main volume)
  int timer;      // speedrun timer overlay
  int rewind;     // hold Backspace / left trigger to rewind
  int trilogy;    // after a game's ending, go on to the next game of the trilogy
  int last_game;  // unlock-style bit of the game played last (1 = Star Wars, 2 = Empire)
  int wide_spawn; // widescreen enemies: 0 = shown and active in the wide view (spawning as the original),
                  // 1 = also spawn at the wide view's edge, 2 = like the original (only the 4:3 view counts)
  int pause_menu; // Start during a stage opens the port's pause menu (0 = the game's own plain pause)
} cfg = {ASPECT_4_3, 0, 0, 80, 0, 0, 0, 0, WIDE_OFF, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 100, 0, 0, 1, 0, 0, 1};
static const char* k_m7_names[] = {"Original", "High (2x)", "Ultra (4x)"};
static int g_msu_songs;    // songs mapped in msu/msu.txt (0 = no pack installed)

static SDL_Keycode g_keymap[12];   // remappable bindings, see bindings_default()
static int g_padmap[12];
static void bindings_default(void);
static void cfg_load(void) {
  bindings_default();
  FILE* f = fopen(g_ini_path, "r");
  if (!f) return;
  char k[64]; int v;
  while (fscanf(f, " %63[^=]=%d", k, &v) == 2) {
    if (!strcmp(k, "aspect")) cfg.aspect = v % ASPECT_COUNT;
    else if (!strcmp(k, "integer_scale")) cfg.integer_scale = !!v;
    else if (!strcmp(k, "fullscreen")) cfg.fullscreen = !!v;
    else if (!strcmp(k, "volume")) cfg.volume = v < 0 ? 0 : v > 100 ? 100 : v;
    else if (!strcmp(k, "smooth")) cfg.smooth = !!v;
    else if (!strcmp(k, "lightsaber_start")) cfg.saber = !!v;
    else if (!strcmp(k, "hero_select")) cfg.hero = !!v;
    else if (!strcmp(k, "widescreen")) cfg.wide = v % WIDE_COUNT;
    else if (!strcmp(k, "extras_unlocked")) cfg.unlocked = v;   // bit mask: 1 = Star Wars, 2 = Empire
    else if (!strcmp(k, "remove_slowdown")) cfg.no_slowdown = !!v;
    else if (!strcmp(k, "orchestral_music")) cfg.msu = !!v;
    else if (!strcmp(k, "mode7_detail")) cfg.m7 = v < 0 ? 0 : v > 2 ? 2 : v;
    else if (!strcmp(k, "restored_stage")) cfg.restored = !!v;
    else if (!strcmp(k, "rumble")) cfg.rumble = !!v;
    else if (!strcmp(k, "crt")) cfg.crt = v < 0 ? 0 : v > 2 ? 2 : v;
    else if (!strcmp(k, "no_sprite_flicker")) cfg.nolimit = !!v;
    else if (!strcmp(k, "sound_interpolation")) cfg.interp = !!v;
    else if (!strcmp(k, "colour")) cfg.color = v < 0 ? 0 : v > 2 ? 2 : v;
    else if (!strcmp(k, "speedrun_timer")) cfg.timer = !!v;
    else if (!strcmp(k, "rewind")) cfg.rewind = !!v;
    else if (!strcmp(k, "trilogy_continue")) cfg.trilogy = !!v;
    else if (!strcmp(k, "last_game")) cfg.last_game = v;
    else if (!strcmp(k, "widescreen_spawning")) cfg.wide_spawn = v < 0 || v > 2 ? 0 : v;
    else if (!strcmp(k, "pause_menu")) cfg.pause_menu = !!v;
    else if (!strcmp(k, "music_volume")) cfg.music_vol = v < 0 ? 0 : v > 100 ? 100 : v;
    else if (!strncmp(k, "key_", 4) && atoi(k + 4) >= 0 && atoi(k + 4) < 12) g_keymap[atoi(k + 4)] = v;
    else if (!strncmp(k, "pad_", 4) && atoi(k + 4) >= 0 && atoi(k + 4) < 12) g_padmap[atoi(k + 4)] = v;
  }
  fclose(f);
}
static void cfg_save(void) {
  FILE* f = fopen(g_ini_path, "w");
  if (!f) return;
  fprintf(f, "aspect=%d\ninteger_scale=%d\nfullscreen=%d\nvolume=%d\nsmooth=%d\nlightsaber_start=%d\nhero_select=%d\nwidescreen=%d\nextras_unlocked=%d\n",
          cfg.aspect, cfg.integer_scale, cfg.fullscreen, cfg.volume, cfg.smooth, cfg.saber, cfg.hero, cfg.wide, cfg.unlocked);
  fprintf(f, "remove_slowdown=%d\norchestral_music=%d\nmode7_detail=%d\nrestored_stage=%d\nrumble=%d\ncrt=%d\nno_sprite_flicker=%d\nsound_interpolation=%d\ncolour=%d\nmusic_volume=%d\nspeedrun_timer=%d\nrewind=%d\n", cfg.no_slowdown, cfg.msu, cfg.m7, cfg.restored, cfg.rumble, cfg.crt, cfg.nolimit, cfg.interp, cfg.color, cfg.music_vol, cfg.timer, cfg.rewind);
  fprintf(f, "trilogy_continue=%d\nlast_game=%d\nwidescreen_spawning=%d\npause_menu=%d\n", cfg.trilogy, cfg.last_game, cfg.wide_spawn, cfg.pause_menu);
  for (int i = 0; i < 12; i++) fprintf(f, "key_%d=%d\npad_%d=%d\n", i, (int)g_keymap[i], i, g_padmap[i]);
  fclose(f);
}

// ---------------------------------------------------------------- input
// Button state with press latching: a press and release inside the same frame (a very quick tap)
// still reaches the game as a one-frame press.
static uint16_t g_btn_down, g_btn_seen, g_btn_release;
static void set_btn(int b, bool down) {
  if (down) { g_btn_down |= 1 << b; g_btn_seen &= ~(1 << b); g_btn_release &= ~(1 << b); }
  else if (g_btn_down & (1 << b)) {
    if (g_btn_seen & (1 << b)) g_btn_down &= ~(1 << b);
    else g_btn_release |= 1 << b;            // not seen by the game yet: release after next frame
  }
}
static void push_buttons(void) {
  uint16_t b = g_btn_down;
  for (int i = 0; i < 12; i++) snes_setButtonState(g_snes, 1, i, (b >> i) & 1);
  g_btn_seen = g_btn_down;
}
static void after_frame_buttons(void) {
  g_btn_down &= ~g_btn_release;
  g_btn_release = 0;
}
// Remappable bindings: one keyboard key and one gamepad button per SNES button (LakeSnes order:
// B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R). Saved in ssw.ini as key_N / pad_N.
static const char* k_btn_names[12] = {"B (jump)", "Y (shoot)", "Select", "Start", "Up", "Down", "Left", "Right",
                                      "A", "X", "L", "R"};
static const SDL_Keycode k_key_default[12] = {SDLK_z, SDLK_a, SDLK_RSHIFT, SDLK_RETURN, SDLK_UP, SDLK_DOWN, SDLK_LEFT,
                                              SDLK_RIGHT, SDLK_x, SDLK_s, SDLK_q, SDLK_w};
static const int k_pad_default[12] = {SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_BACK,
  SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
  SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_B,
  SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
static void bindings_default(void) {
  memcpy(g_keymap, k_key_default, sizeof g_keymap);
  memcpy(g_padmap, k_pad_default, sizeof g_padmap);
}
static int key_to_btn(SDL_Keycode k) {
  for (int i = 0; i < 12; i++) if (g_keymap[i] == k) return i;
  return -1;
}
static int pad_to_btn(int b) {
  for (int i = 0; i < 12; i++) if (g_padmap[i] == b) return i;
  return -1;
}

// ---------------------------------------------------------------- developer features
// All implemented by the original game; the port only flips the game's own flags.
static int g_pad2_hold;
static int g_dev_pulse;       // frames left of the developer flag set for a single Level select / Clear stage
static bool g_debug_menu;     // the game's level-select menu was opened: the next Start goes to the game
static bool g_port_touched;   // the port set cheat/debug flags itself this session
static uint16_t g_pad2_mask;
static void pad2_press(uint16_t mask) { g_pad2_mask = mask; g_pad2_hold = 4; }
static bool in_stage(void) { return game_mode(g_snes->ram) == 0x0004; }
static int game_index(void) { for (int i = 0; g_all_games[i]; i++) if (g_all_games[i] == g_game) return i; return 0; }
static int g_game_bit(void) {   // unlock bit of the running game in cfg.unlocked (by key, so standalone builds agree)
  return !strcmp(g_game->key, "ssw") ? 1 : !strcmp(g_game->key, "esb") ? 2 : 4;
}
#define UNLOCKED (cfg.unlocked & g_game_bit())
#define HAS(f) (g_game->features & (f))
// Extras (the two cheat toggles, level select) unlock per game by beating it -- reaching its ending
// (end_mode/end_scene in game.c) -- or by entering one of the original cheat codes.
static void check_unlock(void) {
  uint8_t* ram = g_snes->ram;
  const GameDef* g = g_game;
  int mode = game_mode(ram), scene = game_scene(ram);
  bool beat = mode == g->end_mode && scene == g->end_scene;
  // a cheat code's own effects showing up while none of the port's toggles are on
  if (cfg.dev || cfg.saber || cfg.hero || g_pad2_hold) g_port_touched = true;
  bool title = mode == g->title_mode && scene == g->title_scene;
  bool code = !g_port_touched &&
              ((ram[g->r_cheats] & g->cheat_any) || ram[g->r_debug] || ram[g->r_debug + 2] ||
               (g->code_lives && title && ram[g->r_lives] == g->code_lives));
  if (!UNLOCKED && (beat || code)) { cfg.unlocked |= g_game_bit(); cfg_save(); crashlog_event("extras unlocked (%s)", beat ? "beaten" : "code"); }
}
static void apply_dev_flags(void) {
  uint8_t* ram = g_snes->ram;
  check_unlock();
  const GameDef* g = g_game;
  if (!UNLOCKED) { cfg.saber = 0; cfg.hero = 0; }
  // the game's developer flag: on while Developer features is on, and for a few frames around the
  // menu's Level select / Clear stage (the game only checks it when the controller-2 press arrives)
  static bool flag_ours;
  if (g_dev_pulse > 0) g_dev_pulse--;
  if (cfg.dev || g_dev_pulse) { ram[g->r_debug] = 1; ram[g->r_debug + 1] = 0; flag_ours = true; }
  else if (flag_ours) { ram[g->r_debug] = 0; ram[g->r_debug + 1] = 0; flag_ours = false; }
  if (g_debug_menu && !in_stage()) g_debug_menu = false;   // the menu only opens during a stage
  static int last_saber = -1, last_hero = -1;
  static const GameDef* last_game;
  if (last_game != g) { last_saber = last_hero = -1; last_game = g; }
  uint8_t* c = &ram[g->r_cheats];
  if (cfg.saber != last_saber) { if (cfg.saber) *c |= g->cheat_a; else if (last_saber == 1) *c &= ~g->cheat_a; last_saber = cfg.saber; }
  if (cfg.hero != last_hero) { if (cfg.hero) *c |= g->cheat_b; else if (last_hero == 1) *c &= ~g->cheat_b; last_hero = cfg.hero; }
  if (cfg.saber) *c |= g->cheat_a;
  if (cfg.hero) *c |= g->cheat_b;
  for (int i = 0; i < 12; i++) snes_setButtonState(g_snes, 2, i, g_pad2_hold > 0 && ((g_pad2_mask >> i) & 1));
  if (g_pad2_hold > 0) g_pad2_hold--;
}
// Start during a stage opens the pause menu (the original only freezes the game, nothing on screen).
// Not the first Start after the menu's Level select: that one picks the stage in the game's menu.
static bool start_opens_pause(int b, bool down) {
  if (b != BTN_START || !down) return false;
  if (g_debug_menu) { g_debug_menu = false; return false; }
  return cfg.pause_menu && game_is_play_mode(game_mode(g_snes->ram));
}
static void level_select(void) { g_dev_pulse = 8; g_debug_menu = true; pad2_press((1 << BTN_L) | (1 << BTN_R)); }
static void stage_clear(void) { g_dev_pulse = 8; pad2_press(1 << BTN_START); }

static void update_title(void) {
  char t[200];
#ifdef SSW_LAUNCHER
  snprintf(t, sizeof t, "%s%s", g_game->name, cfg.dev ? " [developer features]" : "");
#else
  snprintf(t, sizeof t, "%s%s%s", g_game->name, g_use_recomp ? "" : " [interpreter]", cfg.dev ? " [developer features]" : "");
#endif
  SDL_SetWindowTitle(g_win, t);
}
static void apply_fullscreen(void) { SDL_SetWindowFullscreen(g_win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }

static void save_state_file(void) {
  g_state_len = snes_saveState(g_snes, g_state);
  FILE* f = fopen(g_state_path, "wb");
  if (f) { fwrite(g_state, 1, g_state_len, f); fclose(f); }
}
static void load_state_file(void) {
  crashlog_event("load state");
  FILE* f = fopen(g_state_path, "rb");
  if (f) { g_state_len = (int)fread(g_state, 1, 4 * 1024 * 1024, f); fclose(f); }
  if (g_state_len) snes_loadState(g_snes, g_state, g_state_len);
}

// ---------------------------------------------------------------- stages, auto-save / continue
static int stage_number(void) {   // 1-based position in the game's stage list (game.c), 0 = not in a stage
  int m = game_mode(g_snes->ram), sc = game_scene(g_snes->ram);
  for (int i = 0; i < g_game->nstages; i++) if (g_game->stages[i][0] == m && g_game->stages[i][1] == sc) return i + 1;
  return 0;
}
static int g_cont_stage;           // stage number stored in the continue file (0 = none)
static void cont_probe(void) {
  FILE* f = fopen(g_cont_path, "rb");
  g_cont_stage = 0;
  if (f) { int c = fgetc(f); g_cont_stage = c == EOF ? 0 : c; fclose(f); }
}
static void autosave_tick(void) {
  // save once, two seconds into each stage (after the intro card), so Continue restarts it cleanly
  static int last = -1, timer = 0;
  int st = stage_number();
  if (st != last) { last = st; timer = st ? 120 : 0; }
  if (timer > 0 && --timer == 0) {
    int n = snes_saveState(g_snes, g_state);
    FILE* f = fopen(g_cont_path, "wb");
    if (f) { fputc(st, f); fwrite(g_state, 1, n, f); fclose(f); g_cont_stage = st; }
  }
}
// quick restart: the continue file is written 2 s into every stage, so it is the current stage's start
static int restart_available(void) { int st = stage_number(); return st && st == g_cont_stage; }
static void cont_load(void) {
  crashlog_event("continue / restart stage");
  FILE* f = fopen(g_cont_path, "rb");
  if (!f) return;
  fgetc(f);
  int n = (int)fread(g_state, 1, 4 * 1024 * 1024, f);
  fclose(f);
  if (n) snes_loadState(g_snes, g_state, n);
}
// ---------------------------------------------------------------- colour options
// A 32x32x32 lookup on the top 5 bits of each channel (the SNES only has 5 bits per channel).
static uint32_t g_color_lut[32768];
static int g_color_lut_mode = -1;
static void color_apply(uint32_t* buf, int stride, int w, int h) {
  if (!cfg.color) return;
  if (g_color_lut_mode != cfg.color) {
    double sat = cfg.color == 1 ? 1.30 : 0.85, gam = cfg.color == 1 ? 0.95 : 1.0, lift = cfg.color == 2 ? 0.04 : 0.0;
    for (int i = 0; i < 32768; i++) {
      double r = ((i >> 10) & 31) / 31.0, g = ((i >> 5) & 31) / 31.0, b = (i & 31) / 31.0;
      double y = 0.299 * r + 0.587 * g + 0.114 * b;
      r = y + (r - y) * sat; g = y + (g - y) * sat; b = y + (b - y) * sat;
      if (cfg.color == 2) { r = r * 1.02 + lift; g = g + lift; b = b * 0.96 + lift; }   // slightly warm, lifted blacks
      r = r < 0 ? 0 : r > 1 ? 1 : pow(r, gam); g = g < 0 ? 0 : g > 1 ? 1 : pow(g, gam); b = b < 0 ? 0 : b > 1 ? 1 : pow(b, gam);
      g_color_lut[i] = ((int)(r * 255 + 0.5) << 16) | ((int)(g * 255 + 0.5) << 8) | (int)(b * 255 + 0.5);
    }
    g_color_lut_mode = cfg.color;
  }
  for (int y = 0; y < h; y++) {
    uint32_t* p = buf + (size_t)y * stride;
    for (int x = 0; x < w; x++) { uint32_t c = p[x]; p[x] = g_color_lut[((c >> 9) & 0x7c00) | ((c >> 6) & 0x3e0) | ((c >> 3) & 0x1f)]; }
  }
}

// ---------------------------------------------------------------- crash log support
static int g_report_timer;
static int g_test_crash;   // SSW_TEST_CRASH=1: crash on purpose after ~20 s to test the crash handler
static const char* settings_summary(void) {
  static char b[400];
  snprintf(b, sizeof b, "wide=%d m7=%d noslow=%d msu=%d restored=%d crt=%d colour=%d sound=%d nolimit=%d timer=%d rewind=%d "
           "aspect=%d integer=%d smooth=%d full=%d dev=%d saber=%d hero=%d unlocked=%d recomp=%d",
           cfg.wide, cfg.m7, cfg.no_slowdown, cfg.msu, cfg.restored, cfg.crt, cfg.color, cfg.interp, cfg.nolimit, cfg.timer,
           cfg.rewind, cfg.aspect, cfg.integer_scale, cfg.smooth, cfg.fullscreen, cfg.dev, cfg.saber, cfg.hero, cfg.unlocked, g_use_recomp);
  return b;
}

// ---------------------------------------------------------------- CRT filter
// A multiply-blended overlay drawn over the game picture: scanlines aligned to the 224 game lines,
// and for "Full CRT" also an aperture-grille RGB mask and a soft vignette. Rebuilt when the output
// size or mode changes.
static SDL_Texture* g_crt_tex;
static int g_crt_w, g_crt_h, g_crt_mode;
static void crt_overlay(SDL_Renderer* ren, SDL_Rect dst) {
  if (!cfg.crt || dst.w <= 0 || dst.h <= 0) return;
  if (!g_crt_tex || g_crt_w != dst.w || g_crt_h != dst.h || g_crt_mode != cfg.crt) {
    if (g_crt_tex) SDL_DestroyTexture(g_crt_tex);
    g_crt_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, dst.w, dst.h);
    if (!g_crt_tex) return;
    uint32_t* px = malloc((size_t)dst.w * dst.h * 4);
    double lines = 224.0, strength = cfg.crt == 2 ? 0.45 : 0.35;
    for (int y = 0; y < dst.h; y++) {
      double ph = (y + 0.5) * lines / dst.h; ph -= (int)ph;           // position inside a game line
      double d = ph - 0.5;
      double scan = 1.0 - strength * (4.0 * d * d);                    // bright centre, dark gap
      double vy = (y + 0.5) / dst.h - 0.5;
      for (int x = 0; x < dst.w; x++) {
        double r = scan, g = scan, b = scan;
        if (cfg.crt == 2) {
          int k = x % 3;                                               // aperture grille
          r *= k == 0 ? 1.0 : 0.82; g *= k == 1 ? 1.0 : 0.82; b *= k == 2 ? 1.0 : 0.82;
          double vx = (x + 0.5) / dst.w - 0.5;
          double v = 1.0 - 0.55 * (vx * vx + vy * vy) * 1.6;           // vignette
          r *= v; g *= v; b *= v;
        }
        int R = (int)(r * 255), G = (int)(g * 255), B = (int)(b * 255);
        px[y * dst.w + x] = 0xff000000u | (R << 16) | (G << 8) | B;
      }
    }
    SDL_UpdateTexture(g_crt_tex, NULL, px, dst.w * 4);
    free(px);
    SDL_SetTextureBlendMode(g_crt_tex, SDL_BLENDMODE_MOD);
    g_crt_w = dst.w; g_crt_h = dst.h; g_crt_mode = cfg.crt;
  }
  SDL_RenderCopy(ren, g_crt_tex, NULL, &dst);
}

// ---------------------------------------------------------------- rewind
// A ring of save states, one every 4 frames (about 10 seconds). While rewinding, each shown frame
// steps back one snapshot and runs one frame to redraw: roughly 3x real-time backwards.
// Rewinding counts as assistance: achievements and best times are off for the rest of the session.
#define RW_SLOTS 150
#define RW_EVERY 4
static uint8_t* g_rw_buf[RW_SLOTS];
static int g_rw_len[RW_SLOTS], g_rw_head, g_rw_count, g_rw_tick;
static bool g_rewinding, g_rewound;
static void rewind_record(void) {
  if (++g_rw_tick % RW_EVERY) return;
  static uint8_t* tmp;
  if (!tmp) tmp = malloc(4 * 1024 * 1024);
  int n = snes_saveState(g_snes, tmp);
  if (!g_rw_buf[g_rw_head] || g_rw_len[g_rw_head] < n) { free(g_rw_buf[g_rw_head]); g_rw_buf[g_rw_head] = malloc(n); }
  memcpy(g_rw_buf[g_rw_head], tmp, n); g_rw_len[g_rw_head] = n;
  g_rw_head = (g_rw_head + 1) % RW_SLOTS;
  if (g_rw_count < RW_SLOTS) g_rw_count++;
}
static bool rewind_step(void) {   // load the previous snapshot; false when the buffer is empty
  if (g_rw_count <= 1) return false;
  g_rw_head = (g_rw_head + RW_SLOTS - 1) % RW_SLOTS; g_rw_count--;
  int i = (g_rw_head + RW_SLOTS - 1) % RW_SLOTS;
  snes_loadState(g_snes, g_rw_buf[i], g_rw_len[i]);
  if (!g_rewound) crashlog_event("rewind used");
  g_rewound = true;
  return true;
}
static void rewind_clear(void) { g_rw_count = 0; }

// ---------------------------------------------------------------- title screen credit
// Drawn with the title screen's own letter tiles and palette, read from VRAM/CGRAM (BG3: 2bpp
// tiles at $3000, palette 2): A-Z are tiles $19A.., the title writes O with the zero glyph ($1B4),
// '-' is $1C2. It goes on the line under "LICENSED TO NINTENDO" (lines 216-223), which the game
// itself masks out on BG3, so we draw the tiles into the finished picture.
static bool title_credit_active(void) {
  uint8_t* r = g_snes->ram;
  const GameDef* g = g_game;
  if (!(g->features & GF_TITLE_CRED)) return false;
  if (!(game_mode(r) == g->title_mode && game_scene(r) == g->title_scene)) return false;
  return (g_snes->ppu->vram[0x800 + 26 * 32 + 6] & 0x3ff) == g->font_base + 0x25;   // title text in place ("L")
}
static void title_credit(uint32_t* buf, int stride, int x0, int y0, int scale) {
  if (!title_credit_active()) return;
  Ppu* p = g_snes->ppu;
  static const char* text = "PC PORT BY T-MAN";
  int len = (int)strlen(text), cx = (256 - len * 8) / 2;
  for (int i = 0; i < len; i++) {
    char c = text[i];
    if (c == ' ') continue;
    int t = (c == '-' ? 0x1C2 : c == 'O' ? 0x1B4 : 0x19A + (c - 'A')) - 0x180 + g_game->font_base;
    for (int r = 0; r < 8; r++) {
      uint16_t w = p->vram[(p->bgLayer[2].tileAdr + t * 8 + r) & 0x7fff];
      for (int b = 0; b < 8; b++) {
        int ci = ((w >> (7 - b)) & 1) | ((w >> (15 - b)) & 1) << 1;
        if (!ci) continue;
        uint16_t col = p->cgram[2 * 4 + ci];
        int R = col & 31, G = (col >> 5) & 31, B = (col >> 10) & 31, br = p->brightness;
        uint32_t px = ((((R << 3) | (R >> 2)) * br / 15) << 16) | ((((G << 3) | (G >> 2)) * br / 15) << 8) | (((B << 3) | (B >> 2)) * br / 15);
        int sx = cx + i * 8 + b, sy = 216 + r;
        for (int dy = 0; dy < scale; dy++) for (int dx = 0; dx < scale; dx++)
          buf[(y0 + sy * scale + dy) * stride + x0 + sx * scale + dx] = px;
      }
    }
  }
}

// ---------------------------------------------------------------- rumble
static bool g_menu;
static int g_tm_return;            // menu opened from the title screen's PC SETTINGS / ACHIEVEMENTS
static void open_menu(bool open);
static void rumble(uint16_t lo, uint16_t hi, uint32_t ms) {
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    SDL_GameController* gc = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
    if (gc) SDL_GameControllerRumble(gc, lo, hi, ms);
  }
}
static void rumble_tick(void) {
  // player HP and lives going down during gameplay
  static int last_hp = -1, last_lives = -1;
  uint8_t* r = g_snes->ram;
  bool play = game_is_play_mode(game_mode(r));
  int hp = r[g_game->r_hp], lives = r[g_game->r_lives];
  if (play && cfg.rumble && !g_menu) {
    if (last_lives >= 0 && lives < last_lives && lives != 0xFF) rumble(0xC000, 0xFFFF, 500);
    else if (last_hp > 0 && hp < last_hp) rumble(0x5000, 0x9000, 160);
  }
  last_hp = play ? hp : -1; last_lives = play ? lives : -1;
}
static void update_overclock(void) {
  extern int g_snes_overclock;
  // only during gameplay: boot and some loaders rely on real CPU timing
  g_snes_overclock = cfg.no_slowdown && game_is_play_mode(game_mode(g_snes->ram));
}

// ---------------------------------------------------------------- menu overlay (drawn into the 512x480 frame)
static int g_ach_screen;          // achievements list shown inside the menu
static int g_ach_sel;
static int g_toast_id = -1, g_toast_timer;
static int g_sel;
static bool g_quit;
enum { M_RESUME, M_ACH, M_STATS, M_TIMER, M_REWIND, M_CONTROLS, M_CONTINUE, M_RESTART, M_WIDE, M_M7, M_SLOW, M_MUSIC, M_RESTORED, M_RUMBLE, M_ASPECT, M_INTEGER, M_SMOOTH, M_CRT, M_COLOR, M_NOLIMIT, M_FULL, M_VOLUME, M_MUSICVOL, M_INTERP, M_SABER, M_HERO, M_DEV, M_LEVEL, M_CLEAR, M_SAVE, M_LOAD, M_REPORT, M_RESET, M_QUIT, M_GAME, M_TRILOGY, M_SPAWN, M_TITLE, M_PAUSEBTN,
       M_PG_VIDEO, M_PG_AUDIO, M_PG_GAME, M_PG_EXTRAS, M_PG_OPTIONS, M_COUNT };
// menu pages: lists of item ids, -1 terminated. Built per game (build_pages): items for features a game
// doesn't have yet are left out.
// page 0: the pause menu (Start in a stage, Esc, Guide); page 5: Options, holding the settings pages 1-4
static const int k_pg_main_all[] = {M_RESUME, M_RESTART, M_CONTINUE, M_PG_OPTIONS, M_ACH, M_STATS,
                                    M_SAVE, M_LOAD, M_TITLE, M_QUIT, -1};
static const int k_pg_options_all[] = {M_GAME, M_PG_VIDEO, M_PG_AUDIO, M_PG_GAME, M_CONTROLS, M_PG_EXTRAS, M_REPORT, -1};
static const int k_pg_video_all[] = {M_WIDE, M_SPAWN, M_M7, M_ASPECT, M_INTEGER, M_SMOOTH, M_CRT, M_COLOR, M_NOLIMIT, M_FULL, -1};
static const int k_pg_audio_all[] = {M_VOLUME, M_MUSIC, M_MUSICVOL, M_INTERP, -1};
static const int k_pg_game_all[] = {M_SLOW, M_TIMER, M_REWIND, M_RUMBLE, M_PAUSEBTN, M_RESTORED, M_TRILOGY, -1};
static const int k_pg_extras_all[] = {M_SABER, M_HERO, M_LEVEL, M_CLEAR, M_DEV, -1};
#define NPAGES 6
static const int* const k_pages_all[NPAGES] = {k_pg_main_all, k_pg_video_all, k_pg_audio_all, k_pg_game_all, k_pg_extras_all, k_pg_options_all};
static int k_pages_buf[NPAGES][24];
static const int* k_pages[NPAGES] = {k_pages_buf[0], k_pages_buf[1], k_pages_buf[2], k_pages_buf[3], k_pages_buf[4], k_pages_buf[5]};
static const int* const k_pg_main = k_pages_buf[0];
static const char* k_page_titles[NPAGES] = {"SUPER STAR WARS", "VIDEO", "AUDIO", "GAMEPLAY", "EXTRAS", "OPTIONS"};
static int g_games_avail;          // how many of the trilogy's ROMs were found
static const GameDef* g_switch_to; // set by the menu / the ending: switch games at the top of the next frame
static const GameDef* next_game(int dir);
static bool item_present(int id) {
  unsigned f = g_game->features;
  switch (id) {
    case M_WIDE: case M_SPAWN: return f & GF_WIDESCREEN;
    case M_MUSIC: case M_MUSICVOL: return f & GF_MSU;
    case M_RESTORED: return f & GF_RESTORED;
    case M_ACH: return f & GF_ACH;
    case M_GAME: case M_TRILOGY: return g_games_avail > 1;
  }
  return true;
}
static void build_pages(void) {
  for (int p = 0; p < NPAGES; p++) {
    int n = 0;
    for (int i = 0; k_pages_all[p][i] >= 0; i++) if (item_present(k_pages_all[p][i])) k_pages_buf[p][n++] = k_pages_all[p][i];
    k_pages_buf[p][n] = -1;
  }
  static char title[48];
  snprintf(title, sizeof title, "SUPER %s", g_game->menu_name);
  k_page_titles[0] = title;
}
static int g_page;
static int page_len(int pg) { int n = 0; while (k_pages[pg][n] >= 0) n++; return n; }

static void put_text(int x, int y, const char* s, uint32_t col) {
  for (; *s; s++, x += 16) {
    int c = (unsigned char)*s;
    if (c < 32 || c > 126) continue;
    const unsigned char* g = g_font8x16[c - 32];
    for (int r = 0; r < 16; r++) for (int b = 0; b < 8; b++) if (g[r] & (0x80 >> b))
      for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
        int px = x + b * 2 + dx, py = y + r * 2 + dy;
        if (px >= 0 && px < 512 && py >= 0 && py < 480) ((uint32_t*)g_pixels)[py * 512 + px] = col;
      }
  }
}

// ---------------------------------------------------------------- menu look
// Drawn in SNES pixels (256x224, each one a 2x2 block of the 512x480 frame) so the menus sit in
// the game's own resolution: an 8x8 bold pixel font (public-domain font8x8, emboldened), blue
// bevelled panels like the game's title box, gold gradient headings and a lightsaber cursor.
#include "../third_party/font8x8_basic.h"
static unsigned g_ui_tick;
// drawing target: the menu draws into the 512x480 frame (2x2 per pixel); the HUD overlay (toasts,
// timer) draws into its own transparent canvas at 1x
static struct { uint32_t* buf; int w, h, stride, scale, yoff, alpha; } g_cv;
static uint32_t* ui_buf(void) { return (uint32_t*)g_pixels; }
static void ui_canvas_menu(void) { g_cv.buf = (uint32_t*)g_pixels; g_cv.w = 256; g_cv.h = 224; g_cv.stride = 512; g_cv.scale = 2; g_cv.yoff = 16; g_cv.alpha = 0; }
static void ui_px(int x, int y, uint32_t c) {
  if (x < 0 || x >= g_cv.w || y < 0 || y >= g_cv.h) return;
  if (g_cv.scale == 1) { g_cv.buf[y * g_cv.stride + x] = g_cv.alpha ? 0xff000000u | c : c; return; }
  uint32_t* b = g_cv.buf + (2 * y + g_cv.yoff) * g_cv.stride + 2 * x;
  b[0] = b[1] = b[g_cv.stride] = b[g_cv.stride + 1] = c;
}
static uint32_t ui_mix(uint32_t a, uint32_t b, int t, int n) {   // a..b, t of n
  int r = (((a >> 16) & 255) * (n - t) + ((b >> 16) & 255) * t) / n;
  int g = (((a >> 8) & 255) * (n - t) + ((b >> 8) & 255) * t) / n;
  int bl = ((a & 255) * (n - t) + (b & 255) * t) / n;
  return (r << 16) | (g << 8) | bl;
}
static void ui_blend(int x, int y, uint32_t c, int a) {           // alpha 0..256
  if (x < 0 || x >= g_cv.w || y < 0 || y >= g_cv.h) return;
  if (g_cv.scale == 1) {
    uint32_t* b = g_cv.buf + y * g_cv.stride + x;
    int ea = *b >> 24, na = ea + (255 - ea) * a / 256;
    uint32_t m = ea ? ui_mix(*b & 0xffffff, c, a, 256) : c;
    *b = g_cv.alpha ? ((uint32_t)na << 24) | m : m;
    return;
  }
  uint32_t* b = g_cv.buf + (2 * y + g_cv.yoff) * g_cv.stride + 2 * x;
  uint32_t m = ui_mix(b[0], c, a, 256);
  b[0] = b[1] = b[g_cv.stride] = b[g_cv.stride + 1] = m;
}
static int ui_glyph_row(int ch, int row) {   // emboldened 8x8 row, bit 7 = leftmost
  if (ch < 32 || ch > 126) return 0;
  unsigned v = (unsigned char)font8x8_basic[ch][row], r = 0;
  for (int b = 0; b < 8; b++) if (v & (1 << b)) r |= 0x80 >> b;   // font is LSB-left
  return (r | (r >> 1)) & 0xff;
}
static const char* ui_clean(const char* in);
static int ui_text_w(const char* s, int scale) { return (int)strlen(ui_clean(s)) * 8 * scale; }
// The game's own font: the title screen's 2bpp letter tiles (VRAM $3000, tiles $180-$1DF) and its
// palette 2, captured from the player's ROM (see font_capture) and cached in ssw_font.bin. Menu text
// is drawn with these glyphs; colour variants recolour the two lit shades, keeping the game's
// dark outline. Characters the game has no glyph for fall back to the 8x8 font.
static uint8_t g_gfont[96 * 16];     // tiles $180..$1DF, 16 bytes each (2bpp)
static uint16_t g_gpal[4];
static bool g_gfont_ok;
static int gfont_tile(int c) {
  if (c >= 'a' && c <= 'z') c -= 32;
  if (c >= 'A' && c <= 'Z') return c == 'O' ? 0x1B4 : 0x19A + (c - 'A');
  if (c >= '0' && c <= '9') return 0x1B4 + (c - '0');
  switch (c) { case ':': return 0x1BE; case '!': return 0x1C0; case '.': return 0x1C1; case '-': return 0x1C2;
               case '\'': return 0x1C3; case ',': return 0x1C5; }
  return -1;
}
static uint32_t snes_rgb(uint16_t c) { int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31; return (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2)); }
// colour styles: 0 = the title's gold, 1 = pale blue (values), 2 = grey (disabled), 3 = bright (selected)
static int ui_style_of(uint32_t c) {
  if (c == 0xffffff || c == 0xfff0a0) return 3;
  if ((c & 0xff) > 0x80 && ((c >> 16) & 0xff) < 0xb0 && ((c >> 8) & 0xff) < 0xc0 && (c == 0x6c7890 || c == 0x7f94b8 || c == 0x8090a8 || c == 0x7fa8e0 || c == 0x9aa4b0)) return 2;
  if (c == 0xf0c848 || c == 0xf8d860 || c == 0xffe060) return 1;
  return 0;
}
// rewrite text into characters the game's font has: & -> AND, 1/16 -> 1 OF 16, no brackets or %
static const char* ui_clean(const char* in) {
  static char out[160]; int o = 0;
  for (int i = 0; in[i] && o < 150; i++) {
    char c = in[i];
    if (c == '&') { memcpy(out + o, "AND", 3); o += 3; }
    else if (c == '/' && i > 0 && in[i - 1] >= '0' && in[i - 1] <= '9') { memcpy(out + o, " OF ", 4); o += 4; }
    else if (c == '/') out[o++] = '-';
    else if (c == '(' || c == ')' || c == '%' || c == '{' || c == '}') continue;
    else out[o++] = c;
  }
  out[o] = 0;
  return out;
}
static void ui_text_ex(int x, int y, const char* s, uint32_t top, uint32_t bot, int scale) {
  int style = ui_style_of(top);
  if (top == 0xfff2a0) style = 0;   // headings
  uint32_t shade[4];
  shade[1] = 0x000040;                       // the title text's own colours (palette 2): outline,
  uint32_t mid = 0xf88000, hi = 0xf8e0a0;    // orange body, pale highlight
  switch (style) {
    case 1: mid = 0x4888f8; hi = 0xc8e8ff; break;
    case 2: mid = 0x586078; hi = 0x9aa4b8; break;
    case 3: mid = 0xf8c040; hi = 0xffffff; break;
  }
  shade[2] = mid; shade[3] = hi;
  s = ui_clean(s);
  int cx = x;
  for (const char* p = s; *p; p++, cx += 8 * scale) {
    int t = g_gfont_ok ? gfont_tile((unsigned char)*p) : -1;
    if (*p == ' ') continue;
    if (t >= 0) {
      const uint8_t* g = g_gfont + (t - 0x180) * 16;
      for (int r = 0; r < 8; r++) for (int b = 0; b < 8; b++) {
        int ci = ((g[2 * r] >> (7 - b)) & 1) | ((g[2 * r + 1] >> (7 - b)) & 1) << 1;
        if (!ci) continue;
        for (int sy = 0; sy < scale; sy++) for (int sx = 0; sx < scale; sx++) ui_px(cx + b * scale + sx, y + r * scale + sy, shade[ci]);
      }
    } else {   // no game glyph: 8x8 fallback with the same shades (outline, body, highlight)
      for (int pass = 0; pass < 2; pass++) for (int r = 0; r < 8; r++) {
        int bits = ui_glyph_row((unsigned char)*p, r);
        for (int b = 0; b < 8; b++) if (bits & (0x80 >> b))
          for (int sy = 0; sy < scale; sy++) for (int sx = 0; sx < scale; sx++) {
            int px = cx + b * scale + sx, py = y + r * scale + sy;
            if (pass == 0) ui_px(px + 1, py + 1, shade[1]);
            else ui_px(px, py, r < 3 ? shade[3] : shade[2]);
          }
      }
    }
  }
  (void)bot;
}
static void font_from_vram(Snes* sn) {   // when the title screen's text is in VRAM
  for (int i = 0; i < 96 * 8; i++) { uint16_t w = sn->ppu->vram[(g_game->font_vram + g_game->font_base * 8 + i) & 0x7fff]; g_gfont[2 * i] = w & 0xff; g_gfont[2 * i + 1] = w >> 8; }
  for (int i = 0; i < 4; i++) g_gpal[i] = sn->ppu->cgram[8 + i];
  g_gfont_ok = true;
}
static bool title_text_ready(Snes* sn) {
  const GameDef* g = g_game;
  return game_mode(sn->ram) == g->title_mode && game_scene(sn->ram) == g->title_scene &&
         (sn->ppu->vram[0x800 + 26 * 32 + 6] & 0x3ff) == g->font_base + 0x25;
}
// load the cached font, or boot a hidden copy of the game to its title screen and take it from there
static void font_capture(const uint8_t* rom, int len, const char* dir) {
  char path[1200]; snprintf(path, sizeof path, "%sssw_font.bin", dir);
  FILE* f = fopen(path, "rb");
  if (f) {
    bool ok = fread(g_gfont, 1, sizeof g_gfont, f) == sizeof g_gfont && fread(g_gpal, 2, 4, f) == 4;
    fclose(f);
    if (ok && g_gfont[(0x19A - 0x180) * 16 + 2]) { g_gfont_ok = true; return; }
  }
  Snes* h = snes_init();
  snes_loadRom(h, rom, len);
  for (int i = 0; i < 3000 && !title_text_ready(h); i++) runner_frame(h);
  if (title_text_ready(h)) {
    font_from_vram(h);
    f = fopen(path, "wb");
    if (f) { fwrite(g_gfont, 1, sizeof g_gfont, f); fwrite(g_gpal, 2, 4, f); fclose(f); }
  }
  snes_free(h);
}
static void ui_text(int x, int y, const char* s, uint32_t c) { ui_text_ex(x, y, s, c, ui_mix(c, 0x000000, 1, 4), 1); }
static void ui_text_r(int xr, int y, const char* s, uint32_t c) { ui_text(xr - ui_text_w(s, 1), y, s, c); }
static void ui_title(int y, const char* s) {
  int sc = ui_text_w(s, 2) <= 210 ? 2 : 1;
  ui_text_ex(128 - ui_text_w(s, sc) / 2, y, s, 0xfff2a0, 0xe86a10, sc);
}
static void ui_backdrop(void) {   // darken the game picture under the menu
  uint32_t* p = ui_buf();
  for (int i = 0; i < 512 * 480; i++) { uint32_t c = p[i]; p[i] = (c >> 2) & 0x3f3f3f; }
}
static void ui_panel(int x, int y, int w, int h) {
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    bool cut = (i + j < 3) || (w - 1 - i + j < 3) || (i + h - 1 - j < 3) || (w - 1 - i + h - 1 - j < 3);   // bevelled corners
    if (cut) continue;
    int edge = i < j ? i : j;
    int e2 = (w - 1 - i) < (h - 1 - j) ? (w - 1 - i) : (h - 1 - j);
    if (e2 < edge) edge = e2;
    if (edge == 0) ui_px(x + i, y + j, 0x02040c);
    else if (edge <= 2) ui_px(x + i, y + j, ui_mix(0xa8d8ff, 0x1c48c8, j, h));          // bright blue frame
    else if (edge == 3) ui_px(x + i, y + j, 0x08143a);
    else ui_blend(x + i, y + j, ui_mix(0x0c1c4c, 0x040a1c, j, h), 220);                 // deep blue body
  }
}
static void ui_divider(int x, int y, int w) {
  for (int i = 0; i < w; i++) { int a = 256 - abs(i - w / 2) * 512 / w; if (a < 0) a = 0; ui_blend(x + i, y, 0x5a9cff, a); }
}
static void ui_cursor(int x, int y) {   // a small lightsaber: grey hilt, glowing blue blade
  int glow = 160 + (int)(60 * ((g_ui_tick / 4) % 8 < 4 ? (g_ui_tick / 4) % 4 : 3 - (g_ui_tick / 4) % 4) / 3);
  for (int i = 0; i < 4; i++) { ui_px(x + i, y + 3, 0x9098a8); ui_px(x + i, y + 4, 0x606878); }
  ui_px(x + 1, y + 3, 0xd0d8e0);
  for (int i = 4; i < 13; i++) {
    ui_blend(x + i, y + 2, 0x4080ff, glow / 2); ui_blend(x + i, y + 5, 0x4080ff, glow / 2);
    ui_px(x + i, y + 3, 0xe8f4ff); ui_px(x + i, y + 4, ui_mix(0x80c0ff, 0xe8f4ff, 1, 2));
  }
}
static void ui_select_bar(int x, int y, int w) {
  for (int j = -1; j < 9; j++) for (int i = 0; i < w; i++) {
    int a = 120 - i * 100 / w; if (j == -1 || j == 8) a /= 3;
    ui_blend(x + i, y + j, 0x2a5cff, a);
  }
}
static void ui_arrow(int x, int y, int up) {
  for (int r = 0; r < 3; r++) for (int i = -r; i <= r; i++) ui_px(x + i, y + (up ? r : 2 - r), 0x7fa8e0);
}
// split "Label: value" into its two halves (value may be empty)
static void ui_split(const char* in, char* label, char* value, int n) {
  const char* c = strstr(in, ": ");
  if (!c) { snprintf(label, n, "%s", in); value[0] = 0; return; }
  snprintf(label, n, "%.*s", (int)(c - in), in);
  snprintf(value, n, "%s", c + 2);
}
// generic list: rows of label/value with scrolling, selection bar and cursor
static void ui_list(int top, int rows, int count, int sel, void (*item)(int i, char* out, int n), uint32_t (*color)(int i)) {
  int first = sel - rows / 2;
  if (first > count - rows) first = count - rows;
  if (first < 0) first = 0;
  for (int r = 0; r < rows && first + r < count; r++) {
    int i = first + r, y = top + r * 12;
    char buf[80], lab[80], val[80];
    item(i, buf, sizeof buf);
    ui_split(buf, lab, val, sizeof lab);
    uint32_t c = color ? color(i) : 0xc8d8ff;
    if (i == sel) { ui_select_bar(22, y, 214); ui_cursor(11, y); c = 0xffffff; }
    int maxlab = val[0] ? 27 - (int)strlen(val) - 1 : 27;
    if ((int)strlen(lab) > maxlab && maxlab > 3) { lab[maxlab - 1] = '.'; lab[maxlab] = 0; }
    ui_text(24, y, lab, c);
    if (val[0]) ui_text_r(236, y, val, i == sel ? 0xfff0a0 : 0xf0c848);
  }
  if (first > 0) ui_arrow(244, top - 1, 1);
  if (first + rows < count) ui_arrow(244, top + rows * 12 - 5, 0);
}
static void ui_footer(const char* s) { ui_text(128 - ui_text_w(s, 1) / 2, 204, s, 0x7f94b8); }

static void menu_label(int i, char* out, int n) {
  switch (i) {
    case M_RESUME: snprintf(out, n, "Resume"); break;
    case M_ACH: snprintf(out, n, "Achievements: %d/%d", ach_unlocked_count(), ach_total()); break;
    case M_CONTROLS: snprintf(out, n, "Controls..."); break;
    case M_STATS: snprintf(out, n, "Stats & best times..."); break;
    case M_TIMER: snprintf(out, n, "Speedrun timer: %s", cfg.timer ? "On" : "Off"); break;
    case M_REWIND: snprintf(out, n, "Rewind (Bksp/LT): %s", cfg.rewind ? "On" : "Off"); break;
    case M_CONTINUE: if (HAS(GF_RESTORED) && g_cont_stage == 18) snprintf(out, n, "Continue: Restored stage");
      else if (g_cont_stage && g_cont_stage <= g_game->nstages && g_game->stage_names) snprintf(out, n, "Continue: %s", g_game->stage_names[g_cont_stage - 1]);
      else if (g_cont_stage) snprintf(out, n, "Continue: Stage %d", g_cont_stage); else snprintf(out, n, "Continue: none yet"); break;
    case M_WIDE: snprintf(out, n, "Widescreen: %s", k_wide_names[cfg.wide]); break;
    case M_M7: snprintf(out, n, "Mode 7 detail: %s", k_m7_names[cfg.m7]); break;
    case M_SLOW: snprintf(out, n, "Remove slowdown: %s", cfg.no_slowdown ? "On" : "Off"); break;
    case M_RUMBLE: snprintf(out, n, "Rumble: %s", cfg.rumble ? "On" : "Off"); break;
    case M_NOLIMIT: snprintf(out, n, "Sprite flicker: %s", cfg.nolimit ? "Removed" : "Original"); break;
    case M_RESTART: snprintf(out, n, "Restart stage"); break;
    case M_CRT: { static const char* c[] = {"Off", "Scanlines", "Full CRT"}; snprintf(out, n, "CRT filter: %s", c[cfg.crt]); break; }
    case M_RESTORED: snprintf(out, n, "Restored cut stage: %s", !restore_available() ? "n/a" : cfg.restored ? "On" : "Off"); break;
    case M_MUSIC: snprintf(out, n, "Music: %s", !g_msu_songs ? "Original (no pack)" : cfg.msu ? "Orchestral" : "Original"); break;
    case M_ASPECT: snprintf(out, n, "Aspect: %s", k_aspect_names[cfg.aspect]); break;
    case M_INTEGER: snprintf(out, n, "Integer scaling: %s", cfg.integer_scale ? "On" : "Off"); break;
    case M_SMOOTH: snprintf(out, n, "Smoothing: %s", cfg.smooth ? "On" : "Off"); break;
    case M_FULL: snprintf(out, n, "Fullscreen: %s", cfg.fullscreen ? "On" : "Off"); break;
    case M_VOLUME: snprintf(out, n, "Volume: %d%%", cfg.volume); break;
    case M_MUSICVOL: snprintf(out, n, "Orchestral volume: %d%%", cfg.music_vol); break;
    case M_INTERP: snprintf(out, n, "Sound: %s", cfg.interp ? "Crisp" : "Original"); break;
    case M_COLOR: { static const char* c[] = {"Original", "Vivid", "Soft (TV)"}; snprintf(out, n, "Colours: %s", c[cfg.color]); break; }
    case M_SABER: snprintf(out, n, "%s: %s", g_game->cheat_a_name, !UNLOCKED ? "Locked" : cfg.saber ? "On" : "Off"); break;
    case M_HERO: snprintf(out, n, "%s: %s", g_game->cheat_b_name, !UNLOCKED ? "Locked" : cfg.hero ? "On" : "Off"); break;
    case M_GAME: snprintf(out, n, "Game: %s", g_game->menu_name); break;
    case M_TRILOGY: snprintf(out, n, "After the ending: %s", cfg.trilogy ? "Next game" : "Same game"); break;
    case M_SPAWN: snprintf(out, n, "Wide enemies: %s", cfg.wide_spawn == 1 ? "Spawn early" : cfg.wide_spawn == 2 ? "Like original" : "Shown"); break;
    case M_DEV: snprintf(out, n, "Developer features: %s", cfg.dev ? "On" : "Off"); break;
    case M_LEVEL: snprintf(out, n, "Level select%s", !UNLOCKED ? ": Locked" : in_stage() ? "" : " (in a stage)"); break;
    case M_CLEAR: snprintf(out, n, "Clear this stage"); break;
    case M_SAVE: snprintf(out, n, "Save state"); break;
    case M_LOAD: snprintf(out, n, "Load state"); break;
    case M_RESET: snprintf(out, n, "Reset game"); break;
    case M_QUIT: snprintf(out, n, "Quit game"); break;
    case M_PG_VIDEO: snprintf(out, n, "Video..."); break;
    case M_PG_AUDIO: snprintf(out, n, "Audio..."); break;
    case M_PG_GAME: snprintf(out, n, "Gameplay..."); break;
    case M_PG_EXTRAS: snprintf(out, n, "Extras & unlockables..."); break;
    case M_PG_OPTIONS: snprintf(out, n, "Options..."); break;
    case M_TITLE: snprintf(out, n, "Quit to title screen"); break;
    case M_PAUSEBTN: snprintf(out, n, "Start button: %s", cfg.pause_menu ? "Pause menu" : "Original pause"); break;
    case M_REPORT:   // the file is ssw_report_<date>_<hh-mm-ss>.txt; the row only has room for the time
      if (g_report_timer > 0) { const char* r = crashlog_last_report(); size_t l = strlen(r);
        snprintf(out, n, "Saved report %.2s:%.2s:%.2s", l >= 8 ? r + l - 8 : "", l >= 5 ? r + l - 5 : "", l >= 2 ? r + l - 2 : ""); }
      else snprintf(out, n, "Report a problem");
      break;
  }
}
static void ach_item(int i, char* out, int n) { const char *t, *d; int got = ach_get(i, &t, &d); snprintf(out, n, "%s%s", got ? "* " : "  ", t); }
static uint32_t ach_color(int i) { const char *t, *d; return ach_get(i, &t, &d) ? 0xf8d860 : 0x8090a8; }
static void draw_ach_screen(void) {
  ui_panel(8, 6, 240, 212);
  char hdr[48]; snprintf(hdr, sizeof hdr, "ACHIEVEMENTS %d/%d", ach_unlocked_count(), ach_total());
  ui_title(14, hdr);
  ui_divider(16, 34, 224);
  ui_list(40, 10, ach_total(), g_ach_sel, ach_item, ach_color);
  ui_divider(16, 163, 224);
  const char *t, *d; ach_get(g_ach_sel, &t, &d);
  const char* p = d; int y = 168;
  while (*p && y < 200) {
    int n = (int)strlen(p), cut = n;
    if (n > 28) { cut = 28; while (cut > 0 && p[cut] != ' ') cut--; if (!cut) cut = 28; }
    char line[40]; snprintf(line, sizeof line, "%.*s", cut, p);
    ui_text(128 - ui_text_w(line, 1) / 2, y, line, 0xc8d8ff);
    y += 10; p += cut; while (*p == ' ') p++;
  }
  ui_footer("ESC: BACK");
}
// ---------------------------------------------------------------- stats screen
static int g_stats_screen;
static char g_stat_lines[40][48]; static int g_stat_n;
static void stat_item(int i, char* out, int n) { snprintf(out, n, "%s", g_stat_lines[i]); }
static uint32_t stat_color(int i) { return g_stat_lines[i][0] == '-' ? 0xf0a040 : 0xc8d8ff; }
static int g_stat_sel;
static void draw_stats_screen(void) {
  ui_panel(8, 6, 240, 212);
  ui_title(14, "STATISTICS");
  ui_divider(16, 34, 224);
  char lines[12][48];
  int n = extras_stat_lines(lines, 12);
  g_stat_n = 0;
  for (int i = 0; i < n; i++) {   // "Name  value" -> "Name: value"
    char* l = lines[i]; int k = (int)strlen(l);
    while (k > 0 && l[k - 1] != ' ') k--;
    int e = k; while (e > 0 && l[e - 1] == ' ') e--;
    snprintf(g_stat_lines[g_stat_n++], 48, "%.*s: %s", e, l, l + k);
  }
  snprintf(g_stat_lines[g_stat_n++], 48, "-- Best stage times --");
  for (int i = 0; i < g_game->nstages && g_stat_n < 40; i++) {
    uint32_t b = extras_best_stage(g_game->stages[i][0], g_game->stages[i][1]);
    char t[24]; if (b) extras_format_time(t, sizeof t, b); else snprintf(t, sizeof t, "--");
    if (HAS(GF_RESTORED) && i == 17) snprintf(g_stat_lines[g_stat_n++], 48, "Restored stage: %s", t);
    else if (g_game->stage_names) snprintf(g_stat_lines[g_stat_n++], 48, "%s: %s", g_game->stage_names[i], t);
    else snprintf(g_stat_lines[g_stat_n++], 48, "Stage %d: %s", i + 1, t);
  }
  if (g_stat_sel >= g_stat_n) g_stat_sel = g_stat_n - 1;
  ui_list(40, 13, g_stat_n, g_stat_sel, stat_item, stat_color);
  ui_footer("UP/DOWN: SCROLL   ESC: BACK");
}
// ---------------------------------------------------------------- controls screen
static int g_ctrl_screen, g_ctrl_sel, g_ctrl_wait;
static const char* pad_name(int b) {
  static const char* n[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "LStick", "RStick", "LB", "RB",
                            "D-Up", "D-Down", "D-Left", "D-Right", "Misc", "P1", "P2", "P3", "P4", "Touch"};
  return b >= 0 && b < (int)(sizeof n / sizeof n[0]) ? n[b] : "-";
}
static void draw_ctrl_screen(void) {
  ui_panel(8, 6, 240, 212);
  ui_title(14, "CONTROLS");
  ui_divider(16, 34, 224);
  ui_text(24, 40, "SNES", 0x7f94b8); ui_text(104, 40, "KEY", 0x7f94b8); ui_text(172, 40, "PAD", 0x7f94b8);
  for (int i = 0; i <= 12; i++) {
    int y = 52 + i * 11;
    bool sel = i == g_ctrl_sel;
    uint32_t c = sel ? 0xffffff : 0xc8d8ff;
    if (sel) { ui_select_bar(22, y, 214); ui_cursor(11, y); }
    if (i == 12) { ui_text(24, y, "Reset to defaults", c); break; }
    char kn[32]; const char* nm = g_keymap[i] ? SDL_GetKeyName(g_keymap[i]) : "-";
    if (!strncmp(nm, "Right ", 6) && nm[6]) snprintf(kn, sizeof kn, "R%.7s", nm + 6);
    else if (!strncmp(nm, "Left ", 5) && nm[5]) snprintf(kn, sizeof kn, "L%.7s", nm + 5);
    else snprintf(kn, sizeof kn, "%.8s", nm);
    char nmb[16]; snprintf(nmb, sizeof nmb, "%.10s", k_btn_names[i]);
    ui_text(24, y, nmb, c);
    bool wait = g_ctrl_wait && sel && (g_ui_tick / 15) % 2;
    ui_text(104, y, g_ctrl_wait && sel ? (wait ? "..." : "") : kn, sel ? 0xfff0a0 : 0xf0c848);
    ui_text(172, y, g_ctrl_wait && sel ? (wait ? "..." : "") : pad_name(g_padmap[i]), sel ? 0xfff0a0 : 0xf0c848);
  }
  ui_footer(g_ctrl_wait ? "PRESS A KEY OR BUTTON (ESC: CANCEL)" : "ENTER: CHANGE   ESC: BACK");
}
static void bind_key(int row, SDL_Keycode k) {
  for (int i = 0; i < 12; i++) if (i != row && g_keymap[i] == k) g_keymap[i] = g_keymap[row];   // swap
  g_keymap[row] = k;
}
static void bind_pad(int row, int b) {
  for (int i = 0; i < 12; i++) if (i != row && g_padmap[i] == b) g_padmap[i] = g_padmap[row];
  g_padmap[row] = b;
}
static void page_item(int i, char* out, int n) { menu_label(k_pages[g_page][i], out, n); }
static uint32_t page_color(int i) {
  int id = k_pages[g_page][i];
  if ((id == M_RESTART && !restart_available()) || (id == M_CONTINUE && !g_cont_stage)) return 0x6c7890;
  char b[80]; menu_label(id, b, sizeof b);
  return strstr(b, "Locked") || strstr(b, "(not in") || strstr(b, "n/a") || strstr(b, "no save") ? 0x6c7890 : 0xc8d8ff;
}
static void draw_menu(void) {
  g_ui_tick++;
  ui_canvas_menu();
  ui_backdrop();
  if (g_ach_screen) { draw_ach_screen(); return; }
  if (g_ctrl_screen) { draw_ctrl_screen(); return; }
  if (g_stats_screen) { draw_stats_screen(); return; }
  ui_panel(8, 6, 240, 212);
  ui_title(14, k_page_titles[g_page]);
  if (g_page == 0) { const char* sub = game_is_play_mode(game_mode(g_snes->ram)) ? "PAUSED" : "PC EDITION"; ui_text(128 - ui_text_w(sub, 1) / 2, 32, sub, 0x7fa8e0); }
  ui_divider(16, g_page == 0 ? 42 : 34, 224);
  int top = g_page == 0 ? 48 : 40;
  ui_list(top, g_page == 0 ? 12 : 13, page_len(g_page), g_sel, page_item, page_color);
  ui_footer(g_page == 4 && !UNLOCKED ? "BEAT THE GAME TO UNLOCK" : g_page == 5 ? "ENTER: SELECT   ESC: BACK" : g_page ? "ENTER: CHANGE   ESC: BACK" : "ENTER: SELECT   ESC: RESUME");
}
static void menu_change(int dir, bool activate) {
  int item = k_pages[g_page][g_sel];
  switch (item) {
    case M_PG_VIDEO: case M_PG_AUDIO: case M_PG_GAME: case M_PG_EXTRAS: case M_PG_OPTIONS:
      if (activate) { g_page = item - M_PG_VIDEO + 1; g_sel = 0; }
      return;
    case M_TITLE: if (activate) { crashlog_event("quit to title"); snes_reset(g_snes, false); rewind_clear(); g_menu = false; } return;
    case M_PAUSEBTN: cfg.pause_menu ^= 1; break;
    case M_RESUME: if (activate) g_menu = false; break;
    case M_ACH: if (activate) g_ach_screen = 1; break;
    case M_STATS: if (activate) g_stats_screen = 1; break;
    case M_TIMER: cfg.timer ^= 1; break;
    case M_REWIND: cfg.rewind ^= 1; break;
    case M_CONTROLS: if (activate) { g_ctrl_screen = 1; g_ctrl_sel = 0; g_ctrl_wait = 0; } break;
    case M_CONTINUE: if (activate && g_cont_stage) { cont_load(); g_menu = false; } break;
    case M_WIDE: cfg.wide = (cfg.wide + (dir ? dir : 1) + WIDE_COUNT) % WIDE_COUNT; break;
    case M_M7: cfg.m7 = (cfg.m7 + (dir ? dir : 1) + 3) % 3; break;
    case M_SLOW: cfg.no_slowdown ^= 1; break;
    case M_NOLIMIT: cfg.nolimit ^= 1; break;
    case M_RESTART: if (activate && restart_available()) { cont_load(); g_menu = false; } break;
    case M_CRT: cfg.crt = (cfg.crt + (dir < 0 ? 2 : 1)) % 3; break;
    case M_RUMBLE: cfg.rumble ^= 1; if (cfg.rumble) rumble(0x6000, 0x6000, 150); break;
    case M_RESTORED: if (restore_available()) cfg.restored ^= 1; break;
    case M_MUSIC: if (g_msu_songs) { cfg.msu ^= 1; msu_enable(cfg.msu); } break;
    case M_ASPECT: cfg.aspect = (cfg.aspect + (dir ? dir : 1) + ASPECT_COUNT) % ASPECT_COUNT; break;
    case M_INTEGER: cfg.integer_scale ^= 1; break;
    case M_SMOOTH: cfg.smooth ^= 1; break;
    case M_FULL: cfg.fullscreen ^= 1; apply_fullscreen(); break;
    case M_VOLUME:
      if (activate && !dir) cfg.volume = cfg.volume >= 100 ? 0 : cfg.volume + 10;
      else cfg.volume += dir * 10;
      if (cfg.volume > 100) cfg.volume = 100;
      if (cfg.volume < 0) cfg.volume = 0;
      break;
    case M_MUSICVOL:
      if (activate && !dir) cfg.music_vol = cfg.music_vol >= 100 ? 0 : cfg.music_vol + 10;
      else cfg.music_vol += dir * 10;
      if (cfg.music_vol > 100) cfg.music_vol = 100;
      if (cfg.music_vol < 0) cfg.music_vol = 0;
      break;
    case M_INTERP: cfg.interp ^= 1; break;
    case M_COLOR: cfg.color = (cfg.color + (dir < 0 ? 2 : 1)) % 3; break;
    case M_SABER: if (UNLOCKED) cfg.saber ^= 1; break;
    case M_HERO: if (UNLOCKED) cfg.hero ^= 1; break;
    case M_GAME: if (g_games_avail > 1) { g_switch_to = next_game(dir < 0 ? -1 : 1); g_menu = false; } break;
    case M_TRILOGY: cfg.trilogy ^= 1; break;
    case M_SPAWN: cfg.wide_spawn = (cfg.wide_spawn + (dir < 0 ? 2 : 1)) % 3; break;
    case M_DEV: cfg.dev ^= 1; break;
    case M_LEVEL: if (activate && UNLOCKED && in_stage()) { level_select(); g_menu = false; } break;
    case M_CLEAR: if (activate) { stage_clear(); g_menu = false; } break;
    case M_SAVE: if (activate) { save_state_file(); g_menu = false; } break;
    case M_LOAD: if (activate) { load_state_file(); g_menu = false; } break;
    case M_RESET: if (activate) { snes_reset(g_snes, false); g_menu = false; } break;
    case M_QUIT: if (activate) g_quit = true; break;
    case M_REPORT: if (activate) { crashlog_report(); g_report_timer = 240; } break;
  }
  cfg_save();
  update_title();
  { char lab[64]; menu_label(item, lab, sizeof lab); crashlog_event("menu: %s%s", lab, activate ? " (selected)" : ""); }
}
// pixel aspect of the chosen display mode, and how many extra pixels per side reach the wanted ratio
static double pixel_aspect(void) { return cfg.aspect == ASPECT_8_7 ? 1.0 : (4.0 / 3.0) / (256.0 / 224.0); }
static int wide_extra(void) {
  static const double ratio[] = {0, 16.0 / 10.0, 16.0 / 9.0, 21.0 / 9.0};
  if (cfg.wide == WIDE_OFF || cfg.aspect == ASPECT_STRETCH || !(g_game->features & GF_WIDESCREEN)) return 0;
  int e = (int)((224.0 * ratio[cfg.wide] / pixel_aspect() - 256.0) / 2.0 + 0.5);
  return e < 0 ? 0 : e > PPU_WIDE_MAX ? PPU_WIDE_MAX : e;
}
static void menu_key(int dir_y, int dir_x, bool enter) {
  if (g_stats_screen) { if (dir_y) { g_stat_sel += dir_y; if (g_stat_sel < 0) g_stat_sel = 0; if (g_stat_sel >= g_stat_n) g_stat_sel = g_stat_n - 1; } if (enter) g_stats_screen = 0; return; }
  if (g_ctrl_screen) {
    if (dir_y) g_ctrl_sel = (g_ctrl_sel + dir_y + 13) % 13;
    if (enter) { if (g_ctrl_sel == 12) { bindings_default(); cfg_save(); } else g_ctrl_wait = 1; }
    return;
  }
  if (g_ach_screen) {
    if (dir_y) g_ach_sel = (g_ach_sel + dir_y + ach_total()) % ach_total();
    if (enter) { g_ach_screen = 0; if (g_tm_return && g_page == 0) open_menu(false); }
    return;
  }
  if (dir_y) g_sel = (g_sel + dir_y + page_len(g_page)) % page_len(g_page);
  if (dir_x) menu_change(dir_x, false);
  if (enter) menu_change(0, true);
}
static void menu_back(void) {   // settings pages -> Options -> pause menu, landing on the entry we came from
  int entry = M_PG_VIDEO + g_page - 1;
  g_page = g_page == 5 ? 0 : 5; g_sel = 0;
  for (int i = 0; k_pages[g_page][i] >= 0; i++) if (k_pages[g_page][i] == entry) g_sel = i;
}
static void open_menu(bool open) {
  g_menu = open;
  g_tm_return = 0;
  g_ctrl_screen = g_ctrl_wait = g_stats_screen = 0;
  g_page = 0; if (open) g_sel = 0;
  if (open) for (int i = 0; i < 12; i++) set_btn(i, false);
}

// achievement toast: drawn into whatever frame buffer is being shown (scale 2 for the 512-wide
// normal frame, 1 for the native-resolution wide frame)
// HUD overlay in the menu's style: achievement pop-up (top centre) and speedrun timer (top right),
// drawn into a transparent canvas cw x 224 that is stretched over the picture.
static uint32_t g_hud[512 * 224];
static bool draw_hud(int cw) {
  memset(g_hud, 0, sizeof g_hud);
  g_cv.buf = g_hud; g_cv.w = cw; g_cv.h = 224; g_cv.stride = 512; g_cv.scale = 1; g_cv.yoff = 0; g_cv.alpha = 1;
  bool any = false;
  if (g_toast_id >= 0 && !g_menu) {
    const char *t, *d; ach_get(g_toast_id, &t, &d);
    char ttl[48]; snprintf(ttl, sizeof ttl, "%.28s", t);
    int w = ui_text_w(ttl, 1) + 36; if (w < 150) w = 150;
    int x = (cw - w) / 2, y = 30;
    int slide = g_toast_timer > 225 ? (g_toast_timer - 225) * 3 : g_toast_timer < 15 ? (15 - g_toast_timer) * 3 : 0;
    y -= slide;
    ui_panel(x, y, w, 30);
    // star icon
    static const char* star[] = {"...#...", "...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", "##...##"};
    for (int r = 0; r < 8; r++) for (int i = 0; i < 7; i++) if (star[r][i] == '#') ui_px(x + 8 + i, y + 11 + r, ui_mix(0xfff2a0, 0xe86a10, r, 8));
    ui_text_ex(x + 22, y + 6, "ACHIEVEMENT", 0xfff2a0, 0xe86a10, 1);
    ui_text(x + 22, y + 16, ttl, 0xffffff);
    any = true;
  }
  if (cfg.timer && !g_menu) {
    char run[24], st[24]; uint32_t col, scol; int show_split;
    if (extras_timer_text(run, sizeof run, st, sizeof st, &col, &scol, &show_split)) {
      ui_text_r(cw - 6, 26, run, col);
      if (st[0]) ui_text_r(cw - 6, 36, st, scol);
      any = true;
    }
  }
  return any;
}
static void toast_tick(void) {
  if (g_toast_timer > 0) { if (--g_toast_timer == 0) g_toast_id = -1; }
  if (g_toast_id < 0) { int id = ach_pop_toast(); if (id >= 0) { g_toast_id = id; g_toast_timer = 240; } }
}

// ---------------------------------------------------------------- title-screen menu: PC SETTINGS / ACHIEVEMENTS
// The game's title box (BG3) lists its items on every second row, with a dot tile left of the selected
// one. While it is on screen we lay the box out again in VRAM, single-spaced: the game's items, then
// ours -- growing the box by a row when it needs one, together with the per-line window table that
// darkens the artwork behind it -- so it is drawn by the game's own PPU setup in every video mode.
// We keep our own cursor and set the game's cursor variable to match, so Start picks the right item.
static int g_tm_on, g_tm_sel;
static uint16_t g_tm_text[4][13], g_tm_dot, g_tm_blank, g_tm_left, g_tm_right, g_tm_bottom[17], g_tm_attr;
static int g_tm_win_last[2];           // last "window on" line of each window table, at activation
static const char* const k_tm_ours[2] = {"PC SETTINGS", "ACHIEVEMENTS"};
static inline uint16_t* tm_cell(int row, int col) { return &g_snes->ppu->vram[(0x800 + row * 32 + col) & 0x7fff]; }
static bool tm_visible(void) {
  const GameDef* g = g_game; uint8_t* r = g_snes->ram;
  if (!g->tm_items || game_mode(r) != g->title_mode || game_scene(r) != g->title_scene) return false;
  if ((*tm_cell(26, 6) & 0x3ff) != g->font_base + 0x25) return false;       // title text on screen
  return (*tm_cell(g->tm_box_top, 8) & 0x3ff) != (*tm_cell(g->tm_box_top - 2, 8) & 0x3ff);   // and the box
}
static void tm_activate(void) {
  const GameDef* g = g_game;
  int cur = g_snes->ram[g->tm_cursor];
  for (int i = 0; i < g->tm_items && i < 4; i++)
    for (int c = 0; c < 13; c++) g_tm_text[i][c] = *tm_cell(g->tm_first_row + 2 * i, 11 + c);
  g_tm_dot = *tm_cell(g->tm_first_row + 2 * (cur < g->tm_items ? cur : 0), 9);
  g_tm_blank = *tm_cell(g->tm_first_row + 1, 12);
  g_tm_left = *tm_cell(g->tm_first_row + 1, 8);
  g_tm_right = *tm_cell(g->tm_first_row + 1, 24);
  for (int c = 0; c < 17; c++) g_tm_bottom[c] = *tm_cell(g->tm_box_bottom, 8 + c);
  g_tm_attr = g_tm_text[0][0] & 0xFC00;
  for (int k = 0; k < 2; k++) {
    g_tm_win_last[k] = -1;
    for (int y = 0; y < 224; y++) {
      uint8_t* w = &g_snes->ram[(g->tm_window[k] + y * 2) & 0x1ffff];
      if (w[0] <= w[1]) g_tm_win_last[k] = y;
    }
  }
  g_tm_sel = cur < g->tm_items ? cur : 0;
  g_tm_on = 1;
}
// once per frame, after the game's vblank uploads and before the picture is drawn (runner hook)
static void title_menu_tick(void) {
  if (!tm_visible()) { g_tm_on = 0; return; }
  if (!g_tm_on) tm_activate();
  const GameDef* g = g_game;
  int n = g->tm_items + 2, top = g->tm_box_top;
  int bottom = top + 1 + n > g->tm_box_bottom ? top + 1 + n : g->tm_box_bottom;
  for (int k = 0; k < n || top + 1 + k < bottom; k++) {
    int row = top + 1 + k;
    *tm_cell(row, 8) = g_tm_left; *tm_cell(row, 24) = g_tm_right;
    for (int c = 9; c < 24; c++) *tm_cell(row, c) = g_tm_blank;
    if (k < g->tm_items) for (int c = 0; c < 13; c++) *tm_cell(row, 11 + c) = g_tm_text[k][c];
    else if (k < n) {
      const char* t = k_tm_ours[k - g->tm_items];
      for (int c = 0; t[c] && c < 13; c++) { int gt = gfont_tile(t[c]); if (gt >= 0) *tm_cell(row, 11 + c) = (uint16_t)((gt - 0x180 + g->font_base) | g_tm_attr); }
    }
    if (k == g_tm_sel) *tm_cell(row, 9) = g_tm_dot;
  }
  for (int c = 0; c < 17; c++) *tm_cell(bottom, 8 + c) = g_tm_bottom[c];
  // the darkened area grows with the box
  int extra = (bottom - g->tm_box_bottom) * 8;
  for (int k = 0; k < 2 && extra > 0; k++) {
    int last = g_tm_win_last[k];
    if (last < 0) continue;
    uint8_t* src = &g_snes->ram[(g->tm_window[k] + last * 2) & 0x1ffff];
    for (int y = last + 1; y <= last + extra && y < 224; y++) {
      uint8_t* w = &g_snes->ram[(g->tm_window[k] + y * 2) & 0x1ffff];
      w[0] = src[0]; w[1] = src[1];
    }
  }
  // the game's cursor follows ours (its last item while ours is on PC SETTINGS / ACHIEVEMENTS)
  int gc = g_tm_sel < g->tm_items ? g_tm_sel : g->tm_items - 1;
  g_snes->ram[g->tm_cursor] = (uint8_t)gc;   // low byte only: the next byte belongs to something else
}
// returns true if the press was taken by the title menu
static bool title_menu_key(int b, bool down) {
  if (!g_tm_on || g_menu || b < 0) return false;
  const GameDef* g = g_game;
  int n = g->tm_items + 2;
  bool ours = g_tm_sel >= g->tm_items;
  if (b == BTN_DOWN || b == BTN_UP) {
    if (down) g_tm_sel = (g_tm_sel + (b == BTN_DOWN ? 1 : n - 1)) % n;
    return false;   // the game still sees it: its cursor sound and idle timer (its cursor is reset every frame)
  }
  if (!ours) return false;
  if (down && (b == BTN_START || b == BTN_A || b == BTN_B)) {
    open_menu(true);
    g_tm_return = 1;
    if (g_tm_sel == g->tm_items) { g_page = 5; g_sel = 0; }
    else g_ach_screen = 1;
  }
  return true;     // nothing else reaches the game while one of ours is selected
}
// leaving a sub-screen or page: back to the title when that is where it was opened from
static void menu_escape(void) {
  if (g_ach_screen) { g_ach_screen = 0; if (g_tm_return && g_page == 0) open_menu(false); }
  else if (g_ctrl_screen) g_ctrl_screen = 0;
  else if (g_stats_screen) g_stats_screen = 0;
  else if (g_page == 5 && g_tm_return) open_menu(false);
  else if (g_page) menu_back();
  else open_menu(false);
}

// ---------------------------------------------------------------- main
// The trilogy's ROMs: each game looks for its own file name next to the exe (or in the working
// directory); a file given on the command line / dropped on the exe is identified by its checksum.
static uint8_t* g_rom_data[8];
static int g_rom_len[8];
static char g_base[1024];
static uint8_t* load_path(const char* name, int* len) {
  char p[1200];
  snprintf(p, sizeof p, "%s%s", g_base, name);
  uint8_t* d = runner_load_file(p, len);
  return d ? d : runner_load_file(name, len);
}
// Each game's ROM is loaded and checked with rom_check.c (SHA-1 of the exact supported dump; a copier
// header is stripped). A missing file is simply not offered; a wrong one is reported.
static bool g_rom_problem;   // a ROM was there but rejected (already explained to the player)
static GameId rom_id_of(const GameDef* g) {
  return !strcmp(g->key, "esb") ? GAME_SESB : !strcmp(g->key, "srotj") ? GAME_SROTJ : GAME_SSW;
}
static RomCheckResult load_verified(const char* name, GameId id, RomImage* img) {
  char p[1200];
  snprintf(p, sizeof p, "%s%s", g_base, name);
  RomCheckResult r = rom_load_and_verify(p, id, img);
  if (r == ROM_ERR_NOT_FOUND) r = rom_load_and_verify(name, id, img);   // working directory
  return r;
}
static void find_roms(int argc, char** argv) {
  char problems[2048] = "";
  for (int i = 0; g_all_games[i]; i++) {
    RomImage img;
    RomCheckResult r = load_verified(g_all_games[i]->rom_file, rom_id_of(g_all_games[i]), &img);
    if (r == ROM_OK) { g_rom_data[i] = img.data; g_rom_len[i] = (int)img.size; }
    else if (r != ROM_ERR_NOT_FOUND) {
      char line[600]; snprintf(line, sizeof line, "%s:\n%s\n\n", g_all_games[i]->rom_file, rom_check_message(r, rom_id_of(g_all_games[i])));
      strncat(problems, line, sizeof problems - strlen(problems) - 1);
    }
  }
  if (argc > 1) {   // a ROM given on the command line / dropped on the exe: whichever game it is
    RomCheckResult last = ROM_ERR_NOT_FOUND; GameId last_id = GAME_SSW;
    bool used = false;
    for (int i = 0; g_all_games[i] && !used; i++) {
      RomImage img;
      RomCheckResult r = rom_load_and_verify(argv[1], rom_id_of(g_all_games[i]), &img);
      if (r == ROM_OK) {
        free(g_rom_data[i]); g_rom_data[i] = img.data; g_rom_len[i] = (int)img.size;
        cfg.last_game = 0; g_game = g_all_games[i]; used = true;
      } else if (r > last || last == ROM_ERR_NOT_FOUND) { last = r; last_id = rom_id_of(g_all_games[i]); }
    }
    if (!used) {
      char line[700]; snprintf(line, sizeof line, "%s:\n%s\n\n", argv[1], last == ROM_ERR_WRONG_VERSION ?
               "This isn't a supported version of any of the games. Required: the USA Rev 1 cartridges (see README)." :
               rom_check_message(last, last_id));
      strncat(problems, line, sizeof problems - strlen(problems) - 1);
    }
  }
  g_games_avail = 0;
  for (int i = 0; g_all_games[i]; i++) if (g_rom_data[i]) g_games_avail++;
  g_rom_problem = problems[0] != 0;
  if (problems[0]) {
    crashlog_event("ROM check: %s", problems);
    SDL_ShowSimpleMessageBox(g_games_avail ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_ERROR, "Star Wars", problems, NULL);
  }
}
static int game_slot(const GameDef* g) { for (int i = 0; g_all_games[i]; i++) if (g_all_games[i] == g) return i; return -1; }
static const GameDef* next_game(int dir) {   // the next game whose ROM is available, wrapping
  int n = 0; while (g_all_games[n]) n++;
  int i = game_slot(g_game);
  for (int k = 1; k <= n; k++) { int j = ((i + dir * k) % n + n) % n; if (g_rom_data[j]) return g_all_games[j]; }
  return g_game;
}

static void rewind_clear(void);
// (Re)start the emulated console with another game's cartridge. Everything per-game -- recompiled code,
// save paths, records, achievements, ROM patches -- follows g_game.
static void load_game(const GameDef* g) {
  int i = game_slot(g);
  if (i < 0 || !g_rom_data[i]) return;
  if (g_snes) {
    free(g_snes->ppu->wideBuffer); g_snes->ppu->wideBuffer = NULL;
    free(g_snes->ppu->m7hiresBuf); g_snes->ppu->m7hiresBuf = NULL;
    snes_free(g_snes);
  }
  g_game = g;
  g_recomp_fn = g->recomp; g_hook_list = g->hooks; g_hook_enable = 0;
  cfg.last_game = g_game_bit();
  snprintf(g_state_path, sizeof g_state_path, "%s%s.state", g_base, g->key);
  snprintf(g_cont_path, sizeof g_cont_path, "%s%s_continue.state", g_base, g->key);
  cont_probe();
  g_snes = snes_init();
  snes_loadRom(g_snes, g_rom_data[i], g_rom_len[i]);
  snes_setPixelFormat(g_snes, pixelFormatXRGB);
  g_snes->ppu->m7hiresBuf = calloc((256 + 2 * PPU_WIDE_MAX) * 4 * 896, 4);
  crashlog_set_snes(g_snes);
  {   // tester tool: SSW_WARP=MMSS (hex) sends every route into the first stage to mode MM scene SS instead
    const char* w = getenv("SSW_WARP");
    if (w && strlen(w) == 4) {
      unsigned v = (unsigned)strtoul(w, NULL, 16);
      uint8_t* r = g_snes->cart->rom + g->seq_off;
      for (int k = 0; r[k] != 0xFF; k += 4)
        if (r[k + 2] == g->start_mode && r[k + 3] == g->start_scene) { r[k + 2] = v >> 8; r[k + 3] = v & 0xFF; }
      crashlog_event("tester warp to %04X", v);
    }
  }
  if (HAS(GF_RESTORED)) { restore_init(g_snes); restore_update(cfg.restored); }
  if (HAS(GF_WIDESCREEN)) ws_init(g_snes, g_rom_data[i], g_rom_len[i]);
  if (HAS(GF_ACH)) {
    ach_init(g_snes, g_base); ach_set_rom(g_snes->cart->rom);   // live copy: sees the restored-stage route
    const char *t, *d;   // mirror achievements already earned locally (no-op without Steam)
    for (int k = 0; k < ach_total(); k++) if (ach_get(k, &t, &d)) steam_unlock(ach_steam_id(k));
  }
  extras_init(g_snes, g_base);
  extras_set_rom(g_snes->cart->rom);
  g_msu_songs = HAS(GF_MSU) ? msu_init(g_base, g->msu_folder) : 0;
  msu_enable(HAS(GF_MSU) && cfg.msu && g_msu_songs);
  g_port_touched = false;
  rewind_clear();
  build_pages();
  if (g_win) update_title();
  crashlog_event("loaded %s", g->name);
}

// Trilogy: when a game's ending and credits have played and it returns to its title, go on to the next game.
static void trilogy_tick(void) {
  static int seen_end;
  static const GameDef* who;
  if (who != g_game) { who = g_game; seen_end = 0; }
  int m = game_mode(g_snes->ram), sc = game_scene(g_snes->ram);
  if (m == g_game->end_mode && sc == g_game->end_scene) seen_end = 1;
  // the whole ending (credits included) has played once the game is back at its title or rebooting
  else if (seen_end && (m == 0 || (m == g_game->title_mode && sc == g_game->title_scene))) {
    seen_end = 0;
    int j = game_slot(g_game) + 1;
    if (cfg.trilogy && g_all_games[j] && g_rom_data[j]) g_switch_to = g_all_games[j];
  }
}

int main(int argc, char** argv) {
  // Steam Controller (and Steam Deck) support through SDL's built-in HIDAPI driver
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  {
    char* base = SDL_GetBasePath();
    snprintf(g_base, sizeof g_base, "%s", base ? base : "");
    if (base) SDL_free(base);
  }
  // one settings file for the trilogy program; a single-game build keeps its own (esb.ini)
  snprintf(g_ini_path, sizeof g_ini_path, "%s%s.ini", g_base, g_all_games[1] ? "ssw" : g_all_games[0]->key);
  cfg_load();
  find_roms(argc, argv);
  if (!g_games_avail && g_rom_problem) return 1;
  if (!g_games_avail) {
    char msg[1024] = "Couldn't find your game ROM.\n\nPut it next to the program as:\n";
    for (int i = 0; g_all_games[i]; i++) {
      char l[200]; snprintf(l, sizeof l, "  %s  -  %s\n", g_all_games[i]->rom_file, g_all_games[i]->rom_desc);
      strncat(msg, l, sizeof msg - strlen(msg) - 1);
    }
    strncat(msg, "\nor drag the ROM file onto the program.", sizeof msg - strlen(msg) - 1);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Star Wars", msg, NULL);
    return 1;
  }
  // the menus use the games' own title-screen font (captured once from whichever ROM is here, cached)
  {
    int si = -1;
    for (int i = 0; g_all_games[i]; i++) if (si < 0 && g_rom_data[i]) si = i;
    if (si >= 0) {
      const GameDef* keep = g_game;
      g_game = g_all_games[si]; g_recomp_fn = g_game->recomp; g_hook_list = g_game->hooks;
      font_capture(g_rom_data[si], g_rom_len[si], g_base);
      g_game = keep;
    }
  }
  // which game to start: the one dropped on the exe, else the one played last, else the first found
  const GameDef* start = NULL;
  if (argc > 1 && g_rom_data[game_slot(g_game)]) start = g_game;
  for (int i = 0; !start && g_all_games[i]; i++) {
    g_game = g_all_games[i];
    if (g_rom_data[i] && cfg.last_game == g_game_bit()) start = g_game;
  }
  for (int i = 0; !start && g_all_games[i]; i++) if (g_rom_data[i]) start = g_all_games[i];
  g_game = start;
  crashlog_init(NULL, g_base, settings_summary);
  if (getenv("SSW_TEST_CRASH")) g_test_crash = 1200;
  steam_init(g_base);
  g_runner_after_vblank = title_menu_tick;
  load_game(start);

  g_win = SDL_CreateWindow(g_game->name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           256 * 4, 224 * 4, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture* tex[2];
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  tex[0] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 512, 480);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  tex[1] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 512, 480);
  const int WW = 256 + 2 * PPU_WIDE_MAX;
  SDL_Texture* wtex[2];
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  wtex[0] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WW, 240);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  wtex[1] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WW, 240);
  SDL_Texture* htex[2];
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  htex[0] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, (256 + 2 * PPU_WIDE_MAX) * 4, 896);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  htex[1] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, (256 + 2 * PPU_WIDE_MAX) * 4, 896);
  apply_fullscreen();
  update_title();

  SDL_AudioSpec want = {0}, have;
  want.freq = SAMPLE_RATE; want.format = AUDIO_S16; want.channels = 2; want.samples = 1024;
  SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (dev) SDL_PauseAudioDevice(dev, 0);
  for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

  g_state = malloc(4 * 1024 * 1024);
  bool fast = false;
  uint64_t freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
  const double frame_time = 1.0 / 60.0988;
  while (!g_quit) {
    if (g_switch_to) {   // menu "Game" item or the trilogy hand-over after an ending
      const GameDef* g = g_switch_to; g_switch_to = NULL;
      load_game(g); cfg_save();
      if (dev) SDL_ClearQueuedAudio(dev);
    }
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_QUIT: g_quit = true; break;
        case SDL_CONTROLLERDEVICEADDED: SDL_GameControllerOpen(ev.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
          bool down = ev.type == SDL_CONTROLLERBUTTONDOWN;
          int bt = ev.cbutton.button;
          if (g_menu && g_ctrl_wait) {           // rebinding: the next pad button goes to the selected row
            if (down && bt != SDL_CONTROLLER_BUTTON_GUIDE) { bind_pad(g_ctrl_sel, bt); g_ctrl_wait = 0; cfg_save(); }
            break;
          }
          if (bt == SDL_CONTROLLER_BUTTON_GUIDE && down) { open_menu(!g_menu); break; }
          if (g_menu) {
            if (!down) break;
            if (bt == SDL_CONTROLLER_BUTTON_DPAD_UP) menu_key(-1, 0, false);
            else if (bt == SDL_CONTROLLER_BUTTON_DPAD_DOWN) menu_key(1, 0, false);
            else if (bt == SDL_CONTROLLER_BUTTON_DPAD_LEFT) menu_key(0, -1, false);
            else if (bt == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) menu_key(0, 1, false);
            else if (bt == SDL_CONTROLLER_BUTTON_START && g_page == 0 && !g_ach_screen && !g_ctrl_screen && !g_stats_screen) open_menu(false);   // Start again: resume
            else if (bt == SDL_CONTROLLER_BUTTON_A || bt == SDL_CONTROLLER_BUTTON_START) menu_key(0, 0, true);
            else if (bt == SDL_CONTROLLER_BUTTON_B || bt == SDL_CONTROLLER_BUTTON_BACK) menu_escape();
            break;
          }
          int b = pad_to_btn(bt);
          if (start_opens_pause(b, down)) { open_menu(true); break; }
          if (title_menu_key(b, down)) break;
          if (b >= 0) set_btn(b, down);
          SDL_GameController* gc = SDL_GameControllerFromInstanceID(ev.cbutton.which);
          if (down && gc && SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK) &&
              SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) open_menu(true);
          break;
        }
        case SDL_KEYDOWN: case SDL_KEYUP: {
          bool down = ev.type == SDL_KEYDOWN;
          SDL_Keycode k = ev.key.keysym.sym;
          bool alt = ev.key.keysym.mod & KMOD_ALT;
          if (down && (k == SDLK_F11 || (k == SDLK_RETURN && alt))) { cfg.fullscreen ^= 1; apply_fullscreen(); cfg_save(); break; }
          if (g_menu && g_ctrl_wait) {           // rebinding: the next key goes to the selected row
            if (down && !ev.key.repeat) {
              if (k != SDLK_ESCAPE && k != SDLK_F11) { bind_key(g_ctrl_sel, k); cfg_save(); }
              g_ctrl_wait = 0;
            }
            break;
          }
          if (down && k == SDLK_ESCAPE && !ev.key.repeat) {
            if (g_menu) menu_escape(); else open_menu(true);
            break;
          }
          if (g_menu) {
            if (!down) break;
            if (k == SDLK_UP) menu_key(-1, 0, false);
            else if (k == SDLK_DOWN) menu_key(1, 0, false);
            else if (k == SDLK_LEFT) menu_key(0, -1, false);
            else if (k == SDLK_RIGHT) menu_key(0, 1, false);
            else if (k == SDLK_RETURN || k == SDLK_z || k == SDLK_SPACE) menu_key(0, 0, true);
            break;
          }
          int b = key_to_btn(k);
          if (!ev.key.repeat && start_opens_pause(b, down)) { open_menu(true); break; }
          if (!ev.key.repeat && title_menu_key(b, down)) break;
          if (b >= 0) set_btn(b, down);
          if (k == SDLK_TAB) fast = down;
          if (k == SDLK_BACKSPACE) g_rewinding = down;
          if (down && !ev.key.repeat) {
            if (k == SDLK_F5) save_state_file();
            if (k == SDLK_F7) load_state_file();
            if (k == SDLK_F8 && restart_available()) cont_load();
#ifndef SSW_LAUNCHER
            if (k == SDLK_F12) g_use_recomp ^= 1;
#endif
            if (k == SDLK_F1) cfg.dev ^= 1;
            if (k == SDLK_F2) level_select();
            if (k == SDLK_F3) stage_clear();
            if (k == SDLK_F4) { cfg.saber ^= 1; cfg_save(); }
            if (k == SDLK_F6) { cfg.hero ^= 1; cfg_save(); }
            update_title();
          }
          break;
        }
      }
    }
    if (!g_menu) {
      for (int i = 0; i < SDL_NumJoysticks(); i++) {   // left stick as d-pad
        SDL_GameController* gc = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (!gc) continue;
        int x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        static int lx, ly;
        int nx = x < -16000 ? -1 : x > 16000 ? 1 : 0, ny = y < -16000 ? -1 : y > 16000 ? 1 : 0;
        if (nx != lx) { set_btn(BTN_LEFT, nx < 0); set_btn(BTN_RIGHT, nx > 0); lx = nx; }
        if (ny != ly) { set_btn(BTN_UP, ny < 0); set_btn(BTN_DOWN, ny > 0); ly = ny; }
      }
      int frames = fast ? 4 : 1;
      bool rw_held = g_rewinding;
      for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_GameController* gc = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (gc && SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) rw_held = true;
      }
      if (cfg.rewind && rw_held && rewind_step()) frames = 0;   // stepped back: just redraw below
      bool redraw_only = cfg.rewind && rw_held;
      g_snes->ppu->m7hiresK = cfg.m7 == 2 ? 4 : cfg.m7 == 1 ? 2 : 0;
      g_snes->ppu->m7frameUsed = false;
      if (redraw_only && frames == 0) { uint16_t keep = g_btn_down; g_btn_down = 0; push_buttons(); g_btn_down = keep; if (HAS(GF_WIDESCREEN)) { extern int g_ws_spawn_wide, g_ws_objects; g_ws_spawn_wide = cfg.wide_spawn == 1; g_ws_objects = cfg.wide_spawn != 2; ws_update(wide_extra()); } runner_frame(g_snes); }
      if (redraw_only) frames = 0;   // holding rewind with nothing left: pause
      for (int i = 0; i < frames; i++) { if (cfg.rewind) rewind_record(); g_snes->ppu->noSpriteLimit = cfg.nolimit; { extern int g_dsp_interp; g_dsp_interp = cfg.interp; } push_buttons(); apply_dev_flags(); if (HAS(GF_RESTORED)) restore_update(cfg.restored); update_overclock(); steam_tick(); if (HAS(GF_WIDESCREEN)) { extern int g_ws_spawn_wide, g_ws_objects; g_ws_spawn_wide = cfg.wide_spawn == 1; g_ws_objects = cfg.wide_spawn != 2; ws_update(wide_extra()); } runner_frame(g_snes); after_frame_buttons(); autosave_tick(); if (HAS(GF_ACH)) ach_update(g_port_touched || cfg.dev || g_rewound); rumble_tick(); if (HAS(GF_RESTORED)) restore_tick(); trilogy_tick(); extras_frame(g_port_touched || cfg.dev || g_rewound); crashlog_frame(); if (g_test_crash && --g_test_crash == 0) *(volatile int*)0 = 1; }
      if (dev && !fast) {
        int n = SAMPLE_RATE / 60;
        snes_setSamples(g_snes, g_audio, n);
        for (int i = 0; i < n * 2; i++) g_audio[i] = (int16_t)(g_audio[i] * cfg.volume / 100);
        msu_mix(g_audio, n, SAMPLE_RATE, cfg.volume * cfg.music_vol / 100);
        if (SDL_GetQueuedAudioSize(dev) < (Uint32)(have.freq / 5 * 4)) SDL_QueueAudio(dev, g_audio, n * 4);
      }
    }
    toast_tick();
    if (g_report_timer > 0) g_report_timer--;
    // widescreen frames come from the PPU's wide buffer (the menu always uses the normal frame)
    int want_extra = wide_extra();
    bool hires = cfg.m7 && g_snes->ppu->m7hiresK && g_snes->ppu->m7frameUsed && !g_menu;
    bool wide = want_extra > 0 && !g_menu && !hires;
    SDL_Texture* t;
    SDL_Rect src = {0, 16, 512, 448};
    int ww, wh;
    SDL_GetRendererOutputSize(ren, &ww, &wh);
    double ar;
    int hires_e = 0;   // widescreen columns included in the hi-res Mode 7 frame
    if (hires) {
      int K = g_snes->ppu->m7hiresK;
      hires_e = want_extra > 0 ? g_snes->ppu->wideExtra : 0;
      ppu_renderMode7Hires(g_snes->ppu);
      int HW = (256 + 2 * hires_e) * K;
      color_apply(g_snes->ppu->m7hiresBuf, HW, HW, 224 * K);
      t = htex[cfg.smooth ? 1 : 0];
      SDL_Rect up = {0, 0, HW, 224 * K};
      SDL_UpdateTexture(t, &up, g_snes->ppu->m7hiresBuf, HW * 4);
      src = (SDL_Rect){0, 0, HW, 224 * K};
      if (want_extra > 0) ar = (256.0 + 2 * want_extra) * pixel_aspect() / 224.0;
      else ar = cfg.aspect == ASPECT_4_3 ? 4.0 / 3.0 : cfg.aspect == ASPECT_8_7 ? 256.0 / 224.0 : (double)ww / wh;
    } else if (wide) {
      int act = g_snes->ppu->wideExtra;    // 0 while in mode-7 / non-platform scenes: pillarbox
      uint32_t* wb = g_snes->ppu->wideBuffer;
      if (act < want_extra) {
        for (int y = 0; y < 240; y++) {
          for (int x = PPU_WIDE_MAX - want_extra; x < PPU_WIDE_MAX - act; x++) wb[y * WW + x] = 0;
          for (int x = PPU_WIDE_MAX + 256 + act; x < PPU_WIDE_MAX + 256 + want_extra; x++) wb[y * WW + x] = 0;
        }
      }
      t = wtex[cfg.smooth ? 1 : 0];
      title_credit(wb, WW, PPU_WIDE_MAX, 0, 1);
      color_apply(wb + PPU_WIDE_MAX - want_extra, WW, 256 + 2 * want_extra, 224);
      if (HAS(GF_RESTORED)) restore_draw_walls(wb, WW, PPU_WIDE_MAX, 0, 1, -act, 256 + act);
      SDL_UpdateTexture(t, NULL, wb, WW * 4);
      src = (SDL_Rect){PPU_WIDE_MAX - want_extra, 0, 256 + 2 * want_extra, 224};
      ar = (256.0 + 2 * want_extra) * pixel_aspect() / 224.0;
    } else {
      snes_setPixels(g_snes, g_pixels);
      title_credit((uint32_t*)g_pixels, 512, 0, 16, 2);
      if (!g_menu) color_apply((uint32_t*)g_pixels + 16 * 512, 512, 512, 448);
      if (HAS(GF_RESTORED)) restore_draw_walls((uint32_t*)g_pixels, 512, 0, 16, 2, 0, 256);
      if (g_menu) draw_menu();
      t = tex[cfg.smooth ? 1 : 0];
      SDL_UpdateTexture(t, NULL, g_pixels, 512 * 4);
      ar = cfg.aspect == ASPECT_4_3 ? 4.0 / 3.0 : cfg.aspect == ASPECT_8_7 ? 256.0 / 224.0 : (double)ww / wh;
    }
    int dw = ww, dh = (int)(ww / ar);
    if (dh > wh) { dh = wh; dw = (int)(wh * ar); }
    if (cfg.integer_scale && cfg.aspect != ASPECT_STRETCH) {
      int sc = dh / 224; if (sc < 1) sc = 1;
      dh = 224 * sc; dw = (int)(dh * ar);
    }
    SDL_Rect dst = {(ww - dw) / 2, (wh - dh) / 2, dw, dh};
    if (hires && want_extra > hires_e) {   // pillarbox inside the wide frame (e.g. the trench run)
      int cw = (int)((double)dw * (256 + 2 * hires_e) / (256 + 2 * want_extra));
      dst.x += (dw - cw) / 2; dst.w = cw;
    }
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, t, &src, &dst);
    crt_overlay(ren, dst);
    {   // HUD overlay (achievement pop-up, speedrun timer): crisp, on top of filters
      static SDL_Texture* ht;
      if (!ht) { ht = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 512, 224); SDL_SetTextureBlendMode(ht, SDL_BLENDMODE_BLEND); }
      int cw = (int)(224.0 * dst.w / dst.h + 0.5); if (cw > 512) cw = 512; if (cw < 64) cw = 64;
      if (ht && draw_hud(cw)) {
        SDL_Rect hs = {0, 0, cw, 224};
        SDL_UpdateTexture(ht, &hs, g_hud, 512 * 4);
        SDL_RenderCopy(ren, ht, &hs, &dst);
      }
    }
    SDL_RenderPresent(ren);
    next += (uint64_t)(frame_time * freq);
    uint64_t now = SDL_GetPerformanceCounter();
    if (now < next) SDL_Delay((Uint32)((next - now) * 1000 / freq));
    else if (now - next > freq / 4) next = now;
  }
  cfg_save();
  extras_shutdown();
  steam_shutdown();
  SDL_Quit();
  return 0;
}
