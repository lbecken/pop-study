# Animation data: frame definitions and sequences

Status: both tables are extracted to JSON and cross-checked (milestone 2).

Citations are `FILE.S:lines` in `01 POP Source/Source` of the original
repository at commit `25243550` unless a longer path is given.

## How the data is extracted

`FRAMEDEF.S` and `SEQTABLE.S` are assembler source containing only data
directives. Rather than pattern-matching the text, `tools/extract_assets`
assembles them:

1. `merlin_data.py` is a small assembler for the Merlin data subset these files
   use (`=`, `org`, `dum`/`dend`, `ds`, `db`, `dw`, `hex`, `do`/`else`/`fin`,
   local `:labels`, left-to-right expressions). Anything else is an error.
2. `pop_animdata.py` decodes the resulting bytes the way the game does. Label
   names from the source are attached afterwards, by address.
3. `extract_animdata.py` writes JSON and runs the cross-checks below.

Decoding the bytes rather than the text means the result does not depend on how
the author happened to lay out a line, and it yields checks the text cannot:
every label and every branch target must land on an instruction boundary.

```bash
python3 tools/extract_assets/extract_animdata.py --source ../pop-original --out build/data
```

Output is written to `build/data/` (git-ignored): `frames.json`,
`sequences.json`, `animdata-report.txt`.

Evidence that the assembler reproduces the original layout (**confirmed**):

| Check | Result |
| --- | --- |
| `FRAMEDEF.S` occupies `$2800-$2FD1` | fits below `seqtab = $3000` (`GAMEEQ.S:22`) |
| `Fdef`, `ALTSET1`, `ALTSET2`, `SWORDTAB` | `$2800`, `$2CB0`, `$2D78`, `$2F3A` = the 1200/200/450/192-byte areas declared in `FRAMEDEF.S:8-15` |
| `SEQTABLE.S` occupies `$3000-$39F2` | fits below `ctrl = $3a00` (`GAMEEQ.S:23`) |
| Linear decode of the sequence bytes | ends exactly at the last data byte |

There is no assembled original binary in the repository to compare against
byte for byte; that comparison is **not yet done** (see `unknowns.md` P1).

## Frame definitions

Five bytes per frame; frame *n* is at `Fdef + (n−1)×5`
(`getfindex`, `CTRLSUBS.S:941-964`). Field meanings are the author's
(`04 Support/MakeDisk/S/COMMENTS.S:346-400`), confirmed against the code that
reads them.

| Byte | Bits | JSON field | Meaning | Read by |
| --- | --- | --- | --- | --- |
| 0 `Fimage` | 0–6 | `image` | image number in the table, 0 = none | `decodeim`, `CTRLSUBS.S:1034-1037` |
| | 7 | `table` bit 2 | | `CTRLSUBS.S:1018-1020` |
| 1 `Fsword` | 6–7 | `table` bits 0–1 | | `CTRLSUBS.S:1022-1032` |
| | 0–5 | `sword` | sword-table entry, 0 = none | `SETUPSWORD`, `CTRLSUBS.S:867-871` |
| 2 `Fdx` | signed | `dx` | draw offset x, 140-wide units, + = forward | `CTRLSUBS.S:807-811` |
| 3 `Fdy` | signed | `dy` | draw offset y, + = down | `CTRLSUBS.S:823-828` |
| 4 `Fcheck` | 7 | `odd` | half-unit x adjustment | `CTRLSUBS.S:830-839` |
| | 6 | `floor_check` | weight is on the floor this frame | `Fcheckmark`, `GAMEEQ.S:653` |
| | 5 | `thin` | narrow the character for collision | `Fthinmark`, `GAMEEQ.S:654` |
| | 0–4 | `foot` | pixels from image left edge to base x | `Ffootmark`, `GAMEEQ.S:655` |

`table` 0–6 selects `chtable1`–`chtable7` (`GRAFIX.S:184-187`).

Three frame sets share the format (`FRAMEDEF.S:8-15`, `usealtsets`
`CTRLSUBS.S:1685-1715`):

