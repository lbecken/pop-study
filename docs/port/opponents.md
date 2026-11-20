# Port notes: opponents, sword hits, strength, screen changes

Sixth C17 subsystem (milestone 9c): what guards and the shadow do, how sword
thrusts land, how strength is lost and regained, and how the kid and a guard
pass from one screen to the next.

C sources: `modern-port/src/core/pop_auto.h`, `pop_auto.c`; state in
`pop_game.h`. Citations are `AUTO.S:lines` in `01 POP Source/Source` of the
original repository at commit `25243550` unless another file is named.
Everything in sections 1–7 is **confirmed** by reading the source and by
running the original code (section 9) unless marked otherwise.

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `AUTOCTRL` 160-193 | `pop_autoctrl` | Chooses the program for the character and lets it "press" the joystick |
| `GuardProg` 455-459, `Alert` 467-505, `EnGarde` 513-629, `FollowKid` 638-686, `InRange` 694-713, `GenFight` 723-744, `MaybeAdvance` 753-763, `MaybeBlock` 770-790, `MaybeStrike` 797-819 | (static) | A guard's decisions (section 3) |
| `ShadowProg` 226-243 and `ShadLevel4/5/6`, `FinalShad` 250-428 | (static) | The shadow's four appearances (section 5) |
| `MouseProg` 198-214 | (static) | The mouse runs in, stops, leaves |
| `DoRelease` … `DoRelBtn` 822-888, `rndp` 897-899 | (static) | The "key presses", and a random number |
| `AUTOPLAYBACK` 1149-1218 | `pop_autoplayback` | Plays a (frame, command) script |
| `SHADCTRL` `CTRL.S:509-547` | `pop_shadctrl` | Opponent's control step: death, `AUTOCTRL`, `GenCtrl` |
| `DoShad` `TOPCTRL.S:701-740` | `pop_doshad` | Opponent's whole frame |
| `CHECKSTRIKE` 914-930, `TestStrike` 933-1004 | `pop_checkstrike` | Does a thrust land or get blocked |
| `CHECKSTAB` 1009-1040, `STABCHAR` `MISC.S:380-443` | `pop_checkstab`, `pop_stabchar` | What a landed thrust does |
| `chgmeters` `TOPCTRL.S:1313-1354`, `RECHARGEMETER`, `BOOSTMETER` `CTRLSUBS.S:2020-2041`, `MARKMETERS` `MISC.S:225-246` | `pop_chgmeters` … | Strength |
| `CHECKALERT` `MISC.S:827-978` | `pop_checkalert` | Can kid and opponent reach or see each other (`EnemyAlert`) |
| `FIRSTGUARD` `MISC.S:191-218` | `pop_firstguard` | The kid cannot run past a guard who is en garde |
| `VANISHCHAR` `MISC.S:72-81`, `UNHOLY` 450-472, `MOUSERESCUE` 352-373, `BONESRISE` 595-679, `POTIONEFFECT` 257-345, `REFLECTION` 486-582 (all `MISC.S`) | `pop_vanishchar` … `pop_reflection` | Special appearances and effects |
| `DEADENEMY` `SUBS.S:1699-1738`, `deathsong` `TOPCTRL.S:1200-1212`, `kill0` `TOPCTRL.S:1220-1234` | `pop_deadenemy`, `pop_deathsong`, `pop_kill0` | Deaths |
| `CUTCHECK` 1230-1312, `cutchar` 1470-1579, `CUT` 1670-1738, `transferguard` 1320-1332, `updateguard` 1343-1389, `CUTGUARD` 1397-1457 | `pop_cutcheck`, `pop_cutchar`, `pop_cut`, `pop_cutguard` | Passing off a screen edge |
| `ADDGUARD` 1750-1936, `getgdstrength` 1941-1949, `INITIALGUARDS` `SUBS.S:1674-1690` | `pop_addguard`, `pop_initialguards` | Who is on the screen the kid enters |

## 2. An opponent is a second player

There is no separate movement code for guards. Each frame `DoShad` runs the
same steps as the kid's frame, and for control it calls `SHADCTRL`:

