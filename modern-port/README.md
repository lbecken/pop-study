# modern-port

C17 reconstruction of the Prince of Persia (Apple II) game systems.
New code only; the game data it needs is generated at build time from your own
checkout of the original source and is never stored in this repository.

## Status

| Subsystem | State |
| --- | --- |
| Sequence interpreter (`ADDCHARX`, `JUMPSEQ`, `GETSEQ`, `ANIMCHAR`) | ported, tested |
| World queries: block reading across screens, foot position, distances, coordinates, piece classes, frame lookup, `GRAVITY`, `ADDFALL` | ported; each routine compared with the original code on random inputs |
| Player control: `PLAYERCTRL` (input is a hook), `GENCTRL` and its handlers (standing, running, turning, crouching, hanging, sword moves), all `Do…` actions, pick-up | ported; each routine compared with the original code on random inputs |
| Floor: `CHECKFLOOR`, starting to fall, ledge grab, landing and fall damage, spikes | ported; same, plus multi-frame traces against the original |
| Collision with walls, gates, slicers, the mirror (`CHECKBARR`, `COLLISIONS`, bumps, `CHECKGATE`, `CHECKSLICE`, `ENEMYCOLL`) | ported; same |
| Moving level objects: gates, exit, pressure plates and links, spikes, slicers, loose and falling floors, torches, flasks (`MOVER.S`) | ported; same |
| Opponents: guard and shadow behaviour, sword contact, strength and potions, guards and the kid changing screens (`AUTO.S`, `MISC.S`) | ported; same. Fight tables are generated data |
| Sound-effect queue (`ADDSOUND`, `ZEROSOUND`, play order) | ported, tested |
| Sound-effect rendering (speaker flip list to PCM) | written, tested against the Python renderer; no output device yet |
| A whole frame (`NextFrame`, `DoKid`), the kid's death, entering a screen, the start of a level, the demo's player (`TOPCTRL.S`, `SUBS.S`) | ported; same, plus a replay of the whole demo level against the original |
| The loop around the frame: input-device state and press flags, the clock and its messages, song cues, flashes, `MainLoop`, the next level (`TOPCTRL.S`, `SPECIALK.S`, `SUBS.S`) | ported; same. `pop_mainloop` is one complete frame |
| The blitter: images onto a hi-res page in every mode, shifted, mirrored, cropped; save-under (`HIRES.S`) | ported; each routine compared with the original blitter over whole pages |
| The display lists and their drawing, and the full redraw of a screen from the level (`GRAFIX.S`, `FRAMEADV.S` `SURE`) | ported; every screen of the 15 levels compared with the original's page, plus random gadget states. Piece tables are generated data, images are read from your checkout |
| What changes from frame to frame: the redraw marks, wipes, strength meters (`FRAMEADV.S` `FAST`, `GAMEBG.S`) | ported; page, lists and marks compared with the original on random marks |
| Characters on screen, falling floors, messages, colour, reading keys and joystick, sound output, music, SDL3 shell | not started (hooks) |

There is no runnable game yet and no SDL3 dependency yet. The nearest thing
is `tests/replay_demo.py`, which plays the original's demo level in the port,
one `pop_mainloop` call per frame, next to the original's `MainLoop` in a 6502
simulator.
See `../docs/port/` for per-subsystem notes and `../docs/milestones.md` for the plan.

## Layout

```
src/core/     game rules; no I/O, no SDL
src/audio/    effect data to PCM; no I/O, no SDL
src/video/    the blitter, display lists and screen redraw: hi-res page bytes; no I/O, no SDL
src/assets/   loaders for generated data files
tools/        command-line helpers (pop_seqdump, pop_sfxdump, pop_worldrun,
              pop_hiresrun, pop_gfxrun)
tests/        C tests, C-versus-Python comparisons, and C-versus-original
              comparisons (fuzz_world.py, fuzz_control.py, trace_kidstep.py,
              replay_demo.py, fuzz_hires.py, compare_rooms.py)
```

## Build and test

Requirements: a C17 compiler, CMake 3.20 or newer; Python 3.8 or newer for the
original-data tests.

Unit tests only (no original data needed):

```bash
cmake -S modern-port -B build/port
```

```bash
cmake --build build/port
```

```bash
ctest --test-dir build/port --output-on-failure
```

With the original source, which also generates the data and enables the
original-data tests:

```bash
cmake -S modern-port -B build/port -DPOP_SOURCE_DIR=/path/to/Prince-of-Persia-Apple-II
```

Options: `-DPOP_WERROR=ON` (warnings as errors), `-DPOP_SANITIZE=ON`
(AddressSanitizer and UBSan, GCC/Clang).

## Tested platforms

| Platform | Compiler | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04) | GCC 13.3, with `-Werror` and ASan/UBSan | all 23 tests pass |
| Linux x86-64 (Ubuntu 24.04) | Clang 18.1, with `-Werror`; unit test programs also run under valgrind with no errors | all 23 tests pass |
| macOS, Windows | – | **not tested**. The code is plain C17 with no platform calls, and the CMake file has an MSVC branch, but neither has been built. |
