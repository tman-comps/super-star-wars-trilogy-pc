# Super Star Wars trilogy - native PC port
#
# The repository holds no game code or data. The recompiled game code (gen/, gen_esb/, gen_rotj/) is
# generated from YOUR OWN ROMs by `make gen`; everything else here is the port's own code.
#
#   make gen          generate the game code from roms/ssw.sfc, roms/sesb.sfc and roms/srotj.sfc (needs Python 3)
#   make ssw          the trilogy program, Linux (needs SDL2 dev package)
#   make sdl2         download the SDL2 mingw package for Windows builds (third_party/SDL2-mingw)
#   make win          ssw.exe + SDL2.dll, cross-compiled with mingw-w64
#   make launcher     ssw_launcher(.exe): no generated code at all, runs the games on the built-in
#                     SNES core from the player's ROM (what GitHub Actions builds and releases)
#   make esb / ssw_solo / rotj (and esb.exe, rotj.exe)   single-game programs
#   make romhash      tool that prints a ROM's SHA-1 (see src/rom_check.c)

CC      ?= gcc
SDLWIN  := third_party/SDL2-mingw/x86_64-w64-mingw32
CFLAGS  ?= -O2 -g
CFLAGS  += -Ithird_party/lakesnes -Isrc -Igen -w
LAKE    := $(addprefix third_party/lakesnes/,spc.c dsp.c apu.c cpu.c dma.c ppu.c cart.c input.c statehandler.c snes.c snes_other.c)
GEN     := $(wildcard gen/recomp_*.c)
GEN_ESB := $(wildcard gen_esb/recomp_*.c)
GEN_ROTJ := $(wildcard gen_rotj/recomp_*.c)
COMMON  := src/game.c src/runner.c src/widescreen.c src/msu.c src/achievements.c src/achievements_esb.c src/achievements_game.c src/restore.c src/steam.c src/extras.c src/crashlog.c src/rom_check.c

BUILD   := build
LAKE_O  := $(patsubst %.c,$(BUILD)/%.o,$(LAKE))
GEN_O   := $(patsubst %.c,$(BUILD)/%.o,$(GEN) $(GEN_ESB) $(GEN_ROTJ))
GEN_ESB_O := $(patsubst %.c,$(BUILD)/%.o,$(GEN_ESB))
COM_O   := $(patsubst %.c,$(BUILD)/%.o,$(COMMON))

all: ssw_test ssw

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# the big generated files build faster at -O1 with little runtime cost
$(BUILD)/gen/%.o: gen/%.c gen/cpu_helpers.inc src/recomp_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -O2,$(CFLAGS)) -O1 -c $< -o $@

ssw_test: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_main.o
	$(CC) -o $@ $^

ssw: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/main_sdl.o
	$(CC) -o $@ $^ `sdl2-config --libs` -lm -ldl

$(BUILD)/src/main_sdl.o: src/main_sdl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) `sdl2-config --cflags` -c $< -o $@

clean:
	rm -rf $(BUILD) build-* ssw ssw_test ssw.exe ssw_launcher ssw_launcher.exe esb esb.exe ssw_solo rotj rotj.exe rotj_* romhash

.PHONY: all clean win launcher gen sdl2

# ---- Game code generated from the player's ROMs (never committed)
PY ?= python3
gen: gen/recomp_dispatch.c gen_esb/recomp_dispatch.c gen_rotj/recomp_dispatch.c
gen/recomp_dispatch.c: roms/ssw.sfc analysis/ssw/map.json analysis/ssw/hooks.txt tools/recomp.py
	@mkdir -p gen
	$(PY) -I tools/recomp.py roms/ssw.sfc analysis/ssw/map.json third_party/lakesnes/cpu.c gen "" analysis/ssw/hooks.txt
gen_esb/recomp_dispatch.c: roms/sesb.sfc analysis/esb/map.json analysis/esb/hooks.txt tools/recomp.py
	@mkdir -p gen_esb
	$(PY) -I tools/recomp.py roms/sesb.sfc analysis/esb/map.json third_party/lakesnes/cpu.c gen_esb esb_ analysis/esb/hooks.txt
gen_rotj/recomp_dispatch.c: roms/srotj.sfc analysis/rotj/map.json analysis/rotj/hooks.txt tools/recomp.py
	@mkdir -p gen_rotj
	$(PY) -I tools/recomp.py roms/srotj.sfc analysis/rotj/map.json third_party/lakesnes/cpu.c gen_rotj rotj_ analysis/rotj/hooks.txt