```
if his strength has run out: CharLife = 0; DEADENEMY (music; level 13: the exit opens)
if he is the shadow and dead: vanish
AUTOCTRL       ; sets JSTKX, JSTKY, btn and the fresh-press flags
GenCtrl        ; the very routine that reads those variables for the kid
```

So a guard advances by "pushing forward", strikes by "pressing the button",
and is bound by the same frame-driven rules as the player: he can only act on
frames where `GenCtrl` or `FightCtrl` listens (`docs/port/player-control.md`
§2). The primitives (lines 822-888): forward, back, up (= block), down
(= turn, or lower the sword), button (= strike), down+forward (= draw),
down+back (= drop guard).

Three counters are decremented at the top of `AUTOCTRL`: `justblocked`
(blocking impaired after his own strike was blocked), `gdtimer` (set to 15
when the kid strikes; a guard does not advance meanwhile), `refract`
(dazed after being hit).

Off the visible screen an opponent is controlled but not animated or moved,
and beyond 14 units outside its edges he is animated but not moved
(`TOPCTRL.S:713-723`).

## 3. A guard's decisions

**Not en garde (`Alert`).** He does nothing if the kid is dead. Otherwise,
with d = `getopdist` (positive = kid in front):

- kid on his row and within 8 units behind him → draw;
- a sound has set `alertguard` → if the kid is in front, draw; if behind
  (and not overlapping), turn round;
- no sound → draw only if the kid is in front;
- drawing needs `EnemyAlert` ≠ 0 (he can see him), and on level 13 that no
  music is still queued.

**En garde (`EnGarde`)**, acted on only in fighting frames (150 up, not 166):

| Situation | Action |
| --- | --- |
| `EnemyAlert` = 1 (kid visible across a gap or barrier) | wait |
| `EnemyAlert` = 0 and the kid dropped off a ledge mid-fight (`droppedout`) | `FollowKid`: walk to the edge; step off only for a one-storey drop onto plain floor while the kid is on that row; otherwise step back and wait above |
| `EnemyAlert` = 0 otherwise | lower the sword (a skeleton never does) |
| kid knocked down (frames 102–117, bumped) and not within 12 | wait for him to get up |
| d ≥ 35 (or kid behind) | out of range: if the kid is running at him (frames 7–14) strike when d < 40; if jumping (34–43) strike when d < 50; otherwise advance, but only if the next two blocks have floor, else step back |
| d < 12 | too close: step back (reachable only when both face the same way; see §8.7) |
| kid unarmed or sword down | strike if d < 29, else advance |
| kid en garde | `GenFight` |

**`GenFight`**, with a random number r (0–255) per decision and the tables of
section 4:

```
if 10 <= d < 29:  if the kid is in a thrust frame (152, 153, 162):
                      block if r < blockprob (impblockprob while justblocked)
if dazed: stop
if 12 <= d < 29:  unless the kid is starting a thrust (151) or frame 169:
                      strike if r < strikeprob
                      (restrikeprob if he has just blocked: frames 150, 161)
otherwise:        advance if r < advprob  (not while gdtimer runs, except type 0)
```

## 4. The fight tables (not in this repository)

`AUTO.S:109-136` holds eight 12-entry tables indexed by `guardprog`, the
opponent's "program" number (strike, restrike, block, impaired-block and
advance probabilities out of 255; the daze time after a hit; a colour bit;
an extra strength unit), and two 14-entry tables indexed by level (basic
strength, basic colour). They are what makes one guard harder than another.

They are content, not rules, so the port does not contain them: the build
cuts them out of the assembled `AUTO.S` into `autodata.bin` (180 bytes),
together with the shadow's three scripted positions and its level-5 script.
The unit tests use invented tables. The ten tables are kept as one block, as
they lie in memory, because `guardprog` is not range-checked everywhere and
an index past one table reads the next.

A guard's program comes from the level file (`GdStartProg`), limited to
0–11 with 3 as the default; the skeleton is 2, the shadow 3.

## 5. The shadow

