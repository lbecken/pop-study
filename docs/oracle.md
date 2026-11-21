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
| `inverty`, `attractmode` | drawing and game flow | records the call |
| `LoadDesel`, `getdesel`, `SaveDesel` | the second input device | records the call |
| a read of `$C030` | the speaker | recorded through `on_read` |
| `GOATTRACT`, `YouLose`, `YouWin`, `RESTART`, `attractmode` | jumps that abandon the frame and reset the stack | records the call and **ends the run there** (a Python exception out of the simulator), as the jump ends the frame in the game |
| `MainLoop` | the loop's own way round | ends the run without a record: one pass is one test step |
| `strobe` | keyboard and joystick hardware | records the call; the test sets `kbdX`, `kbdY`, `BTN0`, `BTN1`, `keypress` |
| `sure`, `fast`, `PageFlip`, `zeropeels`, `continuemsg`, `printlevel`, `timeleftmsg`, `doflashon`, `doflashoff` | drawing | records the call (with the flash colour) |
| `lrcls`, `lrclse`, `vblank`, `copyscrn`, `drawall`, `fastspeed`, `normspeed`, `burn`, `listtorches` | drawing | nothing. (Until milestone 10c-2 also `zerolsts`, `addmobs`, `setupkid`, `setupshad`, `setupcomix`, `setupsword`, `addreflobj`: these now run, and what they leave in the object list and the redraw buffers is compared) |
| `playback`, `minit`, `mplay`, `gtone`, `musickeys` | sound | records the call (with the queued sounds, the song number); `mplay` answers "song over" |
| `cutprincess`, `playcut`, `flipdisk`, `yellow`, `setrecheck0`, `recheckyel` | scenes, disk, copy protection | records the scene number; the disk check answers "no error" |
| reads of `$C061`, `$C062` | the buttons, read directly by `DEMOKEYS` | memory there is set from `BTN0`, `BTN1` |

(Earlier milestones also trapped `jamspikes`, `addslicers`, `smashmirror`,
`GoneUpstairs`, `cuesong`, `potioneffect`, `DemoCtrl`, `LoadSelect`, `getselect`,
`clrjstk`, `SaveSelect`, the deselected-device routines, `keeptime`, `showtime`,
`initinput`, `LoadNext1` and `DoCleanCut`; those now run for real, since their
ports exist.)

`RESTART` cannot be entered at its label, because it begins by loading the
level from disk. The level-start comparison writes the level into memory and
enters at the `jsr setinitials` inside it, an address found by searching the
assembled routine for that instruction (`Harness.restart_level` in
`fuzz_control.py`).

A trapped routine is not being tested; what is compared is the caller's
behaviour given the same answers on both sides.

## Two memory banks: running the drawing code

The game's code and data are in auxiliary memory; its blitters
(`HIRES.S`), their tables (`HRTABLES.S`) and the hi-res pages are in main
memory at overlapping addresses, and the two are crossed with the Apple
IIe's soft switches. The flat oracle cannot run any of that.
`tools/oracle/pop_banked.py` adds the three switch pairs the game uses
(`ALTZP`: zero page, stack and `$D000-$FFFF`; `RAMRD` and `RAMWRT`:
`$0200-$BFFF`) to the simulator, and `tools/oracle/pop_gfx_oracle.py` builds
the 128K layout of `docs/memory-map.md`:

| Bank | Loaded |
| --- | --- |
| auxiliary | the 17 game modules, the level, `bgtable1`/`bgtable2` (`$6000`, `$8400`), `chtable4` and `chtable7` |
| main | `HRTABLES` (`$E000`), `HIRES` (`$EE00-$F843`), `chtable1`, `2`, `3`, `5`, both hi-res pages |

Routines are started as the game runs, with all three switches on
(auxiliary). A drawing call goes through its `GRAFIX.S` entry point, which
copies the parameters to the main zero page, switches, calls the blitter and
switches back; every instruction of that path is the original's.

