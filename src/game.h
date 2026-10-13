// The games of the trilogy. Everything in the shared front-end that depends on which cartridge is
// running reads it from here: RAM addresses, the scene/sequence table, the recompiled code, and which
// port features have been built for that game so far.
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "cpu.h"

enum {
  GF_WIDESCREEN = 1 << 0,   // widescreen level/object rebuild (widescreen.c)
  GF_RESTORED   = 1 << 1,   // restored cut stage (restore.c)
  GF_MSU        = 1 << 2,   // MSU-1 track mapping (msu.c)
  GF_ACH        = 1 << 3,   // achievement list (achievements.c)
  GF_TITLE_CRED = 1 << 4,   // "PC PORT BY T-MAN" on the title screen
};

typedef struct GameDef {
  const char* key;          // file prefix: ssw / esb (ssw.state, esb_times.txt, ...)
  const char* name;         // "Super Star Wars"
  const char* menu_name;    // in the game's font: "STAR WARS"
  const char* rom_file;     // expected file name next to the exe
  const char* rom_desc;     // for the "ROM not found" message
  const char* msu_folder;   // MSU-1 pack folder next to the exe
  uint32_t rom_size, crc;
  int (*recomp)(Cpu* cpu);
  const uint32_t* hooks;
  // WRAM
  uint16_t r_mode, r_scene, r_lives, r_hp, r_px, r_py;
  uint16_t r_objtype, r_objhp;      // 20 two-byte object slots, slot 0 = player (0 = unknown)
  uint16_t r_debug;         // word the game checks to enable controller-2 developer features
  uint16_t r_cheats;        // byte whose bits are set by the title-screen cheat codes
  uint8_t cheat_any;        // mask of r_cheats bits that count as "a code was entered"
  uint8_t cheat_a, cheat_b; // bits behind the two extras toggles (SSW: lightsaber / hero select)
  const char *cheat_a_name, *cheat_b_name;
  uint8_t code_lives;       // lives value only a cheat code produces at the title (0 = none)
  uint32_t seq_off;         // ROM offset of the scene sequence table (4-byte rows, $FF ends)
  uint8_t title_mode, title_scene;
  uint8_t start_mode, start_scene;   // first stage of a new game
  uint8_t end_mode, end_scene;       // the ending / credits: game beaten
  uint16_t font_vram, font_base;     // title-screen font: BG3 tile data (word address) and the tile that is
                                     // Super Star Wars' $180 (same typeface and glyph order in every game)
  // title screen and GAME OPTION MENU (BG3 text; the port adds PC SETTINGS and ACHIEVEMENTS to the option menu)
  uint16_t tm_cursor;                // WRAM word: the selected item (title menu and option menu)
  uint16_t tm_window[2];             // WRAM per-line window tables (2 bytes a line) that darken the boxes
  uint8_t play_modes[4];             // GameMode values that are gameplay (0 = unused)
  const uint8_t (*stages)[2];        // play order (mode, scene) for continue / level numbers
  const char* const* stage_names;
  int nstages;
  unsigned features;
  uint32_t bg3_shadow;               // WRAM copy of the BG3 tilemap (32x32 words) the game uploads to VRAM $0800
} GameDef;

extern const GameDef g_ssw, g_esb;
extern const GameDef* const g_all_games[];   // NULL-terminated, in trilogy order
extern const GameDef* g_game;                // the one running

const GameDef* game_identify(const uint8_t* rom, int len);   // NULL if not a supported dump
int game_is_play_mode(int mode);
static inline int game_mode(const uint8_t* ram) { return ram[g_game->r_mode] | ram[g_game->r_mode + 1] << 8; }
static inline int game_scene(const uint8_t* ram) { return ram[g_game->r_scene]; }
#endif
