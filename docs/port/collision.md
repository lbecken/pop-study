# Port notes: collisions

Fourth C17 subsystem (milestone 9a): what stops a character at a wall, a gate,
a slicer or the mirror.

C sources: `modern-port/src/core/pop_collide.h`, `pop_collide.c`; state in
`pop_game.h`. Citations are `COLL.S:lines` in `01 POP Source/Source` of the
original repository at commit `25243550` unless another file is named.
Everything in sections 1–4 is **confirmed** by reading the source and by
running the original code (section 6) unless marked otherwise.

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `CHECKBARR` 72-184 | `pop_checkbarr` | Finds barrier edges the character crossed this frame; result in `collideL`, `collideR` |
| `getCData` 194-255 | (static) | Fills one row's collision data for the columns around him |
| `initCDbufs` 267-328 | (static) | Chooses which of last frame's rows becomes "last frame" |
| `getleftbar` 344-359, `getrightbar` 368-385 | (static) | x of a barrier's left / right edge within its block |
| `COLLISIONS` 400-433, `leftcoll` 467-486, `rightcoll` 440-460 | `pop_collisions` | Decides whether the crossing counts, then `collide` |
| `checkcoll1` 495-515, `CHECKCOLL` 527-589 | `pop_checkcoll` | Is this piece a barrier for him right now (gate height, slicer state, mirror)? |
| `AdjustScrn` 602-620 | (static) | x of a barrier that lies on a neighbouring screen |
| `collide` 631-680 | (static) | Puts him back at the barrier's face; ground or air? |
| `AirBump` 685-708, `GroundBump` 713-808, `BumpSound` 704-708 | (static) | The reaction: which bump sequence, the sound |
| `CHECKGATE` 1319-1367, `gatebarr?` 1374-1381 | `pop_checkgate`, `pop_gatebarr` | A gate closing on a standing character knocks him aside |
| `CHECKSLICE` 1186-1255 | `pop_checkslice` | Inside a closed slicer? (kid: from the collision data) |
| `CHECKSLICE2` 1266-1309 | `pop_checkslice2` | The same for opponents, from their edges |
| `ENEMYCOLL` 1389-1453, `DBarr2` 1464-1509 | `pop_enemycoll` | Opponents: backing into a wall or gate while fighting |
| `GoneUpstairs` 1162-1176 | `pop_goneupstairs` | Cue the end-of-level tune, increment `NextLevel` |

Callers (`TOPCTRL.S:635-690`, `701-740`): the kid's frame runs `setupchar`,
`rereadblocks`, `getedges`, `firstguard`, **`checkbarr`**, **`collisions`**,
**`checkgate`**, `checkfloor`, `checkpress`, `checkspikes`, `checkimpale`,
**`checkslice`**. An opponent's frame has no `checkbarr`: it runs
**`enemycoll`** in that place and **`checkslice2`** at the end.

## 2. The scheme

Collision is not "is he inside a wall now" but "did he cross an edge since
last frame". That is what lets a character stand inside a gateway, hang in
front of a wall, or be placed overlapping a barrier by an animation, without
being thrown out.

**Edges.** `GETEDGES` (`docs/port/player-control.md`) gives `CDLeftEj` and
`CDRightEj`: the left and right x of the character's image, pulled in by 3 on
each side for frames marked "thin".

**Barriers.** A piece is a barrier if `CMPBARR` gives it a code; the code
selects how much of the 14-unit block the barrier occupies (`BarL`, `BarR`,
lines 55-56). Barriers are drawn 7 units right of the block grid
(`angle`), so for column *c* with left edge *e* = 58 + 14*c*:

