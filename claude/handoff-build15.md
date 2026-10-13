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
- Remove slowdown (test 4): side-scrolling stages overclocked as before. Mode 7 stages run at the
  console's speed and only a step that runs late is overclocked (from the last frame of the stage's
  usual pace, `GameDef.paces` or measured over its first steps; step flag `GameDef.r_step_flag`). Holding
  the step flag instead does NOT work: the NMI then sees a finished step every frame and the stage stays
  as dangerous. Landspeeder damage per minute, same random input: console 65, full overclock 160,
  boost 80; Empire snowspeeder 61 / 139 / 82. Mode 08 (SSW) and $12 (Empire asteroids) don't use the
  flag and keep the console's speed (they run full speed or aren't affected anyway).
- Speeder damage (test 5, T-Man found the speeders still too hard): in all Mode 7 play modes half of each
  HP loss is given back after the frame (`damage_tick`, setting `speeder_damage_half`, default on).
  Landspeeder, same random input: 42 damage a minute (console 65).
- Clear stage / Level select: developer flag only for 8 frames (`g_dev_pulse`); the first Start
  after Level select goes to the game (`g_debug_menu`); Developer features off clears the game's flag.
- Reports: ssw_report_<date>_<time>.txt/.state (and ssw_crash_...).

## Menus (feedback 5), done in build 14 test 2
T-Man wants the menus to be part of the games, drawn by the game (no overlays). Now:
- Title screen stock. GAME OPTION MENU gets PORT SETTINGS / ACHIEVEMENTS rows (box moved up 4 rows,
  window table rewritten: `option_menu_tick`). Pages show in the option screen's upper box via the game's
  BG3 tilemap (`om_draw_menu`); the CONTROL PAD box stays (achievements use it for the description).
- Pause in a side-scrolling stage = the game's own pause (Start injected; paused when the INIDISP
  shadow `GameDef.r_inidisp` reads brightness 8). The menu goes into BG3 under the HUD (rows 5+), font
  tiles uploaded into free BG3 tile slots, other layers halved by colour math; everything restored and
  Start injected again on close (`stage_menu_tick`). Empire switches BG3 mid-frame below its HUD, so both
  BG3 set-ups get the menu. Save state saves the snapshot taken just before pausing.
- Mode 7 stages: no BG3 below the HUD (mode switch mid-frame), so the pause menu is still drawn over the
  picture (`draw_menu`, same look).
- Achievement pop-up and speedrun timer (test 3): in side-scrolling stages written into BG3 under the HUD
  with the stage's own HUD font (found by searching BG3 tiles for the captured glyphs, `hf_find`), every
  frame after vblank (`hud_tick`) and taken out again after each frame (`hud_lift`, called from
  `sp_after_frame`) so snapshots never contain them. Title and Mode 7 stages: still drawn over (`draw_hud`).
  `SSW_AUTOTEST` has `toast:<n>` to show a pop-up.
- Menu screens are built once as a BG3 cell grid (`cg_*`, `menu_build`), box pieces as codes mapped per
  game (`GameDef.t_box`: Empire's are one lower than Super Star Wars').
- Test without a screen: `SSW_AUTOTEST="w900 k:Return w60 shot:a.ppm q" SDL_VIDEODRIVER=dummy ./ssw`.

## Still open
1. Ideas: Mode 7 pause, pop-ups and timer in-game (would need forcing mode 1 on those lines).
2. Return of the Jedi (ROM USA Rev 1, SHA-1 b8dca1cd...).
3. MSU packs: map the real pack track numbers into msu/msu.txt, msu-esb/msu.txt, add RotJ.
   T-Man needs to re-upload the packs.

## Setup in a new cloud session
Upload the ROMs, copy them to roms/ (ssw.sfc, sesb.sfc, srotj.sfc), then:
`apt-get install -y libsdl2-dev gcc-mingw-w64-x86-64`, `make gen`, `make ssw ssw_wide`,
`make sdl2 && make win` for ssw.exe + SDL2.dll.
