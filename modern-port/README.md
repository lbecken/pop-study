# modern-port

C17 reconstruction of the Prince of Persia (Apple II) game systems.
New code only; the game data it needs is generated at build time from your own
checkout of the original source and is never stored in this repository.

## Status

| Subsystem | State |
| --- | --- |
| Sequence interpreter (`ADDCHARX`, `JUMPSEQ`, `GETSEQ`, `ANIMCHAR`) | ported, tested |
| Everything else | not started |

There is no runnable game yet and no SDL3 dependency yet.
See `../docs/port/` for per-subsystem notes and `../docs/milestones.md` for the plan.

## Layout

```
src/core/     game rules; no I/O, no SDL
src/assets/   loaders for generated data files
tools/        command-line helpers (pop_seqdump)
tests/        C tests and the C-versus-Python comparison
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
| Linux x86-64 (Ubuntu 24.04) | GCC 13.3, with `-Werror` and ASan/UBSan | all 4 tests pass |
| Linux x86-64 (Ubuntu 24.04) | Clang 18.1, with `-Werror`; original-data test also run under valgrind with no errors | all 4 tests pass |
| macOS, Windows | – | **not tested**. The code is plain C17 with no platform calls, and the CMake file has an MSVC branch, but neither has been built. |
