# Port notes: drawing a frame during play

Eleventh C17 subsystem (milestone 10c): what the game draws each frame once
a screen is up. It does not redraw the screen. The game logic marks what
changed, and `FAST` turns the marks, the characters and the falling floors
into display-list entries, which `DRAWALL` (redraw.md) then draws.

C sources: `modern-port/src/video/pop_frameadv.c` and `pop_screen.c` (with
`pop_gfx.h`) for the drawing side, `modern-port/src/core/pop_objs.c` and
`pop_mover.c` for the object list. Citations are `file:line` in `01 POP Source/Source` of the
original repository at commit `25243550` (where a routine starts).
Everything here is **confirmed** by reading the source and by running the
original code (section 9) unless marked.

Status: complete (three parts). The redraw marks, wipes and strength
meters; the object list (characters, swords, impact stars, falling floors)
and its drawing; messages, the two display pages, and the frame as a
whole. The demo level and random play on the other levels are drawn the
same, byte for byte, by the port and by the original code (section 9).

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
| `addchars` `TOPCTRL.S:747`, `setupkid` 803, `setupshad` 830 | `pop_addchars`, `pop_setupkid`, `pop_setupshad` | The characters in sight onto the object list |
| `INDEXCHAR` `CTRLSUBS.S:740`, `QUICKFLOOR` 1171, `QUICKFG` 1258, `CROPCHAR` 1313, `UNEVENFLOOR` 2109, `MARKFLOOR` 1764, `MARKHALF` 1769 | `pop_indexchar` … `pop_markhalf` | Where a character is drawn, what he may be behind, where he is cut off |
| `SETUPSWORD` `CTRLSUBS.S:852`, `SETUPCOMIX` `GAMEBG.S:965` | `pop_setupsword`, `pop_setupcomix` | The sword in his hand; the star where he was hit |
| `ADDCHAROBJ` `GAMEBG.S:1050`, `SETOBJINDX` 1105, `ADDFCHARX` `CTRLSUBS.S:374`, `CVTX` `GRAFIX.S:903` | `pop_addcharobj`, `pop_setobjindx`, `pop_addfcharx` | An entry of the object list |
| `ADDMOBS` `MOVER.S:1984`, `ATM` 2004, `addmobobj` 2085 | `pop_addmobs` | Falling floors onto the object list |
| `drawobjs` `FRAMEADV.S:503`, `sortlist` 2134, `compare` 2182, `loadobj` 716, `drawobjx` 1937, `DrawFF` 1969; `DRAWKID` `GAMEBG.S:391`, `DRAWSWORD` 408, `DRAWSHAD` 414, `DRAWGUARD` 420 | (static, in `pop_fast`) | The objects of a block onto the mid list |
| `PRINTLEVEL` `GAMEBG.S:276`, `TIMELEFTMSG` 187, `CONTINUEMSG` 328, `superimage`, `setupimage` 344-374 | `pop_printlevel`, `pop_timeleftmsg`, `pop_continuemsg`, `pop_show_message` | Messages over the picture |
| `DoCleanCut` `TOPCTRL.S:891`, `drawbg` 918, `DoSure` 942, `DoFast` 963 (the order of calls; in `pop_loop.c`), `COPYSCRN` `GRAFIX.S:1369`, `PAGEFLIP` `SUBS.S:397`, `DOFLASHON` 210, `DOFLASHOFF` 220, `LRCLSE` 238 | `pop_cut_begin`, `pop_cut_copy`, `pop_pageflip`, `pop_doflashon`, `pop_doflashoff`, `pop_lrclse` | Which page is drawn on and which is shown |

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

## 4. The object list

What is not part of the level is drawn from a list of up to 19 objects
(`EQ.S:299`, `339-350`; `pop_game.obj*`), made anew every frame by
`DoFast` (`TOPCTRL.S:963-981`): first the falling floors (`addmobs`), then
the characters (`addchars`: the kid's reflection, the opponent, the kid, in
that order). An entry is a position (byte column, dot offset, line of the
bottom edge), an image (table and number), a facing, four crop edges, a
kind, and the **block it is drawn with**.