```python
from pop_gfx_oracle import GfxOracle
o = GfxOracle("../pop-original")
o.load_table("bgtable1", "IMG.BGTAB1.DUN")
o.load_table("bgtable2", "IMG.BGTAB2.DUN")
o.load_level(open("../pop-original/01 POP Source/Levels/LEVEL1", "rb").read())
o.call("setinitials")
o.set_vars({"VisScrn": 1, "PAGE": 0})
o.call("getscrns")
o.call("DoSure", module="TOPCTRL")       # the original's full-screen redraw
page = o.page(1)                          # 8192 bytes of hi-res page 1
```

Five things in it are this project's, not the original's:

- **Image tables are relocated on loading.** The pointers in the image files
  assume `$6000`; tables kept elsewhere need them moved, and no game code
  does it (`docs/port/blitter.md` §8, unknown G4).
- **Only the table of an image file is loaded**, up to the end of its last
  image. Some files are longer than their table and than the space the
  game has for it (`docs/port/frame-drawing.md` §6); loading a table over
  another one is refused.
- **The copy-protection flag `purpleflag` is 1**, as a passed check leaves
  it (`docs/port/redraw.md` §3). With the 0 that the bare modules start
  with, save-under entries are misfiled on palace levels.
- **`UNPACK.S` is not loaded** (its macros are beyond the assembler). The
  one routine of it that the game calls during play, `INVERTY`, has an
  identical twin in `HIRES.S`; a jump to the twin stands at `UNPACK`'s
  jump-table slot. A test compares the two source texts.
- **Not modelled**: the two `$D000` banks of each language card, the ROM
  switches, 80STORE, and all video output. The oracle produces page bytes,
  never a picture.

**Whole frames.** `modern-port/tests/replay_frames.py` puts the test
harness of the single-routine tests on this oracle (a thin class gives it
the one-bank oracle's interface, with game memory in the auxiliary bank)
and takes the stand-ins for drawing away: `dimchar`, `inverty`,
`zeropeels`, `sure`, `fast`, `PageFlip`, the three message routines,
`doflashon`, `doflashoff`, `lrcls`, `lrclse`, `copyscrn` and `drawall` run
as they are. Still stood in for: the keyboard, the vertical-blank wait
(`vblank` polls `$C019`), sound, the disk and the scenes. The display soft
switches `$C050`–`$C057` are followed through read hooks, to know which
page the monitor would show. A trap is keyed by address only; the harness
checks that no trapped game address is also an address of the blitter's
code in main memory.

## Limits

- **Decimal mode** is modelled as the NMOS 6502 does it; the one routine
  that uses it (`GETMINLEFT`) does not depend on the flags that differ on a
  65C02.
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
| Player control, floor, landing, collision, moving objects, opponents, frame and level top level, the loop around the frame (C) | 161 routines on random states, about 63,800 cases per test run: variables, registers, sounds, level changes, redraw buffers, calls out | C port and original agree |
| The game loop over time (C) | `FirstFrame`, then whole passes of `MainLoop` with random keyboard input; 60 traces of up to 60 frames per test run, compared after each pass | C port and original agree |
| The demo (C) | `StartGame`, the level start, then `MainLoop` passes until the demo ends itself: 393 frames | C port and original agree after every pass |
| The clock's end and the start of level 1 | `MainLoop` run in the simulator from `StartGame` on level 1, and through counted frames 43,380 to 43,505 | source of the observations in `docs/port/game-loop.md` §3, §10 |
| The Python blitter model (milestone 3) | the original `HIRES.S` blitter, called through `GRAFIX`, on the real character and background images at random positions, offsets and modes, 330 calls | page bytes identical |
| The Python room model (milestone 5) | the original's `DoSure` for all 336 screens of the 15 levels | page bytes identical |
| The blitter (C) | every `HIRES.S` routine on random images, positions, cuts, offsets and modes over random page contents | C port and original agree on both pages and the save-under buffers |
| The full redraw (C) | `DoSure` for every screen and null screen of the 15 levels, and for screens with random gadget states, extra pieces and kid positions: the page and the three display lists | C port and original agree |
