# Source inventory

Repository: <https://github.com/jmechner/Prince-of-Persia-Apple-II>
Branch `master`, commit `25243550b6e1d035d7b51b5d7e5114a83ebcb5c0`
(2024-09-27, "Update README.md"). Inspected from a shallow clone; history was
not examined.

All paths below are relative to that repository. The machine-readable version
of this inventory (path, size, SHA-256, classification for every file) is
`assets/manifest.json`, regenerated with `tools/extract_assets/make_manifest.py`.

## Summary

582 files, 507 distinct by content.

| Directory | Files | Contents |
| --- | --- | --- |
| `/` | 3 | `README.md`, `LICENSE`, `.gitattributes` |
| `01 POP Source/Source` | 29 `.S` | Game source, 3.5" disk version; 30,889 lines |
| `01 POP Source/Images` | 16 | Image tables (`IMG.*`): characters and backgrounds |
| `01 POP Source/Levels` | 15 | `LEVEL0`–`LEVEL14`, 2304 bytes each |
| `02 POP Disk Routines/CP.525` | 8 `.S` | 5.25" boot and copy-protection code |
| `02 POP Disk Routines/RW1835` | 6 `.S` | RW18 18-sector disk routines, 3.5" boot |
| `03 Disk Protection` | 7 `.S` | Byte-identical to 7 of the 8 files in `CP.525` |
| `04 Support/MakeDisk/S` | 39 `.S` | Second copy of the game source plus 10 extra files (below) |
| `04 Support/MakeDisk/DRAZ` | 11 `.S` | Source of DRAZ, the author's image-table editor |
| `04 Support/MakeDisk/EDSORC` | 13 `.S` | Source of the level editor ("builder") |
| `04 Support/MakeDisk` | 3 `.S` | Disk-building helpers (`FORMAT18`, `INSTALL18`, `USR18`) |
| `04 Support/DRAZ/{F,I,IP,SS}` | 414 | Art working files: image tables, packed pictures, raw hi-res pages |
| `04 Support/DRAZ/Z` | 1 | Binary; contains DRAZ UI strings, so inferred to be the assembled DRAZ tool |
| `04 Support/Levels` | 16 | Same 15 levels (identical) plus `Finder.Data` |

No build script, makefile, disk image, emulator configuration or assembled game
binary is present. **Confirmed** by the file listing.

## Game source (`01 POP Source/Source`)

**Confirmed** assembler dialect: Merlin (Apple II 6502 assembler). Evidence:
`put`, `dum`/`dend`, `lst off`, `usr` pseudo-ops and `]var` / `:local` labels
throughout; `04 Support/MakeDisk/USR18.S:6` names "Merlin 2.52". Files are
plain ASCII with LF line endings.

Each module ends with a `usr $a9,<track>,<offset>,<length>` directive that
wrote the assembled code straight to an 18-sector disk track
(`USR18.S:6-22`), so the build output was a disk, not object files.

| File | `org` | Role (from file headers and jump tables) |
| --- | --- | --- |
| `BOOT.S` | `$800` | Boot sector and stage-2 loader |
| `MASTER.S` | `$f880` | Disk loading, attract mode, title/epilog sequencing |
| `TOPCTRL.S` | `$2000` | Game start/restart, **main loop**, per-frame orchestration |
| `CTRL.S` | `$3a00` | Player and fight control (input → sequence selection) |
| `CTRLSUBS.S` | `$d000` | Character helpers: `JUMPSEQ`, `SETUPCHAR`, frame lookup |
| `COLL.S` | `$4500` | Collision detection and `ANIMCHAR` (sequence interpreter) |
| `AUTO.S` | `$5400` | Guard/shadow AI, demo playback, screen cuts |
| `SUBS.S` | `$e000` | Misc: `PAGEFLIP`, `PAUSE`, princess-scene playback |
| `MOVER.S` | `$ee00` | Moving level objects (gates, spikes, loose floors…) |
| `MISC.S` | `$f900` | Potions, mouse, meters, misc. effects |
| `SPECIALK.S` | `$d900` | Keyboard, game timer, `CUESONG`, `ADDSOUND` |
| `SEQTABLE.S` | `$3000` | **Animation sequence table** (114 entry points) |
| `SEQDATA.S` | – | Names for sequences and sequence instruction codes |
| `FRAMEDEF.S` | `$2800` | **Frame definitions** (5 bytes per frame) and sword table |
| `FRAMEADV.S` | `$1290` | Builds per-frame image lists from level + objects |
| `GAMEBG.S` | `$4c00` | Background/object drawing helpers, meters |
| `BGDATA.S` | – | Per-tile image and offset tables |
| `GRAFIX.S` | `$400` | Image-list drawing (`DRAWALL`), input, VBL wait, bank glue |
| `HIRES.S` | `$ee00` | Hi-res blitters (`LAY`, `FASTLAY`, `LAYRSAVE`, `PEEL`…) |
| `HRTABLES.S` | `$e000` | Hi-res row address, shift, carry and mask tables |
| `HRPARAMS.S` | – | Zero-page blitter parameters |
| `TABLES.S` | `$e00` | Coordinate conversion tables |
| `UNPACK.S` | `$ea00` | Picture unpacking, double hi-res titles, fades |
| `SOUND.S` | `$ea00` | **Sound effects**: speaker tone routine + 20 effects |
| `SOUNDNAMES.S` | – | Sound-effect and song number names |
| `EQ.S`, `GAMEEQ.S` | – | Global equates, zero page, jump tables, memory layout |
| `MOVEDATA.S` | – | Tile type names and constants |
| `VERSION.S` | `$dfd8` | Version string "Prince of Persia 1.0  9/7/89" |

