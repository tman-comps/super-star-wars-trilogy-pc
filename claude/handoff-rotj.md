# Handoff: Super Return of the Jedi groundwork (2026-10-13)

Branch `claude/funny-edison-skvsr0`, based on `claude/eloquent-ritchie-jwlzz1` (build 15 work).
ROM: Super Return of the Jedi (USA) (Rev 1), `srotj.sfc`, SHA-1 b8dca1cd…, CRC32 4A5F30D8, 2 MB LoROM FastROM.
No menu code in `src/main_sdl.c` was touched (another session is rewriting it).

## What works
- `make gen` builds `gen_rotj/` from `roms/srotj.sfc` (`analysis/rotj/map.json`, prefix `rotj_`).
  The trilogy program (`make ssw`, `make win`), the launcher, and a Jedi-only program (`make rotj`, `rotj.exe`)
  include it. `make esb` and `make ssw_solo` leave it out (`-DNO_ROTJ`).
- `g_rotj` in `src/game.c`: Jedi is third in `g_all_games`, so the Game switch offers it and the trilogy
  hands over to it after Empire's ending. Its own files are `srotj_*` (key "srotj", unlock bit 4).
- No port features yet (`features = 0`): no widescreen, achievements, MSU-1, title-menu items.
  Pause menu, save states, rewind, timer, stats and continue are generic and run.
- Verified (recompiled = interpreter on every frame, WRAM+VRAM+CGRAM+OAM hash), `verify_rotj.sh
  roms/srotj.sfc 8000` (seed 7): all 20 stages OK, each entered through the scene before it, 8000 frames
  of random play, **0 interpreter fallbacks**; plus "full", 60000 frames from a new game with the developer
  stage skip: title, crawl, then 18 stages and their cutscenes in order up to the reactor run (06/2D →
  0A/01), OK, 0 fallbacks; and the ending (06/2C → fireworks → credits 06/17 → title), 4000 frames, OK.
- Super Star Wars (`verify.sh`, 21 scenes) and Empire (`verify_esb.sh`, 25 scenes) re-run after the change:
  all OK, 0 fallbacks (4000 / 3000 frames).

## Engine map (Empire address in brackets; `analysis/rotj/ram.txt`, `names.txt`)
- GameMode $01BE [01B6], SceneID $01C0 [01B8], Lives $01B2 [01AC], MaxHP $01B4 [01AE],
  player HP $0C58 [0CF6] (object slot table), player X/Y $1130/$11A8 [11CE/1246].
- Developer flag $0110 (+$0112) [010C]; cheats $0114 [0110]: $20 infinite thermal detonators,
  $40 all Force powers (same bits as Empire). Title codes checked in the test driver:
  X X B A Y = 99 lives (code_lives 99), B X B X B X B B B Y = $20.
- Scene table $80:8627 [8D4E]. Title (06,15). New game: (00,00) → opening crawl (0E,14) → (0E,12) →
  first stage (14,12); Start in the crawl jumps straight there (immediates at $80:F62D / $80:F634),
  otherwise (06,18) → (06,19) → (14,12) through the table. Ending: (0A,02) → (06,2C) (06,2E) (06,2F)
  fireworks, waits for a button → (06,30) → credits (06,17) = end scene → title.
- Modes: 04 side-scrolling, 06 cutscenes/title, 08 stage card + hero select, 0A Death Star reactor run,
  0C Falcon space battle, 0E opening crawl, 12 Endor speeder bikes, 14 landspeeder / Death Star surface.
  `play_modes` grew from 4 to 6 entries in game.h for this.
