# Port notes: drawing a frame during play

Eleventh C17 subsystem (milestone 10c): what the game draws each frame once
a screen is up. It does not redraw the screen. The game logic marks what
changed, and `FAST` turns the marks, the characters and the falling floors
into display-list entries, which `DRAWALL` (redraw.md) then draws.

C sources: `modern-port/src/video/pop_frameadv.c` (with `pop_gfx.h`).
Citations are `file:line` in `01 POP Source/Source` of the original
repository at commit `25243550` (where a routine starts). Everything here
is **confirmed** by reading the source and by running the original code
(section 6) unless marked.

Status: part 1 of 3. Done: the redraw marks, wipes and strength meters
(sections 1–4). Not yet ported: the object list (characters, swords, impact
stars, falling floors), messages, and the two-page cycle of a running game.

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `FAST` `FRAMEADV.S:189` | `pop_fast` | The lists for one frame, from the redraw buffers |
| `RedBlockFast` 381, `RedDFast` 477 | (static) | One block; one block of the top line |
| `wipesq` 1326, `wiped` 1348 | (static) | Rectangles to clear |
| `drawfloor` 1252, `drawhalf` 1268 | (static) | A floor piece drawn again *over* a character |
| `redrawd` 1092 | (static) | A front edge on the background and foreground planes |
| `add`, `setback`, `setmid`, `addmidezfast` 1158-1175 | (static) | Which list the piece routines add to |
| `metbufs3`, `metbufs2` 2252 | (static) | Is a block under a strength meter marked? |
| `UPDATEMETERS` `GAMEBG.S:479`, `DRAWKIDMETER` 496, `DRAWOPPMETER` 579 | `pop_drawkidmeter`, `pop_drawoppmeter` | The strength meters |

## 2. The redraw buffers

Nine arrays with one byte for each of the 30 blocks of the screen
(`EQ.S:218-229`; `pop_game.rb`), written by the game logic (`MARKRED` and
its relatives, `CTRLSUBS.S:1734-1776`) and read here. A byte is a count of
frames, not a flag: the game shows two hi-res pages in turn, so a change
has to be drawn twice, once on each, and a mark is normally 2. `FAST` takes
one off each time it acts on a mark.

| Buffer | When marked, `FAST` adds | To |
| --- | --- | --- |
| `wipebuf` (+ `whitebuf`) | a black rectangle one block wide, `whitebuf` lines high, up from the block's bottom line | wipe list |
| `redbuf` | the whole block, as in a full redraw | background, foreground |
| `movebuf` | the block's moving parts only (gate bars, spikes, slicer, flame, flask, sword, loose floor) | background |
| `floorbuf` | the floor piece again, with its mask (only if the block to its left is empty space) | mid |
| `halfbuf` | the same for a character climbing up: a mask and a short piece that leave his upper body in front | mid |
| `objbuf` | the objects (characters, falling floors) indexed to this block | mid |
| `fredbuf` | the block's front piece | foreground |
| `topbuf` (10 bytes) | the three lines at the top of the screen that show the row above: wiped and redrawn | wipe, background, foreground |

Order matters and is fixed: within a block, wipe, then the block or its
moving parts, then the floor piece, then objects, then the front piece;
blocks bottom row first, left to right, as in a full redraw
(room-rendering.md). The lists are then drawn plane by plane, so a
character (mid) is covered by a front piece (foreground) however the blocks
were ordered.

Three things in `RedBlockFast` are easy to get wrong:

- a block redrawn whole does not also redraw its moving parts, and their
  mark is *not* taken off (`bpl :skipmove`, 401), so it is acted on the
  next frame;
- likewise `halfbuf` waits while `floorbuf` is marked (430);
- `objbuf` is cleared, not counted down (447-448): objects are added to it
  afresh every frame.

The floor and half pieces go to the **mid** list, between the characters,
in the order the blocks are walked. That is what puts a floor piece in
front of a character who is behind it and was added earlier, and behind one
added later. `setmid` re-points the routine `add`, which all piece routines
end in, by modifying a `jmp` (1164-1175); the port has a selector
(`pop_gfx.addmid_sel`).

`RedDFast` draws the front edge of the row above on both the background and
the foreground plane (`redrawd`), so that it covers a character jumping up
into it.

