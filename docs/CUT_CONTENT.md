# Cut / unused content tracker

Status key: **confirmed** (seen in the ROM), **lead** (evidence, not yet understood), **external** (known
from outside sources, not yet found in the ROM).

## Scene $13: a removed Death Star stage (confirmed; playable reconstruction in the port)

What survives in the ROM:
- `$81:BDF4` (per-scene handler table): scene $13 uses handler `$BEF8`, like the platform stages.
- `$82:B0F5` (spawn-list table): scene $13 has its **own** list at `$82:A2F2` (truly unused IDs
  share the empty default list). Decoded with `tools/spawnlist.py`, it is one zone covering the
  whole map with three records:
  - `[00]` Luke's start position ($00C0, $04A0)  (every stage's list starts with this record);
  - `[01]`, `[02]` two objects of type **$FE** at ($0010, $0490) and ($0900, $0490).
- Type $FE appears in **no other spawn list**. Its definition (`$82:F995`) has a very wide
  collision box and no working behaviour. Two of them at the far left and far right of a room
  2,304 px wide, at floor height, look a lot like the **walls of the trash compactor** (the stage
  developers have said was cut for memory). That's an interpretation, not proof.
- `$80:8A1A` maps scene $13 to setup record $12, which was **blanked** (no layers), so warping in
  crashed. Its leftover bytes differ from the neighbouring records (`01 01 01 00`).
- The sequence table never routes to it, and the developer area menu doesn't list it.
- The game still picks a song for it (module $0A, track $02).

No map of its own survives (see the chunk audit below), so the original layout is lost.

**Port reconstruction** (`restore/`, menu: "Restored cut stage", off by default). A data patch
(`restore/build_patch.py` → `port/src/restore_patch.h`):
- fills setup record $12 from the detention block's record, with a new 10x7-chunk layout
  (`restore/s13_layout.py`) built from the Death Star chunk set. Luke starts exactly where the
  original record puts him, and the drop into the lower corridor uses chunk 13, which no shipped
  level uses;
- keeps the original three records and adds stormtroopers, droids and an exit trigger (the same
  stage-clear native, `$80:ECBB`, the detention block uses);
- routes detention block → scene $13 → the "deactivate the tractor beam" cutscene, using a dead
  duplicate entry in the sequence table;
- writes new data only into end-of-bank padding (`$81:FFA0`, `$82:FED0`, `$92:FFC0`), so nothing
  the game uses is overwritten. The port applies it to its in-memory cartridge copy, can switch it
  off at any time, and checks the original bytes first.
Tested: loads with full graphics, enemies spawn, the drop works, the exit triggers STAGE CLEAR,
and the route from the detention block works.

**Compactor walls** (port-side, `restore_tick` / `restore_draw_walls` in `port/src/restore.c`):
the two $FE positions become walls that start closing in once Luke moves (about 0.4 px per
frame each). They block him, hurt him while he's pinned (2 HP every 24 frames), and crush him if
they meet while he's still in the upper corridor; the way out is the shaft in the middle. They
are drawn with the level's own wall tiles (`ws_world_pixel`), reset when a life is lost, and
work in widescreen. Still to do: a Dianoga.

## Developer debug menu and cheat system (confirmed)

**Cheat checker** `$81:E3D2` compares new button presses against 7 sequences (table at `$81:E4A9`:
sequence pointer + handler):

| # | Buttons | Handler effect |
|---|---|---|
| 0 | X B B A Y | `$0114` = 7 |
| 1 | A B Y X B B X X Y | `$0114` = $0F |
| 2 | A A A A X B B B B Y X X X X A Y Y Y Y B | "ultimate": `$010C`++, `$0110` |= $C0 (both flags below + debug features) |
| 3 | Y Y X X A B X A | `$0110` |= $40 → start with the lightsaber |
| 4 | Y X X X A A A B B B Y Y | `$0110` |= $80 → Select Hero screen (mode $0C) before every stage |
| 5 | A B A B A B A B | jump straight to cutscene $25 |
| 6 | A Y A Y X B X B X B | `$010E`++ (enables pad-2 stage clear) |

Once a code is used, `$0100` = $D342 locks further entries. The "TRY XBBAY" and "TRY YYXXABXA" text
strings are the in-game hints for codes 0 and 3.

**Controller-2 debug features** (`$80:9BF9`, `$80:9C1D`; pad 2 is read at `$1993`):
- with `$010C` or `$010E` set: pad-2 **Start** = instant stage clear (`$80:ECBF`)
- with `$010C` set: pad-2 **L+R** = **GAME DEBUG MENU** (`$81:D546` with A=6)
- with `$010C` set: pad-2 **Select+A+B+X+Y** = spawns a debug object (invincibility/coordinates)