`04 Support/MakeDisk/S` holds the same 29 files (27 byte-identical). Differences
and additions, all **confirmed** by `diff`/hash:

- `TOPCTRL.S` adds a stack reset in `RESTART` marked "added 2/5/90".
- `VERSION.S` reads "1.0  1/23/90 special".
- 5.25" variants: `MASTER525.S`, `GRAFIX525.S`, `UNPACK525.S`.
- `MSYS.S` – the **music player** (see Audio).
- `COMMENTS.S`, `CHARCOMMENTS.S` – the author's own prose documentation of the
  data structures. These are the most valuable files for this project.
- `DEBUGS.S`, `SHSTUFF.S`, `PURPLE.S`, `RYELLOW1.S` (the last identical to the
  copy in `02 POP Disk Routines/CP.525`).

## Graphics

**Present (confirmed by parsing):** 16 image tables in `01 POP Source/Images`,
all of which decode cleanly with the format described in
`docs/graphics-formats.md`.

| File | Images | Used as (from `GAMEEQ.S:9-18`, `MASTER.S:524-527`) |
| --- | --- | --- |
| `IMG.CHTAB1` | 65 | `chtable1` – kid: run, jump, turn… (`FRAMEDEF.S:23-74`) |
| `IMG.CHTAB2` | 70 | `chtable2` – kid: run-turn, jump-hang… (`FRAMEDEF.S:75-100`) |
| `IMG.CHTAB3` | 78 | `chtable3` – includes sword images (`FRAMEDEF.S:424`) |
| `IMG.CHTAB4.GD` / `.FAT` / `.SHAD` / `.SKEL` / `.VIZ` | 32 each | `chtable4` – alternate opponent sets, swapped per level |
| `IMG.CHTAB5` | 45 | `chtable5` – contents not yet verified |
| `IMG.CHTAB6.A`, `.B` | 103, 73 | `chtable6` – alternate set 2 (princess, vizier) and props (`FRAMEDEF.S:322`, `GAMEBG.S:108-127`) |
| `IMG.CHTAB7` | 8 | `chtable7` – contents not yet verified |
| `IMG.BGTAB1.DUN`, `.PAL` | 127 each | `bgtable1` – dungeon / palace background pieces |
| `IMG.BGTAB2.DUN`, `.PAL` | 51, 50 | `bgtable2` |

Which opponent each `CHTAB4` variant represents is **inferred** from the file
suffixes (guard, fat guard, shadow, skeleton, vizier), not yet tied to code.

**Animation data present (confirmed):** `SEQTABLE.S` (sequences, including
per-step x/y displacement and sound taps) and `FRAMEDEF.S` (frame → image,
offsets, foot position). These are assembler source, not binary, so they will be
extracted by parsing the `.S` text.

**Level data present (confirmed):** 15 level blueprints; layout documented by the
author in `04 Support/MakeDisk/S/COMMENTS.S:237-338` and matching `EQ.S:258-265`.

**Working art (inferred from names and sizes, format not yet verified):**
298 `PAC.*` packed pictures and 53 raw 8192/8184-byte hi-res pages under
`04 Support/DRAZ`. Names such as `PAC.RUN`, `PAC.CLIMBUP`, `PAC.RUNJUMP`
suggest these are the digitised source sheets the sprites were cut from.
Another 62 image tables there also parse; one (`IP/IMG.CHTABLE3`) does not.

**Not present:** the title, prologue and epilog double hi-res screens that
`MASTER.S:96-110` loads from disk, at least under any recognisable name
(**not yet verified** against the `PAC.*` files).

## Audio

**Sound effects – present and complete in source (confirmed).** `SOUND.S` is 360
lines: a 20-entry dispatch table (`SOUND.S:30-51`), one parameter block per
effect and a single speaker-toggling `tone` routine (`SOUND.S:331-352`). There
is no sampled audio; an effect is just (pitch, duration) fed to a timing loop.

**Music player – present, in the support directory only (confirmed).**
`04 Support/MakeDisk/S/MSYS.S` is "Music System ][ by Kyle Freeman (c) 1986,
adapted by Jordan Mechner July 1989" (`MSYS.S:4-11`). It contains note period
and length tables, envelope and harmonic tables, and the player.

**Music score data – absent (confirmed for named files).** The player reads
songs from a 1K table at `$d000` (`MSYS.S:20-24`) which the game loads from disk
tracks 34 (title set) and 20 (game set) (`MASTER.S:247-280`). No file in the
repository corresponds to that data, and there is no disk image. Song *numbers
and names* are present (`SOUNDNAMES.S:27-54`, `MASTER.S:113-130`), as are the
points where each is cued. The notes themselves are not.
Next step if wanted: obtain the song tables from a disk image you own; the
`MSYS.S` source is enough to write a decoder for them.

## Licensing

- `LICENSE` contains one line referring to the README.
- The README states the author, as copyright holder of the source, "personally
  [has] no problem with anyone studying it, modifying it, attempting to run it",
  and that this "does NOT constitute a grant of rights of any kind in Prince of
  Persia", an Ubisoft franchise.
- This is permission to study, not an open-source licence. There is no stated
  right to redistribute the code, art, levels or derived binaries.
- Third-party code with its own copyright notices: RW18 disk routines
  (Roland Gustafsson, 1985/1987, `02 POP Disk Routines/RW1835/RW1835.S`), the
  music system (Kyle Freeman, 1986, `MSYS.S`), and copy-protection code
  (HLS Duplication, 1987, `02 POP Disk Routines/CP.525/YELLOW.MAIN.S`).

Policy adopted here: this repository stores only newly written code, prose,
and metadata. Extracted images and any data derived from the original are
generated locally into git-ignored directories.
