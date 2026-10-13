# Handoff: after build 14 test 1 (2026-10-13)

Build 14 test 1 was given to T-Man (Windows native ssw.exe, built from his ROMs; never publish it).
Previous note: claude/handoff-build14.md.

## Done in build 14 test 1
- Widescreen, Super Star Wars: back on build 12's line test (`ssw_line_trusted`, two edge samples,
  own descriptor) and VRAM wrap for empty cells; build 13's strict matching and the
  "no layer matched -> 4:3" rule are Empire-only now (`W->grid_words`). Stage-card check kept.
  New: extra columns past the end of every layer's map are black (`ppu->wideClipL/R`).
  Checked old vs new with `FIRE=1 ssw_wide <rom> 64 1800 s 04 <scene>` on scenes 00-21.
- Known, pre-existing: Sandcrawler interior 04/01, first door: BG1 has a hole right of the door that
  the game fills at runtime when the door opens; a small patch shows in the right extra area until
  then. Grid and chunk cache don't change, so the fill is a direct tilemap write. Not fixed.
- Remove slowdown: only in mode $04 now. In the Mode 7 stages it doubled the damage taken
  (0E04/0E05, same random input: ~69 -> ~128 per minute). Cause of feedback item 2.
- Clear stage / Level select: developer flag only for 8 frames (`g_dev_pulse`); the first Start
  after Level select goes to the game (`g_debug_menu`); Developer features off clears the game's flag.
- Reports: ssw_report_<date>_<time>.txt/.state (and ssw_crash_...).

## Still open
1. Menus (feedback 5): put the PC settings inside the game's own OPTION MENU screen, looking stock,
   instead of PC SETTINGS on the title; pause menu in the game's style (box tiles $1D7-$1D9, dot
   cursor $1DA, gold/green palettes).
2. Return of the Jedi (ROM USA Rev 1, SHA-1 b8dca1cd...).
3. MSU packs: map the real pack track numbers into msu/msu.txt, msu-esb/msu.txt, add RotJ.
   T-Man needs to re-upload the packs.

## Setup in a new cloud session
Upload the ROMs, copy them to roms/ (ssw.sfc, sesb.sfc, srotj.sfc), then:
`apt-get install -y libsdl2-dev gcc-mingw-w64-x86-64`, `make gen`, `make ssw ssw_wide`,
`make sdl2 && make win` for ssw.exe + SDL2.dll.