**GAME DEBUG MENU** (`$81:DADC`): controller 1 Up/Down picks a row, Left/Right changes it, and
Start/Select warps. Rows: AREA, STAGE (checkpoint), LIVES, HEALTH, ACTOR (Luke/Han/Chewie) and
WEAPON. The area list (`$81:E247`, mode/scene pairs, terminated by $80) has exactly the
15 shipped stages. **Scene $13 isn't in it**, so it was removed from the developers' own menu too.
The two mode-$08 flight scenes aren't listed either.

**For the PC port:** a proper level select and a debug or practice menu are already built into the
game. The port can expose them from its own menu, with no button codes or second controller.

## Unused scene IDs $03, $0F, $15 (confirmed empty)

These IDs use the default do-nothing handler and the empty spawn list, so they're just gaps in the
numbering and contain nothing.

## Object scripts that never ran (lead)

Every object type's state table (`$82:FDCD` → def+$2F) lists 455 script entry points. 151 of them
never executed in any coverage run. Most are probably rare states (specific deaths, difficulty
variants), but any that belong to types no spawn list uses are unused enemies. To check next: which
object types appear in no spawn list and no script spawn op.

### Object types: catalogue (partial)

`port/src/test_types.c` plays every debug-menu area from each of its first four checkpoints
(god mode, random input) and logs every object type that appears; `tools/spawnlist.py` lists
the types placed by spawn lists. Of 127 types, **18 have scripts but were neither seen nor placed
by any spawn list**: $1E, $28, $34, $3A, $3E, $40, $42, $4C, $8E, $B4, $BA, $C2, $C4, $E6, $F2,
$F4, $F6, $F8. Some are surely spawned by bosses or scripts the bot never reached (it saw 66
types and missed 34 that spawn lists do place), so this is a shortlist, not a verdict. Spawning
each one in the restored stage: $B4 appears as a crate-like object that fires a blue shot, $C2 as
a small flying sprite, $C4 as a projectile; the rest show nothing there (invisible helpers, or
their graphics aren't loaded in that stage).

## Native routines only reachable from scripts that never ran (lead)

Called from decoded script bytecode but never executed in any coverage run (god-mode bot on every
scene): `$81:FD82`, `$80:9030`, `$9F:DD62`, `$9F:F0D8`, `$9F:F11B`, `$80:ECBB`, `$82:9A61`.
Some will be rare-path code (specific pickups, deaths); others may be unused enemy behaviors.

## Level map chunks: audit (done)

Each level bank has a header (`$03EA`) whose chunk pointer table starts at +$14, followed by
6-byte grid descriptors (width, height, `10 10 00 01`) and LZSS chunk grids. The setup records
(`$80:8A43`, stride $16, 4 layer pointers into bank $81) say which grid each layer uses.

Decoding every grid that a shipped setup record uses shows that most of the "102 never-loaded"
blocks were just parts of real levels the coverage bot never reached. Chunks that **no shipped
grid references**:
- Death Star bank `$92`: chunks 0, 1 (hangar wall variants with extra floating platforms),
  10, 13, 14 (hex-tunnel junction variants with shafts above and below) and 20 (wall);
- bank `$8A`: chunks 0, 1. Their metatile numbers don't fit the shipped metatile set and render
  as garbage, so they're probably leftovers from an earlier version of that tileset;
- bank `$8C`: chunk 27 (a plain filler wall).

`tools/chunkview.py` renders chunks (and whole maps) from a scene's WRAM and VRAM dump.

## Large unreferenced compressed blocks (lead)

Five never-loaded LZSS blocks have no 16- or 24-bit reference anywhere in the ROM:
`$91:835C` (576 B out), `$98:ECB8` (896 B), `$98:EEB6` (256 B), `$99:9C43` (1056 B) and
**`$9A:FBA3` (45,056 B out)**. The big one has the same format and exact output size as `$9C:F47B`,
which the game does load (a byte-per-word table; only its first 1 KB is non-zero). The orphan's first
4 KB are non-zero bytes in the same word layout, but they don't decode as 2bpp, 4bpp or Mode 7
graphics or as a tilemap of any obvious width. Still unidentified.
Separately, `$98:D177` (56 KB out) was never loaded either, but something may reference it.

## Text (lead, needs checking)

The text bank near `$90:91F2` (level names) also holds strings such as "PLAY ME LOUD",
"TURN UP THE VOLUME", "TRY XBBAY…" and "TRY YYXXABXA…". These look like option-menu / cheat hints.
Need to confirm whether the options menu actually displays them.

## External

- **Trash compactor / Dianoga stage**: cut for memory (see scene $13 above).
- **PC (MS-DOS) port by Brain Bug** (1994–95): cancelled; a near-complete prototype leaked.
  It's a separate codebase, but it may show content or layouts the SNES version dropped.
- **Sega Genesis / Sega CD ports**: announced and cancelled; nothing to recover from this ROM.

## Ideas for restoring content in the PC port

- (Done, first pass) a playable stage in scene $13's slot built around the surviving spawn
  list. Next: make the $FE walls close in, and add the Dianoga.
- Add a debug "scene select" that lists the empty IDs, so restored or new stages slot in without
  renumbering anything.