That block index is what ties an object into the walk of section 2. Adding
an object marks `objbuf` for its block (`SETOBJINDX`), and `FAST` draws the
objects of a block when it gets there: after the block's own pieces and
the floor piece, before its front piece. So a character is in front of the
floor to his left and below, and behind what the blocks walked later put on
the mid list. Two indexes are special: 30 ("off the screen") is drawn
before any block, and `$FF`, used for the impact star, after all of them.

**Kinds** (`GAMEEQ.S:665-671`) and how each is drawn (`drawobjx`):

| Kind | Drawn |
| --- | --- |
| kid, reflection | masked (mode 4), saved under; XORed instead on odd frames while he merges with the shadow (`mergetimer`) |
| shadow | XORed (mode 3: an outline that shows what is behind) |
| guard | masked; with `GuardColor` set, one dot further right, which on the real machine changes the colours (**inferred**: the colour model is milestone 10d) |
| sword, impact star | masked |
| falling floor | as the loose floor's sections: mask and A-section three lines up, D-section, B-section in the next block four lines up (`DrawFF`) |

Every character image is saved under (`LAYRSAVE`) before it is drawn and
put back at the start of the frame after next on the same page (redraw.md).

**Several objects in one block** are sorted so that the one lowest on the
screen is drawn first and an opponent never moves towards the back
(`sortlist`, `compare`).

**For each character** (`setupkid`, `setupshad`): the frame's image and
position (`SETUPCHAR`, game-frame.md), one line lower on a pressed plate
(`UNEVENFLOOR`), the edges of the image (`GETEDGES`), then:

- `INDEXCHAR`: his block. On the ground, the block of his image's left
  edge and bottom; otherwise the block he is in. One block further left
  while he climbs up (frames 135–148).
- `QUICKFG` marks the front pieces (`fredbuf`, 3 frames) of every block
  from his feet to his head, one block more on the side of a drawn sword.
- `QUICKFLOOR` marks the floor pieces (`floorbuf`, or `halfbuf` while
  climbing up) of the same blocks while he hangs or falls (actions 2, 3, 4
  and 6) and in frames 78 and 79 on the ground: the frames in which he can
  be *behind* a floor.
- `CROPCHAR` sets the edges he is cut at: the top of a door while he walks
  up the exit stairs; the underside of a solid ceiling; the left edge of a
  panel or wall whose block his leading edge is in. Only the top and right
  cuts are ever set here; the left cut is set for the reflection and for
  the shadow leaving the mirror, the bottom cut never.

**The sword** is a separate object (`SETUPSWORD`): shown when the sword is
out, while sheathing (frames 229–237), and always for a live guard. The
frame's `Fsword` byte numbers an entry of a table of image, forward and
down offsets (`swordtab`, 3 bytes each); the images are in the third
character table.

**The impact star** (`SETUPCOMIX`) appears in the frame a strength
decreases: 15 lines above the feet for the kid and 11 for others, 5 dots
ahead, lower for a crouching or dead character, none for one cut in half.
Its x is then made even for the kid and odd for anyone else, which the
source comments as "kid red, opps blue": the colour follows from the dot's
parity (**inferred** until the colour model exists).

**A falling floor** is listed when it is on the visible screen and not more
than its height below the bottom edge, or on the screen below and still
within 17 lines of its top (it is then drawn 192 lines lower, at the bottom
of this one). It marks the floor and front pieces of the block to its right
and of the block above that when it reaches up there.

## 5. The frame as a whole

The game draws on the hi-res page that is not on the screen and then
switches the two (`PageFlip`): `PAGE` is `$00` while page 1 is being drawn
and page 2 shown, `$20` the other way round. Every frame is drawn on the
page that was on the screen two frames ago, which is why a change is
marked for two frames and why there are two save-under lists (redraw.md).

An ordinary frame (`DoFast`, `TOPCTRL.S:963-981`), with the port's hook or
routine for each step:

