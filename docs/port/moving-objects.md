# Port notes: moving level objects

Fifth C17 subsystem (milestone 9b): everything in a level that changes by
itself or when touched: gates, the exit, pressure plates and what they are
linked to, spikes, slicers, loose and falling floors, torches, flasks, the
sword.

C sources: `modern-port/src/core/pop_mover.h`, `pop_mover.c`; state in
`pop_game.h`. Citations are `MOVER.S:lines` in `01 POP Source/Source` of the
original repository at commit `25243550` unless another file is named.
Everything in sections 1–5 is **confirmed** by reading the source and by
running the original code (section 7) unless marked otherwise.

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `searchtrob` 105-119, `addtrob` 129-156 | `pop_addtrob` | Put an object on the transition list, or change the direction of one already there |
| `ANIMTRANS` 629-684, `animobj` 691-770 | `pop_animtrans` | One frame for every listed object; remove those that have stopped |
| `animexit` 777-830, `animgate` 837-920, `animplate` 927-947, `animslicer` 955-1009, `animflask` 1016-1035, `animsword` 1042-1055, `animtorch` 1063-1075, `animspikes` 1124-1169, `animfloor` 1176-1236, `animspace` 1238-1239 | (static) | The per-piece state machines (section 3) |
| `TRIGSLICER` 217-238, `CLOSEEXIT` 246-256, `SMASHMIRROR` 259-262, `TRIGFLASK` 269-282, `TRIGSWORD` 289-297, `TRIGTORCH` 304-316, `TRIGSPIKES` 323-345, `JAMSPIKES` 354-358, `BREAKLOOSE(1)` 389-415 | `pop_trigslicer` … `pop_breakloose1` | Start one object |
| `PUSHPP` 425-471, `JAMPP` 480-497, `trigger` 506-537, `trigobj` 546-555, `openexit` 562-571, `triggate` 581-619 | `pop_pushpp`, `pop_jampp` | A pressure plate goes down and triggers what it is linked to |
| `gettimer` … `getscrn` 1549-1581 | (static) | Fields of a link entry (section 4) |
| `redtrobj` … `redplate` 1253-1329, `check` 1360-1367, `checkright` 1411-1450, `checkabover` 1459-1539 | (static) | Redraw marks for a changed piece, if it can be seen |
| `ANIMMOBS` 1588-1635, `animmob` 1642-1650, `mobfloor` 1657-1751, `knockloose` 1756-1783, `makespace` 1789-1802, `passthru` 1807-1828, `makerubble` 1834-1870, `markmob` 1875-1894 | `pop_animmobs` | Falling floors |
| `checkcrush` 1903-1934, `crushchar` 1942-1977 | (static) | A falling floor lands on the kid |
| `SHAKEM` 2120-2147, `shakeit` 2152-2168 | `pop_shakem` | Loose floors of a row start to wiggle |
| `GETFLAMEFRAME` 1084-1102, `GETFLASKFRAME` 1111-1117 | `pop_getflameframe`, `pop_getflaskframe` | Next animation frame |
| `CHECKPRESS` `CTRL.S:1939-2005` | `pop_checkpress` | What the character is standing on or hanging from |
| `CHECKSPIKES` `CTRLSUBS.S:1868-1909` | `pop_checkspikes` | Spikes below any column he covers |
| `shakeloose` `TOPCTRL.S:1271-1291` | `pop_shakeloose` | A jump or landing jars the floors |
| `ADDSLICERS` `SUBS.S:250-299` | `pop_addslicers` | Start the slicers of his row |
| `ADDTORCHES` `SUBS.S:146-184` | `pop_addtorches` | Animate torches, flasks and swords of the visible screen |
| `CRUMBLE` `SUBS.S:108-140` | `pop_crumble` | Level 13: the floor above collapses |
| `MIRAPPEAR` `SUBS.S:1743-1758` | `pop_mirappear` | Level 4: the mirror appears when the exit opens |
| `ADDLOWERSOUND` `SUBS.S:1373-1406` | `pop_addlowersound` | Gate-closing sound, if the gate can be seen |
| `RND` `GRAFIX.S:1386-1395` | `pop_rnd` | seed ← 5·seed + 23 (mod 256) |
| `UNINDEX` `CTRLSUBS.S:648-656` | `pop_unindex` | Block index → column, row |