| Set | Slots | Defined | Used for | Image tables referenced |
| --- | --- | --- | --- | --- |
| main | 240 | 227 | kid; mouse; other characters outside 150–189 | chtable1 (images 1–64), chtable2 (1–70), chtable3 (1–63), chtable5 (1–44) |
| altset1 | 40 (frames 150–189) | 35 | `CharID` 1–4: opponent fighting, falling, dying | chtable4 (1–32) |
| altset2 | 90 | 85 | `CharID` ≥ 5 except the mouse: princess, vizier | chtable6 (10–103), chtable7 (1–8) |

So the same frame *number* means different pictures for different characters:
entry 94 `Pstand` shows "frame 11", which for the princess is altset2 entry 11,
not the kid's run frame 11.

The sword table has 3 bytes per entry (image in chtable3, dx, dy;
`CTRLSUBS.S:974-1005`, `1050-1055`); 50 entries are defined, using chtable3
images 29–78.

### Cross-check against the image tables

Every image number referenced by a non-empty frame or sword entry exists in the
corresponding `IMG.*` file (**confirmed**, 0 missing):

- chtable1–3, 5, 7: one file each, all references in range.
- chtable4: all 35 altset1 frames are in range for all five `IMG.CHTAB4.*`
  variants (32 images each), consistent with them being interchangeable sets.
- chtable6: altset2 references images up to 103. `IMG.CHTAB6.A` has exactly 103
  images; `IMG.CHTAB6.B` has only 73. **Inferred:** altset2 as written matches
  the `.A` file. What uses `.B` is **not yet verified**.

## Sequence table

### Layout

`$3000`: 114 two-byte entry pointers; sequence *n* starts at pointer *n−1*
(`JUMPSEQ`, `CTRLSUBS.S:700-711`). Byte-code follows.

A byte in `$F1`–`$FF` is an instruction; any other value is a frame number and
ends the current tick (`ANIMCHAR`, `COLL.S:994-1156`). Operand counts below are
taken from the interpreter, not from the table text.

| Byte | Name | Operands | Count in table |
| --- | --- | --- | --- |
| `$FF` | `goto` | address (2) | 114 |
| `$FE` | `aboutface` | – | 11 |
| `$FD` / `$FC` | `up` / `down` | – | 1 / 1 |
| `$FB` | `chx` | signed dx | 292 |
| `$FA` | `chy` | signed dy | 100 |
| `$F9` | `act` | action code | 100 |
| `$F8` | `setfall` | signed x-velocity, y-velocity | 19 |
| `$F7` | `ifwtless` | address (2) | 2 |
| `$F6` | `die` | – (no effect in `ANIMCHAR`) | 4 |
| `$F5` / `$F4` | `jaru` / `jard` | – | 1 / 10 |
| `$F3` | `effect` | effect number | 2 |
| `$F2` | `tap` | sound selector | 28 |
| `$F1` | `nextlevel` | – | 1 |

Totals: 686 instructions and 839 frame bytes in 2545 bytes; 225 distinct frame
numbers are shown (including 0, the "no image" frame).

### Structural checks (all pass)

- 114 entry pointers, each on an instruction boundary.
- All 175 address labels in the byte-code area fall on instruction boundaries.
- All 116 `goto` / `ifwtless` targets fall on instruction boundaries.
- Every entry point, followed tick by tick, reaches a repeating state; none runs
  off the table and none loops without producing a frame.
- The instruction codes defined in `SEQTABLE.S:10-24` and `SEQDATA.S:116-130`
  agree with each other and with `ANIMCHAR`.
- Every frame number a sequence can show is defined in at least one frame set.

### Traces

For each entry, `sequences.json` holds a *trace*: the list of ticks `ANIMCHAR`
would produce starting there, up to the point where it re-enters a tick already
seen. Each tick lists the instructions executed, the frame shown, and that
tick's dx/dy. `ifwtless` is followed on the not-weightless path and flagged
(`has_ifwtless`; three entries: `stepfall`, `stepfall2`, `bumpfall`).

A trace follows the sequence data only. It does not model the game code that
redirects a character mid-sequence (input, collisions, falling).

Summary of where the 114 entries end up (**confirmed** by trace):