| Level | Behaviour |
| --- | --- |
| 4 | Created when the kid jumps through the mirror (`REFLECTION`: it takes his maximum strength as its own and leaves him 1 unit). Runs off screen 4 to the left and vanishes |
| 5 | Waits by the potion on screen 24; when the gate at block (1, 0) has opened past 20 it plays a 16-byte script (forward, button, back, forward) and vanishes off the left edge |
| 6 | Stands across the chasm on screen 1; when the kid is in frame 43 of his jump and still left of x = 128, it presses the button and steps forward (onto the plate that shuts the gate) |
| 12 | Drops in on screen 15 once the sword is gone. Swords down and face to face it mirrors the kid's steps towards it; when they overlap they merge: maximum strength +1 and refilled, `mergetimer` = 42, the "rejoin" tune. Swords out, it fights like a guard of type 3, and `UNHOLY` / `chgmeters` make every wound to one a wound to both |

## 6. Sword contact and strength

`CHECKSTRIKE` runs once per frame after both characters have moved, first
with the opponent as the striker, then with the kid.

- Only frames 153 and 154 of a thrust are tested, and only on the same row.
- **Blocked** if the target is within 29 and in frame 150 or 161: the target
  goes to 161 ("successful block"), the striker to seq `blockedstrike`; a
  striking guard gets `justblocked` = 4.
- **Lands** on frame 154 only, if 12 ≤ d < 29 (8 ≤ d < 29 when the target's
  sword is not out): the target's `CharAction` becomes 99.

`CHECKSTAB` then turns action 99 into a wound (`STABCHAR`): a character
without his sword out dies at once; otherwise −1 unit, seq `stabbed`, or
`stabkill` if that was the last unit; killed with his back to an edge he is
pushed over it (seq `fightfall`). A skeleton is never wounded. If both were
hit in the same frame the kid's hit is cancelled: the player wins a tie.
A hit opponent is dazed for `refractimer` frames.

Strength is never changed directly. `DECSTR` and the potions set `ChgKidStr`
/ `ChgOppStr`; `chgmeters` adds them once per frame (only if the result does
not exceed the maximum); the main loop zeroes them before the next frame
(`TOPCTRL.S:369-371`).

## 7. Screens

**`cutchar`**: a character leaves the screen

- upward when y < 10 or y ≥ 240 (not while falling or bumped);
- downward when y ≥ 215 (except the fall off screen 1 of level 6, which ends
  the level);
- sideways, judged by the edge of his image on the side he faces: facing
  left, to the left when that edge is ≤ 54 (and to the right if it is ≥ 198,
  i.e. he has backed off the screen); facing right, to the right when it is
  ≥ 201, unless a wall panel in the last column hides the next screen (and
  to the left if it is ≤ 57);
- never sideways while climbing, standing up, striking, blocking or turning.

`CUT` then changes `CharScrn` through the level map and shifts him: ±140 in
x, or ±3 rows and ±189 lines.

**The guard** (`CUTCHECK`): if he is alive, en garde, and has followed to
within 25 units of the edge the kid left by, and no fresh guard waits on the
new screen, he is taken along. Otherwise his row, x, facing, program and
(if dead) the sequence position of his corpse are written back into the
level's guard-start entry for that screen, and he is removed. A guard who
falls off the bottom is gone for good, except the skeleton of level 3, which
lands on screen 3.

**`ADDGUARD`** (on entering a screen): the shadow's set pieces on levels 5,
6 and 12; otherwise the screen's guard-start entry: a skeleton on level 3,
else a guard, standing alert, or lying dead where he was left.

**The level file does not say where a guard stands**, only in which block:
its x is 0 or 255 and its sequence fields `$FF`. `INITIALGUARDS` computes x
(middle of the block) and marks each guard "fresh" once per level
(`SUBS.S:1674-1690`, called from `TOPCTRL.S:276`).

## 8. Semantic pitfalls and findings

1. **A guard's extra strength unit comes from the wrong guard.**
   `ADDGUARD` calls `getgdstrength`, which adds `extrastrength[guardprog]`,
   at line 1905, and only stores this guard's program in `guardprog` at lines
   1923-1928. So the extra unit depends on the type of whichever opponent was
   active before. The table gives the unit to one type only. Looks like an
   oversight (**inferred**); the port does the same, and the unit test
   `test_guards_on_screens` shows it.