Not ported: `ADDMOBS`, `ATM`, `addmobobj` (1984-2112), which put falling
floors on the list of things to draw.

## 2. The two lists

**TROBs** ("transitional objects", `GAMEEQ.S:48-54`): up to 31 entries of
(block index, screen, direction). An object is on the list while it is
changing; its *state* is not in the list but in the level data
(`BlueSpec`). Each frame `ANIMTRANS` reads the state, lets the piece's routine
change it, and writes it back. A routine ends an object by setting its
direction negative. Direction means different things per piece: for a gate 0
= closing, 1 = opening, 2 = opening and jam, 3–8 = dropping (and the value is
the index into a speed table).

**MOBs** (`GAMEEQ.S:49-61`): up to 15 falling floors: x (column × 4), y (line
of its bottom), screen, velocity, type (only 0 exists), row.

In both lists entry 0 is the working copy that the routines operate on.

Order per frame (`TOPCTRL.S:503-506`): `animmobs`, then `animtrans`, then the
characters.

## 3. State machines

The state is one byte per piece. Frame counts below were checked by the unit
tests in `tests/test_mover.c` and against the original.

| Piece | State | Sequence |
| --- | --- | --- |
| Gate | 0 shut … 188 open; 189–238 open, waiting; `$FF` jammed open | Opening: +4 a frame, 47 frames. At 188 the state jumps to 238 and counts down 1 a frame; the closing sound starts once it passes below 188. So a gate stays fully open 50 frames and takes 188 more to close: 285 frames from trigger to shut. Dropped by a "down" plate: 40, 60, 80, 100 a frame, shut in 4 frames |
| Exit | 0 … 172 | +4 a frame, 43 frames; then the stairs tune is cued, `exitopen` = 1, and on level 4 the mirror appears. It never closes, except `CLOSEEXIT` at the start of a level: set to 172 and dropped like a gate |
| Pressure plate | the index of its link entry | Timer in the link entry: set to 5 when stepped on, −1 a frame, plate pops up when it reaches 1 (4 frames). Stepping on it again while down restarts the timer and re-triggers the links. Timer 31 = permanently down |
| Spikes | 0 hidden; 1–5 extending; `$80`+n out, n counting down from 15; 6–9 retracting; `$FF` jammed (a body on them) | 25 frames for the whole cycle. Passing over extended spikes puts the timer back to 15 |
| Slicer | low 7 bits: 1–15 cycle, 2 = shut, 6 and above = open; bit 7: has cut someone | 15-frame cycle. Runs only while on the visible screen and the kid's row and (the kid is alive or the blades are bloodied); otherwise it is dropped from the list the next time it is open |
| Loose floor | 0 idle; 1–9 counting; `$80`–`$83` wiggling | Stepped on: counts to 10 (9 frames), then the piece becomes empty space and a falling floor takes over. Jarred: wiggles 4 frames and settles |
| Torch | flame frame 0–17 | A random other frame each time, or the next one |
| Flask | bits 5–7 potion, bits 0–4 bubble frame 1–8 | Next frame; starts at a random one |
| Sword | countdown | Gleams when it reaches 0, then waits a random 40–103 frames |

Torches, flasks and the sword animate only on the visible screen: they are
added by `ADDTORCHES` on entering a screen and drop themselves from the list
when `trscrn` is no longer `VisScrn`.

## 4. Links: what a plate triggers

A plate's state is an index into two 256-byte tables in the level
(`LINKLOC`, `LINKMAP`). Each entry (lines 1549-1581):

| Bits | `LINKLOC` | `LINKMAP` |
| --- | --- | --- |
| 0–4 | block index 0–29 of the target | the plate's timer (only in a chain's first entry) |
| 5–6 | low 2 bits of the target's screen | |
| 5–7 | | high 3 bits of the target's screen |
| 7 | 1 = last entry of this chain | |

