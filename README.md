# Super Star Wars Trilogy — PC Port

A native PC version of the SNES **Super Star Wars** games, made by statically recompiling the original game code to C and running it on an accurate SNES graphics and sound core, with PC features added on top.

| Game | Status |
|------|--------|
| Super Star Wars (1992) | Playable start to finish, all PC features |
| Super Star Wars: The Empire Strikes Back (1993) | Playable start to finish, all PC features |
| Super Star Wars: Return of the Jedi (1994) | Playable start to finish, all PC features (new: not playtested yet) |

All three games run in one program as a trilogy: after one game's ending the next one starts by itself, skipping its logos and title and going straight to its opening crawl. Each can also be built on its own.

> ⚠️ **This repository contains no game code or data.** You need your own dumps of the original cartridges. No ROMs, graphics, music, levels or game code are included or will be provided. Please don't ask.

---

## Features

- **Native speed, faithful gameplay.** The recompiled code is checked frame for frame against the reference interpreter on every stage.
- **Widescreen** 16:10, 16:9 and 21:9 for the side-scrolling and Mode 7 stages. The extra columns are rebuilt from each game's level data. A *Wide enemies* setting chooses between enemies shown in the wide view (default), the stricter original 4:3 behaviour, or early spawning.
- **Mode 7 detail.** The speeder and flight stages are redrawn at 2× or 4× resolution.
- **Remove slowdown.** The SNES versions lag in busy scenes; this removes it. The speeder and flight stages always ran slower than full speed and were balanced for that, so there the game keeps its usual pace and the faster processor only helps a step that would otherwise lag. A *Speeder damage* setting (Gameplay) halves the damage taken in those stages; it's on by default.
- **Menus inside the games.** *PORT SETTINGS* and *ACHIEVEMENTS* are rows in each game's own GAME OPTION MENU, and the settings pages appear in that screen's own box. Start in a stage opens a **pause menu** inside the game's own pause (Resume, Restart stage, Continue from the start of the last stage, Options, Achievements, Stats, Save/Load state, Quit to title). The game draws all of this itself, with its own font, boxes and colours. Achievement pop-ups and the speedrun timer are drawn by the game too, with its own HUD font. In the speeder and flight stages, which have no text layer, these and the pause menu are drawn over the picture in the same style.
- **Achievements:** 16 per game, with pop-ups, a list in the menu, and optional Steam support.
- **Speedrun timer** with per-stage splits and saved personal bests, plus play statistics.
- **Rewind:** hold Backspace or the left trigger.
- **CRT filter** (scanlines or full CRT), **colour styles**, **sprite flicker removal** and crisper sound interpolation.
- **Orchestral music** through your own MSU-1 audio packs (see `msu/`, `msu-esb/` and `msu-rotj/`; set up for the standard Conn & Relikk MSU-1 packs).
- **Extras** unlocked by beating a game or entering one of its original cheat codes: the developers' own level-select debug menu, Lightsaber start and Hero select (Star Wars), and All Force powers and Infinite thermal detonators (Empire and Jedi).
- **Restored cut content** (Super Star Wars, off by default): a reconstruction of the Death Star stage left in the cartridge but cut before release. See `docs/CUT_CONTENT.md`.
- Rumble, full keyboard and controller rebinding, 60 fps at any resolution, and crash reports you can send.

---

## What you need

| Game | Region / Version | File name | SHA-1 (headerless) |
|------|------------------|-----------|--------------------|
| Super Star Wars | USA, Rev 1 | `ssw.sfc` | `834b5c6b82b1ee22cc583075334800b51269624b` |
| Super Star Wars: The Empire Strikes Back | USA, Rev 1 | `sesb.sfc` | `82bf21e0ba427bcc9c6390bf028dfd76de3c94d3` |
| Super Star Wars: Return of the Jedi | USA, Rev 1 | `srotj.sfc` | `b8dca1cd8b2ac99e46078fc91fd42ee336e761e4` |

Other versions (Europe, Japan, Rev 0) are **not supported** and are rejected at startup with a message saying which version is needed. A 512-byte copier header is stripped automatically.

**Checking your ROM on Windows:** open PowerShell in the folder with your ROM and run

```powershell
Get-FileHash .\ssw.sfc -Algorithm SHA1
```

(or build `romhash`, see below, which also handles copier headers). You only need the ROMs of the games you want to play.

---

## How to play

There are two versions:

- **The launcher** (`ssw_launcher.exe`) contains no game code. It runs the games on the built-in SNES core straight from your ROMs. GitHub builds this one automatically, and it's the one attached to Releases.
- **The native build** (`ssw.exe`) translates the games' code to native C from *your* ROMs when you build it, so it runs faster. You build it yourself (see below).

