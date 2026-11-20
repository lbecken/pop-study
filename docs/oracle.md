# The oracle: running the original routines

The project can execute the original game code. This is the reference every
ported routine is now compared against.

Code: `tools/oracle/pop_oracle.py`, built on the assembler and 6502 simulator
in `tools/extract_assets/mos6502.py`.

## What it does

1. Assembles 17 game-side modules from your checkout of the original source.
2. Loads them at their original addresses into one 64K memory.
3. Lets a test set any variable by its original name, call any routine, and
   read registers and memory afterwards.

```python
from pop_oracle import Oracle
o = Oracle("../pop-original")
o.load_level(open("../pop-original/01 POP Source/Levels/LEVEL1", "rb").read())
o.set_vars({"CharScrn": 1, "CharBlockX": 4, "CharBlockY": 1, "CharFace": 0xFF})
piece, x, y, flags = o.call("getinfront")          # by jump-table name
o.call("FACEDX", a=5, module="CTRLSUBS")           # or by a module's own label
```

## Modules loaded

| Module | Extent | Module | Extent |
| --- | --- | --- | --- |
| `GRAFIX` | `$0400-$0DAA` (see below) | `AUTO` | `$5400-$5DC0` |
| `TABLES` | `$0E00-$1288` | `CTRLSUBS` | `$D000-$D8FE` |
| `FRAMEADV` | `$1290-$1FA9` | `SPECIALK` | `$D900-$DE25` |
| `TOPCTRL` | `$2000-$27F8` | `VERSION` | `$DFD8-$DFF4` |
| `FRAMEDEF` | `$2800-$2FD0` | `SUBS` | `$E000-$E965` |
| `SEQTABLE` | `$3000-$39F1` | `SOUND` | `$EA00-$EB2E` |
| `CTRL` | `$3A00-$44FE` | `MOVER` | `$EE00-$F863` |
| `COLL` | `$4500-$4BF1` | `MISC` | `$F900-$FE2B` |
| `GAMEBG` | `$4C00-$5215` | | |

No two overlap (checked at load). The main-memory modules (`HIRES`,
`HRTABLES`, `MASTER`, `UNPACK`) share addresses with these in the other memory
bank and are not loaded.

## Traps: standing in for what cannot run

`Cpu6502.traps[address]` runs a Python function when execution reaches an
address and then returns as `rts` would. The control tests use it for the
routines the flat memory cannot serve, and to record calls into subsystems
the port does not have yet (`modern-port/tests/fuzz_control.py`):

| Trapped | Why | The trap |
| --- | --- | --- |
| `dimchar` (`GRAFIX.S:879-883`) | reads image tables in main memory | answers width and height from the same image files |
| `jamspikes` (`MOVER.S:354-358`) | goes on into the moving-object list | writes `$FF` to the spikes' state, as the first two instructions do; records the call |
| `addslicers`, `potioneffect`, `GoneUpstairs` | unported subsystems | records the call |
| `LoadSelect`, `getselect`, `clrjstk`, `SaveSelect`, `DemoCtrl` | input devices, demo | records the call; the test supplies the joystick variables |

A trapped routine is not being tested; what is compared is the caller's
behaviour given the same answers on both sides.

## Limits

- **Flat memory.** There is no bank switching; writes to the soft switches
  `$C000-$C0FF` are ignored. Routines that draw through the main-memory
  blitters, or use the disk, cannot be run.
- **`GRAFIX.S` is cut short.** Its last section is 65816 code for the Apple
  IIGS control panel, which the assembler does not handle. Everything before it
  is assembled and nothing moves. `UNPACK.S` (macros) and `BOOT.S` (`sav`) do
  not assemble at all; neither is game logic.
- **Caller's flags.** `Oracle.call` sets the N and Z flags from A, as if the
  caller had just loaded it. `GETLEFT`, `GETRIGHT`, `GETUP` and `GETDOWN` begin
  with `beq` without loading anything and rely on that; all 17 call sites do
  load A immediately before (**confirmed** by listing them).
- **The assembler and simulator are this project's own.** Every module fits
  its memory slot, and results agree with independently written models wherever
  both exist, but neither has been checked against an independent assembler,
  emulator or disk image (unknowns P1, P5).
- **Two tolerated source oddities**, both accepted by Merlin: a stray `fin`
  (`SPECIALK.S:248`) and a `do` left open to the end of `GRAFIX.S` (line 2016).

## What has been checked with it

| Subject | Check | Result |
| --- | --- | --- |
| Level parser | `RDBLOCK` on all 720 blocks of level 1, plus off-screen and null-screen cases | agree |
| Sequence interpreter | `ANIMCHAR` for all 114 sequences × 80 ticks × 2 facings: every character field, `jarabove`, `alertguard`, queued sounds | 18,240 ticks agree with the model the C port matches |
| Frame lookup | `GETFRAMEINFO` for every frame and character type | agrees with the decoded tables |
| Position arithmetic | `ADDCHARX`, `GETBASEX`, `SETUPCHAR` on random inputs | agree with the Python ports |
| Geometry tables | byte/offset, block edges, floor lines as loaded in memory | agree |
| Sound effects | `PLAYBACK`, `ADDSOUND` | source of the effect data (`docs/audio-analysis.md`) |
| World queries, gravity (C) | 39 routines on random states, 15,600 cases per test run | C port and original agree |
| Player control, floor, landing (C) | 52 routines on random states, about 20,700 cases per test run: variables, registers, sounds, level changes, redraw buffers, calls out | C port and original agree |
| A kid frame over time (C) | the ported steps of `DoKid` in order, 60 traces of 60 frames of random input per test run, compared after each call | C port and original agree |
