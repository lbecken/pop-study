# Memory map (preliminary)

Taken from the equate files of the 3.5" version. Addresses are **confirmed** as
declared in the source; what occupies the gaps, and exact module lengths, are
**not yet verified** (the modules were never assembled here).

Target machine: 128K Apple IIe / IIc / IIGS. The game uses main and auxiliary
64K banks, both language-card areas, and both zero pages.

> "Game code sits in auxmem & aux l.c. and uses aux z.p. Routines in main l.c.
> (including MASTER and HIRES) are called via intermediary routines in GRAFIX
> (in auxmem). RW18 sits in bank 1 of main language card."
> — `MASTER.S:171-180`

## Main memory

| Address | Contents | Source |
| --- | --- | --- |
| `$0000-$0017` | Blitter parameters (`PAGE`, `XCO`, `YCO`, `OFFSET`, `IMAGE`, `OPACITY`, `TABLE`, `PEEL*`, cut-offs, `BANK`) | `EQ.S:365-382` |
| `$0800` | Boot sector, then `chtable3` in game | `BOOT.S:2`, `GAMEEQ.S:11` |
| `$2000-$3FFF` | Hi-res page 1 | `HRTABLES.S:9-44` |
| `$4000-$5FFF` | Hi-res page 2 (`PAGE` = `$20` is added to row addresses) | `HRTABLES.S:12`, `SUBS.S:397-418` |
| `$5000-$53FF` | Staging area for the 1K music table before it is moved | `MASTER.S:247-261` |
| `$6000` | `chtable1` | `GAMEEQ.S:9`, `GRAFIX.S:184-187` |
| `$8400` | `chtable2` | same |
| `$A800` | `chtable5` | same |
| `$D000` LC bank 1 | RW18 disk routines | `EQ.S:11`, `MASTER.S:178` |
| `$D000`, `$D800` | `peelbuf1`, `peelbuf2` (save-under buffers, one per hi-res page) | `EQ.S:12-13`, `GRAFIX.S:997-1016` |
| `$E000-$E9AF` | `hrtables`: `YLO` `$E000`, `YHI` `$E0C0`, `SHIFT0-6` `$E180`, `CARRY0-6` `$E500`, `MIRROR` `$E880`, `MASKTAB` `$E900`, pointer and edge-mask tables `$E980` (addresses **confirmed** by assembling `HRTABLES.S`) | `EQ.S:14`, `HRPARAMS.S:4-40` |
| `$EA00` | `unpack` | `EQ.S:15` |
| `$EE00` | `hires` blitters | `EQ.S:16` |
| `$F880` | `master` | `EQ.S:17` |

`HIRES.S:40-41` declares `peelbuf2 = $d600` while `EQ.S:13` says `$d800`.
The live value is `$D800` (**confirmed**: the only code that uses either
name is `ZEROPEEL`, `GRAFIX.S:1004-1013`, and `GRAFIX.S` takes its equates
from `EQ.S`; `HIRES.S` never uses its own two). Each buffer is 2,048 bytes.

## Auxiliary memory

