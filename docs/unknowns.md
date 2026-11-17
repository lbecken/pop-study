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
| G1 | How characters are masked against the background (no mask is stored with the image). | Read `LayMask`, `FASTMASK`, `MASKTAB` in `HIRES.S` / `HRTABLES.S`. |
| G2 | Mirrored drawing for right-facing characters and its effect on x alignment and colour. | Read `MLAY` (`HIRES.S`) and `MIRROR` table. |
| G3 | Colour: how dot parity and bit 7 map to the colours seen on screen, and what the game does to keep sprite colours stable when shifted. | Read `SHIFTn`/`CARRYn` generation and `LayXOR`; then pick a rendering model (decision D3). |
| G4 | When and how image-table pointers are relocated from `$6000` to their load addresses. | Read `relocate` in `04 Support/MakeDisk/DRAZ/DRAZ.S:3081-3101`. |
| G5 | `PAC.*` packed picture format. | Read `SNGEXPAND` in `UNPACK.S`; test against one `PAC.*` + its raw sibling (e.g. `SS/PAC.SS1` and `SS/SS1`). |
| G6 | Whether the title/prologue/epilog screens exist among the `PAC.*` files. | Decode after G5. |
| G7 | Why `04 Support/DRAZ/IP/IMG.CHTABLE3` does not parse. | Hex-inspect; may be an older table layout. |
| G8 | Contents of trailing bytes in image-table files. | Dump and compare with neighbouring image data. |
| G9 | Which opponent each `IMG.CHTAB4.*` set belongs to, by code rather than by file name. | Trace `ch4trk` / `CHset` selection (`MASTER.S:524-591`) back to the per-level table. |
| G10 | Block width in game units (assumed 14). | Read `BlockEdge` data in `TABLES.S`. |

## Audio

| # | Question | Next step |
| --- | --- | --- |
| A1 | Music score data is not in the repository. | Decide whether to source it from a disk image you own (decision D4). |
| A2 | Song data format. | Derivable from `04 Support/MakeDisk/S/MSYS.S` (`NEWNOTE`, lines 275-463). Not yet analysed. |
| A3 | Whether `MSYS.S` matches the player on the shipped 3.5" disk. | Compare against a disk image. |

## Engine

| # | Question | Next step |
| --- | --- | --- |
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

## Process

| # | Question | Next step |
| --- | --- | --- |
| P1 | Whether the source assembles unmodified with a modern Merlin-compatible assembler. This would give a byte-exact reference binary and an oracle for unit tests. | Try Merlin32 on one self-contained module (`SEQTABLE.S`). |
| P2 | Git history of the original repository was not examined (shallow clone). | `git fetch --depth=1000` and review. |
| P3 | The author's 1989 technical document referenced in the README is not in the repository. | Read it at jordanmechner.com/library; cite, do not copy. |
