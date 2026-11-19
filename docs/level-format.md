# Level blueprint format

Status: parsed and tested for all 15 levels (milestone 5).
Parser: `tools/extract_assets/pop_level.py`.

Citations are `FILE.S:lines` in `01 POP Source/Source` of the original
repository at commit `25243550` unless a longer path is given.

## File layout

A level is 2304 bytes (`$900`), loaded at `$B700` in auxiliary memory.
The author documents it in `04 Support/MakeDisk/S/COMMENTS.S:237-338`; the
table below is **confirmed** against `EQ.S:258-287` and the code cited.

| Offset | Size | Name | Contents |
| --- | --- | --- | --- |
| 0 | 720 | `BLUETYPE` | 24 screens × 30 blocks: piece type and flags |
| 720 | 720 | `BLUESPEC` | same shape: per-block state or pattern |
| 1440 | 256 | `LINKLOC` | pressure-plate link list, part 1 |
| 1696 | 256 | `LINKMAP` | pressure-plate link list, part 2, and plate timers |
| 1952 | 96 | `MAP` | 24 screens × 4 neighbours |
| 2048 | 256 | `INFO` | start positions; first 64 bytes belong to the editor |

Screens are numbered 1–24 and 0 means "no screen". Blocks are numbered 0–29,
left to right and top to bottom, ten per row. Screen *s* starts at
`(s−1) × 30` in both block arrays (`CALCBLUE`, `GRAFIX.S:1757-1778`).

## `BLUETYPE`

| Bits | Meaning | Evidence |
| --- | --- | --- |
| 0–4 | piece id 0–29 (`idmask`) | `EQ.S:486`, `FRAMEADV.S:2056-2057` |
| 5 | `reqmask` | `EQ.S:485`; meaning **not yet verified** |
| 6–7 | `secmask` | `EQ.S:484`; meaning **not yet verified** |

In the 15 shipped levels bit 5 is set on 3,806 of 8,970 blocks in reachable
screens, and bits 6–7 are never set (**confirmed** by scan).

Piece ids (`MOVEDATA.S:8-37`):

| | | | | |
| --- | --- | --- | --- | --- |
| 0 space | 1 floor | 2 spikes | 3 posts | 4 gate |
| 5 dpressplate | 6 pressplate | 7 panelwif | 8 pillarbottom | 9 pillartop |
| 10 flask | 11 loose | 12 panelwof | 13 mirror | 14 rubble |
| 15 upressplate | 16 exit | 17 exit2 | 18 slicer | 19 torch |
| 20 block | 21 bones | 22 sword | 23 window | 24 window2 |
| 25 archbot | 26 archtop1 | 27 archtop2 | 28 archtop3 | 29 archtop4 |

Id 5 (`dpressplate`) does not appear in any reachable screen; the drawing code
substitutes it for a pressure plate whose timer is running
(`FRAMEADV.S:2059-2081`). Id 13 (`mirror`) does not appear either; where the
mirror comes from is **not yet verified** (`TOPCTRL.S:85-90` names a fixed
mirror location on level 4).

## `BLUESPEC`

What the byte means depends on the piece. Values found in the shipped levels
(**confirmed** by scan of reachable screens):

| Piece | In the file | In the running game |
| --- | --- | --- |
| gate | 1 = starts open, 2 = starts closed | height × 4: 188 (open) … 0 (closed) |
| loose floor | 0 | 0 = still; bit 7 set + frame while shaking |
| flask | type 1–5 | type in bits 5–7, bubble frame in bits 0–4 |
| pressure plates | index of their first link entry | same |
| space | back-wall pattern 0–3, or 255 for none | same |
| floor | back-wall pattern 0–2; 255 is treated as 0 | same |
| block | pattern 0 or 1 | same |
| panels | pattern 0–2 | same |
| torch, spikes, slicer, exit, sword | 0 | animation frame or state |

The conversion from file to game values is `SETINITIALS` (`SUBS.S:1441-1462`)
with `GETINITOBJ` (`FRAMEADV.S:2217-2247`): for gates the setting indexes the
two-entry table `initsettings` (`FRAMEADV.S:22-23`), loose floors are zeroed,
flask types are shifted left five bits, and every other piece is left alone.
It runs over screens `INFO[0] − 1` down to 1.

## Link list

A pressure plate's `BLUESPEC` byte points at its first link entry; entries
follow until one has the "last" bit (`COMMENTS.S:293-309`).

| Byte | Bits | Meaning |
| --- | --- | --- |
| `LINKLOC` | 0–4 | target block 0–29 |
| | 5–6 | low two bits of the target screen |
| | 7 | last entry for this plate |
| `LINKMAP` | 5–7 | high three bits of the target screen |
| | 0–4 | plate timer; a plate is drawn pressed when this is 2 or more |

The timer bits of every plate's first entry are 0 in all shipped levels
(**confirmed**), so all plates start up. How the game walks the list when a
plate is pressed is **not yet analysed**.

## `MAP`

Four bytes per screen: the screens to the left, right, above and below
(`COMMENTS.S:313-328`; `GETLEFT` etc., `CTRLSUBS.S:244-275`). Following the map
from the start screen and laying screens on a grid gives a consistent layout
for all 15 levels: no screen is reached at two different positions
(**confirmed** by `render_room.py`).

## `INFO`

| Offset | Name | Notes |
| --- | --- | --- |
| 0 | – | "number of screens + 1" as used by `SETINITIALS` (`SUBS.S:1442`) |
| 1–63 | – | editor data, **not analysed** |
| 64, 65, 66 | `KidStartScrn`, `KidStartBlock`, `KidStartFace` | `EQ.S:275-277` |
| 68, 69 | `SwStartScrn`, `SwStartBlock` | 0 in every shipped level |
| 71… | `GdStartBlock`, `GdStartFace`, `GdStartX`, `GdStartSeqL`, `GdStartProg`, `GdStartSeqH` | six arrays of 24, one slot per screen (`EQ.S:282-287`); semantics **not yet analysed** |

## Findings

1. **All 15 levels parse** and every gate setting in a processed screen is 1
   or 2.
2. **`LEVEL14` carries leftovers.** `INFO[0]` is 7, so only screens 1–6 are
   processed and reachable, but screens 13 and 18 still hold gates with
   game-format states (188 and 0) rather than editor settings. They are never
   read.
3. **Reachable screens per level** range from 6 (level 14) to 24; 299 in total.
4. **`floor` with spec 255** occurs 97 times; the drawing code maps any value
   above 3 to pattern 0 for floor but to "no pattern" for space
   (`FRAMEADV.S:1055-1072`).

## Output

`render_room.py` writes `level.json` per level: start positions, reachable
screens, and for each screen its neighbours, piece ids, flag bits, spec bytes
and a three-line text map. It is derived data and is not committed.