| Address | Contents | Source |
| --- | --- | --- |
| `$0000-$0017` | Blitter parameters (aux zero page mirror of the above) | `EQ.S:365-382` |
| `$0018-$003F` | Input state, `SCRNUM`, blueprint pointers, neighbouring screens | `EQ.S:390-424` |
| `$0040-$00E7` | Game globals: `Char`, `Kid`, `Shad` records, `FCharVars`, collision and state variables | `GAMEEQ.S:389-496` |
| `$00E8…` | `locals` scratch | `GAMEEQ.S:384` |
| `$0200-$0211` | Mode flags, joystick calibration, `soundon`, `keypress`, `IIGS` | `EQ.S:431-449` |
| `$0212…` | `milestone`, `GuardColor`, `MSset`… | `GAMEEQ.S:503-515` |
| `$0300-$031F` | `MinLeft`, `FrameCount`, `SPEED`, `SongCue`, `NextLevel`… | `GAMEEQ.S:517-541` |
| `$0320…` | Collision-detection buffers, `Op` record, key buffer | `GAMEEQ.S:543-559` |
| `$03C0-$03EF` | `sortX`, `BELOW`, `SBELOW` | `EQ.S:451-455` |
| `$03F0-$03F8` | `bluepTRK`, `level`, `BBundID`… (parameters shared with MASTER) | `EQ.S:457-467` |
| `$0400` | `grafix` | `EQ.S:21` |
| `$0E00-$1288` | `tables` (byte/offset/block/pixel conversion, `Mult7/10/30`, block edges, floor lines); extent **confirmed** by assembling `TABLES.S` | `EQ.S:22, 243-256` |
| `$1290` | `frameadv` | `EQ.S:23` |
| `$2000` | `topctrl` | `GAMEEQ.S:20` |
| `$2800` | Frame definitions (`Fdef` 1200 bytes, `altset1` 200, `altset2` 450, `swordtab` 192) | `GAMEEQ.S:21`, `FRAMEDEF.S:8-15` |
| `$3000` | `seqtab` sequence table | `GAMEEQ.S:22` |
| `$3A00` | `ctrl` | `GAMEEQ.S:23` |
| `$4500` | `coll` | `GAMEEQ.S:24` |
| `$4C00` | `gamebg` | `GAMEEQ.S:25` |
| `$5400` | `auto` | `GAMEEQ.S:26` |
| `$5E00` | `redbufs` redraw buffers (eight 30-byte buffers and a 10-byte one) | `EQ.S:24, 218-229` |
| `$6000`, `$8400` | `bgtable1`, `bgtable2` (`TOPCTRL.S:125`); `chtable6` shares `$6000` in princess scenes | `GAMEEQ.S:14-18`, `GRAFIX.S:184-187`, `MASTER.S:648` |
| `$9600` | `chtable4` (opponent set, swapped per level) | `GAMEEQ.S:12`, `MASTER.S:553-591` |
| `$9F00` | `chtable7` | `GAMEEQ.S:15` |
| `$AC00-$B5FF` | `imlists` image lists (bg, fg, wipe, peel, mid, obj, msg) | `EQ.S:26-27, 294-356` |
| `$B600` | `mobtables`: TROB/MOB lists, `soundtable` | `GAMEEQ.S:28, 46-65` |
| `$B6F0` | `savedgame` | `GAMEEQ.S:29, 67-74` |
| `$B700-$BFFF` | `blueprnt` working level: `BLUETYPE`, `BLUESPEC`, `LINKLOC`, `LINKMAP`, `MAP`, `INFO` | `EQ.S:28, 258-287` |

## Auxiliary language card

| Address | Contents | Source |
| --- | --- | --- |
| `$D000` bank 1 | `bluecopy` (backup of the level) | `EQ.S:30-32` |
| `$D000` | `MusicTable` (1K of song data; which bank is not yet verified) | `MSYS.S:20`, `MASTER.S:247-261` |
| `$D000` | `ctrlsubs` | `GAMEEQ.S:32` |
| `$D400` | `msys` music player | `GAMEEQ.S:31` |
| `$D900` | `specialk` | `GAMEEQ.S:33` |
| `$DFD8` | `textline` version string | `GAMEEQ.S:34` |
| `$E000` | `subs` | `GAMEEQ.S:35` |
| `$EA00` | `sound` | `GAMEEQ.S:36` |
| `$EE00` | `mover` | `GAMEEQ.S:37` |
| `$F900` | `misc` | `GAMEEQ.S:38` |
| `$FC00` | `debugs` | `GAMEEQ.S:39` |

How `ctrlsubs`, `msys` and the bank-1 data share `$D000-$DFFF` between the two
4K banks is **not yet verified** (next step: read `MISC.S` `MOVEAUXLC` and the
bank switching in `GRAFIX.S:1589-1660`).

## Assembled extents

**Confirmed** by assembling the original modules with the project's own
assembler (`tools/extract_assets/mos6502.py`). Each fits below the module that
follows it, several with only a byte or two to spare, which is also evidence
that the assembler reproduces the original instruction sizes. The byte values
have not been compared with an independent assembler or a disk image.

