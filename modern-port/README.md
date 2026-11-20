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
| Sound-effect queue (`ADDSOUND`, `ZEROSOUND`, play order) | ported, tested |
| Sound-effect rendering (speaker flip list to PCM) | written, tested against the Python renderer; no output device yet |
| Moving level objects, opponents, input devices, screen changes, drawing, SDL3 shell | not started |

There is no runnable game yet and no SDL3 dependency yet. The nearest thing
is `tests/test_control_original.c`, which stands the kid on a floor of level 1
and runs him forward and back to a stop through the ported per-frame steps.
See `../docs/port/` for per-subsystem notes and `../docs/milestones.md` for the plan.

## Layout

```
src/core/     game rules; no I/O, no SDL
src/audio/    effect data to PCM; no I/O, no SDL
src/assets/   loaders for generated data files
tools/        command-line helpers (pop_seqdump, pop_sfxdump, pop_worldrun)
tests/        C tests, C-versus-Python comparisons, and C-versus-original
              comparisons (fuzz_world.py, fuzz_control.py, trace_kidstep.py)
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
| Linux x86-64 (Ubuntu 24.04) | GCC 13.3, with `-Werror` and ASan/UBSan | all 14 tests pass |
| Linux x86-64 (Ubuntu 24.04) | Clang 18.1, with `-Werror`; unit test programs also run under valgrind with no errors | all 14 tests pass |
| macOS, Windows | – | **not tested**. The code is plain C17 with no platform calls, and the CMake file has an MSVC branch, but neither has been built. |