| Original | Port |
| --- | --- |
| `zerolsts` | `pop_zeroobjs`; hook `zero_lists` → `pop_zerolsts` |
| `SCRNUM = VisScrn`; `develpatch`: a forced full redraw (`redrawflg`) lists the whole screen first | in `pop_loop.c`; hook `draw_screen` → `pop_sure` |
| `addmobs`, `addchars` | `pop_addmobs`, `pop_addchars` (section 4) |
| `fast` | hook `draw_changes` → `pop_fast` |
| `dispmsg` | `pop_dispmsg`; hook `show_message` → `pop_show_message` |
| `drawall` | hook `draw_lists` → `pop_drawall` |
| then `PageFlip` (`FrameAdv`) | hook `show_frame` → `pop_pageflip` |

A change of screen (`DoCleanCut`, `891-906`, with `drawbg` and `DoSure`):

| Original | Port |
| --- | --- |
| `PAGE = $20`; `lrclse` with a space, `PAGE2off`, `TEXTon`: a blank text screen while the new one is built | hook `cut_begin` → `pop_cut_begin` |
| `cutplan = 0`, `CUTTIMER = 2`; `SCRNUM = VisScrn` | in `pop_loop.c` |
| `zerolsts`, `sure` | hooks `zero_lists`, `draw_screen` |
| `zeropeels`, `zerored` | hook `zero_peels` → `pop_gfx_zeropeels`; `pop_zerored` |
| `drawall`: the background, on page 2 | hook `draw_lists` |
| `markmeters` | `pop_markmeters` |
| `PAGE = 0`; `copyscrn`: the background copied to page 1 | hook `cut_copy` → `pop_cut_copy` |
| `DoFast`: characters added on page 1 | as above |
| `PageFlip`: page 1 shown; page 2, the bare background, is drawn on next | hook `show_frame` |

So after a cut the first frame's characters are on page 1 only, and the
second frame adds them to a page 2 that has never held any.

