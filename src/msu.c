// MSU-1 style music replacement.
//
// The game talks to its sound CPU through ports $2140-$2143: port 3 = command, ports 0/1 =
// argument, port 2 = sequence counter (sender at $80:9977). Relevant commands:
//   0E nn   load music module nn          06 tt   play track tt of the loaded module
//   10 vvss volume/fade (vv = target)      1A / 1C stop / reset music
// A song is identified by (module, track). When the player has a replacement PCM for it, we stream
// that instead and rewrite the play command to 1A, which silences the SPC music while its sound
// effects keep working (verified: idle RMS 3379 -> 394, effects unaffected).
//
// Files: <exe dir>/msu/msu.txt maps songs to track numbers, and the PCM for track N is any file in
// msu/ whose name ends in "-N.pcm" (standard MSU-1 PCM: "MSU1", u32 loop point in samples, then
// 44.1 kHz 16-bit stereo little-endian). Lines:
//   songvar AAAA        WRAM address holding the game's own song number when 06 is sent
//   song SS N [once]    game song SS -> track N (what the cartridge MSU-1 patches key on)
//   MM TT N [once]      module MM, track TT -> track N (checked first)
// "once" plays the track a single time instead of looping (jingles), like MSU-1's no-repeat mode.
// "finish" also plays once, and holds the next song back until it ends (the cartridge patches make
// the game wait for the stage-start fanfare; here only the music waits).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "snes.h"
#include "msu.h"
#include "game.h"

extern uint8_t (*g_snes_write_filter)(Snes*, uint32_t, uint8_t);

static char g_dir[1024];
static int g_map[256][256];          // (module, track) -> MSU track number | MSU_ONCE, 0 = none
static int g_song[256];              // "song SS N" lines: game song number -> MSU track
static int g_songvar = -1;           // "songvar AAAA": WRAM address of the game's song number
#define MSU_ONCE 0x10000             // play once instead of looping (jingles, title)
#define MSU_FINISH 0x20000           // play once and finish before the next song (stage-start fanfare)
static uint8_t g_logged[256][256];
static uint8_t g_port[4];
static int g_module = -1;
static int g_enabled;
static FILE* g_pcm;
static uint32_t g_loop, g_data_len;  // g_loop = g_data_len for play-once tracks  // in samples (stereo frames)
static uint32_t g_pos;               // current sample
static double g_frac;
static int g_vol = 255, g_vol_target = 255, g_vol_step = 0;
static int g_current = 0;
static int g_finish;                 // current track must finish before the next song starts
static int g_queued;                 // song waiting for it (map value), 0 = none
static int g_sn, g_si;            // buffered source frames / read index

static void stop(void) { if (g_pcm) fclose(g_pcm); g_pcm = NULL; g_current = 0; g_finish = 0; g_queued = 0; }

static FILE* open_track(int n, uint32_t* loop, uint32_t* len) {
  DIR* d = opendir(g_dir);
  if (!d) return NULL;
  char suffix[32]; snprintf(suffix, sizeof suffix, "-%d.pcm", n);
  struct dirent* e; FILE* f = NULL;
  while ((e = readdir(d))) {
    size_t L = strlen(e->d_name), S = strlen(suffix);
    if (L > S && !strcmp(e->d_name + L - S, suffix)) {
      char p[2048]; snprintf(p, sizeof p, "%s/%s", g_dir, e->d_name);
      f = fopen(p, "rb");
      if (f) break;
    }
  }
  closedir(d);
  if (!f) return NULL;
  char hdr[4]; uint8_t lp[4];
  if (fread(hdr, 1, 4, f) != 4 || memcmp(hdr, "MSU1", 4) || fread(lp, 1, 4, f) != 4) { fclose(f); return NULL; }
  *loop = lp[0] | lp[1] << 8 | lp[2] << 16 | (uint32_t)lp[3] << 24;
  fseek(f, 0, SEEK_END); long sz = ftell(f);
  *len = (uint32_t)((sz - 8) / 4);
  fseek(f, 8, SEEK_SET);
  return f;
}

static int play(int v) {
  int n = v & 0xffff;
  if (n == g_current && g_pcm) return 1;
  if (g_pcm && g_finish) { uint32_t a, b; FILE* f = open_track(n, &a, &b); if (!f) return 0; fclose(f); g_queued = v; return 1; }
  stop();
  g_pcm = open_track(n, &g_loop, &g_data_len);
  if (!g_pcm) return 0;
  if (v & (MSU_ONCE | MSU_FINISH)) g_loop = g_data_len;
  g_finish = !!(v & MSU_FINISH);
  g_current = n; g_pos = 0; g_frac = 0; g_sn = g_si = 0;
  g_vol = g_vol_target = 255;
  return 1;
}

static uint8_t filter(Snes* s, uint32_t adr, uint8_t v) {
  if (((adr >> 16) & 0x7f) >= 0x40) return v;
  uint32_t a = adr & 0xffff;
  if (a < 0x2140 || a > 0x2143) return v;
  g_port[a - 0x2140] = v;
  // A command is a 16-bit store of (counter, command) to $2142/$2143. Bulk uploads also write data
  // bytes to $2143, but never straight after $2142 -- so only that order counts as a command.
  static uint32_t last = 0;
  uint32_t prev = last; last = a;
  if (a != 0x2143 || prev != 0x2142 || !g_enabled) return v;
  int arg = g_port[0] | g_port[1] << 8;
  switch (v) {
    case 0x0E: g_module = arg & 0xff; break;
    case 0x06: {
      int n = g_module >= 0 ? g_map[g_module][arg & 0xff] : 0;
      if (!n && g_songvar >= 0) n = g_song[s->ram[g_songvar]];
      if (getenv("SSW_MSU_LOG")) fprintf(stderr, "msu: module %02X track %02X song %02X -> %d%s\n", g_module & 0xff, arg & 0xff,
                                         g_songvar >= 0 ? s->ram[g_songvar] : 0xff, n & 0xffff, n & MSU_FINISH ? " finish" : n & MSU_ONCE ? " once" : "");
      if (n && play(n)) return 0x1A;          // ours plays; keep the SPC music silent
      if (!n && g_module >= 0 && !g_logged[g_module][arg & 0xff]) {   // help complete the mapping
        g_logged[g_module][arg & 0xff] = 1;
        char p[1100]; snprintf(p, sizeof p, "%s/unmapped.txt", g_dir);
        FILE* f = fopen(p, "a");
        if (f) {
          if (g_songvar >= 0) fprintf(f, "song %02X  ?   # mode %02X scene %02X\n", s->ram[g_songvar], s->ram[g_game->r_mode], s->ram[g_game->r_scene]);
          else fprintf(f, "%02X %02X  ?   # mode %02X scene %02X\n", g_module, arg & 0xff, s->ram[g_game->r_mode], s->ram[g_game->r_scene]);
          fclose(f);
        }
      }
      stop();
      break;
    }
    case 0x10: {                                // fade/volume: high byte = target volume
      int target = (arg >> 8) & 0x7f;
      g_vol_target = target * 255 / 0x7f;
      g_vol_step = 8;
      break;
    }
    case 0x1A: case 0x1C: if (!g_finish) stop(); break;   // the game stops the fanfare right before the next song
  }
  return v;
}