Both have the same features. To play:

1. Put the program, `SDL2.dll`, and your ROMs (named as in the table above) in one folder.
2. Run the program. You can also drag a ROM file onto it.

Settings are saved in `ssw.ini` next to the program. Saves, best times, stats and achievements are kept per game (`ssw_*`, `esb_*` and `srotj_*` files).

---

## Controls

All of these can be changed in *Options → Controls*.

| SNES button | In game | Keyboard | Controller (Xbox layout) |
|-------------|---------|----------|---------------------------|
| D-pad | Move | Arrow keys | D-pad / left stick |
| B | Jump | Z | A (bottom) |
| Y | Fire | A | X (left) |
| A | Change weapon | X | B (right) |
| X | Special | S | Y (top) |
| L / R | | Q / W | LB / RB |
| Select | Change weapon | Right Shift | Back |
| Start | Pause menu | Enter | Start |

| Shortcut | Key |
|----------|-----|
| Menu | Esc, the Guide button, or Back+Start |
| Save / load state | F5 / F7 |
| Restart stage | F8 |
| Rewind (when enabled) | hold Backspace / left trigger |
| Fast forward | hold Tab |
| Fullscreen | F11 or Alt+Enter |

**Steam Controller:** while Steam is running, it applies its Desktop Configuration to programs it didn't start (A acts as Enter, and so on). Add the program to Steam as a non-Steam game, or set a gamepad template as Steam's Desktop Layout.

---

## Building from source

The build uses a Makefile with GCC: native on Linux, and cross-compiled to Windows with mingw-w64. On Windows itself, use WSL or MSYS2.

**Requirements**

- GCC and GNU make; `mingw-w64` for Windows builds
- SDL2 development files (`libsdl2-dev` on Debian/Ubuntu). For Windows builds, `make sdl2` downloads the official SDL2 mingw package into `third_party/SDL2-mingw`.
- Python 3, only for `make gen`
- `curl`, only for `make sdl2`

**Launcher (no ROM needed to build)**

```sh
make sdl2               # once, for Windows builds
make launcher           # ssw_launcher (Linux) + ssw_launcher.exe + SDL2.dll
```

**Native build (needs your ROMs)**

```sh
mkdir roms
cp /path/to/your/ssw.sfc /path/to/your/sesb.sfc /path/to/your/srotj.sfc roms/
make gen                # translates the game code from your ROMs into gen/, gen_esb/ and gen_rotj/
make ssw                # Linux
make sdl2 win           # Windows: ssw.exe + SDL2.dll
```

`roms/`, `gen/`, `gen_esb/` and `gen_rotj/` are ignored by git, so they are never committed. For single games, use `make esb` / `esb.exe` (Empire only), `make rotj` / `rotj.exe` (Jedi only) or `make ssw_solo` (Star Wars only).

**Tools**

- `make romhash`: prints a ROM's SHA-1 (`./romhash ssw.sfc`).
- `tools/`: the recompiler (`recomp.py`), the code tracer and disassembly emitter, and level, script and coverage tools. Each one reads your own ROM.
- `analysis/`: the code analysis the recompiler needs (code addresses and CPU modes, plus names for RAM and routines). These files contain no bytes from the games.
- `verify.sh` / `verify_esb.sh` / `verify_rotj.sh`: compare the recompiled build against the interpreter, scene by scene.

---

## Credits

- **Port by T-Man**
- Original games developed by Sculptured Software and LucasArts, published by JVC
- Built with the help of [Claude](https://claude.ai) (Anthropic), an AI assistant, used for the decompilation and recompilation work, writing and reviewing code, testing and documentation
- [LakeSnes](https://github.com/angelo-wf/LakeSnes) SNES core (MIT), modified; [SDL2](https://libsdl.org) (zlib); DejaVu fonts and the public-domain font8x8. See `THIRD_PARTY_LICENSES.txt` and `third_party/`.
- Inspired by other SNES reimplementation projects such as [zelda3](https://github.com/snesrev/zelda3)

---

## Legal

This is an unofficial fan project made for preservation and personal use. It is **not affiliated with, endorsed by, or connected to** Lucasfilm Ltd., The Walt Disney Company, LucasArts, JVC or Sculptured Software.

*Star Wars* and all related names, characters and marks are trademarks of Lucasfilm Ltd. All rights to the original games belong to their respective owners.

This repository contains only original code and tools. It does not include, and will never distribute, any copyrighted game code or data. You must own the original cartridges to use this project.

This project is not for sale and accepts no donations.
