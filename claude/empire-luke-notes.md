# Empire Luke in Super Star Wars: notes (2026-10-13)

Goal (T-Man): after beating both games, play Super Star Wars as Empire Luke with all of Empire's moves,
blaster + thermal detonators + lightsaber, Force meter and all 8 Force powers at once (New Game+ style).

## Done (branch `claude/empire-luke`)
- `src/empire_luke.c`: builds a new player sprite set at start-up from both ROMs and swaps it in.
  Extras menu item EMPIRE LUKE (cfg `empire_luke`). `ELUKE_TEST 1` opens it for test builds; the intended
  lock is `(cfg.unlocked & 3) == 3` (both games' extras unlocked).
- Luke's palette (sprite palette 7) gets Empire's colours; HUD face = Empire's face tiles.
- Empire Luke starts with the lightsaber (Super Star Wars' lightsaber-start flag, not counted as a code).
- `src/test_luke.c` (`make ssw_luke`): scripted input (`SEQ=`), warp (`WARP=MMSS`), DMA/unpack/draw/pose
  logging (`LZLOG`, `POSELOG=<table addr>`, `DRAWLOG`, `RANGE=lo:hi:frame`, `WATCH=`), dumps (`DUMP=`),
  screenshots (`SHOTAT=`, `SHOTSTEP=`), `ELUKE=<sesb.sfc>`, `POKE=adr:val` (every frame). Needs `INTERP=1`
  for anything that reads the CPU's PC.

## How the player is drawn (same engine in both games)
- Sprite sets are headerless-LZSS blocks (unpacker SSW `$81:85DC` src `$3E` dst `$45`; Empire `$81:87D4`
  src `$52` dst `$59`). Flags byte LSB first: 1 = literal, 0 = `b0 b1`: distance `b0 | (b1>>4)<<8`
  (0 = end), length `(b1&15)+3`.
- SSW Luke: `$85:8002` -> `$7F:1820`, 0x792E bytes (next block at `$7F:914E`). Han/Chewie use other blocks.
  Empire Luke (one outfit, every stage): `$8A:8002`, 0x85DC bytes. Empire also loads `$93:8002` (shared)
  and `$A8:B1AF` (small) at stage start; `$88:8002` / `$90:8002` are other heroes' sets.
- Block: `+0` pool offset, `+2` entry count N, `+4..` entries; pose k's frame is at block + word(+2+2k).
  Frame = parts until `$FF`: flags (bits 4-5: 0 = 8x8 tile, 1 = 16x16 from 4 tiles TL TR BL BR,
  2/3 = sub-frame; bits 6-7 = h/v flip), dx, dy (signed), then 1 / 4 tile words (pool offsets) or a
  sub-frame number s, which means pose s+1. After a sub-frame the part offsets reset to 0.
- Drawer: SSW `$81:BFC7` (frame `[$3E]`, table base `$04F8` = block+4, position `$088A/$088C`, tile
  cache `$7E:2000`, piece queue `$0544/$0644/$0744` -> NMI loop `$80:DD80`). Empire `$81:B5FB` (frame
  `[$52]`, table `$07AA`, position `$0346/$0348`, queue `$0806/$0906/$0A06`, NMI `$80:D620`).
- Pose logs (`POSELOG`) print the entry index from +4, i.e. pose number - 1.
- Palettes in ROM: SSW Luke `$90:C782` (16 colours), Empire Luke `$96:9D54`. SSW keeps a CGRAM copy at
  `$7E:1D9F` (+ fade buffers containing palette 7 at `$7E:26E2`, `$7E:28E2`). Hurt flash = palette 4.
  Empire: Luke palette 6, lightsaber palette 5. HUD face: Empire tiles at `$9B:FD7D` (4 tiles).

## Pose meanings
- SSW (pose numbers): 1-8 run, 9-16 blaster in hand while running, 17-24 saber while running, 25 take-off,
  26-28 jump, 29-36 blaster/saber for jump, 37 land, 38 crouch, 43-45 hit/burning, 52-56 knocked down,
  67 turn, 68 stand, 69 stand+shoot (blaster, gun overlay 70-72), 76-86 standing lightsaber swing,
  87-97 jumping swing, 98-109 crouching swing, 110-123 + 133-148 saber blades/arcs, 124-132 somersault.
  SSW's blaster can't aim; Y with saber walks through the swing poses.
- Empire (pose numbers): draws Luke as upper body + legs. 14-21 running legs with 22-29 upper bodies;
  72/73 stand; 75/76 crouch/land; 93-95 jump (saber mode); 3/4 bent legs; 113-115 standing legs;
  103 hit; 46-50 knocked down; 152-159 somersault; blaster: 7/8 forward, 11/12 diagonal up, 13/14 up,
  9/10 diagonal down (upper bodies, + standing legs 113); saber swing upper bodies 136-140 (forward),
  126-131 (up). A button switches blaster/saber, Y fires/swings.
- The mapping is `build_map()` in `src/empire_luke.c`.

## Moves, weapons, Force (test build 2, `eluke_play`)
- SSW already has slide (Down+jump while running, poses 53-57) and somersault (Up+jump, poses 125-132).
- Double jump: jump pressed again after 6+ airborne frames sets Y speed `$0FF1` to -1856.
- Object tables (word per slot, 20 slots, 0 = player): type `$09D9`, HP `$0A79` (player max `$08FF`),
  event `$0B69`, script ptr `$0B91`/bank `$0BB9`, `$0C09`, script wait `$0C59`, X `$0F51`, X speed `$0F79`,
  Y `$0FC9`, Y speed `$0FF1`. After a hit `$04EE` counts down 32 frames. Damage routine `$80:EC6A`.
- Defeating an enemy = HP 0 + event $44 delivered like `$80:CF91` (`send_event`).
- Force meter + powers + thermal detonators: L/R choose, X uses; HUD line from `eluke_hud` in `hud_build`.

## Next steps
1. Visuals: spin on the double jump, a flying saber for Saber Control, thrown bomb + blast for detonators,
   a look for Invisible / Deflect.
2. Turn `ELUKE_TEST` off; unlock after beating both games.
