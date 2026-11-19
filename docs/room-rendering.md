# Room rendering

Status: the full-screen redraw is modelled in Python and renders all 299
reachable screens of the 15 levels (milestone 5). No characters, no colour.

Model: `tools/extract_assets/pop_room.py` (what to draw) and `pop_hires.py`
(how bytes reach the screen). Citations are `FILE.S:lines` in
`01 POP Source/Source` of the original repository at commit `25243550`.

```bash
python3 tools/extract_assets/render_room.py --source ../pop-original --out build/rooms --level 1
```

Writes one PNG and one JSON (the image lists) per screen, the decoded level,
and an overview that places every screen by the level's own map.

## Geometry

**Confirmed** from `SURE` (`FRAMEADV.S:49-109`) and `TABLES.S`:

- A screen is 10 × 3 blocks. A block is 4 bytes (28 dots, 14 game units) wide
  and 63 lines high. Block bottoms are on lines 65, 128 and 191.
- Byte-aligned: every background piece is placed at a byte column, never
  shifted. Only the flask's bubbles use a dot offset.
- Each piece is drawn as up to five sections, each a separate image
  (`BGDATA.S:38-41`):

| Section | Anchor (bottom-left) | What it is, as the code uses it |
| --- | --- | --- |
| A | block left, bottom − 3 | the piece inside its own block |
| B | same | the part that shows in the block to the **right** |
| C | block left, bottom | the part that shows in the block **above and to the right** |
| D | same | the piece's front edge along the bottom of its own block |
| front | relative to A | drawn after characters, so they pass behind it |

So a block's picture is assembled from three pieces: its own A, D and front,
the B of its left neighbour, and the C of the piece below and to the left.

## Order

`SURE` works through rows **bottom to top** (the source comment says top to
bottom; the loop starts at row 2 and counts down) and columns left to right.
For each block, `RedBlockSure` (`FRAMEADV.S:346-359`) does:

1. `drawc` – C of the piece below-left, only if this block is open
   (space, pillar top, panel without floor, or arch top), followed by the
   left neighbour's B-mask
2. `drawmc` – the moving part of a gate below-left
3. `drawb` – B of the left neighbour, unless this block is a solid block
4. `drawmb` – moving B: gate bars, spikes, loose floor, torch flame, exit door
5. `drawd`, `drawmd` – own D, and a loose floor's D
6. `drawa`, `drawma` – own A (masked first if the left neighbour intrudes),
   then spikes, slicer, flask bubbles, sword
7. `drawfrnt` – own front piece, into the foreground list

Then a fourth pass draws the D-sections of the **bottom row of the screen
above**, three lines high at the top of the screen (`FRAMEADV.S:111-178`). If
there is no screen above, it is drawn as a row of floor.

Neighbouring screens contribute: the right-hand column of the screen to the
left supplies B-sections for column 0 (`getprev`, `FRAMEADV.S:561-602`), and
the top row of the screen below supplies C-sections for the bottom row
(`getbelow`, `616-700`). A missing left screen counts as solid blocks; a
missing lower screen counts as floor.

## Lists and blitters

Nothing is drawn directly. Routines append to three lists, which are then
drawn in order (`DRAWALL`, `GRAFIX.S:484-505`):

| List | Limit | Drawn with | Opacity values used |
| --- | --- | --- | --- |
| background | 199 | `FASTLAY` | store, OR, AND |
| mid | 45 | `LAY` | store (flask bubbles; characters go here in play) |
| foreground | 99 | `FASTMASK` then `FASTLAY` | mask + OR pairs, or store |

- An image number with bit 7 set means `bgtable2`, otherwise `bgtable1`
  (`setbgimg`, `GRAFIX.S:819-838`).
- "AND" entries are mask images: the image bytes are ANDed onto the screen to
  cut a hole before the visible piece is ORed in (`maska`, `maskb`,
  `gatecmask`, `doormask`).
- Foreground pieces are drawn twice: once as a mask derived from the image
  itself, once ORed (`maddfore`, `FRAMEADV.S:1176-1181`). Pieces that cover
  whole bytes are simply stored: solid blocks, the upper arch pieces, and
  posts in the dungeon set (`FRAMEADV.S:790-823`).
- `FASTLAY` clips at the top of the screen only. `ADDBACK` and `ADDFORE` drop
  any entry whose bottom line is 192 or more (`GRAFIX.S:209-211`), which is how
  pieces hanging off the top are discarded.
- The screen is cleared to `$80` first (`CLS`, `HIRES.S:204-222`).

## Pieces with state