| Code | Pieces | Barrier occupies |
| --- | --- | --- |
| 1 | wall panels, gate | *e*+19 … *e*+20 (a 2-unit post at the block's right) |
| 3 | mirror, slicer | *e*+7 … *e*+9 |
| 4 | solid block | *e*+7 … *e*+20 (the whole block) |

**Collision data.** For each of the ten columns of a row, one byte (`CD`) and
the screen it was read from (`SN`):

- low nybble `$F`: the barrier's left edge is left of his right edge
  ("he is past its left edge");
- high nybble `$F`: the barrier's right edge is right of his left edge;
- `$FF`: both, so he overlaps it. `$00`: no barrier in that column.

Only the columns from one left of his left edge to one right of his right
edge are filled each frame (`begrange` … `endrange`−1); the rest keep `SN` =
`$FF`, "no data". The same is done for the row below and the row above, into
separate buffers.

**Detection.** `CHECKBARR` first makes one of last frame's three rows the
"last frame" row: his own if `CharBlockY` is unchanged (or changed by ±3,
which is the same row seen from the screen above or below), "above" if he
moved down a row, "below" otherwise. Then, per column: if the screen matches
and a nybble that was 0 last frame is `$F` now, that edge was crossed.
`collideL` is the column whose **left** edge he crossed (moving right),
`collideR` the one whose **right** edge he crossed (moving left).

**Reaction** (`COLLISIONS`):

```
if AMtimer != 0: tick the speaker, AMtimer -= 1, return     ; cheat: walls off
if hanging (CharAction 2 or 6) or climbing (frames 135-148): return
left edge crossed:  only if facing right, or sword drawn
right edge crossed: only if facing left,  or sword drawn
CHECKCOLL: flask - no; gate - only if lower than his image is tall, less 6;
           slicer - only if shut; mirror - not for the kid in frames 39-43
           facing left (he smashes it); anything else - yes
collide:   CharX += distance back to the barrier's face
           floor on his side of the barrier?  GroundBump : AirBump
```

`GroundBump` (lines 713-808):

| Situation | Result |
| --- | --- |
| More than 14 lines above the floor (and no sword drawn) | `AirBump` |
| `CharYVel` ≥ 22 | 5 units back, nothing else; `checkfloor` will land him |
| Sword drawn, hit from the front | seq `bumpengfwd`, sound |
| Sword drawn, backed into it | seq `bumpengback`, 1 unit forward, no sound |
| Was in frame 24, 25 (standing jump), 40–42 (running jump) or 102–106 (falling) | seq `hardbump`, sound |
| Otherwise | seq `bump`, sound |

`AirBump`: 4 units back; if already falling just zero `CharXVel`, else seq
`bumpfall`; sound. The sound is `SmackWall` and it also sets `alertguard`.

## 3. Semantic pitfalls

1. **A barrier he did not see coming is not a barrier.** A column has "last
   frame" data only if it was within one column of his edges last frame. If a
   character arrives overlapping a barrier that was further away than that,
   nothing is detected, and he is then inside it with both nybbles already
   set. The unit test `test_too_fast_to_notice` shows this with a 31-unit
   step. Whether any move in the game is that fast is **not yet verified**
   (next step: the largest per-frame `dx` in the sequence table plus the
   widest image, against 14 + the range margin).
2. **Backing into a wall is not stopped** unless the sword is drawn: a left
   edge only stops a character facing right, a right edge only one facing
   left (lines 441-446, 468-473).
3. **`CHECKCOLL` compares the slicer state unmasked** (`cmp #slicerExt`,
   lines 564-566), while `CHECKSLICE` masks bit 7 (lines 1207-1209). Bit 7 is
   the "blood on the blades" flag that slicing sets (line 1225). So a slicer
   that has already cut someone is never a *barrier*, though it still slices.
   Whether that is intended is unknown; the port keeps both tests as written.
4. **Two ways of indexing `FloorY`.** `GroundBump` uses `lda FloorY+1,x` with
   x = `CharBlockY` (line 720): no wrap, so row −1 (`$FF`) reads 256 bytes on.
   The floor code uses `inx` / `lda FloorY,x`, which wraps
   (`docs/port/player-control.md` §4.2). The port flags the first case as
   unmodelled.
5. **`:deadbump` cannot be reached.** `GroundBump` tests `CharLife` = 0 (line
   740), but its only caller, `collide`, has already returned for any
   `CharLife` ≥ 0 (lines 634-635). Found by the mutation check: removing the
   test changes nothing.
6. **`BarL`/`BarR` entry 2 is unused.** The table's comment (lines 51-53)
   calls code 2 "flask"; `CMPBARR` no longer returns it and `CHECKCOLL`
   rejects flasks first.
7. **`ENEMYCOLL` measures the gate before measuring the character.**
   `gatebarr?` compares with `imheight`, which `getedges` sets; `ENEMYCOLL`
   calls `gatebarr?` first and `setupchar`/`getedges` afterwards (lines
   1402-1431), so the first test uses whatever height the last `getedges`
   left. In the opponent's frame that is his own from a few calls earlier
   (`TOPCTRL.S:728-732`), so in practice the value is current.
8. **`DBarr2` mirrors `DBarr`.** Same arithmetic with the facing test
   reversed, because it measures to a barrier *behind* the character (line
   1466, "reversed from DBarr").
9. **The loop over columns can wrap.** `getCData` runs from `begrange` until
   the index *equals* `endrange`, counting up in 8 bits (lines 205-254). If
   the edges are reversed or far off screen it goes round through 255. The
   port loops the same way.
10. **Self-modifying code.** `getCData` and `initCDbufs` patch the address
    operands of their own store instructions to choose a buffer (lines
    195-196, 306-307). The port passes a buffer index.
11. **`CHECKBARR`'s X and Y are not results** on every path: they are loaded
    from `collideL`/`collideR` only at the normal exit, not when a turning
    character returns early (lines 80-82, 182-183). The port returns nothing.

## 4. `AMtimer`: a development key left in the collision code

`COLLISIONS` and `ENEMYCOLL` both begin by testing `AMtimer`; while it is
non-zero the character passes through barriers, with a speaker tick per
frame. It is set to 10 frames by a key handler (`SPECIALK.S:401-405`,
`initAMtimer = 10` at line 55, key `kantimatter` = control-Q at line 117).
That handler is in `TempDevel`, headed "Temp development keys (remove for
final version)" (`SPECIALK.S:376-379`): it is assembled only when
`DebugKeys` is set and acts only when the `develment` flag is on
(`SPECIALK.S:380-383`). In the source as released `DebugKeys = 0`
(`SPECIALK.S:4`), so the handler is not assembled and the only other write
to `AMtimer` is its reset at level start (`TOPCTRL.S:283`): the timer can
never be non-zero in that build (**confirmed** by listing every reference).
The two tests in `COLL.S` themselves
are unconditional, so the port keeps them; nothing in the port sets
`AMtimer`.