Unlike `SURE`, `FAST` does nothing for the top line when there is no screen
above (`beq :done`, 287): `SURE` draws a row of floor edges there, and
nothing ever marks them afterwards.

## 3. The strength meters

The meters are on the bottom line of the screen (line 191), over the bottom
row of blocks: the kid's from the left edge, the opponent's from the right.
They are not marked or drawn on their own account. `FAST` looks, before
anything else, whether a block under a meter is marked in `redbuf`,
`floorbuf`, `halfbuf`, `fredbuf` or `wipebuf` (not `movebuf`, not `objbuf`):
blocks 20–22 for the kid's, 28–29 for the opponent's. If so the meter is
added to the message list, which is drawn last. A change of strength
therefore reaches the screen by marking those blocks (`MarkKidMeter`,
`MISC.S`; `checkmeters`, `TOPCTRL.S:1299`).

A meter is drawn in as few images as possible: lines of three, two or one
units (`bline`), then the last unit as a single image, then empty units
(`blank`, drawn with AND) up to the maximum.

Positions are a byte column and a dot offset per unit
(`KidStrX`/`KidStrOFF`, `OppStrX`/`OppStrOFF`, `GAMEBG.S:93-97`): units are
8 dots apart, so the offset grows by one per unit and wraps after seven.
The opponent's table is the mirror image and its images are drawn mirrored.

- **The last unit blinks.** With one unit of strength left, it is drawn
  only when the page being drawn is page 2 (`lda PAGE / beq :skip`,
  526-528). As the pages alternate, it is on screen every other frame. An
  empty unit is drawn in its place on page 1 (for the kid: by the loop that
  fills up to the maximum; for the opponent: a single blank).
- **The opponent's meter has no empty units**: his maximum is not shown.
- **Who has a meter**: not the mouse (`ShadID` 24) or the skeleton (4), and
  the shadow (1) only on level 12 (`GAMEBG.S:586-594`).

## 4. Findings

- **The height of a wipe and the image number are one memory cell**
  (`height = IMAGE`, `width = IMAGE+1`, `EQ.S:384-385`), and `DRAWALL`
  passes its parameters to the blitter through the same cells and leaves
  them there. This is visible in exactly one place: a torch whose state is
  beyond the flame table (18 or more) is added with whatever image number
  and mode are in those cells (`SETUPFLAME` returns without setting them,
  `GAMEBG.S:736-737`; redraw.md), which after a wipe is the wipe's height.
  The game never produces such a state (`GETFLAMEFRAME`, `MOVER.S:1084`,
  returns 0–17, and all 394 torches in the level files start at 0); the
  comparison does, and found this. The port keeps one cell for both
  (`pop_gfx.IMAGE`) and has `pop_drawall` leave behind what the original
  leaves.
- **`sortlist` makes one pass only** (`FRAMEADV.S:2134-2172`): the flag
  `switches` that should ask for another pass is cleared and tested but
  never set. Found by reading; the routine is not ported yet (it comes with
  the object list), so this is **confirmed** by reading only.

## 5. Intentional deviations

- **The drawing parameters are `pop_gfx` fields**, separate from the game
  state. In the original they are zero-page cells that the game logic uses
  too (`DIMCHAR` and the wipe-height code write `IMAGE`, for one). The port
  follows the cells through the drawing code only; what the game logic
  would have left in them is not mirrored. The single known read of a stale
  value is the torch case above, which needs a state the game does not
  produce.
- **More units than a meter has places for** (13 or more) makes the
  original read position tables past their end; the port sets `unmodelled`.
- **The editor's checks** (`inbuilder`) are left out.

## 6. Checks

| Check | What it compares |
| --- | --- |
| `tests/test_gfx.c` (`gfx_unit`) | Hand-worked cases for each kind of mark and for the meters, on made-up tables |
| `tests/compare_fast.py` | A screen is drawn in full on both sides, then three times in a row: random marks in all nine buffers (sparse to every block), random wipe heights, some gadget states changed, strengths and opponent kinds set, on either page. C against the original's `ZEROLSTS`, `FAST`, `DRAWALL`: the page (8,192 bytes), all five lists, the redraw buffers afterwards, and the two meter flags |
| `tests/mutation_gfx.py` | Deliberate one-token errors in the redraw code must be noticed by that comparison or by `compare_rooms.py` |

Recorded results are in verification.md.
