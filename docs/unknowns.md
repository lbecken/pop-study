# Unknowns

Each item is something not yet verified, with the next inspection step.
Items move to the relevant document once resolved.

## Timing

| # | Question | Next step |
| --- | --- | --- |
| T1 | Real duration of a game frame and its variance. Source says only "approx. 11 fps" and 725 frames per game minute (`SPECIALK.S:68-70`); `MainLoop` has no delay and no wait for vertical blank, so a frame takes as long as its logic, its drawing and its sounds take. Narrowed by counting cycles: with the original's whole frame running in the simulator (`replay_frames.py demo`), the 390 ordinary passes of the demo took 51,231 to 224,292 cycles, quartiles 72,407 / 119,819 / 128,699, mean 108,204, which at 1,020,484 cycles a second is 106 ms or 9.4 frames a second before sound; the two passes that drew a whole screen took about 0.6 s. The clock counts frames (`docs/port/game-loop.md` §3), so at that pace the "hour" of 43,505 frames would last about 77 minutes of such play. **Not verified**: the simulator's cycle counts against hardware; the time the sounds add; levels other than the demo. | Count the cycles of `playback` for the sounds of each frame and add them; run the traces of `replay_frames.py` for per-level figures; compare one frame with an emulator. |
| T2 | Per-frame delays outside drawing. Found by reading: `PREAD` (`GRAFIX.S:1193-1213`), a loop whose length depends on the joystick position, run each frame when the joystick is selected; `pause` 25 in `setupkid` while the kid has frame 0 (`TOPCTRL.S:807-810`); the freeze loop on ESC. `KEYS` and `BREAD` have none. `FAST` and the blitters have not been timed. | Cycle-count `PREAD` and `pause` in the simulator; read `FRAMEADV.S` `FAST`. |
| T3 | `WHOOP` and `GTONE` timing (the effects in `SOUND.S` are resolved: `docs/audio-analysis.md`). | Assemble `GRAFIX.S` (needs the 65C02 `tsb`) and run them in the simulator. |
| T4 | How long each song freezes the game. | Needs the song data (see A1). |

## Graphics

