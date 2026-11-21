# Port notes: the display lists and the full redraw of a screen

Tenth C17 subsystem (milestone 10b): the lists of images that make up a
frame, the routine that draws them, and `SURE`, which fills the lists for a
whole screen from the level. With the blitter of milestone 10a this draws
any screen of any level, without characters.

C sources: `modern-port/src/video/pop_gfx.h`, `pop_lists.c`,
`pop_frameadv.c`; loaders in `modern-port/src/assets/pop_gfxfiles.c`.
How `SURE` builds a screen out of sections (A, B, C, D, front), the order of
rows and columns, and what each piece table means, is described in
`docs/room-rendering.md`, written for the Python model that this code is a
translation of; that description stands. This file covers the lists, the
drawing, and what the port and its tests add. Citations are `file:line` in
`01 POP Source/Source` of the original repository at commit `25243550`
(where a routine starts). Everything here is **confirmed** by reading the
source and by running the original code (section 7) unless marked.

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `ADDBACK` `GRAFIX.S:203`, `ADDFORE` 235, `ADDMSG` 258, `ADDWIPE` 285, `ADDMID` 341, `ADDMIDEZO` 386, `ADDPEEL` 431 | `pop_addback` … `pop_addpeel` | Put an image on a list |
| `ZEROLSTS` 962, `ZEROPEELS` 983, `ZEROPEEL` 999 | `pop_zerolsts`, `pop_gfx_zeropeels`, `pop_zeropeel` | Empty lists |
| `DRAWALL` 484 with `DOGEN` 513, `SNGPEEL` 636, `DRAWWIPE` 533, `DRAWBACK` 566, `DRAWMID` 676, `DRAWFORE` 600, `DRAWMSG` 760, `setbgimg` 814, `setcharimg` 842 | `pop_drawall` | Draw everything on the lists |
| `SURE` `FRAMEADV.S:49` and what it calls: `getprev` 561, `getbelow` 616, `getobjid` 2041, `RedBlockSure` 346, `RedDSure` 368, the section routines 754-1246, the pieces with state 1365-1927; `setupflask` `GAMEBG.S:675`, `setupflame` 735 | `pop_sure` | The lists for a whole screen |

The partial redraw (`FAST`), the strength meters and what else fills the
mid, wipe, message and peel lists during play are in frame-drawing.md
(milestone 10c).

## 2. The lists

A frame is not drawn as it is computed. The game first collects what is to
be drawn in lists, then `DRAWALL` goes through them in a fixed order:

| List | Entries | Drawn with | Holds |
| --- | --- | --- | --- |
| peel (one per page) | 45 | `PEEL` | what the characters covered two frames ago, to be put back first |
| wipe | 19 | `FASTBLACK` | rectangles to clear |
| background | 199 | `FASTLAY` | level pieces |
| mid | 45 | `FASTLAY`, `LAY`, or `LAYRSAVE` + `LAY` | characters (saved under), and pieces that need a dot offset |
| foreground | 99 | `FASTMASK` / `FASTLAY` | parts of pieces that go over characters |
| message | 31 | `LAY`, optionally saved under | text boxes |

Each list is a set of byte arrays; entry 0 of the first array is the count.
`ADDBACK` and `ADDFORE` drop an entry whose line is 192 or more. That is how
pieces that would start above the screen disappear: their line number has
wrapped (`docs/room-rendering.md`).

An image is named by one byte: in the background, foreground and message
lists bit 7 selects the second background table and the rest is the number
in it; a mid entry has a type byte that says which kind of table and how to
draw.

There are two hi-res pages (the game draws on the hidden one and flips),
and so two peel lists and two save-under buffers: what is saved while
drawing a page is put back the next time *that* page is drawn, which is two
frames later.

## 3. Findings

- **Bars in front of the kid.** `DrawGateBF?` (`FRAMEADV.S:829-840`) adds a
  gate's bars to the foreground when the block being drawn is the one right
  of the kid's block and row. It does not ask whether the kid is on the
  screen being drawn, only that he is *not* on the screen to the right. The
  Python model had left this routine out; it never mattered for screens
  drawn with nobody on them. It is in the C port and in the comparison (the
  kid is placed at random).