2. **A script's last entry is never run.** `AUTOPLAYBACK` stops counting at
   254 (lines 1155-1158), so an entry for frame 255 — which is how the one
   script in `AUTO.S` ends — is never reached; the character simply repeats
   the last command.
3. **Scripts run one frame "late".** An entry is taken when the frame count
   has reached its number, one per call, so the entry for frame 0 runs on the
   first call (count 1).
4. **Unreachable code in `STABCHAR`.** The lines that make a killed skeleton
   invincible (`MISC.S:394-402`) come after a branch that has already sent
   every skeleton elsewhere (386-388).
5. **Killing the vizier presses a plate.** On level 13 `DEADENEMY` opens the
   exit by calling `PUSHPP` on block 0 of screen 24 (`SUBS.S:1734-1738`).
6. **`CHECKALERT` scans from left to right only**, skipping a slicer at the
   left end and a gate at the right end of the stretch (`MISC.S:898-918`), so
   what counts as "clear" is not symmetric.
7. **Facing each other, the distance measure is never below 13**, because
   `GETOPDIST` adds 13 when the two face opposite ways
   (`docs/port/player-control.md`). Three thresholds in the fight code can
   therefore never decide anything, and the mutation check found each of
   them as a change without effect: `GenFight`'s lower limits of 10 and 12
   (it is only reached at 12 or more, and its second range test follows a
   first one with the same upper limit), and `EnGarde`'s off-guard distance
   of 8 (it is only entered with the sword out). For the same reason the
   "too close" case (lines 624-629), which sends a guard *forward* when the
   two face each other and back when they face the same way, can only be
   reached facing the same way.
8. **The random number generator is stepped every frame** by the main loop
   (`TOPCTRL.S:367`), besides the draws made here and by torches, flasks and
   the sword. (This narrows unknown E6.)
9. **`]cutdir` is the third tenant of `$F2`**, with `tempobjid` and
   `ADDSLICERS`' counter; the port writes it through `tempobjid`.
10. **`FIRSTGUARD`'s test is "within 15 behind"** (`cmp #-15 / bcc`,
    `MISC.S:207-209`): the kid is bumped back only once he has got just past
    the guard.

## 8a. Intentional deviations

- **Tables generated, not embedded** (section 4).
- **Hooks**: the upside-down potion's redraw (`inverty`), drawing the
  reflection (`addreflobj`), the end of a demo script (`attractmode`), and an
  opponent played by hand (`LoadDesel`, `getdesel`, `SaveDesel`).
- **`AUTOPLAYBACK` takes a pointer and a length**; reading before the start
  or past the end is flagged as unmodelled.
- **Guard-start entries** are read from the level array at their original
  offsets; a screen number that would index outside the level is flagged.

## 9. Tests

| Test | What it shows |
| --- | --- |
| `auto_unit` (`tests/test_auto.c`, 128 checks, synthetic data and tables) | A guard noticing, turning, drawing; each fight decision with a known random number; hit, block, tie; meters; every potion; `EnemyAlert` for floor, gap, wall, gate; a guard created, left behind and remembered; cuts; a script; the shadow from the mirror |
| `control_c_vs_original` (`tests/fuzz_control.py`) | 29 more entry points (116 in all), on states built for them: two characters at the distances the code distinguishes, guard-start entries, the level places the code knows by number |
| `kidstep_c_vs_original` (`tests/trace_kidstep.py`) | The opponent's whole frame, sword contact, strength and screen changes in the frame trace: traces now start with `INITIALGUARDS` and `ADDGUARD`, half of them beside a guard |
| `tests/mutation_check.py` | 79 deliberate errors in `pop_auto.c`: 76 noticed, 3 without effect (the dead thresholds of §8.7) |

Limits:

- The trace's kid is driven by random input, so fights happen by accident
  rather than design; they do happen (in the recorded run the opponent was
  en garde in more than half of the frames in which one was present).
- `firstguard`, `unholy`, `kill0` and the kid's death handling are compared
  routine by routine but are not yet in the frame trace, because the
  original's `DoKid` wrapper around them is not ported.
- The demo level's kid script and guard are not ported (`DemoCtrl`).
- Nothing here has been compared with the running game.
