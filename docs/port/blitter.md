# Port notes: the blitter

Ninth C17 subsystem (milestone 10a): the routines that put an image on a
hi-res page. Everything the game shows goes through them.

C sources: `modern-port/src/video/pop_hires.h`, `pop_hires.c`. Citations are
`HIRES.S` lines (in the routine table, the line a routine starts at) in
`01 POP Source/Source` of the original repository at
commit `25243550` unless another file is named. Everything in sections 1–7
is **confirmed** by reading the source and by running the original blitter
(section 9) unless marked otherwise.

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `LAY` 658, `MLAY` 1186, and the six routines they choose from: `LayGen` 681, `LayMask` 833, `LayXOR` 1008, `MLayGen` 1202, `MLayMask` 1356, `MLayXOR` 1541 | `pop_hr_lay` | Draw an image: any dot offset, six draw modes, mirrored or not, cropped to a usable area |
| `PREPREP` 311, `CROP` 375 | (static) | Image header; what part is inside the area |
| `LAYRSAVE` 542 | `pop_hr_layrsave` | Save what is under the place an image will be drawn |
| `PEEL` 1721 | `pop_hr_peel` | Put a saved piece back |
| `FASTLAY` 1740, `fastlaySTA` 1825 | `pop_hr_fastlay` | Byte-aligned drawing for background pieces |
| `FASTMASK` 1892 | `pop_hr_fastmask` | Byte-aligned masking |
| `FASTBLACK` 2005 | `pop_hr_fastblack` | Fill a rectangle |
| `CLS` 206, `COPYSCRN` 2060 | `pop_hr_cls`, `pop_hr_copyscrn` | Whole pages |
| `INVERTY` 2098 | `pop_hr_inverty` | Turn the picture upside down (section 6) |

Not ported: `LRCLS` (text page), `GETWIDTH` (the port reads the image
header), `SETFASTAUX`/`SETFASTMAIN` (which bank the fast routines read
images from).

## 2. The page