| Module | Extent | Bytes | Next module starts |
| --- | --- | --- | --- |
| `TABLES.S` | `$0E00-$1288` | 1161 | `frameadv` `$1290` |
| `FRAMEADV.S` | `$1290-$1FA9` | 3354 | `topctrl` `$2000` |
| `TOPCTRL.S` | `$2000-$27F8` | 2041 | frame definitions `$2800` |
| `FRAMEDEF.S` | `$2800-$2FD0` | 2001 | `seqtab` `$3000` |
| `SEQTABLE.S` | `$3000-$39F1` | 2546 | `ctrl` `$3A00` |
| `CTRL.S` | `$3A00-$44FE` | 2815 | `coll` `$4500` |
| `COLL.S` | `$4500-$4BF1` | 1778 | `gamebg` `$4C00` |
| `GAMEBG.S` | `$4C00-$5215` | 1558 | `auto` `$5400` |
| `AUTO.S` | `$5400-$5DC0` | 2497 | `redbufs` `$5E00` |
| `GRAFIX.S` (without its IIGS tail) | `$0400-$0DAA` | 2475 | `tables` `$0E00` |
| `CTRLSUBS.S` | `$D000-$D8FE` | 2303 | `specialk` `$D900` |
| `SPECIALK.S` | `$D900-$DE25` | 1318 | `textline` `$DFD8` |
| `VERSION.S` | `$DFD8-$DFF4` | 29 | `subs` `$E000` |
| `SUBS.S` | `$E000-$E965` | 2406 | `sound` `$EA00` |
| `SOUND.S` | `$EA00-$EB2E` | 303 | `mover` `$EE00` |
| `MOVER.S` | `$EE00-$F863` | 2660 | `misc` `$F900` |
| `MISC.S` | `$F900-$FE2B` | 1324 | end of memory |
| `HRTABLES.S` (main) | `$E000-$E9AF` | 2480 | `unpack` `$EA00` |
| `HIRES.S` (main) | `$EE00-$F843` | 2628 | `master` `$F880` |
| `MASTER.S` (main) | `$F880-$FFF5` | 1910 | CPU vectors `$FFFA` |

`MISC.S` runs past `debugs = $FC00` (`GAMEEQ.S:39`); the debug module is not in
the game directory and is presumably absent from the release build
(**inferred**).

`UNPACK.S` (macros) and `BOOT.S` (`sav`) do not assemble, and `GRAFIX.S` is
assembled without its final 65816 section. The original source also contains a
stray `fin` (`SPECIALK.S:248`) and a `do` left open to the end of `GRAFIX.S`;
Merlin accepts both.

## Hardware I/O used

| Address | Use | Source |
| --- | --- | --- |
| `$C000`, `$C010` | Keyboard data / strobe clear | `MASTER.S:675-677` |
| `$C002-$C005` | `RAMRD` / `RAMWRT` main/aux | `MASTER.S:153-156` |
| `$C008/$C009` | `ALTZP` off/on | `MASTER.S:151-152` |
| `$C019` | Vertical-blank status (IIe/IIGS) | `GRAFIX.S:1943-1948` |
| `$C030` | Speaker toggle | `SOUND.S:16, 335` |
| `$C050-$C057` | Text/graphics, mixed, page 1/2, lo/hi-res | `MASTER.S:139-146` |
| `$C05E/$C05F` | Double hi-res on/off (titles; also `vibes`) | `MASTER.S:137-138`, `SUBS.S:408-412` |
| `$C061/$C062` | Buttons 0/1 | `SUBS.S:893-894` |
| `$C083`, `$C08B` | Language-card bank 2 / bank 1 select | `MASTER.S:160-161` |

Joystick paddle reads and the IIc vertical-blank interrupt path
(`GRAFIX.S:1955-1978`) exist but are not mapped here yet.

## Disk layout

Modules were written to 18-sector tracks by the `usr` directive at the end of
each file, e.g. `usr $a9,1,$a80,*-org` in `MASTER.S:1446` = disk ID `$A9`
(side 1), track 1, byte offset `$A80`. The loader's track numbers are in
`MASTER.S` (`loadperm` 1068-1138, `LoadStage1A/2/3` 1168-1367, level and image
set tracks 524-527). A full track map is **not yet compiled**.
