# Handoff: Super Return of the Jedi (2026-10-13)

Branch `claude/funny-edison-skvsr0` (main merged in, through PR #3). ROM: Super Return of the Jedi (USA)
(Rev 1), `srotj.sfc`, SHA-1 b8dca1cd…, CRC32 4A5F30D8, 2 MB LoROM FastROM.
Day 1: groundwork (recompiled, verified). Night 1 (T-Man: "make it match Star Wars and Empire"): menus,
Clear stage / Level select, MSU-1 music, title credit, achievements, widescreen.

## What works
- `make gen` builds `gen_rotj/` from `roms/srotj.sfc` (`analysis/rotj/map.json`, prefix `rotj_`). The
  trilogy program (`make ssw`, `make win`), the launcher, and a Jedi-only program (`make rotj`, `rotj.exe`)
  include it; `make esb` / `make ssw_solo` leave it out (`-DNO_ROTJ`).
- Jedi is third in `g_all_games`: the Game switch offers it, the trilogy hands over to it after Empire's
  ending. Its files are `srotj_*` (key "srotj", unlock bit 4).
- Features: `GF_TITLE_CRED | GF_MSU | GF_ACH | GF_WIDESCREEN` (everything Empire has).
  - Title credit "PC PORT BY T-MAN"; option screen with PORT SETTINGS / ACHIEVEMENTS in Jedi's own box
    (the bigger box hides the logo sprites it covers, and puts them back); settings pages drawn by the game;
    pause menu in a stage drawn in Jedi's style; Clear stage and Level select (Jedi's GAME DEBUG MENU).
  - MSU-1: `msu-rotj/` (from the MSU session, keyed on Jedi's song number $01EA). Checked with test tracks:
    the title and crawl tracks play and the SNES music goes quiet. Needs T-Man's pack in `msu-rotj/`.
  - 16 achievements (`src/achievements_rotj.c`, Steam ACH_32-47), checked with `rotj_test ACH=`: a new game
    with the stage skip unlocks them in order; Rebel Alliance needs Luke, Han, Chewbacca, Leia, Wicket.
  - Widescreen: all side-scrolling stages rebuilt from the level data (selfcheck: 0 mismatches), the
    landspeeder / Death Star surface / speeder bikes / space battle widened; the reactor run stays 4:3 (its
    HUD would repeat), as Star Wars' trench run does. Jabba's dance hall (04/07) stays 4:3 (no layer comes
    from level data). Past the ends of a level: black, as in Star Wars.
  - Remove slowdown: side-scrolling stages overclocked; the vehicle stages are paced on the step flag $0B59
    (measured live, e.g. landspeeder 2 frames a step). The reactor run doesn't use the flag: console speed.
  - Speeder damage (half damage in Mode 7) does nothing in Jedi's landspeeder: a crash costs a life there,
    there's no health to halve.
- Verified (recompiled = interpreter on every frame, WRAM+VRAM+CGRAM+OAM hash), `verify_rotj.sh roms/srotj.sfc
  8000` (seed 7), re-run after all of the above: all 20 stages (each entered through the scene before it),
  the ending (06/2C to the credits and title) and "full" (60000 frames from a new game with the stage skip,
  18 stages to the reactor run) OK, 0 interpreter fallbacks. With widescreen on (WS=64), interpreter =
  recompiled on 04/02, 04/0A, 14/12, 0C/10.
- Star Wars and Empire: verify.sh (21 scenes) and verify_esb.sh (25) all OK after the groundwork; title, option
  screen and settings pages pixel-identical to `main` (SSW_AUTOTEST screenshots compared byte for byte);
  widescreen output identical to `main` (ssw_wide on 4 stages, esb_test WS=64 on 3).

## Engine map (Empire address in brackets; `analysis/rotj/ram.txt`, `names.txt`)
- GameMode $01BE [01B6], SceneID $01C0 [01B8], Lives $01B2 [01AC], MaxHP $01B4 [01AE], object types $0BB8+2n
  [0C56] (slot 0 = the hero: 4 Luke, 6 Han, 8 Chewbacca, A Wicket, C/E/10 Leia), HP $0C58+2n [0CF6], player
  X/Y $1130/$11A8 [11CE/1246].
- Developer flag $0110 (+$0112) [010C]; cheats $0114 [0110]: $20 infinite thermal detonators (title code
  B X B X B X B B B Y, checked), $40 all Force powers. X X B A Y = 99 lives (code_lives 99, checked).
- Developer keys: controller 2 L+R = GAME DEBUG MENU (level select), controller 1 B+Start = stage skip
  ($80:8C31), controller 2 A+B+X+Y+Select = invincibility. Jedi reads them only while running, not in its
  pause loop, so the port presses them after the pause menu resumed the game (`dev_press`, GameDef
  `clear_pad`/`clear_buttons`). The reactor run has no stage skip.
- Scene table $80:8627 [8D4E]. Title (06,15). New game: (00,00) → crawl (02,00) → (0E,14) → (0E,18) →
  (06,18) → (06,19) → (14,12). Start in the crawl jumps to (14,12) (immediates at $80:F62D / $80:F634).
  Ending: (0A,02) → (06,2C) (06,2E) (06,2F) fireworks, waits for a button → (06,30) → credits (06,17) = end
  scene → title. Life lost: (08,31) then (08,SS) = hero select / stage card; game over (06,16).
- Modes: 02 crawl, 04 side-scrolling, 06 cutscenes/title, 08 stage card + hero select, 0A Death Star reactor
  run, 0C space battle, 0E text screens, 12 speeder bikes, 14 landspeeder / Death Star surface.
- Title / option screen: BG3 map at VRAM $1000 (GameDef `bg3_map`; $0800 in the others), copy at $7F:1440,
  font tiles at $4000 with 'A' = $11A (font_base $100), option cursor $0731, window-1 table $2100 (indirect
  HDMA), INIDISP shadow $7B, step flag $0B59. Option box: rows 9-15, heading row 10 (Star Wars/Empire 8-16,
  row 9): `k_om_layouts` in main_sdl.c.
- Widescreen (`k_ws_rotj` in widescreen.c): layer descriptors = Empire's - $85 ($05AB...), chunk cache
  $1C96/$1C6C, grid of words in $7F, visibility hooks 81CC58/81CC64, despawn 80E0F7 (dp $53), spawn window
  81972C/819731 (the other two edges are RAM compares, $0B23/$0B25, not moved). Jedi-only switches:
  fill_lines (open-sky lines), bg3_hud = 32 (BG3 is HUD only at the top; Jedi draws skies below), wrap256
  (backgrounds the game fills only 256 pixels wide), level-end clipping.

## Stage list (play order) and names (from the hero select cards; vehicle stages have none)
14/12 LANDSPEEDER, 04/02 TATOOINE, 04/07 JABBA'S DANCE HALL, 04/01 JABBA'S PALACE, 04/08 RANCOR PIT,
04/05 SAIL BARGE ATTACK, 04/06 INSIDE SAIL BARGE, 12/11 SPEEDER BIKES, 04/0A EWOK VILLAGE A, 04/0B EWOK
VILLAGE B, 04/03 ENDOR, 0C/10 SPACE BATTLE, 04/09 POWER GENERATOR, 04/0F INSIDE DEATH STAR, 14/13 DEATH STAR
SURFACE, 04/0E TOWER, 04/0C TOWER ENTRANCE (card: "TOWER ENTRANCE -VADER-"), 04/0D EMPEROR'S CHAMBER,
0A/01 DEATH STAR TUNNEL, 0A/02 REACTOR ESCAPE.

## Known issues / not verified
- Warping straight into the speeder bikes (12/11) skips the cutscenes before it and the original code then
  crashes (nested NMI; interpreter and recompiled alike). Entering through (06,20) as the game does is fine;
  the verify script and coverage enter every stage through the scene before it.
- Coverage: map.json has 43,804 instructions, from the vectors plus 75 interpreter runs. Code the map
  doesn't know still runs on the interpreter (slower, same result); none was hit in the verify runs.
- Not played by a human yet. Passwords, continues, game over → continue, the option menu's settings, the
  7-continues / credits / sound-test codes are untested. Steamworks needs ACH_32-47 entered.

## How to test
```
cp ssw.sfc sesb.sfc srotj.sfc roms/
apt-get install -y libsdl2-dev gcc-mingw-w64-x86-64
make gen && make -j4 rotj_test rotj_lockstep ssw
./verify_rotj.sh roms/srotj.sfc 8000                 # every stage, the ending, a full run
./rotj_test roms/srotj.sfc 4000 recomp 1 300 04 08    # screenshots every 300 frames, warp to the Rancor pit
WS=64 WSHOT=3000 ./rotj_test roms/srotj.sfc 3001 recomp 3 0 04 02   # widescreen picture -> wide.ppm, selfcheck
ACH=./ CLEAR=900 GOD=1 ./rotj_test roms/srotj.sfc 45000 recomp 4   # achievements on a skip-through run
STATE=srotj.state ./rotj_test roms/srotj.sfc 2000 recomp 1 0 14 12  # a save state for the program (F7)
./rotj_lockstep roms/srotj.sfc 04 08 7 2000           # first diverging instruction, if a stage ever mismatches
tools/emu/cover_rotj.sh 15000 4                       # more coverage, then rebuild map.json (see the script)
SSW_AUTOTEST="w1500 k:Down w20 k:Return w200 shot:o.ppm q" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./ssw srotj.sfc
```
All `rotj_test` switches are listed at the top of `src/test_rotj.c`.