| Final loop | Entries |
| --- | --- |
| frame 15 (stand) | 41 |
| frame 171 (en garde, ready) | 20 |
| frame 106 (free fall) | 12 |
| frame 109 (crouch) | 5 |
| the 8-frame run cycle | 5 (`startrun`, `running`, `runjump`, `runturn`, `turnrun`) |
| frame 185 (dead) | 4 |
| other (guard alert, princess, vizier, mouse…) | 27 |

Run-related traces:

| Entry | Lead-in ticks | Lead-in dx | Loop | Loop dx |
| --- | --- | --- | --- | --- |
| 1 `startrun` | 7 (frames 1–7) | 14 | 8, 9, 10, 11, 12, 13, 14, 7 | 26 |
| 84 `running` | 1 (frame 7) | 0 | same | 26 |
| 43 `turnrun` | 7 | 13 | same | 26 |
| 6 `runturn` | 14 (frames 53–65, then 13) | 7 | same cycle, entered at frame 14 | 26 |
| 4 `runjump` | 12 (frames 34–44, then 7) | 67 | same | 26 |
| 13 `runstop` | 9 | 9 | 15 | 0 |

This confirms by computation the tick table in `architecture.md` §3. One
refinement: the trace reports the loop as starting at the tick that shows frame
8, because the first frame-7 tick is reached through the start-up bytes and the
later ones through the `goto`; the frames and displacements are the same.

## Findings

Properties of the original data that a port must reproduce or consciously
ignore.

1. **Names differ between files for two entries.** Entry 29 is `stepfwd1` in
   `SEQDATA.S:39` and `step1` in `SEQTABLE.S:61`; entry 96 is `Vapproach`
   (`SEQDATA.S:94`) and `Vwalk` (`SEQTABLE.S:128`). Entries 30–42 have no name
   in `SEQDATA.S`, only the comment at line 40; the code reaches them by
   arithmetic from `stepfwd1`.
2. **Two labels can share an address.** Entries 27 `sdiveroll` and 79 `crawl`
   point at the same bytes, as do the labels `fullstep` and `step14`.
3. **Four defined main frames are never shown by any sequence:** 161, 175, 176,
   206. Frame 161 is set directly by code when a strike is blocked
   (`AUTO.S:961-970`); 206 is commented "unused" in `FRAMEDEF.S`; 175 appears
   only in a commented-out line (`SEQTABLE.S:247`).
4. **`CharPosn` is also written outside the interpreter**, so the sequence table
   is not the only source of frame numbers: `AUTO.S:969-970` (161),
   `TOPCTRL.S:1232-1233` (185, death), and `SUBS.S:488, 550, 1249-1250` (0, to
   hide a character in scenes).
5. **`die` does nothing** in `ANIMCHAR` (`COLL.S:1096-1098`), though it appears
   four times.
6. **`tap` with selectors other than 0, 1, 2** would be silently ignored; only
   those three are handled (`COLL.S:1115-1134`).

## JSON layout

`frames.json`:

```
provenance         generator, SHA-256 of each source file
main, altset1, altset2   [ { frame, name, raw, empty, image, table, chtable,
                             sword, dx, dy, odd, floor_check, thin, foot } ]
swords             [ { sword, name, image, dx, dy } ]
```

`sequences.json`:

```
provenance
origin, end, instruction_codes
entries   [ { number, address, label, seqdata_name,
              ticks_before_loop, loop_length, loop_frames, loop_dx, loop_dy,
              lead_in_dx, lead_in_dy, has_ifwtless,
              ticks: [ { start, ops: [...], frame, dx, dy } ] } ]
items     flat disassembly: [ { addr, op, labels?, frame? | args? | target?, target_label? } ]
```

`name` is the trailing comment of the source line (for example the author's
frame name). These files are derived from the original source and are not
committed.

## Verification

```bash
POP_SOURCE_DIR=../pop-original python3 -m pytest tests -q
```

Recorded on 2026-11-18 against commit `25243550`: 51 passed (45 passed and 6
skipped without `POP_SOURCE_DIR`). `extract_animdata.py` reported 0 structural
errors and the two naming findings above.