roms/%.sfc:
	@echo "Missing $@ -- copy your own dump of the cartridge there (see README.md)"; exit 1

romhash: tools/romhash.c src/rom_check.c src/rom_check.h
	$(CC) -O2 -Isrc tools/romhash.c src/rom_check.c -o $@

# ---- SDL2 for Windows builds (zlib license), downloaded rather than stored in the repo
SDL2_VER := 2.30.9
sdl2: $(SDLWIN)/lib/libSDL2.a
$(SDLWIN)/lib/libSDL2.a:
	mkdir -p third_party
	curl -fsSL -o third_party/sdl2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$(SDL2_VER)/SDL2-devel-$(SDL2_VER)-mingw.tar.gz
	cd third_party && tar xzf sdl2.tar.gz && rm -rf SDL2-mingw && mv SDL2-$(SDL2_VER) SDL2-mingw && rm sdl2.tar.gz

# ---- Windows (cross-compiled with mingw-w64)
WINCC   := x86_64-w64-mingw32-gcc
SDLWIN  := third_party/SDL2-mingw/x86_64-w64-mingw32
WBUILD  := build-win
WCFLAGS := -O2 -Ithird_party/lakesnes -Isrc -Igen -w -I$(SDLWIN)/include/SDL2 -Dmain=SDL_main
W_LAKE  := $(patsubst %.c,$(WBUILD)/%.o,$(LAKE))
W_GEN   := $(patsubst %.c,$(WBUILD)/%.o,$(GEN) $(GEN_ESB) $(GEN_ROTJ))
W_COM   := $(patsubst %.c,$(WBUILD)/%.o,$(COMMON) src/main_sdl.c)

$(WBUILD)/gen/%.o: gen/%.c gen/cpu_helpers.inc src/recomp_rt.h
	@mkdir -p $(dir $@)
	$(WINCC) $(filter-out -O2,$(WCFLAGS)) -O1 -c $< -o $@

$(WBUILD)/gen_esb/%.o: gen_esb/%.c gen_esb/cpu_helpers.inc src/recomp_rt.h
	@mkdir -p $(dir $@)
	$(WINCC) $(filter-out -O2,$(WCFLAGS)) -Igen_esb -O1 -c $< -o $@

$(WBUILD)/gen_rotj/%.o: gen_rotj/%.c gen_rotj/cpu_helpers.inc src/recomp_rt.h
	@mkdir -p $(dir $@)
	$(WINCC) $(filter-out -O2,$(WCFLAGS)) -Igen_rotj -O1 -c $< -o $@