int msu_init(const char* base_dir, const char* folder) {
  stop(); g_module = -1;
  memset(g_logged, 0, sizeof g_logged);
  snprintf(g_dir, sizeof g_dir, "%s%s", base_dir, folder);
  char p[1100]; snprintf(p, sizeof p, "%s/msu.txt", g_dir);
  FILE* f = fopen(p, "r");
  memset(g_map, 0, sizeof g_map);
  memset(g_song, 0, sizeof g_song);
  g_songvar = -1;
  if (f) {
    char line[256];
    while (fgets(line, sizeof line, f)) {
      char ms[8], once[8] = ""; unsigned m, t; int n;
      if (line[0] == '#') continue;
      if (sscanf(line, "songvar %x", &m) == 1) { if (m < 0x20000) g_songvar = (int)m; continue; }
      if (sscanf(line, "%7s %x %d %7s", ms, &t, &n, once) < 3 || t > 255 || n < 0 || n > 0xffff) continue;
      int v = n | (!strcmp(once, "once") ? MSU_ONCE : !strcmp(once, "finish") ? MSU_FINISH : 0);
      if (!strcmp(ms, "song")) g_song[t] = v;
      else if (sscanf(ms, "%x", &m) == 1 && m < 256) g_map[m][t] = v;
    }
    fclose(f);
  }
  g_snes_write_filter = filter;
  // count mapped tracks that actually have a PCM file
  int have = 0;
  for (int i = 0; i < 256 * 257; i++) {
    int v = i < 256 * 256 ? g_map[i >> 8][i & 0xff] : g_song[i - 256 * 256];
    if (!v) continue;
    uint32_t a, b; FILE* f = open_track(v & 0xffff, &a, &b);
    if (f) { fclose(f); have++; }
  }
  return have;
}

void msu_enable(int on) { g_enabled = on; if (!on) stop(); }
int msu_playing(void) { return g_pcm != NULL; }

// buffered source reader with looping
static int16_t g_sbuf[8192 * 2];
static int refill(void) {
  g_sn = g_si = 0;
  while (g_sn < 8192) {
    if (g_pos >= g_data_len) {
      if (g_loop >= g_data_len) break;
      g_pos = g_loop;
      fseek(g_pcm, 8 + (long)g_pos * 4, SEEK_SET);
    }
    uint32_t want = 8192 - g_sn;
    if (want > g_data_len - g_pos) want = g_data_len - g_pos;
    size_t got = fread(&g_sbuf[g_sn * 2], 4, want, g_pcm);
    if (!got) break;
    g_sn += (int)got; g_pos += (uint32_t)got;
  }
  return g_sn > 0;
}

// Mix n stereo frames at out_rate into out (on top of the SPC sound effects).
static int track_ended(void) {           // returns 1 if a queued song took over
  int q = g_queued;
  g_finish = 0; g_queued = 0; stop();
  if (q && getenv("SSW_MSU_LOG")) fprintf(stderr, "msu: fanfare done, starting %d\n", q & 0xffff);
  return q && play(q) && refill();
}

void msu_mix(int16_t* out, int n, int out_rate, int volume_pct) {
  if (!g_pcm) return;
  double step = 44100.0 / out_rate;
  for (int i = 0; i < n; i++) {
    if (g_si >= g_sn && !refill() && !track_ended()) return;
    if (g_vol != g_vol_target) {
      if (g_vol < g_vol_target) g_vol = g_vol + g_vol_step > g_vol_target ? g_vol_target : g_vol + g_vol_step;
      else g_vol = g_vol - g_vol_step < g_vol_target ? g_vol_target : g_vol - g_vol_step;
    }
    int gain = g_vol * volume_pct;   // 0..25500
    int L = out[2 * i] + g_sbuf[g_si * 2] * gain / 25500;
    int R = out[2 * i + 1] + g_sbuf[g_si * 2 + 1] * gain / 25500;
    out[2 * i] = (int16_t)(L > 32767 ? 32767 : L < -32768 ? -32768 : L);
    out[2 * i + 1] = (int16_t)(R > 32767 ? 32767 : R < -32768 ? -32768 : R);
    g_frac += step;
    while (g_frac >= 1.0) { g_frac -= 1.0; g_si++; if (g_si >= g_sn && !refill() && !track_ended()) return; }
  }
}

int msu_current_track(void) { return g_pcm ? g_current : 0; }
