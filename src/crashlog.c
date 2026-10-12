// Crash reports and a running log, so testers can send back what happened.
//   ssw_log.txt         rewritten at every start; one line per notable event (stage changes, settings,
//                       state loads, rewinds...). Flushed as it goes.
//   ssw_crash.txt/.state  written by the crash handler: exception, where, last stage, recent events,
//                       and a save state from just before the crash (the last good snapshot).
//   ssw_report.txt/.state written on demand from the menu ("Report a problem").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include "snes.h"
#include "crashlog.h"
#include "game.h"
#ifdef _WIN32
#include <windows.h>
#endif

#define NEV 64
static char g_ev[NEV][160];
static int g_ev_n;
static FILE* g_log;
static char g_dir[1024];
static Snes* s;
static const char* (*g_settings)(void);
static uint8_t* g_snap;          // last good save state (taken once a second)
static int g_snap_len;
static unsigned long g_frame;

void crashlog_event(const char* fmt, ...) {
  char line[160];
  va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
  char stamped[180];
  snprintf(stamped, sizeof stamped, "[%lu] %s", g_frame, line);
  strncpy(g_ev[g_ev_n % NEV], stamped, sizeof g_ev[0] - 1);
  g_ev[g_ev_n % NEV][sizeof g_ev[0] - 1] = 0;
  g_ev_n++;
  if (g_log) { fputs(stamped, g_log); fputc('\n', g_log); fflush(g_log); }
}

static void write_report(const char* base, const char* what) {
  char p[1200];
  snprintf(p, sizeof p, "%s%s.txt", g_dir, base);
  FILE* f = fopen(p, "w");
  if (f) {
    time_t t = time(NULL);
    const GameDef* g = g_game;
    fprintf(f, "Star Wars trilogy PC port - %s\nbuild: %s\ngame: %s\ntime: %s", what, CRASHLOG_BUILD, g->name, ctime(&t));
    if (s) fprintf(f, "frame %lu, mode %02X scene %02X, player x %04X y %04X, HP %d, lives %d\n", g_frame,
                   s->ram[g->r_mode], s->ram[g->r_scene], s->ram[g->r_px] | s->ram[g->r_px + 1] << 8,
                   s->ram[g->r_py] | s->ram[g->r_py + 1] << 8, s->ram[g->r_hp], s->ram[g->r_lives]);
    if (g_settings) fprintf(f, "settings: %s\n", g_settings());
    fprintf(f, "\nrecent events (oldest first):\n");
    int start = g_ev_n > NEV ? g_ev_n - NEV : 0;
    for (int i = start; i < g_ev_n; i++) fprintf(f, "  %s\n", g_ev[i % NEV]);
    fclose(f);
  }
  if (g_snap && g_snap_len) {
    snprintf(p, sizeof p, "%s%s.state", g_dir, base);
    f = fopen(p, "wb");
    if (f) { fwrite(g_snap, 1, g_snap_len, f); fclose(f); }
  }
}

#ifdef _WIN32
static LONG WINAPI on_crash(EXCEPTION_POINTERS* e) {
  char what[200];
  HMODULE mod = GetModuleHandleA(NULL);
  snprintf(what, sizeof what, "CRASH: exception %08lX at offset %08llX in ssw.exe",
           (unsigned long)e->ExceptionRecord->ExceptionCode,
           (unsigned long long)((uintptr_t)e->ExceptionRecord->ExceptionAddress - (uintptr_t)mod));
  crashlog_event("%s", what);
  write_report("ssw_crash", what);
  MessageBoxA(NULL, "The game crashed. Sorry!\n\nssw_crash.txt and ssw_crash.state were saved next to ssw.exe.\n"
                    "Please send both files so the bug can be fixed.", "Star Wars", MB_OK | MB_ICONERROR);
  return EXCEPTION_EXECUTE_HANDLER;
}
#else
static void on_signal(int sig) {
  char what[64]; snprintf(what, sizeof what, "CRASH: signal %d", sig);
  crashlog_event("%s", what);
  write_report("ssw_crash", what);
  signal(sig, SIG_DFL); raise(sig);
}
#endif

void crashlog_set_snes(Snes* snes) { s = snes; crashlog_event("game: %s", g_game->name); }
void crashlog_init(Snes* snes, const char* dir, const char* (*settings)(void)) {
  s = snes; g_settings = settings;
  snprintf(g_dir, sizeof g_dir, "%s", dir);
  char p[1200]; snprintf(p, sizeof p, "%sssw_log.txt", g_dir);
  g_log = fopen(p, "w");
  g_snap = malloc(4 * 1024 * 1024);
#ifdef _WIN32
  SetUnhandledExceptionFilter(on_crash);
#else
  signal(SIGSEGV, on_signal); signal(SIGABRT, on_signal); signal(SIGFPE, on_signal); signal(SIGILL, on_signal);
#endif
  time_t t = time(NULL);
  char ts[64]; snprintf(ts, sizeof ts, "%s", ctime(&t)); ts[strcspn(ts, "\n")] = 0;
  crashlog_event("start, build %s, %s", CRASHLOG_BUILD, ts);
  if (settings) crashlog_event("settings: %s", settings());
}

void crashlog_frame(void) {
  g_frame++;
  if (!s) return;
  static int pm = -1, ps = -1;
  int m = s->ram[g_game->r_mode], sc = s->ram[g_game->r_scene];
  if (m != pm || sc != ps) { crashlog_event("%s: now in mode %02X scene %02X", g_game->key, m, sc); pm = m; ps = sc; }
  if (g_frame % 60 == 0) g_snap_len = snes_saveState(s, g_snap);
}

void crashlog_report(void) {
  crashlog_event("report requested from the menu");
  if (s) g_snap_len = snes_saveState(s, g_snap);
  write_report("ssw_report", "REPORT (saved from the menu)");
}