- **Copy protection in the save-under lists.** `DOGEN` clears the first of
  two bytes (`dummy`) whenever a palace level is being drawn
  (`GRAFIX.S:520-524`, "purple copy-protection"), and `ADDPEEL` finds the
  second page's list through `dummy[purpleflag]` (`441-446`, "should be
  1!"). With `purpleflag` at 1 nothing happens; with 0, from the first
  palace screen on, everything saved while drawing page 2 is filed in page
  1's list: characters would leave trails. `purpleflag` is set to 1 by
  `flipdisk` in the 3.5-inch build (`TOPCTRL.S:1546-1549`). Ported as it is
  (`pop_gfx.purpleflag`, 1 after `pop_gfx_init`). The same flag is tested
  once more, in the final princess scene, where a wrong value starts a
  sequence commented "crash (copy protect)" (`SUBS.S:1116-1121`). What the
  5.25-inch build's check is, is **not yet verified** (unknown E9).
- **Screen 0**, the "null screen" beyond the map, is drawable: every block
  reads as empty space (`GOnull`, `FRAMEADV.S:2102-2109`), the absent left
  neighbour counts as wall and the absent upper one as floor, so it is a
  black room with a wall on the left. It is what the game shows when a
  character leaves the map (game-frame.md §5 says the game never cuts to
  it; the comparison draws it anyway).
- **Stale values that cannot show.** In the pass for the row above the
  screen, two variables keep whatever the last block left (`spreced`, and
  `state` when there is no screen above). Both only select the pattern of a
  B-section, and in that pass every B-section lands above the top of the
  screen and is dropped. Found by the bug-injection check: "initialising"
  them changes nothing.
- **A gate's top piece is missing at eight heights.** The bars are drawn in
  8-line pieces from the bottom up; the remainder at the top is a 1–8-line
  piece chosen by its height. When the bars end exactly at the block's top
  the computed height is 0 and no top piece is drawn (`FRAMEADV.S:1900-1905`),
  which is right; this happens for gate positions 28–31, 60–63, 92–95,
  124–127, 156–159 and for the fully open gate (188).

## 4. Data

- **Piece tables**: `bgdata.bin`, generated at build time from the user's
  checkout: `BGDATA.S` assembled as one block, the flame and bubble tables
  of `GAMEBG.S` appended, and a directory of all 97 symbols. The loader
  fills `pop_bgdata` by name and fails if a name is missing. Reading a
  table past its end reads the next table, as in the original.
- **Images**: the original `IMG.*` files, read as they are (`pop_imgtab`).
  Their pointers assume a load address of `$6000`; the port subtracts it
  and never needs the tables relocated (blitter.md §8).

Neither is in this repository.

## 5. Intentional deviations

- **`pop_gfx` holds the drawing state**, `pop_game` the game state; the
  redraw reads the level, the neighbour screens, `BGset1` and the kid's
  block from `pop_game`.
- **Reading a block with no screen selected** (the original then reads
  through a stale pointer) sets `unmodelled`.
- **A zero in two wall tables** where the original would fall into the
  neighbouring case sets `unmodelled`; the tables have no such zero.
- **`add`** is a `jmp` that `setback` and `setmid` re-point in the
  original; the port has a selector (frame-drawing.md §2).

## 6. What the comparison cannot see

Screens are compared as page bytes and list contents; a wrong port that
only leaves the modelled memory is skipped, not reported (verification.md).
Three deliberate errors are invisible for that reason and are covered by
unit tests instead: reading a block of screen 0, a list entry on line 192,
and the gate's top piece of height 0.

## 7. Checks

| Check | What it compares |
| --- | --- |
| `tests/test_gfx.c` (`gfx_unit`) | 254 hand-worked checks (with those of frame-drawing.md) on made-up piece tables, images and a made-up level |
| `tests/compare_rooms.py` | Every screen the 15 level files declare, and the null screen of each level, as the level starts: the finished page (8,192 bytes) and the three lists, C against the original's `DoSure`. Then each level again a number of times with random gadget states, random extra pieces on the screen and where its neighbours show, and the kid at a random block |
| `tests/mutation_gfx.py` | 105 deliberate one-token errors in `pop_frameadv.c` and `pop_lists.c` must be noticed by that comparison (92 are; 13 make no difference, each with its reason) |

The original runs in the two-bank simulator (oracle.md). Nothing is
compared with an Apple II or an emulator; there is no colour; and no screen
has a character on it yet.

Recorded results are in verification.md.