| # | Question | Next step |
| --- | --- | --- |
| G1 | Masking for draw paths other than `LayMask`: `FASTMASK`, the "visible mask" opacity 5, and what floor pieces use. (`LayMask` itself is resolved: `docs/run-cycle-analysis.md`.) | Read `FASTMASK` and `LayGen` callers in `HIRES.S` / `FRAMEADV.S`. |
| G2 | Effect of mirroring on colour: a mirrored byte swaps dot parity. (Geometry is resolved: exact mirror.) | Decide with G3. |
| G3 | Colour: how dot parity and bit 7 map to the colours seen on screen. The port has a rule-based model (`docs/port/presentation.md` §3), written from the standard descriptions of Apple II hi-res, not from this source and not compared with an emulator or a machine; its known omissions are listed there (half-dot shift, fringes, signal blur). The source itself mentions colour twice, and both agree with parity deciding it: the impact star's x is made even or odd for "kid red, opps blue" (`GAMEBG.S:1010-1021`), and a guard of the other colour is drawn one dot to the right (`DrawShifted`, `GAMEBG.S:441-449`). | Render frames in an emulator with composite output and compare with `render_demo.py`'s; decide then whether the 280-pixel model is enough. |
| G19 | The fourth display switch. `PageFlip` reads `$C05E` when `vibes` is set and `$C05F` otherwise (`SUBS.S:408-412`); `vibes` is set while the weightless potion wears off ("Screen flashes as weightlessness ends", `TOPCTRL.S:1696-1699`) and by the potion code (`MISC.S:321`). On a IIe these are the annunciator-3 switches, which with an 80-column card select double hi-res; what the player saw is **not yet verified**. The port records the switch (`pop_gfx.sw_an3`) and shows nothing for it. | Find a description of `$C05E` without 80-column mode on the IIe/IIc; try it in an emulator. |
| G4 | When and how image-table pointers are relocated from `$6000` to their load addresses. Narrowed: the blitter uses the pointers as they are (`HIRES.S:264-277`) and no loaded game module rewrites them, so the tables on the game disk must already hold relocated pointers; the test oracle relocates on load (`docs/port/blitter.md` §8). `MASTER.S` (the loader) has not been searched line by line. | Read `relocate` in `04 Support/MakeDisk/DRAZ/DRAZ.S:3081-3101` and the table loading in `MASTER.S`. |
| G5 | `PAC.*` packed picture format. | Read `SNGEXPAND` in `UNPACK.S`; test against one `PAC.*` + its raw sibling (e.g. `SS/PAC.SS1` and `SS/SS1`). |
| G6 | Whether the title/prologue/epilog screens exist among the `PAC.*` files. | Decode after G5. |
| G7 | Why `04 Support/DRAZ/IP/IMG.CHTABLE3` does not parse. | Hex-inspect; may be an older table layout. |
| G8 | Contents of trailing bytes in image-table files. | Dump and compare with neighbouring image data. |
| G9 | Which file each `chset` number 0–5 names. The per-level table is `MISC.S:774` (levels 0–2: 0; 3: 1; 4, 5, 7–11: 2; 6: 3; 12: 4; 13, 14: 5). Matching by what is known of the game gives 0 and 2 = guard, 1 = skeleton, 3 = fat guard, 4 = shadow, 5 = vizier, but that mapping is **inferred**, not read from the source. (The port's demo replay, level 0, depends only on set 0.) The five files have different image sizes (`docs/port/player-control.md` §5), so the answer affects collision for opponents, not only their look. | Compare disk track contents with the files, given a disk image. |
| G11 | What uses `IMG.CHTAB6.B`. The altset2 frames reference chtable6 images up to 103, which only `IMG.CHTAB6.A` has (`docs/animation-data.md`). | Trace where `chtable6` is loaded (`MASTER.S` stage 2, side A vs side B) and which scenes run from each. |
| G16 | Background set 2 (levels 7, 8, 9, 12, 13) is loaded from a different track than set 0 but the game directory has one dungeon pair; rendered here with the same files. `04 Support/DRAZ/IP` holds other variants (`IMG.BGTAB.DUN1/2`, `RED1/2`, `TWR1/2`). | Compare with a disk image; parse the variant files and see which the level-7 piece numbers fit. |
| G18 | The mirror: piece id 13 is in no level file. | Read the `mirlevel` handling in `TOPCTRL.S` and `MOVER.S`. |
| G13 | Cropping at screen edges and against foreground (`CROP`, `cropchar`, the `CU/CD/CL/CR` cut-offs). Not modelled; the blitter model refuses off-screen images. | Read `CROP` (`HIRES.S:375-507`) and `CROPCHAR` (`CTRLSUBS.S:1313`). |
| G14 | The XOR draw mode used for the shadow and the one-dot shift used for guards (`DrawEored`, `DrawShifted`, `GAMEBG.S:442-458`). | Read `LayXOR` / `MLayXOR` in `HIRES.S`. |
| G15 | Save-under ("peel") buffers: `LAYRSAVE`, `PEEL`, and the two-frame delay. | Read `HIRES.S:542-639`, `GRAFIX.S` `SNGPEEL`/`ADDPEEL`. |

## Audio

| # | Question | Next step |
| --- | --- | --- |
| A1 | Music score data is not in the repository. | Decide whether to source it from a disk image you own (decision D4). |
| A2 | Song data format and the player's synthesis method. | Derivable from `04 Support/MakeDisk/S/MSYS.S` (`NEWNOTE`, lines 275-463). Not yet analysed. `MSYS.S` should assemble with `mos6502.py` (not yet tried), after which it can be run on any song table. |
| A4 | How an Apple II speaker and case colour the 1-bit signal, if previews should sound like the machine. | Outside the source; would need a measured response or a recording. |
| A3 | Whether `MSYS.S` matches the player on the shipped 3.5" disk. | Compare against a disk image. |

## Engine

| # | Question | Next step |
| --- | --- | --- |
| E19 | The fall-height reading of the landing thresholds (one storey safe, two cost a unit, three kill) is inferred from the constants. | Run the kid-frame trace off ledges of each height once collision is in; compare with an emulator when a disk image is available. |
| E20 | Whether any move is fast enough to arrive inside a barrier that was out of range the frame before, which the edge-crossing scheme would not detect (`docs/port/collision.md` §3.1). | Largest per-frame dx in the sequence table plus the widest image, against the 14-unit column and the one-column margin; then try it in the kid-frame trace. |
| E21 | Whether it is intended that a slicer which has cut someone is no longer a barrier. That it is so is confirmed: the "blood" bit persists (`docs/port/moving-objects.md` §6.8) and `CHECKCOLL` compares the state unmasked. | Not answerable from the source; compare behaviour in an emulator when a disk image is available. |
| E23 | Whether a guard's extra strength unit being taken from the previous guard's type (`docs/port/opponents.md` §8.1) shows in play: which levels have the type that carries the extra unit, and in what order the kid meets guards there. | List `GdStartProg` per level against the table in `autodata.bin`. |
| E22 | Whether link chain 0 is harmless in the shipped levels: a dead body on an "up" plate triggers it (`docs/port/moving-objects.md` §6.1). | Decode `LINKLOC[0]` / `LINKMAP[0]` of each level and see what, if anything, a rubble trigger of it does. |
| E24 | Which build switches the released disk was assembled with. In this checkout `SPECIALK.S:3` has `FinalDisk = 0` while `TOPCTRL.S:4` and `MASTER.S:3` have 1, so the typed codes "POP" and "SKIP" are assembled into the key handler (`docs/port/game-loop.md` §9). | Type the codes in an emulator with a disk image; compare the assembled `SPECIALK` with the disk's. |
| E16 | Level data semantics not yet read: `BLUETYPE` bits 6–7 (`secmask`, `EQ.S:484`). (Bit 5, the link list and the guard start arrays are resolved: `docs/port/moving-objects.md` §4, `docs/port/opponents.md` §7.) | Grep `secmask`. |
| E5 | Neighbouring-screen drawing. (The screen change itself is resolved: `docs/port/opponents.md` §7, `docs/port/game-frame.md` §5.) | Read `FRAMEADV.S`. |
| E7 | Input hardware: joystick thresholds (`setcenter`, `cvtpdl`), the keyboard buffer used for typed codes (`addkey`, `checkcode`). (Device selection and the press-flag protocol are resolved: `docs/port/game-loop.md` §2.) | Read `GRAFIX.S` `SETCENTER`, `SPECIALK.S` `addkey`. |
| E9 | Copy-protection hooks that alter game state. Catalogued so far, all ported as they are: `yellowflag` → `timebomb = $40` on level 7, ORed into every character image number (`SUBS.S:1593-1596`, `CTRLSUBS.S:1036`; `docs/port/game-frame.md` §6); `purpleflag` → on palace levels the second page's save-under list is misfiled (`GRAFIX.S:441-446`, `520-524`; `docs/port/redraw.md` §3), and the final scene starts a "crash" sequence (`SUBS.S:1116-1121`); `redherring` / `redherring2` → the disk-flip prompt goes to the title sequence instead (`TOPCTRL.S:1559-1565`). Not verified: what each failure looks like in play, how the 5.25-inch build sets the flags, and whether there are more (the checks themselves are in `MASTER.S` and the disk routines). | Run a level-7 trace with `yellowflag = 0` and a palace level with `purpleflag = 0` once characters are drawn (milestone 10c); read `MASTER.S` for the checks. |
| E10 | Differences between the 3.5" and 5.25" builds (`*525.S`). | `diff` the pairs. |
| E11 | How `$D000-$DFFF` is shared between language-card banks. | See `docs/memory-map.md`. |
| E12 | Meaning of each `CharAction` value set by `act`. The floor code gives: 0, 1, 7 on the ground; 2 and 6 hanging; 3 a controlled fall; 4 free fall; 5 bumped (`docs/port/player-control.md` §3). What distinguishes 0, 1 and 7, and 2 from 6, is not yet verified. | Read the `CharAction` tests in `COLL.S` and `AUTO.S` as those are ported. |
| E13 | Which code paths write `CharPosn` or `CharSeq` directly, bypassing the table. Found so far: five `CharPosn` sites (`docs/animation-data.md` finding 4); `CharSeq` is written only by `JUMPSEQ`, by `goto` in `ANIMCHAR`, and at `AUTO.S:1881-1883`. Of the 78 `jumpseq` calls, those in the control and floor code are ported (59 distinct sequences, `pop_control.h`). | Read `AUTO.S:1870-1890`; the remaining calls are in `COLL.S`, `AUTO.S`, `TOPCTRL.S`, `MOVER.S`. |
| E15 | Whether any caller depends on the register or flag state a routine leaves beyond its documented outputs. Found and modelled so far: the block readers' X and Y; `GETLEFT` and siblings needing the caller's Z flag; `DoStrike` taking the frame in X; `PickItUp`, `CHECKLEDGE`, `GETSPIKES`, `REMOVEOBJ`, `DOIMPALE` using Y and `BlueSpec` from the last block read; `clrall` returning 1 (`docs/port/player-control.md` §4). | Each new routine's comparison against the original shows any further dependence. |

## Process

| # | Question | Next step |
| --- | --- | --- |
| P1 | Byte-exactness of the project's assembler. It assembles 18 of the 22 original modules and each fits its memory slot (`docs/memory-map.md`), but the bytes have not been compared with an independent assembler or a disk image. | Try Merlin32 on a few modules and compare; or compare with a disk image. |
| P4 | `UNPACK.S` (`mac` macros) and `BOOT.S` (`sav`) do not assemble, and `GRAFIX.S` assembles only up to its final 65816 section (IIGS control panel). None is game logic. | Add macros and 65816 if those modules are ever needed. |
| P5 | The 6502 simulator is verified only for the instructions its tests exercise. Decimal mode (used once, by `GETMINLEFT`) follows the NMOS 6502 and is tested on a handful of values only. | Run a public 6502 functional test suite, including its decimal-mode test, against it. |
| P2 | Git history of the original repository was not examined (shallow clone). | `git fetch --depth=1000` and review. |
| P3 | The author's 1989 technical document referenced in the README is not in the repository. | Read it at jordanmechner.com/library; cite, do not copy. |