- Menu fields from `main` (merged): bg3_shadow $7F:1440 (BG3 tilemap copy; Jedi's BG3 map is at VRAM
  **$1000**, not $0800 like the others, so main_sdl's `vram[0x800 + ...]` font/title checks don't match
  Jedi yet and its option-screen rows and in-stage pause menu stay off), r_inidisp $7B (0F, 08 when
  paused, checked), t_box as Empire's (title box side/corner/edge = font+$56/$57/$58), r_step_flag
  $0B59 (Empire's $0BB3 by xmatch; the mode 12 handler waits on it), paces left to automatic measuring.
- MSU: `msu-rotj/` came with `main`; GF_MSU is not set yet (the MSU session keys on song number $01EA).
- Font: BG3 tiles at VRAM $4000, 'A' = tile $11A, so font_base $100 (O = $134, blank = $15C).
- Title menu box: rows 13-19, items START GAME / OPTION MENU / PASSWORD on rows 14/16/18, columns 11+.
  Cursor variable and window tables not found yet, so `tm_items = 0` (the port leaves the title alone).

## Stage list (play order, `k_rotj_stages`)
14/12 Tatooine landspeeder, 04/02 palace gates (Leia), 04/07 Jabba's palace, 04/01 dance hall (Chewie),
04/08 Rancor pit, 04/05 attack on sail barge, 04/06 inside sail barge, 12/11 speeder bikes,
04/0A Ewok village A, 04/0B Ewok village B, 04/03 Endor, 0C/10 space battle, 04/09 power generator,
04/0F Death Star entrance, 14/13 Death Star surface, 04/0E tower, 04/0C Vader, 04/0D Emperor's chamber,
0A/01 reactor tunnel, 0A/02 reactor core.
Names marked by stage cards: Rancor pit, attack on / inside sail barge, Ewok village A, tower,
Emperor's chamber. The others are provisional (screenshots + the debug menu's area list); T-Man to confirm.

## Things that differ from Empire (and need the menu session's attention)
- **Stage skip**: Jedi's developer skip is controller 1 B+Start with the developer flag on ($80:8C31),
  not controller 2 Start. The pause menu's "Clear stage" (`stage_clear()` in main_sdl.c, pad 2 Start)
  will not work for Jedi until it presses B+Start on pad 1 for this game. Level select (pad 2 L+R) is
  the same as Empire.
- The reactor run (mode 0A) has no developer skip at all.

## Known issues / not verified
- Warping straight into the speeder bikes (12/11) skips the cutscenes before it and the original code
  then crashes (nested NMI with the stack used as a DMA list; interpreter and recompiled crash the same
  way). Entering through (06,20) as the game does is fine, so the verify script and coverage enter
  every stage through the scene before it.
- Coverage: `analysis/rotj/map.json` has 43,804 instructions (Empire 38,366), traced from the vectors plus
  75 interpreter runs (`tools/emu/cover_rotj.sh` seeds 1-3, the verify inputs, the ending). Code the map
  doesn't know still runs, on the interpreter (slower, same result); none was hit in the verify runs.
  Not verified: random play longer than 8000 frames per stage, other seeds, the game-over/continue/password
  screens, the option menu.
- Not played by a human yet. Passwords, continues, hero select choices, game over → continue paths,
  the 7-continues / credits / sound-test codes are untested.

## What's left (by game, as for Empire)
1. Title menu items (PC SETTINGS / ACHIEVEMENTS): find the cursor word and window tables; or follow
   whatever the menu rewrite does.
2. Widescreen: hooks (`analysis/rotj/hooks.txt`, matched with tools/xmatch.py from Empire's
   80A559/80A565/80DF0A) and a level reader in widescreen.c.
3. Achievements (16), MSU-1 track mapping (`msu-rotj/`), restored content if any.
4. Stage clear for the pause menu (above). Remove slowdown already only acts in mode 04.

## How to test
```
cp ssw.sfc sesb.sfc srotj.sfc roms/
apt-get install -y libsdl2-dev gcc-mingw-w64-x86-64
make gen && make -j4 rotj_test rotj_lockstep ssw
./verify_rotj.sh roms/srotj.sfc 8000          # every stage + full run; SCENES="0402:0402" for one
./rotj_test roms/srotj.sfc 4000 recomp 1 300 04 08   # screenshots every 300 frames, warp to the Rancor pit
./rotj_lockstep roms/srotj.sfc 04 08 7 2000   # first diverging instruction, if a stage ever mismatches
tools/emu/cover_rotj.sh 15000 4               # more coverage (interpreter), then rebuild map.json (see the script)
```
`rotj_test` environment switches (CDL, GOD, CLEAR, TRACE, RAMLOG, WATCHW, CODE, WILD, ...) are listed
at the top of `src/test_rotj.c`. `SSW_WARP=MMSS` in the real program only works for Jedi if the opening
crawl is left to play out (it patches the scene table, not the crawl's Start shortcut).
