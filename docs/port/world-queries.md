# Port notes: world queries, frame lookup, gravity

Second C17 subsystem (milestone 8, first part): the routines everything in
player control and physics is built on.

C sources: `modern-port/src/core/pop_game.h`, `pop_world.h`, `pop_world.c`.
Citations are `FILE.S:lines` in `01 POP Source/Source` of the original
repository at commit `25243550`.

## 1. Routines

All in `CTRLSUBS.S` unless noted. "Reads"/"writes" list original variables.

| Original | C | In | Out |
| --- | --- | --- | --- |
| `GETLEFT/RIGHT/UP/DOWN` 244-275 | `pop_getleft` … | A = screen | A = neighbour; 0 for screen 0 |
| `CALCBLUE` `GRAFIX.S:1757-1778` | `pop_calcblue` | A = screen 1–32 | `BlueType`, `BlueSpec` |
| `RDBLOCK` 137-140 | `pop_rdblock` | A = screen, X = column, Y = row (any byte values) | A = X = piece id, Y = block 0–29; `tempscrn`, `tempblockx`, `tempblocky` |
| `RDBLOCK1` 141-159 | `pop_rdblock1` | the three `temp` variables | same |
| `GETUNDERFT` 400-404 | `pop_getunderft` | `CharScrn`, `CharBlockX/Y` | as `RDBLOCK` |
| `GETINFRONT` 405-415 | `pop_getinfront` | + `CharFace` | + `infrontx` |
| `GET2INFRONT` 416-427 | `pop_get2infront` | | |
| `GETBEHIND` 428-438 | `pop_getbehind` | | + `behindx` |
| `GETABOVE` 439-445 | `pop_getabove` | | + `abovey` |
| `GETABOVEINF`, `GETABOVEBEH` 446-468 | `pop_getaboveinf`, `pop_getabovebeh` | | + both |
| `GETBASEX` 329-340 | `pop_getbasex` | `CharX`, `CharFace`, `Fdx`, `Fcheck` | A = foot x |
| `GETDIST` 476-477 | `pop_getdist` | same | A = units to the block edge ahead (0–13); `OFFSET` |
| `GETDIST1` 478-490 | `pop_getdist1` | A = x | same |
| `GETBLOCKEJ` 497-502 | `pop_getblockej` | A = column −5…14 | A = x of its left edge |
| `GETBLOCKX`, `GETBLOCKXP` 523-530 | `pop_getblockx`, `…xp` | A = x | A = column; `OFFSET` = unit within it |
| `GETBLOCKY`, `GETBLOCKYP` 543-560 | `pop_getblocky`, `…yp` | A = y | A = row 0–3, `$FF` above |
| `GETBASEBLOCK` 622-626 | `pop_getbaseblock` | | `CharBlockX` |
| `FACEDX` 633-640 | `pop_facedx` | A = dx | A = dx or −dx |
| `CMPSPACE`, `CMPBARR`, `CMPWALL` 1508-1568 | `pop_cmpspace` … | A = piece id | A = class |
| `GETFRAMEINFO`, `usealtsets` 1648-1715 | `pop_getframeinfo` | `CharPosn`, `CharID` | `Fimage`, `Fsword`, `Fdx`, `Fdy`, `Fcheck`, `framepoint` |
| `REREADBLOCKS` `CTRL.S:1929-1931` | `pop_rereadblocks` | | frame info + `CharBlockX` |
| `LOADKID` … `SAVESHADWOP` 1940-2020 | `pop_loadkid` … | | 16-byte record copies |
| `GRAVITY` `SUBS.S:1618-1642` | `pop_gravity` | `CharAction`, `CharYVel`, `weightless` | `CharYVel` |
| `ADDFALL` `SUBS.S:1651-1662` | `pop_addfall` | `CharY`, `CharYVel`, `CharAction`, `CharXVel` | `CharY`, `CharX`, block info |

## 2. How the pieces fit

- A character knows its screen and its block column and row. `GETUNDERFT`,
  `GETINFRONT` and the rest add ±1 to those and call `RDBLOCK`.
- `RDBLOCK` accepts coordinates outside the screen. `handler` moves them back
  on screen one step at a time, switching to the neighbouring screen through
  the level map each time. Screen 0 ("no screen") reads as a solid block.
- The block column is recomputed from the **foot** position (`GETBASEX` →
  `GETBLOCKXP`), not from `CharX`. `GETBLOCKXP` subtracts 7 first: characters
  stand on the "centre plane", drawn 7 units right of the block grid
  (`angle = 7`, `GAMEEQ.S:658`).
