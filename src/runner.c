// Frame loop: runs the game on the recompiled CPU (or the reference interpreter) on top of
// LakeSnes's PPU/APU/DMA, with identical timing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "recomp_rt.h"
#include "runner.h"

static uint64_t g_misses = 0;
static uint32_t g_miss_log[4096];
static int g_miss_n = 0;
int g_hook_enable = 0;
static void hook_none(Cpu* cpu, uint32_t adr) { (void)cpu; (void)adr; }
void (*g_recomp_hook)(Cpu* cpu, uint32_t adr) = hook_none;
// the recompiled code of the game being run (set by the frontend when it picks a game)
int (*g_recomp_fn)(Cpu* cpu) = NULL;
const uint32_t* g_hook_list = NULL;
#ifdef SSW_LAUNCHER
const uint32_t g_hook_addrs[] = {0x80B692, 0x80B69E, 0x80E6BF, 0};   // keep in sync with analysis/ssw/hooks.txt
// Launcher build: contains none of the code generated from the game. Everything runs on the
// interpreter from the player's own ROM; recomp_run is never reached.
int g_use_recomp = 0;
int recomp_run(Cpu* cpu) { (void)cpu; return RECOMP_MISS; }
#else
int g_use_recomp = 1;
#endif
int g_recomp_single = 0;
// optional coverage logging (interpreter mode only): same format as the snes9x logger
uint8_t* g_cdl = NULL;          // 16 MB, indexed by 24-bit address
uint32_t* g_fetch_log = NULL;   // pairs of (vm, address) for script opcode fetches
uint32_t g_fetch_n = 0, g_fetch_cap = 0;

static void note_miss(Cpu* cpu) {
  uint32_t a = ((uint32_t)cpu->k << 16) | cpu->pc;
  g_misses++;
  for (int i = 0; i < g_miss_n; i++) if (g_miss_log[i] == a) return;
  if (g_miss_n < 4096) g_miss_log[g_miss_n++] = a;
}

void runner_step(Snes* snes) {
  Cpu* cpu = snes->cpu;
  if (!g_use_recomp || cpu->resetWanted || cpu->stopped || cpu->waiting || cpu->intWanted) {
    if (g_cdl && !cpu->resetWanted && !cpu->stopped && !cpu->waiting && !cpu->intWanted) {
      uint32_t a = ((uint32_t)cpu->k << 16) | cpu->pc;
      g_cdl[a] |= (cpu->e ? 0x10 : 0) | (cpu->mf ? 1 : 2) | (cpu->xf ? 4 : 8);
      uint32_t a7 = a & 0x7fffff;
      if ((a7 == 0x00D421 || a7 == 0x018FEC) && g_fetch_n + 2 <= g_fetch_cap) {
        uint16_t d = cpu->dp + 0x4B;
        uint32_t ptr = snes->ram[d] | (snes->ram[(d + 1) & 0xffff] << 8) | (snes->ram[(d + 2) & 0xffff] << 16);
        uint32_t y = cpu->xf ? (cpu->y & 0xff) : cpu->y;
        g_fetch_log[g_fetch_n++] = a7 == 0x00D421 ? 0 : 1;
        g_fetch_log[g_fetch_n++] = (ptr + y) & 0xffffff;
      }
    }
    if (g_hook_enable) {
      uint32_t a = (((uint32_t)cpu->k << 16) | cpu->pc) | 0x800000;
      for (const uint32_t* h = g_hook_list ? g_hook_list : g_hook_addrs; *h; h++) if (*h == a) { g_recomp_hook(cpu, a); break; }
    }
    cpu_runOpcode(cpu);
    return;
  }
  g_snes_event = 0;
  int r = g_recomp_fn ? g_recomp_fn(cpu) : recomp_run(cpu);
  if (r == RECOMP_MISS) {
    // a JML written into WRAM by the game (Empire's NMI trampoline at $0100) is expected, not a miss
    int ramjml = (cpu->k == 0 || cpu->k == 0x80) && cpu->pc < 0x2000 && snes->ram[cpu->pc] == 0x5C;
    if (!ramjml) note_miss(cpu);
    cpu_runOpcode(cpu);   // hook points are all in traced code, so misses never land on one
  }
}

// mirrors snes_runFrame()
void (*g_runner_after_vblank)(void) = NULL;   // front-end: last-moment VRAM/WRAM changes before drawing
void runner_frame(Snes* snes) {
  while (snes->inVblank) runner_step(snes);
  if (g_runner_after_vblank) g_runner_after_vblank();
  uint32_t frame = snes->frames;
  while (!snes->inVblank && frame == snes->frames) runner_step(snes);
  extern void snes_catchupApuPublic(Snes* snes);
  snes_catchupApuPublic(snes);
}

uint64_t runner_misses(void) { return g_misses; }
int runner_miss_list(uint32_t* out, int max) {
  int n = g_miss_n < max ? g_miss_n : max;
  memcpy(out, g_miss_log, n * sizeof(uint32_t));
  return n;
}

uint8_t* runner_load_file(const char* path, int* len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *len = (int)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* d = malloc(*len);
  if (fread(d, 1, *len, f) != (size_t)*len) { fclose(f); free(d); return NULL; }
  fclose(f);
  return d;
}

// SHA-1 of the dump this port is built against
static uint32_t crc32(const uint8_t* d, int n) {
  uint32_t c = 0xFFFFFFFF;
  for (int i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); }
  return ~c;
}
// Exactly Super Star Wars (USA) (Rev 1): everything in the port (recompiled code, data patches,
// widescreen level reader) assumes this ROM byte for byte.
int runner_check_rom(const uint8_t* d, int len) {
  if (len == 0x100200) { d += 0x200; len -= 0x200; }  // copier header
  if (len != 0x100000) return 0;
  return crc32(d, len) == 0xBE47F6C6;
}
