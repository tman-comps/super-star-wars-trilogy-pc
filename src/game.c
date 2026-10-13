// Game definitions for the trilogy front-end (see game.h).
// Single-game programs leave the others out: -DNO_SSW, -DNO_ESB, -DNO_ROTJ (see the Makefile).
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
  0x0800, 2, 1 << 3,
};
#else
int recomp_run(Cpu* cpu) { (void)cpu; return RECOMP_MISS; }
const uint32_t g_hook_addrs[] = {0};
#endif
#if defined(SSW_LAUNCHER)
// launcher build: the interpreter calls the same hook points (keep in sync with analysis/esb/hooks.txt)
const uint32_t esb_g_hook_addrs[] = {0x80A559, 0x80A565, 0x80DF0A, 0};
const uint32_t rotj_g_hook_addrs[] = {0};   // keep in sync with analysis/rotj/hooks.txt
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
  0x0800, 2, 1 << 3,
};
#endif

#ifndef NO_ROTJ
// Super Return of the Jedi (USA) (Rev 1): same Sculptured engine, moved around in RAM.
// Recompiled code, scene table, stage list, menus, MSU-1 music, achievements. No widescreen yet.
int rotj_recomp_run(Cpu* cpu);
extern const uint32_t rotj_g_hook_addrs[];
static const uint8_t k_rotj_stages[][2] = {   // play order, from the game's scene sequence table
  {0x14,0x12},{0x04,0x02},{0x04,0x07},{0x04,0x01},{0x04,0x08},{0x04,0x05},{0x04,0x06},{0x12,0x11},
  {0x04,0x0A},{0x04,0x0B},{0x04,0x03},{0x0C,0x10},{0x04,0x09},{0x04,0x0F},{0x14,0x13},{0x04,0x0E},
  {0x04,0x0C},{0x04,0x0D},{0x0A,0x01},{0x0A,0x02}};
static const char* const k_rotj_names[] = {   // the hero select cards' AREA names (the vehicle stages have none)
  "LANDSPEEDER", "TATOOINE", "JABBA'S DANCE HALL", "JABBA'S PALACE", "RANCOR PIT", "SAIL BARGE ATTACK",
  "INSIDE SAIL BARGE", "SPEEDER BIKES", "EWOK VILLAGE A", "EWOK VILLAGE B", "ENDOR", "SPACE BATTLE",
  "POWER GENERATOR", "INSIDE DEATH STAR", "DEATH STAR SURFACE", "TOWER", "TOWER ENTRANCE", "EMPEROR'S CHAMBER",
  "DEATH STAR TUNNEL", "REACTOR ESCAPE"};
const GameDef g_rotj = {
  "srotj", "Super Return of the Jedi", "RETURN OF THE JEDI", "srotj.sfc", "Super Return of the Jedi (USA) (Rev 1)", "msu-rotj",
  0x200000, 0x4A5F30D8,
  RECOMP(rotj_recomp_run), rotj_g_hook_addrs,
  0x01BE, 0x01C0, 0x01B2, 0x0C58, 0x1130, 0x11A8,   // Empire's 01B6 01B8 01AC 0CF6 11CE 1246
  0x0BB8, 0x0C58,                                    // object types (slot 0 = the hero) and HP
  0x0110, 0x0114, 0x60, 0x40, 0x20, "FORCE POWERS", "ENDLESS BOMBS", 99,
  0x0627, 0x06, 0x15, 0x14, 0x12, 0x06, 0x17,
  0x4000, 0x100,
  0x0731, {0x2100, 0},       // option-screen cursor, window 1 table (indirect HDMA, 224 lines)
  {0x04, 0x0C, 0x12, 0x14, 0x0A},
  k_rotj_stages, k_rotj_names, (int)(sizeof k_rotj_stages / 2),
  GF_TITLE_CRED | GF_MSU | GF_ACH,
  // BG3 tilemap copy at $7F:1440 (uploaded to VRAM $1000, not $0800 as in the others), INIDISP shadow
  // $7B (reads 8 in the pause loop), box pieces as in Empire (side $156, corner $157, edge $158, font $100),
  // step flag $0B59 (Empire's $0BB3), paces measured while playing; the stage skip is controller 1
  // B+Start ($80:8C31), not controller 2 Start
  0x11440, 0x007B, {0x54, 0x56, 0x57, 0x58, 0x59}, 0x0B59, NULL,
  0x1000, 1, (1 << 0) | (1 << 3),
};
#endif

const GameDef* const g_all_games[] = {
#ifndef NO_SSW
  &g_ssw,
#endif
#ifndef NO_ESB
  &g_esb,
#endif
#ifndef NO_ROTJ
  &g_rotj,
#endif
  NULL};
#if !defined(NO_SSW)
const GameDef* g_game = &g_ssw;
#elif !defined(NO_ESB)
const GameDef* g_game = &g_esb;
#else
const GameDef* g_game = &g_rotj;
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
  for (int i = 0; i < 6; i++) if (g_game->play_modes[i] && g_game->play_modes[i] == mode) return 1;
  return 0;
}