- Columns are 14 units wide and column 0 starts at x = 58, so
  column = ⌊(x + 12) / 14⌋ − 5 and the unit within it is (x + 12) mod 14. The
  original reads these from 256-entry tables (`TABLES.S`); the port computes
  them, and the test suite checks the rule against the assembled tables.

## 3. Semantic pitfalls

1. **`FACEDX` does the opposite of its comment.** The comment says "DX if char
   is facing right, −DX if facing left"; the code (`bit CharFace / bmi ]rts`)
   returns dx unchanged when facing **left** and negates it when facing right.
   Confirmed by running it.
2. **`GETLEFT` and its siblings test a flag they did not set.** They open with
   `beq ]rts` and so depend on the caller having just loaded the screen number.
   All 17 call sites do. The port takes the screen number as an argument and
   tests it.
3. **Coordinates are signed for the "off the left/top" test and unsigned for
   "off the right/bottom".** `handler` tests bit 7 first, then `>= 10` / `>= 3`.
   Column `$FF` is −1; column 200 would also be treated as negative.
4. **The Y register is an output.** Callers do `jsr getunderft` and then
   `lda (BlueSpec),y`. The port records it as `g->ry` (and X as `g->rx`). On a
   null screen `RDBLOCK` does not set Y at all; it keeps the row the caller
   passed in, and the port does the same.
5. **Only an exact 0 faces right in `GETDIST1`** (`lda CharFace / beq`), while
   `ADDCHARX`, `FACEDX` and `CMPWALL` test bit 7. The two disagree for values
   other than `$00` and `$FF`.
6. **`GETINFRONT` indexes a two-byte table with `CharFace + 1`.** `plus1` and
   `minus1` are adjacent, so a face of 1 or 2 reads on into the other table;
   anything else reads code.
7. **`GETBLOCKY` returns `$FF`, not 0, above the top row**, and 3 below the
   bottom row.
8. **A solid block counts as "space" in `CMPSPACE`** (it has no floor to stand
   on), but is a barrier in `CMPBARR` and a wall in `CMPWALL`.
9. **Panels are walls only when facing left** (`CMPWALL`).
10. **`GRAVITY` clamps with an unsigned compare.** An upward velocity of −2
    (`$FE`) becomes +1 after one step, but −16 (`$F0`) becomes `$F3`, which is
    "greater than 33" as a byte, so it is replaced by the terminal velocity 33
    immediately.
11. **`ADDFALL` moves y on every call**, not only while falling; the x movement
    and the block re-read happen only when `CharAction` is 4.
12. **`GETFRAMEINFO` depends on who the character is.** Kid and mouse use the
    main frame set; characters 1–4 use the alternate set for frames 150–189,
    and 2–4 also map their falling frames 102–106 onto 172–176; characters 5
    and above use the second alternate set.

## 4. Intentional deviations

The original never checks its inputs; out-of-range values read whatever is in
memory. The port reproduces that wherever the memory involved is part of data
it holds, and otherwise sets `g->unmodelled` and returns a harmless value.

| Situation | Original | Port |
| --- | --- | --- |
| Screen number above 24 in `GETLEFT` etc. | reads past `MAP` into `INFO` | same (the blueprint is one array) |
| `GETBLOCKEJ` index 20–39 | reads the tables that follow `BlockEdge` | same |
| Screen 25–32 in `CALCBLUE` | pointer runs on past the screen table | same |
| Screen 0 or above 32 in `CALCBLUE` | menu data, or past `Mult30` | `unmodelled` |
| `CharFace` other than `$FF`, `$00`, 1, 2 in `GETINFRONT` etc. | reads code as data | `unmodelled` |
| Frame number outside the frame tables | reads the sequence table | `unmodelled`, zeros |

No valid game state reaches the `unmodelled` cases; the differential test
requires the flag to stay clear.

## 5. Tests

| Test | Data | What it checks |
| --- | --- | --- |
| `world_unit` (`tests/test_world.c`) | synthetic level and frame table | each routine on hand-worked cases, including off-screen and null-screen reads, both facings, table thresholds, the gravity wraparound and the `unmodelled` cases |
| `world_c_vs_original` (`tests/fuzz_world.py`) | the 15 original levels | 39 routines × 400 random states: the value returned, X and Y where they are outputs, the selected blueprint screen, and 73 state variables, C against the original code in the simulator |

The differential test found one fault while it was being written, in the test
harness rather than the port (pitfall 2: the simulator was calling `GETLEFT`
with stale flags). With that modelled, 58,500 cases with a second seed also
agree.

Random states are deliberately wider than valid ones: columns −6…15, rows
−2…4, any x and y, all 15 levels, seven character types.
