// Runtime interface shared by the generated code and the frame loop.
#ifndef RECOMP_RT_H
#define RECOMP_RT_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

enum {
  RECOMP_EVENT = 0,       // vblank/frame/interrupt/wait state changed: return to the frame loop
  RECOMP_MISS = 1,        // PC is not recompiled code: interpret one instruction
  RECOMP_OTHER_BANK = 2,  // control moved to another bank: re-dispatch
};

extern volatile int g_snes_event;
extern int g_recomp_single;   // debug: return after every instruction

#define RECOMP_STOP(cpu) (g_snes_event | g_recomp_single | (cpu)->intWanted | (cpu)->waiting | (cpu)->stopped)

int recomp_run(Cpu* cpu);

// port hook points (sym/hooks.txt)
extern int g_hook_enable;
extern void (*g_recomp_hook)(Cpu* cpu, uint32_t adr);
extern const uint32_t g_hook_addrs[];   // zero-terminated (Super Star Wars)
extern int (*g_recomp_fn)(Cpu* cpu);     // recompiled code of the current game (NULL = Super Star Wars)
extern const uint32_t* g_hook_list;      // hook points of the current game (NULL = g_hook_addrs)

#endif
