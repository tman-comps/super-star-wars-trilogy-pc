// Optional Steam achievements. Nothing here links against Steam: if steam_api64.dll (Windows) or
// libsteam_api.so (Linux / Steam Deck) sits next to the program and Steam is running with an app
// ID (launched from Steam, or steam_appid.txt next to the program), achievements are mirrored to
// Steam through the Steamworks flat C API. Otherwise everything stays local.
//
// Steam achievement API names are ACH_00 .. ACH_15 (Super Star Wars, src/achievements.c),
// ACH_16 .. ACH_31 (Super Empire Strikes Back, src/achievements_esb.c) and ACH_32 .. ACH_47 (Super Return of
// the Jedi, src/achievements_rotj.c)
// (see steam/achievements.txt for names and descriptions to enter in Steamworks).
#include <stdio.h>
#include <string.h>
#include "steam.h"
#ifdef _WIN32
#include <windows.h>
#define LIBNAME "steam_api64.dll"
static void* lib_open(const char* p) { return (void*)LoadLibraryA(p); }
static void* lib_sym(void* l, const char* n) { return (void*)GetProcAddress((HMODULE)l, n); }
#else
#include <dlfcn.h>
#define LIBNAME "libsteam_api.so"
static void* lib_open(const char* p) { return dlopen(p, RTLD_NOW); }
static void* lib_sym(void* l, const char* n) { return dlsym(l, n); }
#endif

typedef int (*fn_init_flat)(char* err);   // SteamAPI_InitFlat(SteamErrMsg*), SDK 1.58+: 0 = OK
typedef int (*fn_init)(void);             // SteamAPI_Init(), older SDKs: true = OK
typedef void* (*fn_get)(void);
typedef int (*fn_set)(void*, const char*);
typedef int (*fn_store)(void*);
typedef int (*fn_req)(void*);
typedef void (*fn_void)(void);

static void* g_stats;
static fn_set p_set;
static fn_store p_store;
static fn_void p_run, p_shutdown;
static int g_active, g_dirty;

int steam_init(const char* base_dir) {
  char path[1200];
  snprintf(path, sizeof path, "%s%s", base_dir, LIBNAME);
  void* l = lib_open(path);
  if (!l) l = lib_open(LIBNAME);
  if (!l) return 0;
  fn_init_flat init_flat = (fn_init_flat)lib_sym(l, "SteamAPI_InitFlat");
  fn_init init = (fn_init)lib_sym(l, "SteamAPI_Init");
  int ok = 0;
  if (init_flat) { char err[1024] = {0}; ok = init_flat(err) == 0; if (!ok) fprintf(stderr, "steam: %s\n", err); }
  else if (init) ok = init() != 0;
  if (!ok) return 0;
  static const char* getters[] = {"SteamAPI_SteamUserStats_v013", "SteamAPI_SteamUserStats_v012",
                                  "SteamAPI_SteamUserStats_v011", "SteamAPI_SteamUserStats"};
  for (unsigned i = 0; i < sizeof getters / sizeof getters[0] && !g_stats; i++) {
    fn_get g = (fn_get)lib_sym(l, getters[i]);
    if (g) g_stats = g();
  }
  p_set = (fn_set)lib_sym(l, "SteamAPI_ISteamUserStats_SetAchievement");
  p_store = (fn_store)lib_sym(l, "SteamAPI_ISteamUserStats_StoreStats");
  p_run = (fn_void)lib_sym(l, "SteamAPI_RunCallbacks");
  p_shutdown = (fn_void)lib_sym(l, "SteamAPI_Shutdown");
  fn_req req = (fn_req)lib_sym(l, "SteamAPI_ISteamUserStats_RequestCurrentStats");   // gone in newer SDKs
  if (!g_stats || !p_set || !p_store) return 0;
  if (req) req(g_stats);
  g_active = 1;
  return 1;
}

void steam_unlock(int id) {
  if (!g_active) return;
  char name[16]; snprintf(name, sizeof name, "ACH_%02d", id);
  p_set(g_stats, name);
  g_dirty = 1;
}

void steam_tick(void) {
  if (!g_active) return;
  if (p_run) p_run();
  if (g_dirty && p_store(g_stats)) g_dirty = 0;
}

void steam_shutdown(void) { if (g_active && p_shutdown) p_shutdown(); g_active = 0; }
int steam_active(void) { return g_active; }
