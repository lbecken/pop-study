# Port notes: player control, floor checks, falling and landing

Third C17 subsystem (milestones 8b and 8c): what the kid does in answer to the
joystick, and what happens when the floor under him ends or arrives.

C sources: `modern-port/src/core/pop_control.h`, `pop_control.c`; state in
`pop_game.h`. Citations are `FILE.S:lines` in `01 POP Source/Source` of the
original repository at commit `25243550`. Everything in sections 1–4 is
**confirmed** by reading the source and by running the original code
(section 6) unless marked otherwise.

## 1. Routines

`CTRL.S` unless noted. A routine's inputs are original variables; "→ seq"
means it starts an animation sequence with `jumpseq`.

### Control

| Original | C | What it decides |
| --- | --- | --- |
| `PLAYERCTRL` 554-582 | `pop_playerctrl` | Kid's control step: dies if `KidStrength` is 0, counts `stunned` down, then reads the selected input device (`LoadSelect`, `getselect`, `clrjstk`; game-loop.md §2) and calls `UserCtrl`; on level 0 calls `DemoCtrl` and `GenCtrl` |
| `UserCtrl` 610-622 | `pop_userctrl` | Mirrors the stick for a character facing right, calls `GenCtrl`, mirrors it back |
| `clrall` 625-632 | `pop_clrall` | Clears the four direction flags; returns 1 |
| `GENCTRL` 648-733 | `pop_genctrl` | Dispatch by `CharLife`, `CharAction`, `CharSword`, `CharID`, `CharPosn` (section 2) |
| `GuardCtrl` 736-756 | `pop_guardctrl` | A guard standing alert (frame 166): en garde, or turn |
| `FightCtrl` 759-889 | `pop_fightctrl` | Sword drawn: sheathe, turn, strike, block, advance, retreat |
| `DoBlock` 896-956, `DoStrike` 958-989, `DoRetreat` 991-1005, `DoAdvance` 1007-1024 | `pop_doblock` … | The four fighting moves |
| `standing` 1031-1247, `Stairs` 1249-1259 | `pop_standing` | Pick up, draw sword, turn, run, step, jump, climb down, crouch, enter the exit |
| `crouching` 1268-1299 | `pop_crouching` | Pick up, stand, crawl |
| `starting` 1301-1313, `stjumpup` 1315-1326 | `pop_starting`, `pop_stjumpup` | Turn a start or a jump-up into a standing jump |
| `turning` 1328-1346 | `pop_turning` | Turn straight into a run |
| `arunning` 1348-1406 | `pop_arunning` | Stop, turn, running jump, dive |
| `hanging` 1408-1529 | `pop_hanging` | Climb up, hang straight, drop |
| `DoStartrun` 1537-1561 … `DoJumphigh` 1880-1924 | `pop_dostartrun` … `pop_dojumphigh` | The actions those handlers start |
| `TryPickup` 2094-2118, `PickItUp` 2127-2162 | `pop_trypickup`, `pop_pickitup` | Flask or sword in front: crouch, then drink / take |

### Floor

| Original | C | What it does |
| --- | --- | --- |
| `CHECKFLOOR` 106-138 | `pop_checkfloor` | Dispatch by `CharAction` (section 3) |
| `falling` 76-104 | `pop_falling` | In free fall: reached the floor line? land, fall through, or try to grab |
| `hitflr` 140-242 | `pop_hitflr` | Landing: spikes, soft / medium / hard by `CharYVel` |
| `fallon` 248-311 | `pop_fallon` | Grab a ledge on the way down |
| `onground` 313-354 | `pop_onground` | Floor still there? else start to fall. Level 12's phantom bridge |
| `startfall` 356-436, `CDpatch` 438-463 | `pop_startfall`, `pop_cdpatch` | Choose the falling sequence; keep him out of the wall he fell beside |
| `InsideBlock` 465-502 | `pop_insideblock` | Push him out of a solid block |
| `CHECKIMPALE` 2016-2056, `DOIMPALE` 2058-2086 | `pop_checkimpale`, `pop_doimpale` | Running or landing on spikes |

### Helpers

| Original | C | In → out |
| --- | --- | --- |
| `FACEJSTK` `SPECIALK.S:788-797` | `pop_facejstk` | `JSTKX` negated; `clrF` ↔ `clrB` |
| `DECSTR` `MISC.S:691-735` | `pop_decstr` | A = amount → `ChgKidStr` or `ChgOppStr` = −amount (or −all, returning 0, if fatal) |
| `GETSPIKES` `MOVER.S:365-382` | `pop_getspikes` | Spike state → 0 safe, 1 sprung, 2 springing |
| `GETOPDIST` `CTRLSUBS.S:2052-2102` | `pop_getopdist` | Signed distance to the opponent, + ahead, ±127 far or on another screen |
| `CHECKLEDGE` `CTRLSUBS.S:1814-1858` | `pop_checkledge` | Can this piece be grabbed, with `blockid` clear above it? |
| `INDEXBLOCK` 615-631, `MARKRED` 1734-1737, `MARKWIPE` 1744-1757 (`CTRLSUBS.S`) | `pop_indexblock`, `pop_markred`, `pop_markwipe` | Redraw marks |
| `REMOVEOBJ` `SUBS.S:1415-1432` | `pop_removeobj` | Flask or sword becomes plain floor |
| `SETUPCHAR` 794-841, `decodeim` 1017-1039, `GETEDGES` 1071-1157 (`CTRLSUBS.S`) | `pop_setupchar`, `pop_getedges` | Screen position, image number and table, edges and blocks covered |
| `GETFWDDIST` `COLL.S:822-914`, `DBarr` 925-983, `gatebarr?` 1374-1381 | `pop_getfwddist` | How far he can step: A = units, X = 0 edge / 1 barrier / 2 clear, Y = piece |

## 2. Control: how it is organised

**Input.** The handlers never look at a device. They read (`CTRL.S:636-646`):

- `JSTKX`: negative = forward, positive = back, 0 = centred;
  `JSTKY`: negative = up, positive = down; `btn`: negative = pressed.
- `clrF`, `clrB`, `clrU`, `clrD`, `clrbtn`: negative means "freshly pushed and
  not yet acted on". A handler that acts on a push stores 1 in the flag, so
  one push gives one action however long it is held.

"Forward" is relative to the character. The stick is physical, and its
negative x is *left*; so for a character facing left the raw values already
mean forward/back, and for one facing right `UserCtrl` negates `JSTKX` and
swaps `clrF`/`clrB` around the call.

**Dispatch** (`GENCTRL`), first match wins:

| Condition | Handler |
| --- | --- |
| `CharLife` ≥ 0 (dead) | if standing (frames 15, 166, 158, 171) → seq `dropdead`; return |
| `CharAction` = 5 (bumped) or 4 (falling) | discard pushes (`clrall`); return |
| `CharSword` = 2 | `FightCtrl` |
| `CharID` ≥ 2 (a guard) | `GuardCtrl` |
| frame 15; frames 50–52 | `standing` |
| frame 48 | `turning` |
| frames 1–3 | `starting` |
| frames 67–69 | `stjumpup` |
| frames 4–14 | `arunning` |
| frames 87–99 | `hanging` |
| frame 109 | `crouching` |
| anything else | nothing: the animation plays out |

So control is frame-driven: a sequence gives up control by passing through
one of these frames. That is what makes moves feel committed.

**`standing`**, in order: fresh button → `TryPickup`; shadow with down+forward
→ `DoEngarde`; kid with a sword and an alert enemy within reach → turn or
`DoEngarde`; then with the button down: back → turn, up, down, forward → one
careful step (`DoStepfwd`); with it up: forward → `DoStartrun`, back →
`DoTurn`, up, down. "Up" in front of open stairs → `Stairs`; with the stick
also forward → `DoStandjump`; else `DoJumpup`. "Down" → step off an edge he
faces, climb down an edge behind him, else crouch.

**`DoJumpup`** decides between four jumps by looking at the blocks above:
grab the ledge above-in-front (`DoJumphang`), grab the one above after moving
back (`DoJumphang` after `CharX` −14+dist), jump back off an edge to grab it
(`DoJumpedge`), or just jump (`DoJumphigh`, which picks the "bump the ceiling"
or the free sequence by reading the block above the hands).

**`DoRunjump`** (`CTRL.S:1752-1843`; this resolves unknown E14) is what makes
running jumps land. With `ztemp` = `CharX` projected 4 units forward:

```
look at the next block, then the one after      ; RJLookahead = 1
    if it is spikes or has no floor: edge found, n = blocks skipped (0 or 1)
    none found: jump now, wherever he is
a = distance from ztemp's foot to the block edge + 14*n - 14     ; RJLeadDist
if -8 <= a < 2:      CharX += a + 4 (forward); jump   ; take off lined up
else if a >= 2:      return                           ; too early: ask again
else (a < -8):       CharX += 1; jump                 ; too late: will miss
```

"Ask again" works because `clrU` is still negative: the same push is retried
every frame of the run until the take-off point comes within reach. The
player's timing window is therefore several frames wide, and the take-off is
always snapped to the same spot.

## 3. Floor: how it is organised

`CHECKFLOOR` runs once per frame, after movement. By `CharAction`
(`CTRL.S:106-138`):

| `CharAction` | Meaning here | Does |
| --- | --- | --- |
| 6 | hanging straight | nothing |
| 5 | bumped | `onground`, only if crouched or dead (frames 109, 185) |
| 4 | free fall | `falling` |
| 3 | controlled fall | `fallon`, only in frames 102–105 |
| 2 | hanging | nothing |
| 0, 1, 7 | on the ground | `onground` |

(This is the part of unknown E12 the floor code settles; the values are set
by the `act` instruction in sequences.)

**`onground`**: only frames whose `Fcheck` has bit 6 (`fcheckmark`) need
floor. If the piece under the foot has none, `startfall`: `CharBlockY` += 1
(so "the floor below" becomes "his floor"), a falling sequence chosen by the
frame he was in (step off 9 / 13, jump 26, running jump 44, hang-drop 81–85,
fighting 150–179), one `animchar` at once, and a correction so that he does
not start the fall inside a wall.

**`falling`**: while `CharY` is above the floor line of `CharBlockY`, try to
grab (`fallon`). Once past it: if the piece there has floor → `hitflr`;
otherwise `CharBlockY` += 1 and he keeps falling.

**`hitflr`**: `CharY` snaps to the floor line; then by speed:

| `CharYVel` | Kid | Shadow (`CharID` 1) | Guards (≥ 2) |
| --- | --- | --- | --- |
| < 22 (`OofVelocity`) | `softland` (`landengarde` if sword drawn) | same | `landengarde` |
| 22–32 | `medland`, −1 strength, Splat; `hardland` if that was the last unit | `softland` | `hardland`, dead |
| ≥ 33 (`DeathVelocity`), or already dead | `hardland`, −all, Splat | same | same |

With gravity 3 per frame up to 33 (`docs/port/world-queries.md`), those
thresholds are what make one storey safe, two cost a unit and three kill;
that reading is **inferred** from the constants, not measured in play.
Landing on spikes that are out impales instead (`DOIMPALE`: spikes jammed,
`CharX` centred on them, Impaled sound, −all).

**`fallon`** (ledge grab): needs the button held, alive, `CharYVel` < 32, and
`CharY` within 25 lines of the floor line. It moves him 8 units back, asks
`CHECKLEDGE` about the block above-in-front, and on success aligns him with
it, starts `fallhang` and sets `stunned` = 12, which blocks climbing up for
12 frames (`hanging` tests it). On failure `CharX` is restored.

## 4. Semantic pitfalls

1. **A loose floor's "already falling" test is a `bit`, not a compare.**
   `CHECKLEDGE` does `lda ztemp / cmp #loose / bne … / bit tempstate / bne :no`
   (`CTRLSUBS.S:1836-1842`). `bit` ANDs A with the operand, and A is the piece
   id `loose` = 11 = `%1011`. A loose floor is therefore refused as a ledge
   when bits 0, 1 or 3 of its state are set, not whenever its state is
   non-zero. Such states do occur (`MOVER.S:389-415`, `1176-1236`,
   `2152-2168`): a floor that has been stepped on counts 1, 2, … 9 and
   detaches at 10, so in the one frame its state is 4 it can still be
   grabbed; a floor that is only wiggling holds `$80`, `$81`, `$82`, `$83`,
   so it is refused as a ledge in three of its four frames although it is
   not going to fall. The port keeps the test as written.
2. **`FloorY` is indexed with a wrapped 8-bit `CharBlockY + 1`**
   (`ldx CharBlockY / inx / lda FloorY,x`, five places). Row −1 (`$FF`) gives
   index 0. Rows 4–8 read the next table, `BlockAy`; the port reproduces that
   and flags anything further as unmodelled.
3. **`clrall` returns 1 and callers store it.** `jsr clrall / sta clrD` is the
   idiom for "discard other pushes and mark this one used"
   (`CTRL.S:625-632`). It does not touch `clrbtn`.
4. **The kid is faster than everyone else.** `DoStrike` and `DoAdvance` start
   `faststrike` / `fastadvance` for `CharID` 0 and `strike` / `advance` for
   others (`CTRL.S:958-1024`).
5. **Blocking is timed against the opponent's frame, differently per side.**
   The kid blocks if the opponent is in frame 151, 152 or 162; in 153 he is
   one frame late and the code calls `animchar` an extra time to catch up;
   in 168 he is early and waits. An enemy blocks only at frame 152, and
   retreats instead of blocking at nothing (`CTRL.S:896-956`).
6. **`DoStartrun` calls `getfwddist` twice** (`CTRL.S:1537-1561`): once for
   the kind of obstacle (X, Y), again for the distance.
7. **`TryPickup` loads `CharX` and immediately overwrites A** with −14
   (`CTRL.S:2103-2104`). A leftover; no effect.
8. **Register interfaces that are easy to miss.** `DoStrike` takes
   `CharPosn` in X; `PickItUp` takes the piece id in A and the block index in
   Y; `CHECKLEDGE`, `GETSPIKES`, `REMOVEOBJ` and `DOIMPALE` use Y and the
   `BlueSpec` pointer exactly as the last `rdblock` left them. The port
   carries these as `g->ry` and `g->blue`, and the comparison in section 6
   includes them. (This narrows unknown E15.)
9. **`GETEDGES` halves a 9-bit x.** `FCharX` is in 280-wide units and may be
   slightly negative; the routine rotates the high byte's bit 0 into the low
   byte (`CTRLSUBS.S:1084-1090`).
10. **Level 12 writes floor into the level.** With `mergetimer` negative, on
    the top row of screen 2, or of screen 13 from column 6 on, `onground`
    turns the empty piece under the kid into floor and marks it and its
    right-hand neighbour for redraw (`CTRL.S:326-351`).
11. **Scratch variables overlap between modules.** `tempobjid` is a `COLL.S`
    local at `$F2`, and `SUBS.S` has a different local (`tempstate`) at the
    same address. The port has one field per name. Among the routines ported
    so far nothing else writes that byte (the comparison in section 6
    includes `tempobjid` after every routine and agrees), but this is the
    kind of coupling to look for when more of `SUBS.S` is ported.

## 5. Intentional deviations

- **Hooks** (`pop_game_hooks`) stand where the original calls drawing, sound
  or hardware. Subsystems that were hooks when this was written
  (`addslicers`, `jamspikes`, `potioneffect`, `DemoCtrl`, the input step of
  `PLAYERCTRL`) have since been ported and are called directly.