**Messages** are entries of the message list, drawn last: a box saved
under (bit 6 of the mode), then words and digits ORed into it. "LEVEL n"
(`PRINTLEVEL`; levels 13 and 14 are announced as 12), "n MINUTES LEFT"
(`TIMELEFTMSG`, which works the time out again with `GETMINLEFT`; under two
minutes it shows seconds, and the box moves down when the kid is on the
middle row and not falling), and "Press button to continue"
(`CONTINUEMSG`: lower or higher by the parity of the dead kid's column).
Positions and image numbers are in `GAMEBG.S:162-181`.

**What the monitor is shown** is a matter of four soft switches and one
byte (`pop_gfx.sw_*`, `scrncolor`): page 1 or 2, hi-res or lo-res, graphics
or text, and the value the text / lo-res page was last filled with. A
flash (`DOFLASHON`) fills that page with a colour and switches to lo-res.
Nothing else of the text page is used in play. Within one pass of the main
loop the monitor can be shown several things in turn, so the port also
keeps the switches touched, in order (`pop_gfx.swlog`); a front end
presents a picture at each of them. Turning page bytes into a picture is
milestone 10d.

## 6. Findings

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
  never set. One pass from the end of the list orders two objects, and
  three if they were added lowest-first; three objects at lines 100, 120
  and 140 added in that order come out as 140, 100, 120. The port makes one
  pass and agrees with the original on random lists of up to nine objects
  in a block. Whether three characters and swords in one block ever are
  out of order in play is **not yet verified**; next step: log the sort
  lists during the demo and a fight (milestone 10c-3).
- **`INDEXCHAR` tests the frame number where it means the action.** After
  the test for the climbing frames it compares the same value with 2, 3, 4
  and 6 under the label `:fall` (`CTRLSUBS.S:757-765`); `QUICKFLOOR`, built
  the same way, has the action in the accumulator at that point (2 and 6
  hanging, 3 and 4 falling). Frames 2, 3, 4 and 6 are from the start of a
  run. So a falling
  character is not indexed one block to the left, and one starting to run
  is, for those frames. That this is a slip is **inferred**; the code is
  ported as it is.
- **A falling floor's object entry is incomplete.** `addmobobj` sets no
  bottom cut, facing or table (`MOVER.S:2085-2109`); the slot keeps what
  its last user wrote. The facing and table are not used for a floor. The
  bottom cut is: a floor in a slot that never held a character has a
  bottom cut of 0 (**inferred** to draw nothing; not tried). Slots are filled floors
  first, then characters, so slot *n* has held the kid as soon as *n* − 1
  floors were falling while he was in sight; in practice the cut is 192.
- **`CROPCHAR` still calls the debugger.** On the exit stairs, if the door's
  top is not above the character's feet, it calls `showpage` with `$F0`
  (`CTRLSUBS.S:1327-1330`, "temp!"), an entry of the debugging module that
  the final build does not include (`GAMEEQ.S:372-374`). What is at that
  address in the shipped game is **not yet verified** (next step: the
  memory map of `debugs` in `MASTER.S`); the port sets `unmodelled`.
- **Frame 0 is drawn as a delay.** With the kid in "frame 0", `setupkid`
  does not add him and runs a delay loop instead (`pause` 25,
  `TOPCTRL.S:807-809`; about 32,000 cycles by count, not measured), which
  keeps the frame's length. The port has no delay here.
- **`backtolife`**, which makes the kid blink, is set only by a development
  key that the final build leaves out (`TOPCTRL.S:1173-1182`, inside
  `do FinalDisk ... else`). The drawing code that reads it is ported.
- **A flash lasts as long as the next frame takes to draw.** `MainLoop`
  calls `flashon`, then `FrameAdv`, then `flashoff` (`TOPCTRL.S:385-393`).
  `DOFLASHON` shows the lo-res screen in one colour; the `PageFlip` at the
  end of `FrameAdv` switches hi-res back on. `DOFLASHOFF`, after that, sets
  switches that are already set: in every compared frame the display state
  is the same before and after it. So the length of a flash is the time the
  drawing code needs for that frame, not a counted number of frames
  (**confirmed** by the order of calls and by the logged switches; the
  time itself is not measured: unknown T1).
- **How long a frame takes** can now be counted, because the original's
  whole frame runs in the simulator: 51,231 to 224,292 cycles for the
  ordinary frames of the demo (mean 108,204: 106 ms, 9.4 frames a second,
  before the frame's sounds), and about 0.6 s for a frame that draws a
  whole screen. The game has no frame timer; these are simply how long the
  code runs (unknown T1 has the caveats).
- **The blank screen of a redraw** is the text page filled with spaces,
  shown from `drawbg` until the `PageFlip` at the end of `DoCleanCut`: the
  player sees black while the new screen is built on page 2, copied to
  page 1 and given its characters.
- **Three kinds of shared memory tie the game logic to the drawing code**,
  all found by the whole-frame comparison, where each showed up as a game
  variable that differed after a frame:
  the cell `IMAGE` / `height` (above), which `DIMCHAR` also writes each time
  the game logic asks for an image's size; the cell `OFFSET`, used by
  `GETBLOCKX` in the game logic and by every shifted draw; and zero page
  `$E8`–`$F7`, a scratch area in which eleven modules declare their own
  local variables (`locals = $e8`, `GAMEEQ.S:384`; `COLL.S` uses `$F0`–`$F3`
  directly), so that the collision code's `tempobjid` and the drawing
  code's `xsave` are the same byte. The port shares the first two cells
  between its game and drawing state (section 7) and keeps the scratch
  variables apart.
- **The game logic needs the sizes of the images that are loaded.**
  `GETEDGES` asks the blitter for the size of the character's image
  (`DIMCHAR`), and with it decides which blocks to mark. The opponents'
  table (`chtable4`) is a different file on different levels, with images
  of other sizes. The port's earlier tests gave both sides one fixed set of
  sizes; with each level's own table in memory the original marked other
  front pieces than the port (3 of the first 20 drawn traces). The port
  now takes the sizes from the tables it has loaded
  (`pop_imgtab_dims`).
- **An image file can be longer than its table.** `IMG.CHTAB4.GD` is 8,999
  bytes; its 32 images end at byte 5,281, and the loader reads 24 sectors
  (6,144 bytes) into a space that ends where the level begins
  (`MASTER.S:549-586`, `EQ.S:27-28`). The remaining 3,718 bytes are a
  leftover of the kid's table: 3,636 of them equal `IMG.CHTAB1` at the same
  offsets, and 8,999 is exactly where `IMG.CHTAB1`'s images end
  (**confirmed** by comparing the files; that the guard table was saved
  from a buffer which had held the kid's is **inferred**). Nine of the
  sixteen image files have such a tail. The test oracle first loaded the
  whole file and overwrote the level; it now loads a table up to the end of
  its last image and refuses tables that overlap. `chtable6` and `chtable7`, for the
  princess scenes, take the place of other tables and are not in memory
  during play (`GAMEEQ.S:12-15`).

## 7. Intentional deviations

- **The drawing parameters are `pop_gfx` fields**, separate from the game
  state. In the original they are zero-page cells that the game logic uses
  too. Two of them matter across that line, `IMAGE` (the game's `height`)
  and `OFFSET`: every drawing routine that is given the game state starts
  from the game's values and hands its own back (`pop_gfx_cells_in`,
  `_out`), so each pair of fields behaves as the one cell. `XCO`, `YCO`,
  `OPACITY` and `TABLE` are not shared this way: the game logic writes
  some of them (`CVTX` uses `XCO` as its work area) but nothing reads what
  the other side left, except the torch case above, which needs a state
  the game does not produce.
- **Module-local scratch variables are separate variables.** In the
  original they overlap in `$E8`–`$F7`; a value is never meant to pass from
  one module to another that way, and over the demo and the drawn traces
  none does (the game states would differ in a later frame). The
  whole-frame comparison leaves out the one compared variable that lies in
  that area (`tempobjid`).
- **The object list is part of the game state** (`pop_game`): the game
  logic fills it and the drawing code reads it.
- **More than 19 objects**, or more than 15 in one block, make the original
  write past its arrays; the port sets `unmodelled`. Likewise **more units
  than a meter has places for** (13 or more).
- **The editor's checks** (`inbuilder`) and the delay loop of frame 0 are
  left out.

## 8. What the comparisons cannot see

A case in which either side leaves the modelled memory is skipped
(verification.md). Deliberate errors that only turn a compared case into a
skipped one are caught by unit tests instead; the bug-injection scripts
name the test for each (`unit:`). In this part: the equality case of the
stairs test in `CROPCHAR`, a sword number beyond the table, and the height
a wipe leaves behind.

## 9. Checks

| Check | What it compares |
| --- | --- |
| `tests/test_gfx.c` (`gfx_unit`) | Hand-worked cases for each kind of mark, the meters, and the drawing of each kind of object, on made-up tables |
| `tests/test_objs.c` (`objs_unit`) | Hand-worked cases for the object list: positions, block indexes, marks, crops, sword, star, falling floors, who is listed |
| `tests/fuzz_control.py`, group `@objs` | The 15 object-list routines on random game states, C against the original: every game variable, the redraw buffers and the twelve arrays of the object list. The frame-level routines of earlier milestones (`FrameAdv`, `DoCleanCut`, `MainLoop`) now run this code on both sides as well |
| `tests/compare_fast.py` | A screen is drawn in full on both sides, then several times in a row: random marks in all nine buffers, random wipe heights, some gadget states changed, strengths set, a random object list with images from the character tables, on either page and changing pages as play does. C against the original's `ZEROLSTS`, `FAST`, `DRAWALL`: both pages (16,384 bytes), all five lists, the save-under list, the redraw buffers afterwards, and the two meter flags |
| `tests/replay_frames.py demo` | The self-running demo of level 0 from its first frame to its end with nothing of the drawing stood in for: the original's `MainLoop` (game logic, `DoCleanCut`, `DoFast`, the lists, the blitter in main memory, `PageFlip`) in the two-bank simulator against `pop_mainloop` with the drawing code behind its hooks. After every frame: the whole game state as in `replay_demo.py`, both hi-res pages, which page is shown and drawn on, hi-res / text, the text page's fill byte |
| `tests/replay_frames.py traces` | The same for 60 frames of random keyboard input from random places on levels 1–14 (`trace_kidstep.py`'s traces), each level with its own background and opponent tables, half of them with the level or time message on the screen |
| `tests/mutation_gfx.py`, `tests/mutation_check.py` | Deliberate one-token errors in the drawing code and in the object-list code must be noticed by those comparisons |

Recorded results are in verification.md.