`trigger` walks entries from the plate's index until one has bit 7 set; a
first byte of `$FF` means "linked to nothing". For each target that is a gate
or an exit it decides a direction and lists it:

| Plate | Gate |
| --- | --- |
| "up" plate (`upressplate`) | open it; if it is already fully open, just restart its wait |
| "down" plate (`pressplate`) | drop it fast (nothing if already shut) |
| rubble (a floor fell on an up plate) | open and jam open for good |

An exit can only be opened, and only from state 0. (This and
`BREAKLOOSE1`'s test of bit 5 of the piece byte, "required floor piece:
blocked below", settle two of the three points of unknown E16.)

## 5. Falling floors

`mobfloor` (1657-1751): velocity +3 a frame up to 29 (it tests "< 29" before
adding, so the top speed is 30), y += velocity. When y reaches the floor
plane of its row (the row's bottom line − 3) it looks at the piece there:

- empty space: on to the next row, and through the bottom of the screen to
  the screen below (y − 192);
- another loose floor: that one becomes space too and a second falling floor
  is created 6 lines lower with half the speed (`knockloose`);
- anything else: crash sound, the loose floors of that row are jarred, and
  the piece it hit becomes rubble if it was plain floor, spikes, a flask, a
  torch or a plate. A plate is pushed first.

On the null screen it just falls until y ≥ 209.

A landed floor is removed in the same frame: its velocity is set to −2,
incremented once by `animmob`, and the clean-up at the end of `ANIMMOBS`
deletes entries with velocity −1. (`crumbletime2 = 10` at line 77 is unused.)

**Crushing** (1903-1977): the kid is hit if the floor is on his screen and
column and its bottom is less than 30 lines above his feet. He escapes if he
is running (frames 5–14; not on level 13) or not on the ground. Otherwise he
loses one unit, is put on the floor line, and crouches (seq `crush`), or
collapses if that was his last unit.

## 6. Semantic pitfalls and findings

1. **A dead body on an "up" plate triggers link chain 0.** `JAMPP` (480-497)
   turns the plate into plain floor *and clears its state* before `pushpp1`
   reads the state as the link index (line 430). So whatever chain starts at
   index 0 is triggered, as rubble (gates jam open). A falling floor on an up
   plate goes through `makerubble` instead, which changes only the piece type
   first, and triggers the plate's own chain. A dead body on a "down" plate
   turns it into a `dpressplate` and triggers its own chain. Whether
   chain 0 is deliberately harmless in the shipped levels is **not yet
   verified** (next step: decode entry 0 of each level's link table).
2. **Targets that are neither gate nor exit are listed anyway.** `trigobj`
   leaves `trdirec` alone for them (546-555), so it still holds the value
   from the previous target or from the plate itself, and `trigger` adds the
   block to the list; `animobj` throws it out on the next frame (line 764).
3. **Fully extended spikes are harmless for one frame.** `GETSPIKES` calls
   states below 5 "springing" and states with bit 7 "sprung"; state 5 itself,
   which lasts one frame before the timer starts, falls through to "safe"
   (365-382).
4. **The first entry of the drop-speed table is never used.** `gatevel` is
   `0,0,0,20,40,…`; a dropping gate starts with direction 3 and the index is
   incremented before the first use (900-909), so the first step is 40.
5. **On level 4 the exit's last state is written to the mirror's screen.**
   `animobj` stores the new state through the `BlueSpec` pointer after the
   piece's routine returns (766-768). `animexit`'s last frame calls
   `MIRAPPEAR`, whose `rdblock` moves that pointer to screen 4. Run on the
   real `LEVEL4`: the exit (screen 24, block 12) keeps state 168 instead of
   172, and block 12 of screen 4, a torch, receives 172, which the torch's
   own animation replaces on its next frame. No visible effect is expected
   (**inferred**: 168 is still far above the threshold of 120 that lets the
   kid walk in, `CTRL.S` `stairthres`).
6. **The visibility checks can return from the middle of a comparison.** For
   an object that is not on the visible screen, `check`, `checkright` and
   `checkabover` fall into `cmp scrnAbove / bne ]rts` (1346-1347, and line
   1531 for the screen below-left). On that path the carry is whatever the
   compare left (set if `trscrn` ≥ the screen compared with) and Y is
   unchanged, and the caller then marks a redraw buffer with them. So
   objects on unrelated screens sometimes mark a block of the visible screen
   for redraw. It costs a redraw and nothing else. The port reproduces carry
   and Y exactly.
7. **`ADDSLICERS` and collision share a scratch byte.** Its counter
   (`tempstate`, `SUBS.S`) is at `$F2`, the address of `COLL.S`'s
   `tempobjid`. Nothing reads `tempobjid` across such a call, but the port
   writes through it so that memory agrees.
8. **A bloodied slicer stays bloodied.** `animslicer` preserves bit 7 through
   every later cycle (959-971). With `CHECKCOLL`'s unmasked compare
   (`docs/port/collision.md` §3.3) this means that once a slicer has cut
   someone it is never again a barrier, only a hazard. (This settles the
   factual half of unknown E21; whether it was intended is not knowable from
   the source.)
9. **Level 13 is different in four places:** loose floors are not jarred
   (`SHAKEM`, line 2121), a wiggling state neither settles nor redraws
   (1225-1227) so that `CRUMBLE`'s negative start states act as random delays
   before the fall, and running does not save the kid from a falling floor
   (1943-1945).
10. **`CHECKSPIKES` looks down through empty space,** row after row on the
    character's screen, so spikes spring when he passes over them at any
    height (`CTRLSUBS.S:1894-1909`).
11. **Empty space on the palace set has state 1.** `makespace` returns 1 when
    `BGset1` = 1, which draws a striped back wall there (1789-1802).

## 6a. Intentional deviations

- **Lists and buffers as arrays, laid out as in memory.** The redraw buffers
  are one block (`pop_game.rb`, offsets `POP_RB_*`), because the original
  indexes them with unchecked values and index 30 of one is byte 0 of the
  next; an index beyond the whole block is flagged as unmodelled. The lists
  are flagged when their count exceeds their size.
- **Reads beyond the speed table** (`gatevel`, direction above 8) and of
  `BlockAy+1,x` for row −1 are flagged, not reproduced.
- **Calls into what is not ported:** none in this file. `potioneffect` and
  the input hooks remain in the control code.

## 7. Tests

| Test | What it shows |
| --- | --- |
| `mover_unit` (`tests/test_mover.c`, 129 checks, synthetic data) | Timelines worked out by hand: plate → gate open, wait, close (285 frames); plate → gate drop; a body on a plate; the spike cycle with `GETSPIKES` at each state; loose floor count, fall, landing as rubble; wiggle; crush and escape; slicers out of step and stopping; screen objects; the exit |
| `control_c_vs_original` (`tests/fuzz_control.py`) | The 27 entry points of this subsystem (87 routines in all), each on 400 random states with lists, link chains and piece states made for it, against the original in the 6502 simulator: variables, both lists, level bytes changed, all redraw buffers, sounds |
| `kidstep_c_vs_original` (`tests/trace_kidstep.py`) | `animmobs` and `animtrans` at the start of each frame, `checkpress`, `checkspikes`, `shakeloose` in the kid's frame, `addtorches` on entering a screen: the level now reacts to the kid over 60-frame traces |
| `tests/mutation_check.py` | 45 deliberate errors in `pop_mover.c`: 44 noticed, 1 without effect (the closing sound is only made for odd states and its limit is even) |

Coverage of `pop_mover.c` under the fuzz run: 99.7 % of lines, 97 % of
branch directions.

Limits:

- `RND` is ported exactly, but what seeds it at game start and how often
  other code calls it is not ported, so the port's torches will not flicker
  in step with a real game's until those callers are.
- Falling floors are simulated but not drawn (`ADDMOBS`).
- Guards do not exist yet, so nothing but the kid presses plates or is
  crushed. (`checkcrush` only ever looks at the kid: line 1904.)
- Nothing here has been compared with the running game.