- **Image sizes come from a generated table.** `GETEDGES` calls `DIMCHAR`,
  which reads the image tables in main memory. The port reads
  `imgdims.bin` (width and height only, generated from the user's checkout).
  Character table 3 is the opponent set, which the game swaps per level. The
  five opponent files have 32 images each but different sizes for most of
  them (**confirmed** by parsing: the fat guard differs from the guard in 8
  images, the vizier in 19, the shadow and skeleton in 30), so all five are
  exported and `pop_dimfile_use_opponent` selects one. Nothing calls it yet;
  the guard set is the default. Which set goes with which level is unknown
  G9.
- **Stray reads are flagged, not reproduced.** An image number the table does
  not have, a sequence pointer outside the table, a `FloorY` index past
  `BlockAy`, a redraw-buffer index past 29: the original would use whatever
  is in memory; the port sets `g->unmodelled` and returns 0 or does nothing.
- **Copy protection is treated as passing.** `decodeim` ORs `timebomb` into
  the image number (`CTRLSUBS.S:1036`, "must be 0!"). The field exists; nothing
  sets it.

## 6. Tests

| Test | What it shows |
| --- | --- |
| `control_unit` (`tests/test_control.c`, 143 checks, synthetic data) | Hand-derived expectations for the helpers and one or two paths through each handler; builds without the original |
| `control_original` (`tests/test_control_original.c`) | The 59 sequence numbers and 2 sound numbers in `pop_control.h` equal the original's (compile-time); on `LEVEL1`, holding forward gives frames 1–6 then the run cycle, and releasing returns to frame 15, facing either way |
| `control_c_vs_original` (`tests/fuzz_control.py`) | The 52 routines of this subsystem (and since milestone 9a the collision ones), each on 400 random states, against the original routine in the 6502 simulator: all tracked variables, A/X/Y/carry where defined, queued sounds, level bytes changed, redraw buffers, hook calls in order |
| `kidstep_c_vs_original` (`tests/trace_kidstep.py`) | The ported part of the per-frame kid update, in the original's order, for 60 consecutive frames of random input per trace, state carried from frame to frame, compared after every call |

Recorded runs are in `docs/verification.md`.

How the comparison was itself checked:

- **Coverage.** Under the fuzz run, gcov reports about 97 % of the lines and
  95 % of branch directions of `pop_control.c` taken (the exact figure moves
  a little with the seed). The lines not reached are the "unmodelled" exits,
  the three `animchar` hooks that no sequence started here triggers in its
  first frame, and in some runs the stairs. The multi-frame trace alone
  reaches about two thirds of the lines: it is a test of the routines working
  together, not of every branch.
- **Deliberate bugs** (`tests/mutation_check.py`). 35 one-token changes (a
  threshold off by one, a wrong comparison, a deleted statement) are each
  built and run against `fuzz_control.py`; the result is recorded in
  `docs/verification.md`. The first version of the generator missed six
  boundary cases (for example the grab distance of exactly 25 lines, or an
  opponent exactly 90 units away); those values were added to the generator.
  Four of the changes cannot be caught because they do not change behaviour:
  an unused table entry (`BarL[2]`), a carry that cannot happen (`FCharX` is
  even before its increment), a block row the clamp above excludes, and a
  value `DoRunjump`'s arithmetic cannot produce. Finding those four is itself
  information about the original: each is a dead case in the 6502 code too.
  (`BarL`'s own comment, `COLL.S:51-53`, calls entry 2 "flask"; `CMPBARR` no
  longer returns that code.) A run of 150 rounds per routine missed two of
  the other 31, different ones for different seeds; 400 rounds caught all of
  them, which is why the CTest run uses 400.

Limits:

- The states are random, not reachable-only; agreement is over a superset of
  what the game does, minus the stray-read cases, which are skipped (well
  under 1 %).
- `DIMCHAR` and the hooks are stand-ins on the original's side too, so what is
  compared is the control logic given the same answers from them.
- When this was written the `kidstep` frame had no collision; it has since
  been added (`docs/port/collision.md`). Pressure plates, loose floors, spike
  triggering, moving gates and slicers, guards and screen changes are still
  absent from the frame.
- Nothing here has been compared with the running game.

## 7. Not ported yet

- The input devices (`getselect`, `clrjstk`, `LoadSelect`, `SaveSelect`) and
  `DemoCtrl`; `SHADCTRL` and the opponent AI (`AUTO.S`).
- `ctrlplayer`'s death handling (`TOPCTRL.S:1095` on), `kill0`, `unholy`.
- `CHECKPRESS`, `checkspikes`, `checkslice`, `shakeloose` and all of
  `MOVER.S`.
- (Collision, `COLL.S` up to `GETFWDDIST`, was ported next:
  `docs/port/collision.md`.)