## 5. Intentional deviations

- **Hooks** for what is not ported: `smashmirror` (`MOVER.S:259`) and the
  speaker read at line 403. `CUESONG` (`SPECIALK.S:1332-1335`) only stores
  the request in `SongCue` / `SongCount`, and is ported as that.
- **`GoneUpstairs` is ported**, so the sequence interpreter's level-exit
  instruction now runs it instead of calling out.
- **Buffers as arrays.** The eight buffers are 16 bytes apart in memory with
  ten bytes used. An index 10–15 lands in the unused part and is reproduced;
  16 and above (or 10 and above for `SNabove`, the last one) would hit other
  variables and is flagged as unmodelled.
- **Stray reads flagged**, as in the other subsystems: block-edge lookups
  outside the edge table, `FloorY+1,x` for row −1.

## 6. Tests

| Test | What it shows |
| --- | --- |
| `collide_unit` (`tests/test_collide.c`, 109 checks, synthetic data) | Hand-derived cases: the data bytes for a wall ahead, detection on crossing, each kind of bump, facing rules, gate heights, slicer and mirror, neighbouring screens |
| `control_c_vs_original` (`tests/fuzz_control.py`) | The eight collision entry points among its 60 routines, on random states including random collision buffers, against the original in the 6502 simulator |
| `kidstep_c_vs_original` (`tests/trace_kidstep.py`) | `checkbarr`, `collisions`, `checkgate` and `checkslice` in their places in the kid's frame, 60 frames per trace: the kid now stops at walls |
| `tests/mutation_check.py` | 24 deliberate errors in `pop_collide.c`: 23 noticed, 1 without effect (pitfall 5) |

Limits:

- Gates do not move and slicers do not run yet (`MOVER.S`), so in the trace
  they are as the level file has them.
- `firstguard` (kid running into a guard, `MISC.S`) is not ported.
- The screen-change code is not ported. The trace keeps `VisScrn` and the
  neighbour variables pointing at the kid's screen itself; a trace in which
  the kid runs off the screen edge ends when a lookup leaves the tables.
- Nothing here has been compared with the running game.
