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
| Characters, swords, impact stars and falling floors: which are listed for drawing, where, cut how (`TOPCTRL.S` `addchars`, `CTRLSUBS.S`, `MOVER.S` `ADDMOBS`), and their drawing with save-under | ported; the list compared on random game states, the two pages on random lists |
| Messages, the two display pages, screen changes; a whole frame of the game with its drawing (`TOPCTRL.S` `DoFast`, `DoCleanCut`, `SUBS.S` `PAGEFLIP`, `GAMEBG.S`) | ported; the demo and random play compared with the original frame by frame, both hi-res pages byte for byte |
| From page bytes to pixels: the dots, and a simple model of the colours a monitor makes of them (`pop_present.c`; not from the game's source) | done; the colours checked by eye only |
| A program to start: the game as one object (`src/app`), a headless player, and an SDL3 shell (`src/sdl`: window, keys, sound effects, fixed timestep) | new code; draws exactly what the tested runner draws; the shell has only been run without a display |
| Title sequence, scenes with the princess, endings, music, saved games, the original's special keys | not ported |

With SDL3 installed the build produces `pop_sdl`, which plays the levels of
your checkout in a window:

```
cmake -S . -B build -DPOP_SOURCE_DIR=<original checkout> [-DCMAKE_PREFIX_PATH=<SDL3 prefix>]
cmake --build build
build/pop_sdl --source <original checkout> --data build/data [--level 1]
```

Arrows (or J L I K, U O) and Shift; `../docs/port/shell.md` has the rest and,
importantly, what of it has never been tried: it was written and tested on a
machine without a display. Without SDL3, `build/pop_headless` plays the game
into PNG frames and a WAV file.

Both take `--images <dir>`: a directory of replacement image tables to use
instead of the checkout's pictures (`../docs/blender-to-sdl3.md`). No such
artwork exists yet; `../tools/assets/make_placeholder_set.py` makes a set of
boxes, which shows the route working and the geometry the rules use.

The strongest evidence that the port is right
is `tests/replay_frames.py demo`, which plays the original's demo level in
the port, one `pop_mainloop` call per frame with the drawing code behind its
hooks, next to the original's `MainLoop` and blitter in a 6502 simulator,
and compares the two hi-res pages after every frame. To look at it:

```
python3 tools/render_demo.py <build>/pop_worldrun <build>/data <original checkout> <out dir>
```

writes frames of the demo, as the port alone draws them, as PNG files with a
contact sheet (`../docs/port/presentation.md`). They show the original
artwork from your checkout and are not part of this repository.
See `../docs/port/` for per-subsystem notes and `../docs/milestones.md` for the plan.

## Layout

```
src/core/     game rules; no I/O, no SDL
src/audio/    effect data to PCM; no I/O, no SDL
src/video/    the blitter, display lists and screen redraw: hi-res page bytes; no I/O, no SDL
src/assets/   loaders for generated data files and for the original's image files
src/app/      the game as one object: files, levels, keys, sound samples; no SDL
src/sdl/      the SDL3 shell (built when SDL3 is found)
tools/        command-line helpers (pop_seqdump, pop_sfxdump, pop_worldrun,
              pop_hiresrun, pop_gfxrun, pop_headless), render_demo.py
tests/        C tests, C-versus-Python comparisons, and C-versus-original
              comparisons (fuzz_world.py, fuzz_control.py, trace_kidstep.py,
              replay_demo.py, fuzz_hires.py, compare_rooms.py, compare_fast.py,
              replay_frames.py), checks of the shell (check_app.py, check_sdl.py,
              check_images.py)
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
