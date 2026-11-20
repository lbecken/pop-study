# Unknowns

Each item is something not yet verified, with the next inspection step.
Items move to the relevant document once resolved.

## Timing

| # | Question | Next step |
| --- | --- | --- |
| T1 | Real duration of a game frame and its variance. Source says only "approx. 11 fps" and 725 frames per game minute (`SPECIALK.S:68-70`). | Measure in an emulator with a disk image, or cycle-count a representative frame. |
| T2 | Whether any per-frame delay hides in routines not yet read (`keys`, `controller`, `fast`). The `MainLoop` body and `PAGEFLIP` have none. | Read `SPECIALK.S` `KEYS`, `GRAFIX.S` `controller`, `FRAMEADV.S` `FAST`. |
| T3 | `WHOOP` and `GTONE` timing (the effects in `SOUND.S` are resolved: `docs/audio-analysis.md`). | Assemble `GRAFIX.S` (needs the 65C02 `tsb`) and run them in the simulator. |
| T4 | How long each song freezes the game. | Needs the song data (see A1). |

## Graphics

| # | Question | Next step |
| --- | --- | --- |
| G1 | Masking for draw paths other than `LayMask`: `FASTMASK`, the "visible mask" opacity 5, and what floor pieces use. (`LayMask` itself is resolved: `docs/run-cycle-analysis.md`.) | Read `FASTMASK` and `LayGen` callers in `HIRES.S` / `FRAMEADV.S`. |
| G2 | Effect of mirroring on colour: a mirrored byte swaps dot parity. (Geometry is resolved: exact mirror.) | Decide with G3. |
| G3 | Colour: how dot parity and bit 7 map to the colours seen on screen, and what the game does to keep sprite colours stable when shifted. | Read `SHIFTn`/`CARRYn` generation and `LayXOR`; then pick a rendering model (decision D3). |
| G4 | When and how image-table pointers are relocated from `$6000` to their load addresses. | Read `relocate` in `04 Support/MakeDisk/DRAZ/DRAZ.S:3081-3101`. |
| G5 | `PAC.*` packed picture format. | Read `SNGEXPAND` in `UNPACK.S`; test against one `PAC.*` + its raw sibling (e.g. `SS/PAC.SS1` and `SS/SS1`). |
| G6 | Whether the title/prologue/epilog screens exist among the `PAC.*` files. | Decode after G5. |
| G7 | Why `04 Support/DRAZ/IP/IMG.CHTABLE3` does not parse. | Hex-inspect; may be an older table layout. |
| G8 | Contents of trailing bytes in image-table files. | Dump and compare with neighbouring image data. |
| G9 | Which file each `chset` number 0–5 names. The per-level table is `MISC.S:774` (levels 0–2: 0; 3: 1; 4, 5, 7–11: 2; 6: 3; 12: 4; 13, 14: 5). Matching by what is known of the game gives 0 and 2 = guard, 1 = skeleton, 3 = fat guard, 4 = shadow, 5 = vizier, but that mapping is **inferred**, not read from the source. The five files have different image sizes (`docs/port/player-control.md` §5), so the answer affects collision for opponents, not only their look. | Compare disk track contents with the files, given a disk image. |
| G11 | What uses `IMG.CHTAB6.B`. The altset2 frames reference chtable6 images up to 103, which only `IMG.CHTAB6.A` has (`docs/animation-data.md`). | Trace where `chtable6` is loaded (`MASTER.S` stage 2, side A vs side B) and which scenes run from each. |
| G16 | Background set 2 (levels 7, 8, 9, 12, 13) is loaded from a different track than set 0 but the game directory has one dungeon pair; rendered here with the same files. `04 Support/DRAZ/IP` holds other variants (`IMG.BGTAB.DUN1/2`, `RED1/2`, `TWR1/2`). | Compare with a disk image; parse the variant files and see which the level-7 piece numbers fit. |
| G17 | Partial redraw: redraw buffers, wipes and how `FAST` chooses what to redraw. | Read `RedBlockFast` (`FRAMEADV.S:381-475`) and the marking routines in `CTRLSUBS.S`. |
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
| E17 | How strength changes are applied: the control code only sets `ChgKidStr` / `ChgOppStr` (`DECSTR`); what adds them to `KidStrength` and when is not yet read. | Read `TOPCTRL.S` for `ChgKidStr` (after `NextFrame`) and `MISC.S` around `DECSTR`. |
| E19 | The fall-height reading of the landing thresholds (one storey safe, two cost a unit, three kill) is inferred from the constants. | Run the kid-frame trace off ledges of each height once collision is in; compare with an emulator when a disk image is available. |
| E20 | Whether any move is fast enough to arrive inside a barrier that was out of range the frame before, which the edge-crossing scheme would not detect (`docs/port/collision.md` §3.1). | Largest per-frame dx in the sequence table plus the widest image, against the 14-unit column and the one-column margin; then try it in the kid-frame trace. |
| E21 | Whether it is intended that a slicer which has cut someone is no longer a barrier. That it is so is confirmed: the "blood" bit persists (`docs/port/moving-objects.md` §6.8) and `CHECKCOLL` compares the state unmasked. | Not answerable from the source; compare behaviour in an emulator when a disk image is available. |
| E22 | Whether link chain 0 is harmless in the shipped levels: a dead body on an "up" plate triggers it (`docs/port/moving-objects.md` §6.1). | Decode `LINKLOC[0]` / `LINKMAP[0]` of each level and see what, if anything, a rubble trigger of it does. |
| E16 | Level data semantics not yet read: `BLUETYPE` bits 6–7 (`secmask`, `EQ.S:484`) and the guard start arrays in `INFO`. (Bit 5, `reqmask`, and the link list are resolved: `docs/port/moving-objects.md` §4.) | Read `AUTO.S` `ADDGUARD`; grep `secmask`. |
| E3 | Opponent AI (`ShadCtrl`, `AutoCtrl`, `guardprog`). | Read `AUTO.S`. |
| E5 | Screen-cut logic and neighbouring-screen drawing. | Read `cutcheck`, `PrepCut`, `FRAMEADV.S`. |
| E6 | What seeds the random number generator and which code calls it besides the moving objects. (`RND` itself is ported: seed ← 5·seed + 23 mod 256.) | Grep `RNDseed` and `jsr rnd` across the modules. |
| E7 | Input: joystick thresholds, keyboard buffering, the `clr*` flag protocol. | Read `GRAFIX.S` `controller`, `SPECIALK.S` `KEYS`, `CLRJSTK`. |
| E8 | Demo-level recorded input format (`demo`, `PreRecPtr`). | Read `AUTO.S` `AUTOPLAYBACK`. Useful later as a replay test. |
| E9 | Copy-protection hooks that alter game state (`yellowflag`, `purpleflag`, `timebomb`, `redherring`). `decodeim` ORs `timebomb` into the image number (`CTRLSUBS.S:1036`). | Catalogue them so the port can treat them as always-passing. |
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
| P5 | The 6502 simulator is verified only for the instructions its tests exercise. | Run a public 6502 functional test suite against it before using it as an oracle for larger routines. |
| P2 | Git history of the original repository was not examined (shallow clone). | `git fetch --depth=1000` and review. |
| P3 | The author's 1989 technical document referenced in the README is not in the repository. | Read it at jordanmechner.com/library; cite, do not copy. |