A hi-res page is 8,192 bytes at `$2000` (page 1) or `$4000` (page 2), 40
bytes for each of 192 lines, 7 dots per byte with bit 0 leftmost. Bit 7 is
not a dot (on the real machine it chooses the colour pair and shifts the
byte's dots by half a dot). The lines are not in order in memory: line `y`
is at `$2000 + (y & 7)·$400 + ((y >> 3) & 7)·$80 + (y >> 6)·$28`, which the
blitter looks up in the tables `YLO`, `YHI` (`HRTABLES.S`).

The port keeps both pages as the 16,384 bytes of memory they occupy, in
that order. The original computes addresses and does not check them: a
piece drawn too far right continues on whatever line follows in memory. To
compare byte for byte the port has to do the same, so it does address
arithmetic too, with the same 8- and 16-bit wrap. Writes that would land
outside the two pages or the save-under buffers set `unmodelled`.

## 3. Images, offsets and modes

An image is `width` (bytes), `height` (lines), then `width × height` bytes,
bottom line first, each with bit 7 set (graphics-formats.md). It is drawn at
byte column `XCO`, with its bottom line on screen line `YCO`, moved right by
`OFFSET` dots (0–6): each image byte is split by table lookup into the part
that stays in its screen byte (`SHIFTn`) and the part that spills into the
next (`CARRYn`). An image therefore touches `width + 1` bytes per line.

`OPACITY` selects what happens to the screen byte:

| Value | Name | Result |
| --- | --- | --- |
| 0 | AND | screen AND image |
| 1 | OR | screen OR image |
| 2 | STA | image |
| 3 | "special XOR" | (screen OR image) XOR the image again one dot further right, bit 7 set: leaves the image's outline |
| 4 | mask | screen AND mask, then OR image |
| 5 | "visible mask" | the mask stored instead of ANDed (a debugging aid; the game's drawing code never asks for it, **inferred** from the absence of a 5 in the callers read so far) |
| +128 | | mirrored |

In the shifted modes the dots to the left of the image in its first byte,
and to the right of the spill in the last, are taken from the screen and
kept (`AMASKS`, `BMASKS`).

**The mask** of a byte is made on the fly from the image byte (`MASKTAB`): a
screen dot survives only if neither the image dot at its place nor either
neighbour *within the same byte* is set. So a character is drawn with a
one-dot black outline, except across byte boundaries, where the outline is
missing: the table sees one byte at a time, and the shifted mask's carry
only moves existing mask bits.

**The special XOR** uses the offset plus one for its second copy, and offset
5 when the offset is 6 (`shiftoffset` 517-525): at offset 6 the outline is
on the other side. (Its left-edge mask is taken with the second offset,
`LDA AMASKS,X` after `shiftoffset`, line 1047; that has no effect, because
the first byte is ORed with the whole screen byte in any case.)

**Mirroring** (bit 7 of `OPACITY`): `XCO` then names the byte *after* the
right edge; the image's bytes are read right to left and each reversed
(`MIRROR`). The mirrored routines step along the line by incrementing the
low address byte alone.

## 4. Cropping

`LEFTCUT`, `RIGHTCUT`, `TOPCUT`, `BOTCUT` bound the area that may be drawn
on (0, 40, 0, 192 for the whole screen). `CROP` works out, from 8-bit
arithmetic on `XCO`, `YCO`, the size and the cuts:

- rows below `BOTCUT` are skipped (the image pointer is advanced past them);
- the top is cut at `TOPCUT`, or at the screen's top when `YCO − height`
  wraps (the test is "less than 191", so an image whose top is exactly one
  line above the screen is recognised by the wrap to 255);
- an `XCO` that is negative as a signed byte, or left of `LEFTCUT`, is "off
  the left": the first carry is then taken from the hidden image byte
  instead of the screen;
- off the right, the spill byte is not drawn.

An image off the left is assumed not to reach the right edge as well
("Assume image is <=40 bytes wide", 462).

## 5. Save-under

Characters are drawn over the finished background. Before each one,
`LAYRSAVE` copies the screen bytes it will cover (one byte wider than the
image, for the spill) into a buffer, in image format, and returns where
(`PEELIMG`, `PEELXCO`, `PEELYCO`); `PEEL` later puts the piece back with the
fast store routine. There is one buffer per hi-res page, `$D000` and `$D800`
in the main language card, 2,048 bytes each (memory-map.md), filled in
sequence and emptied every frame by the game (`ZEROPEEL`, `GRAFIX.S:997-1016`).
`LAYRSAVE` does not check for the buffer's end.

## 6. Upside down

The "flip" potion does not redraw anything upside down. `INVERTY` reverses
the line-address tables (`YLO[y]` ↔ `YLO[191−y]`, same for `YHI`), after
which every routine above draws line `y` where line `191 − y` was. A second
call puts the tables back. The game's call goes to a copy of the routine in
`UNPACK.S` (540-565, through that module's jump table); the two are the same
code (**confirmed** by comparing the two texts).

## 7. The fast routines

`FASTLAY` and `FASTMASK` are for background pieces: no dot offset, no
mirroring, no side cropping, right to left within a line. An image taller
than the room above `YCO` is cut at the top line ("limited Y-clipping",
1772); `FASTBLACK` has no such cut. Their XOR mode is a plain XOR, so
it toggles bit 7 as well as the dots.

## 8. Findings

- **The outline of a masked character has gaps at byte boundaries**
  (section 3).
- **`OPACITY` 5** is implemented in all four mask routines but written
  slightly differently in the mirrored ones (the last byte of a line is
  formed from the screen byte first, 1509-1514, where the unmirrored
  routine uses the edge mask alone, 976-980); with mode 4 the two
  forms give the same byte.
- **The flip is a table swap** (section 6), which is why it also turns the
  strength meters and messages upside down: nothing in the drawing code
  knows about it.
- **`peelbuf2` is declared twice with different values**; the one in
  `HIRES.S` is unused (memory-map.md).
- **Image tables must be relocated.** The pointers in every image file of
  the repository assume the table is loaded at `$6000`, and the blitter
  uses them unchanged, but five of the eight tables are kept elsewhere. No
  game code relocates them (**confirmed** by search of the loaded modules);
  it is **inferred** that the disk-building tools did (unknown G4). The
  test oracle relocates on load; the C port takes images by pointer and
  has no table addresses.

## 9. Intentional deviations

- **Images are passed by pointer and length**, not by table and number;
  reading past the end sets `unmodelled`.
- **Tables are computed**, not embedded: `SHIFTn`, `CARRYn`, `MIRROR`,
  `MASKTAB`, `YLO`, `YHI` by the rules above (run-cycle-analysis.md: the
  rules reproduce the assembled `HRTABLES.S` byte for byte); the two edge
  mask tables, 7 bytes each, are written out.
- **No self-modifying code**: the patched addresses and opcodes are locals.
- **A line index of 192 or more** reads the address tables beyond their
  end; the port follows the original into the next table (`YHI`, then
  `SHIFT0`), since both are known.
- **One memory bank.** The original reads images from either bank and
  switches around each byte; the port has no banks.

## 10. Checks

| Check | What it compares |
| --- | --- |
| `tests/test_hires.c` (`hires_unit`) | 88 hand-worked checks on made-up images; no original data needed |
| `tests/fuzz_hires.py` | Every routine above on random images, positions, cuts, offsets and modes over random page contents, C against the original blitter: all 16,384 bytes of both pages, the 4,096 bytes of the save-under buffers, and the save-under variables, after every call |
| `tests/mutation_hires.py` | 59 deliberate one-token errors in `pop_hires.c` must be noticed by that comparison (49 are; 10 make no difference, each with its reason) |
| `tests/test_gfx_oracle.py` (pytest) | The original blitter against the Python model of milestone 3, on the real image tables |

The original blitter runs in this project's 6502 simulator extended with
the Apple IIe's two memory banks (oracle.md), called as the game calls it:
from auxiliary memory through the `GRAFIX.S` entry points, which copy the
parameters to the main zero page and switch banks. Nothing is compared with
an Apple II or an emulator, and no colour is involved: these are bytes.

Recorded results are in verification.md.
