# Handoff: where build 14 work stopped (2026-10-12)

## Feedback on build 13 (T-Man's playtest)
1. Much easier now: good.
2. Stage 2 (landspeeder, 0E04) "insane", heavy damage. Not investigated yet. Next: measure damage with Remove slowdown on/off and widescreen on/off.
3. **Widescreen regressions in build 13, on almost every level, including level 1** (level 1 was fine in build 12). The Sandcrawler exterior (04/0B) also flips to 4:3 at random. The report state saved at Sandcrawler interior 04/01 is in the session uploads.
4. After "Clear stage", Start does the vanilla pause instead of the pause menu. Cause: stage_clear()/level_select() set cfg.dev=1 permanently, and start_opens_pause() requires !cfg.dev. Fix: make dev mode temporary for those two actions, and pass Start to the game only while the debug menu is actually open.
5. Menus: wants all PC settings inside the game's own OPTION MENU screen, looking stock, rather than PC SETTINGS on the title. Also wants the pause menu to look vanilla: the game's box tiles ($1D7-$1D9), dot cursor ($1DA), gold/green palettes.
6. "Report a problem" overwrites earlier reports. Use timestamped file names.

## Widescreen: what changed in build 13 and what broke
Build-13 changes in widescreen.c:
- line_desc() / desc_matches_line(): each line samples 32 tiles, all must match, and the tiles must vary (build 12 sampled 2 edge tiles).
- Out-of-map → -2 (transparent) instead of the VRAM wrap.
- "No layer matched → 4:3" rule.
- BG3-uniform → 4:3 (stage cards).

The strict matching rejects lines with any dynamic tile. The game redraws the moving Sandcrawler into BG2 at runtime, so its static level data is in the wrong place. Rejected lines fall back to the VRAM wrap, which gives floating pieces and patches; "no layer matched" gives the random 4:3.

In-progress, UNCOMMITTED edit in the old dev workspace (probably lost): tolerant matching (≤1/8 mismatches) and removal of the "no layer matched" rule. The 4:3 flips were fixed; the floating Sandcrawler pieces were not.

**The pre-trilogy widescreen.c (whose Super Star Wars results were good) is in the repo at `docs/reference/widescreen_build12.c`.** It is reference only and not compiled.

**Recommended next step:** for Super Star Wars, restore that file's line_trusted() (two edge samples, own descriptor only) and its VRAM wrap for out-of-map. Keep only build 13's stage-card check (BG3 top rows uniform). Keep build 13's matching for Empire only (W->grid_words != 0), where it was verified stage by stage. Then re-check level 1, 04/0B and 04/01 with the ssw_wide WALKRIGHT=1 mode and the ssw_wshot replay tool (both are test drivers in src/).

## Not started
- Return of the Jedi: the ROM was uploaded (USA Rev 1, SHA-1 b8dca1cd…).
- MSU packs and patches for all three games were uploaded. Next: map the real pack track numbers into msu/msu.txt and msu-esb/msu.txt, and add RotJ.

## Repo
GitHub: tman-comps/super-star-wars-trilogy-pc (private). It holds build-13 code plus the rom_check integration. A new cloud session started from the repo needs the ROMs re-uploaded to roms/ and `make gen` before native builds.
