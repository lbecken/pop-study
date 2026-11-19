# Unknowns

Each item is something not yet verified, with the next inspection step.
Items move to the relevant document once resolved.

## Timing

| # | Question | Next step |
| --- | --- | --- |
| T1 | Real duration of a game frame and its variance. Source says only "approx. 11 fps" and 725 frames per game minute (`SPECIALK.S:68-70`). | Measure in an emulator with a disk image, or cycle-count a representative frame. |
| T2 | Whether any per-frame delay hides in routines not yet read (`keys`, `controller`, `fast`). The `MainLoop` body and `PAGEFLIP` have none. | Read `SPECIALK.S` `KEYS`, `GRAFIX.S` `controller`, `FRAMEADV.S` `FAST`. |
| T3 | Exact frequency and length of each sound effect. | Cycle-count `tone` (`SOUND.S:331-352`) for each (pitch, duration) pair. |
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
| G9 | Which file each `chset` number 0–5 names. The per-level table is `MISC.S:774` (levels 0–2: 0; 3: 1; 4, 5, 7–11: 2; 6: 3; 12: 4; 13, 14: 5). Matching by what is known of the game gives 0 and 2 = guard, 1 = skeleton, 3 = fat guard, 4 = shadow, 5 = vizier, but that mapping is **inferred**, not read from the source. | Compare disk track contents with the files, given a disk image. |
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
| A2 | Song data format. | Derivable from `04 Support/MakeDisk/S/MSYS.S` (`NEWNOTE`, lines 275-463). Not yet analysed. |
| A3 | Whether `MSYS.S` matches the player on the shipped 3.5" disk. | Compare against a disk image. |

## Engine

| # | Question | Next step |
| --- | --- | --- |
| E16 | Level data semantics not yet read: `BLUETYPE` bit 5 (`reqmask`) and bits 6–7 (`secmask`), the guard start arrays in `INFO`, and how the link list is walked when a plate is pressed. | Read `MOVER.S` `PUSHPP` and `AUTO.S` `ADDGUARD`. |
| E1 | Collision system (`checkbarr`, `collisions`, the `CD*` buffers). | Read `COLL.S` from the top. |
| E2 | `gravity` / `addfall` / `checkfloor` and fall damage. | Read `CTRL.S` / `CTRLSUBS.S`. |
| E3 | Opponent AI (`ShadCtrl`, `AutoCtrl`, `guardprog`). | Read `AUTO.S`. |
| E4 | TROB/MOB state machines. | Read `MOVER.S` with `COMMENTS.S:1-87`. |
| E5 | Screen-cut logic and neighbouring-screen drawing. | Read `cutcheck`, `PrepCut`, `FRAMEADV.S`. |
| E6 | Random number generator. | Read `RND` at `GRAFIX.S:1386`. |
| E7 | Input: joystick thresholds, keyboard buffering, the `clr*` flag protocol. | Read `GRAFIX.S` `controller`, `SPECIALK.S` `KEYS`, `CLRJSTK`. |
| E8 | Demo-level recorded input format (`demo`, `PreRecPtr`). | Read `AUTO.S` `AUTOPLAYBACK`. Useful later as a replay test. |
| E9 | Copy-protection hooks that alter game state (`yellowflag`, `purpleflag`, `timebomb`, `redherring`). `decodeim` ORs `timebomb` into the image number (`CTRLSUBS.S:1036`). | Catalogue them so the port can treat them as always-passing. |
| E10 | Differences between the 3.5" and 5.25" builds (`*525.S`). | `diff` the pairs. |
| E11 | How `$D000-$DFFF` is shared between language-card banks. | See `docs/memory-map.md`. |
| E12 | Meaning of each `CharAction` value set by `act` (0–7 seen in the table). | Read the `CharAction` tests in `CTRL.S`, `COLL.S`, `CTRLSUBS.S`. |
| E14 | How `DoRunjump` adjusts `CharX` before a running jump so the take-off lines up with a ledge (`CTRL.S:1752` on). | Read to the end of `DoRunjump`; add to the run-jump trace. |
| E13 | Which code paths write `CharPosn` or `CharSeq` directly, bypassing the table. Found so far: five `CharPosn` sites (`docs/animation-data.md` finding 4); `CharSeq` is written only by `JUMPSEQ`, by `goto` in `ANIMCHAR`, and at `AUTO.S:1881-1883`. There are 78 `jumpseq` calls. | Read `AUTO.S:1870-1890`; list the 78 `jumpseq` calls per trigger as each control routine is ported. |
| E15 | Whether any caller of `ADDCHARX`, `JUMPSEQ` or `ANIMCHAR` depends on the register or flag state they leave (the C port does not model it). | Check each call site as it is ported; `jumpseq` leaves X = index, `animchar` leaves A = frame. |

## Process

| # | Question | Next step |
| --- | --- | --- |
| P1 | Whether the source assembles unmodified with a modern Merlin-compatible assembler. This would give a byte-exact reference binary and an oracle for unit tests. The project's own data assembler handles `FRAMEDEF.S` and `SEQTABLE.S` and its output fits the declared memory areas, but has not been compared with an independent assembler or a disk image. | Try Merlin32 on `SEQTABLE.S` and compare bytes with `merlin_data.py`. |
| P2 | Git history of the original repository was not examined (shallow clone). | `git fetch --depth=1000` and review. |
| P3 | The author's 1989 technical document referenced in the README is not in the repository. | Read it at jordanmechner.com/library; cite, do not copy. |
