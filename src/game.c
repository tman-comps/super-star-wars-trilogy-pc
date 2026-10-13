// Game definitions for the trilogy front-end (see game.h).
// Build with -DNO_ESB for a Super Star Wars-only program, -DNO_SSW for an Empire-only program.
#include <stddef.h>
#include "game.h"
#include "recomp_rt.h"
#ifdef SSW_LAUNCHER        // interpreter-only build: no recompiled code linked
#define RECOMP(f) NULL
#else
#define RECOMP(f) f
#endif

#ifndef NO_SSW
// Super Star Wars (USA) (Rev 1)
static const uint8_t k_ssw_stages[][2] = {   // as in the game's debug area list; 18 = restored stage
  {0x04,0x10},{0x0E,0x04},{0x04,0x0B},{0x04,0x01},{0x04,0x09},{0x04,0x0A},{0x0E,0x05},{0x04,0x0D},
  {0x04,0x0C},{0x04,0x0E},{0x04,0x00},{0x04,0x11},{0x04,0x12},{0x0E,0x14},{0x10,0x06},{0x08,0x08},{0x08,0x02},
  {0x04,0x13}};
// usual frames per game step in the Mode 7 stages that wait on the step flag, measured on the
// console's speed (the most common interval; the rest is slowdown). Others are measured while playing.
static const uint8_t k_ssw_paces[][3] = {{0x0E, 0x04, 3}, {0x0E, 0x05, 4}, {0x0E, 0x14, 3}, {0x10, 0x06, 2}, {0}};
const GameDef g_ssw = {
  "ssw", "Super Star Wars", "STAR WARS", "ssw.sfc", "Super Star Wars (USA) (Rev 1)", "msu", 0x100000, 0xBE47F6C6,
  RECOMP(recomp_run), g_hook_addrs,
  0x04DA, 0x0368, 0x08FB, 0x0A79, 0x0F51, 0x0FC9,
  0x09D9, 0x0A79,
  0x010C, 0x0110, 0xC0, 0x40, 0x80, "SABER START", "HERO SELECT", 0,
  0x1D4A, 0x0A, 0x20, 0x04, 0x10, 0x0A, 0x1C,
  0x3000, 0x180,
  0x050A, {0x2A00, 0x2C00},
  {0x04, 0x0E, 0x08, 0x10},
  k_ssw_stages, NULL, (int)(sizeof k_ssw_stages / 2),
  GF_WIDESCREEN | GF_RESTORED | GF_MSU | GF_ACH | GF_TITLE_CRED,
  0x9800, 0x005A, {0x55, 0x57, 0x58, 0x59, 0x5A}, 0x0997, k_ssw_paces,
};
#else
int recomp_run(Cpu* cpu) { (void)cpu; return RECOMP_MISS; }
const uint32_t g_hook_addrs[] = {0};
#endif
#if defined(SSW_LAUNCHER)
// launcher build: the interpreter calls the same hook points (keep in sync with analysis/esb/hooks.txt)
const uint32_t esb_g_hook_addrs[] = {0x80A559, 0x80A565, 0x80DF0A, 0};
#endif

#ifndef NO_ESB
// Super Empire Strikes Back (USA) (Rev 1)
int esb_recomp_run(Cpu* cpu);
extern const uint32_t esb_g_hook_addrs[];
static const uint8_t k_esb_stages[][2] = {   // play order, from the game's scene sequence table
  {0x04,0x01},{0x04,0x02},{0x04,0x06},{0x04,0x03},{0x04,0x05},{0x04,0x04},{0x04,0x09},{0x0E,0x0A},
  {0x04,0x15},{0x04,0x14},{0x04,0x07},{0x04,0x08},{0x12,0x00},{0x04,0x0C},{0x04,0x0D},{0x04,0x0E},
  {0x04,0x0F},{0x04,0x13},{0x04,0x12},{0x04,0x10},{0x0E,0x1A},{0x04,0x17},{0x04,0x18},{0x04,0x16},
  {0x12,0x02}};
static const char* const k_esb_names[] = {
  "HOTH", "TAUNTAUN RIDE", "ICE FIELDS", "ICE CAVE ENTRANCE", "WAMPA CAVE", "ICE CAVE EXIT", "ECHO BASE",
  "SNOWSPEEDER", "BATTLE OF HOTH", "ECHO BASE INTERIOR", "ECHO BASE ESCAPE", "HANGAR", "ASTEROID FIELD",
  "DAGOBAH", "DARK CAVE", "DAGOBAH SWAMP", "BESPIN", "BESPIN TUNNELS", "CLOUD CITY", "CLOUD CITY CHEWIE",
  "CLOUD CAR ATTACK", "CARBON FREEZING", "REACTOR SHAFT", "VADER", "ESCAPE"};
static const uint8_t k_esb_paces[][3] = {{0x0E, 0x0A, 4}, {0x0E, 0x1A, 5}, {0}};
const GameDef g_esb = {
  "esb", "Super Empire Strikes Back", "EMPIRE STRIKES BACK", "sesb.sfc", "Super Empire Strikes Back (USA) (Rev 1)", "msu-esb",
  0x180000, 0x1E7EA62C,
  RECOMP(esb_recomp_run), esb_g_hook_addrs,
  0x01B6, 0x01B8, 0x01AC, 0x0CF6, 0x11CE, 0x1246,
  0, 0x0CF6,
  0x010C, 0x0110, 0x60, 0x40, 0x20, "FORCE POWERS", "ENDLESS BOMBS", 99,
  0x0D4E, 0x0A, 0x1B, 0x04, 0x01, 0x0A, 0x1D,
  0x4000, 0x12A,
  0x07BE, {0x2400, 0x25C0},
  {0x04, 0x0E, 0x12, 0},
  k_esb_stages, k_esb_names, (int)(sizeof k_esb_stages / 2),
  GF_TITLE_CRED | GF_ACH | GF_MSU | GF_WIDESCREEN,
  0x13400, 0x006E, {0x54, 0x56, 0x57, 0x58, 0x59}, 0x0BB3, k_esb_paces,
};
#endif

const GameDef* const g_all_games[] = {
#ifndef NO_SSW
  &g_ssw,
#endif
#ifndef NO_ESB
  &g_esb,
#endif
  NULL};
#ifndef NO_SSW
const GameDef* g_game = &g_ssw;
#else
const GameDef* g_game = &g_esb;
#endif

static uint32_t crc32(const uint8_t* d, int n) {
  uint32_t c = 0xFFFFFFFF;
  for (int i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); }
  return ~c;
}
// Exactly the supported dumps: everything (recompiled code, data patches, level readers) assumes them byte for byte.
const GameDef* game_identify(const uint8_t* d, int len) {
  if (!d) return NULL;
  if ((len & 0x3ff) == 0x200) { d += 0x200; len -= 0x200; }   // copier header
  uint32_t c = 0; int have = 0;
  for (int i = 0; g_all_games[i]; i++) {
    if ((uint32_t)len != g_all_games[i]->rom_size) continue;
    if (!have) { c = crc32(d, len); have = 1; }
    if (c == g_all_games[i]->crc) return g_all_games[i];
  }
  return NULL;
}
int game_is_play_mode(int mode) {
  for (int i = 0; i < 4; i++) if (g_game->play_modes[i] && g_game->play_modes[i] == mode) return 1;
  return 0;
}