| Piece | Drawn from | Rule (**confirmed**) |
| --- | --- | --- |
| gate | `drawgateb`, `1814-1907` | bottom piece at `A − (state/4 + 1)`, then 8-line bar sections upward, then a 1–8 line top piece chosen to fill the 62-line opening exactly; if the bottom piece would cross the floor line the background is redrawn first |
| gate, seen from below | `drawgatec`, `1701-1730` | one of 8 images by `(state/4) mod 8` |
| exit | `drawexitb`, `1616-1694` | stairs, then door slats every 4 lines from `A − 14 − state/4` upward; no stairs on the kid's start screen (it is an entrance) |
| torch | `drawtorchb`, `1493-1506` | flame frame = state (0–17), drawn one byte into the block to the torch's right; skipped for a torch in the last column of the screen to the left |
| flask | `drawflaska`, `1512-1525` | bubble frame from the low 5 bits; taller flask and a different dot offset by type |
| spikes | `drawspikea/b`, `1453-1488` | frame = state; nothing when retracted |
| slicer | `drawslicera/f`, `1548-1611` | jaw pair by a 7-step sequence table |
| loose floor | `1365-1448` | A, B offset and D by wobble frame |
| pressure plate | `getobjid1`, `2059-2098` | pressed plate becomes `dpressplate`; a pressed "up" plate is drawn as plain floor |
| sword | `drawsworda`, `1530-1543` | one of two images |
| space, floor | `drawb`, `1055-1072` | back-wall pattern chosen by the spec byte |
| block, panels | several | variant chosen by the spec byte |

The palace set adds a stripe image above certain B-sections
(`FRAMEADV.S:1026-1037`); the dungeon set does not.

## Which image files

`MISC.S:772-773` gives a background set number per level:

| Levels | Set | File used here |
| --- | --- | --- |
| 0, 1, 2, 3 | 0 | `IMG.BGTAB*.DUN` |
| 4, 5, 6, 10, 11, 14 | 1 | `IMG.BGTAB*.PAL` (**confirmed**: code tests `BGset1 = 1` for palace) |
| 7, 8, 9, 12, 13 | 2 | `IMG.BGTAB*.DUN` (**inferred**) |

Set 2 is loaded from a different disk track than set 0 (`MASTER.S:524-525`),
but the repository's game directory has only one dungeon pair. Whether set 2
differed on the shipped disk is **not yet verified** (unknown G16).

## Results over all levels

**Confirmed** by `render_room.py` and the test suite:

- 299 screens build and draw with no error.
- Largest lists: 164 background entries (limit 199), 30 foreground (limit 99),
  2 mid.
- No screen writes a byte past column 39.
- 2,656 list entries are dropped for being off the top; typically 9 per
  screen, the B-sections of the row above.
- Every image number the tables produce exists in the image files: 51 and 53
  distinct `bgtable1` images are used (dungeon, palace), 17 and 28 of
  `bgtable2`.
- Laid out by `MAP`, adjacent screens of a level line up: floors, walls and
  gates continue across screen edges.

## Things a port must reproduce

1. **Rows bottom to top.** Later rows overdraw earlier ones where sections
   overlap; reversing the order changes the picture.
2. **Persistent variables.** The routines pass `XCO`, `YCO`, `IMAGE`,
   `OPACITY`, `PRECED`, `spreced` and `state` through shared memory, and some
   paths use a value left by an earlier routine: `domaskb`, `drawlooseb`,
   `drawgatec` and one branch of `drawgateb` do not set `XCO`; the pass for the row above resets `PRECED`
   but not `spreced`; with no screen above, `state` keeps the last block's
   value; with no screen below, `SBELOW` is not refreshed. The model keeps
   these variables persistent for that reason.
3. **8-bit line arithmetic.** Positions above the screen wrap to 193–255 and
   are then rejected by the "192 or more" test.
4. **Out-of-range torch state.** For a state above 17 `setupflame` returns
   early and `drawtorchb` still adds an entry, with whatever image was set
   last (`GAMEBG.S:735-737`, `FRAMEADV.S:1504-1506`). No shipped level starts
   in that state; whether play can reach it is **not yet verified**.
5. **No right-edge clipping** in the fast blitters; the tables and the exit
   code keep pieces inside the screen instead.

## Not modelled

- Characters, falling floors and other objects; the gate bars that are drawn
  over the kid when he stands behind them (`DrawGateBF?`, `FRAMEADV.S:829-842`).
- The partial redraw (`FAST`) driven by the redraw buffers, wipes, and the
  save-under buffers.
- Strength meters and messages.
- Colour.

## Verification

- Unit tests on synthetic levels and synthetic piece tables check section
  placement, neighbour rules, row order, gate geometry, the palace/dungeon
  difference, torches, flasks and pressure plates.
- Integration tests build all 299 screens and lock the list sizes of level 1
  screen 1 and a hash of its page as a regression reference.
- **By eye:** level 1 screen 1 shows the start room as the game is known to
  look (brick walls, two torches, pillars, a gate at the upper left); the
  palace exit screen of level 5 shows columns, the exit door and gates.
- **Not done:** comparison with an emulator screenshot. The hash above locks
  the current output; it is not evidence that the output is right.