$(WBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(WINCC) $(WCFLAGS) -c $< -o $@

ssw.exe: $(W_LAKE) $(W_GEN) $(W_COM)
	$(WINCC) -o $@ $^ -L$(SDLWIN)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc

win: ssw.exe
	cp $(SDLWIN)/bin/SDL2.dll .

# ---- Launcher build: no code generated from the game (gen/ is not linked). The games run on the
# SNES core from the player's ROM. Safe to distribute; slower CPU emulation but the same features.
LBUILD  := build-launcher
L_COMMON := $(filter-out src/runner.c,$(COMMON))
$(LBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DSSW_LAUNCHER `sdl2-config --cflags` -c $< -o $@
ssw_launcher: $(LAKE_O) $(patsubst %.c,$(LBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(CC) -o $@ $^ `sdl2-config --libs` -lm -ldl

WLBUILD := build-win-launcher
$(WLBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(WINCC) $(WCFLAGS) -DSSW_LAUNCHER -c $< -o $@
ssw_launcher.exe: $(W_LAKE) $(patsubst %.c,$(WLBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(WINCC) -o $@ $^ -L$(SDLWIN)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc

launcher: ssw_launcher ssw_launcher.exe
	cp $(SDLWIN)/bin/SDL2.dll .

ssw_lockstep: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/lockstep.o
	$(CC) -o $@ $^

ssw_test_menu: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_menu.o
	$(CC) -o $@ $^

ssw_cover: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_cover.o
	$(CC) -o $@ $^

ssw_bgdump: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_bgdump.o
	$(CC) -o $@ $^

ssw_readers: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_readers.o
	$(CC) -o $@ $^

ssw_lzcheck: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_lzcheck.o
	$(CC) -o $@ $^

ssw_vramw: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_vramw.o
	$(CC) -o $@ $^

ssw_dump: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_dump.o
	$(CC) -o $@ $^

ssw_wide: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_wide.o
	$(CC) -o $@ $^

ssw_lag: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_lag.o
	$(CC) -o $@ $^

ssw_oc: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_oc.o
	$(CC) -o $@ $^

ssw_apu: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_apu.o
	$(CC) -o $@ $^

ssw_msumute: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_msu_mute.o
	$(CC) -o $@ $^ -lm

ssw_music: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_music.o
	$(CC) -o $@ $^

ssw_msu: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_msu.o
	$(CC) -o $@ $^ -lm

ssw_script: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_script.o
	$(CC) -o $@ $^

ssw_ach: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_ach.o
	$(CC) -o $@ $^

ssw_m7: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_m7.o
	$(CC) -o $@ $^

ssw_m7bench: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_m7bench.o
	$(CC) -o $@ $^

ssw_types: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_types.o
	$(CC) -o $@ $^

ssw_soak: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_soak.o
	$(CC) -o $@ $^ -ldl

ssw_boss: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_boss.o
	$(CC) -o $@ $^ -lm -ldl

# header dependencies (coarse: any header change rebuilds the hand-written sources, not gen/)
HDRS := $(wildcard src/*.h third_party/lakesnes/*.h)
$(LAKE_O) $(COM_O) $(patsubst %.c,$(BUILD)/%.o,$(wildcard src/*.c)): $(HDRS)
$(W_LAKE) $(W_COM): $(HDRS)

$(BUILD)/gen_esb/%.o: gen_esb/%.c gen_esb/cpu_helpers.inc src/recomp_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -O2,$(CFLAGS)) -Igen_esb -O1 -c $< -o $@

esb_test: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_esb.o
	$(CC) -o $@ $^ -lm -ldl

# ---- Super Return of the Jedi
$(BUILD)/gen_rotj/%.o: gen_rotj/%.c gen_rotj/cpu_helpers.inc src/recomp_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -O2,$(CFLAGS)) -Igen_rotj -O1 -c $< -o $@

rotj_test: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/test_rotj.o
	$(CC) -o $@ $^ -lm -ldl

rotj_lockstep: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/lockstep_rotj.o
	$(CC) -o $@ $^ -lm -ldl

# ---- Standalone single-game programs (same front-end, only one game's code linked)
#   make esb / esb.exe        Super Empire Strikes Back only (-DNO_SSW -DNO_ROTJ)
#   make ssw_solo / ssw_solo.exe   Super Star Wars only (-DNO_ESB -DNO_ROTJ)
#   make rotj / rotj.exe      Super Return of the Jedi only (-DNO_SSW -DNO_ESB)
EBUILD := build-esb
$(EBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DNO_SSW -DNO_ROTJ `sdl2-config --cflags` -c $< -o $@
esb: $(LAKE_O) $(GEN_ESB_O) $(patsubst %.c,$(EBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(CC) -o $@ $^ `sdl2-config --libs` -lm -ldl
SBUILD := build-ssw-solo
$(SBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DNO_ESB -DNO_ROTJ `sdl2-config --cflags` -c $< -o $@
ssw_solo: $(LAKE_O) $(patsubst %.c,$(BUILD)/%.o,$(GEN)) $(patsubst %.c,$(SBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(CC) -o $@ $^ `sdl2-config --libs` -lm -ldl

WEBUILD := build-win-esb
$(WEBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(WINCC) $(WCFLAGS) -DNO_SSW -DNO_ROTJ -c $< -o $@
esb.exe: $(W_LAKE) $(patsubst %.c,$(WBUILD)/%.o,$(GEN_ESB)) $(patsubst %.c,$(WEBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(WINCC) -o $@ $^ -L$(SDLWIN)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc

GEN_ROTJ_O := $(patsubst %.c,$(BUILD)/%.o,$(GEN_ROTJ))
RBUILD := build-rotj
$(RBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DNO_SSW -DNO_ESB `sdl2-config --cflags` -c $< -o $@
rotj: $(LAKE_O) $(GEN_ROTJ_O) $(patsubst %.c,$(RBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(CC) -o $@ $^ `sdl2-config --libs` -lm -ldl

WRBUILD := build-win-rotj
$(WRBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(WINCC) $(WCFLAGS) -DNO_SSW -DNO_ESB -c $< -o $@
rotj.exe: $(W_LAKE) $(patsubst %.c,$(WBUILD)/%.o,$(GEN_ROTJ)) $(patsubst %.c,$(WRBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(WINCC) -o $@ $^ -L$(SDLWIN)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc
