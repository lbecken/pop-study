# pop-study

A study of Jordan Mechner's 1989 Apple II *Prince of Persia* source release, and
(incrementally) a portable C17 + SDL3 reconstruction of its game systems.

Original source: <https://github.com/jmechner/Prince-of-Persia-Apple-II>
(inspected at commit `25243550b6e1d035d7b51b5d7e5114a83ebcb5c0`, branch `master`).

## What is in this repository

Only **new** material: documentation, tooling, tests and metadata.
No original source code, artwork, level data or audio is stored here; the tools
read a local checkout of the original repository that you supply.
See [docs/source-inventory.md](docs/source-inventory.md#licensing) for the
licensing position this policy is based on.

| Path | Contents |
| --- | --- |
| `docs/source-inventory.md` | What the original repository contains (and does not) |
| `docs/architecture.md` | Main loop, run-animation pipeline, draw path, with citations |
| `docs/memory-map.md` | Memory layout taken from the original equate files |
| `docs/graphics-formats.md` | Image-table format and the visual verification procedure |
| `docs/animation-data.md` | Frame-definition and sequence formats, extraction checks, findings |
| `docs/run-cycle-analysis.md` | The run, tick by tick: positions, foot contacts, timing, transitions |
| `docs/level-format.md` | Level blueprint layout, piece ids, initial states |
| `docs/room-rendering.md` | How a screen is assembled from piece sections; order, lists, special pieces |
| `docs/audio-analysis.md` | Sound effects measured by running the original code; music status |
| `docs/oracle.md` | Running original routines in the 6502 simulator as a test reference |
| `docs/port/` | Per-subsystem port notes: interface, pseudocode, pitfalls, tests |
| `docs/verification.md` | Test commands, recorded results, what has not been verified |
| `docs/compatibility-matrix.md` | Which behaviours are faithful, approximate, missing or unverified |
| `modern-port/` | The C17 port (see its README): animation, level queries, player control, falling and landing, collision, moving level objects, opponents and sword fighting, the whole game loop (frame, input state, clock, messages, level change), the blitter and the full redraw of a screen, sound effects; no window yet |
| `docs/unknowns.md` | Open questions, each with the next inspection step |
| `docs/milestones.md` | Milestone plan, directory layout, decisions awaiting input |
| `assets/manifest.json` | Hashed inventory of all 582 original files (metadata only) |
| `tools/extract_assets/` | Image-table decoder, Merlin data assembler, frame/sequence extractor, blitter model, run-cycle renderer, level parser, room renderer, manifest generator (Python 3, stdlib only) |
| `tools/assets/` | The other direction, for replacement pictures: image-table encoder, PNG reader, checker for a directory of replacement tables, placeholder-set generator (`docs/blender-to-sdl3.md`) |
| `tools/extract_audio/` | Sound-effect extraction: runs the original sound module in a 6502 simulator |
| `tools/oracle/` | Loads the original modules and calls their routines |
| `tests/` | pytest suite |

## Quick start

```bash
git clone https://github.com/jmechner/Prince-of-Persia-Apple-II ../pop-original
```

```bash
python3 tools/extract_assets/extract_images.py --source ../pop-original --out build/extracted --scale 4
```

```bash
python3 tools/extract_assets/extract_animdata.py --source ../pop-original --out build/data
```

```bash
python3 tools/extract_assets/render_run_cycle.py --source ../pop-original --out build/run_cycle
```

```bash
python3 tools/extract_assets/render_room.py --source ../pop-original --out build/rooms --level 1
```

```bash
python3 tools/extract_audio/extract_sfx.py --source ../pop-original --out build/audio
```

```bash
python3 tools/extract_assets/make_manifest.py --source ../pop-original --out assets/manifest.json
```

```bash
python3 tools/assets/make_placeholder_set.py ../pop-original build/placeholders
```

```bash
POP_SOURCE_DIR=../pop-original python3 -m pytest tests -q
```

Without `POP_SOURCE_DIR` the 55 integration tests are skipped and only the
synthetic-data unit tests run. `build/` is git-ignored.

The C port:

```bash
cmake -S modern-port -B build/port -DPOP_SOURCE_DIR=../pop-original
```

```bash
cmake --build build/port
```

```bash
ctest --test-dir build/port --output-on-failure
```

## Status

Done: milestone 1 (inventory, image-table extraction), milestone 2 (frame
definitions and animation sequences extracted and cross-checked) and milestone
3 (run cycle drawn at its real screen positions and analysed), milestone 4
(the sequence interpreter in C17, tested against the original table and
against the Python decoder), milestone 5 (level parser and a Python model of
the full-screen redraw that renders every screen of every level) and milestone
6 (sound effects obtained by running the original sound code in a 6502
simulator, with a C queue and renderer) and milestone 8 (the level-reading,
frame-lookup and gravity routines, then player control, floor checks, falling
and landing in C; every routine is compared with the original code on random
inputs, and the ported part of a whole kid frame is compared with the original
over many consecutive frames) and milestone 9
(collision; everything in the level that moves; the opponents: guards
and the shadow, sword fighting, strength, and passing from screen to screen;
the routine that produces a whole frame, the kid's death, the start of a
level; then the loop around the frame: input state, the 60-minute clock,
messages, song cues, the step to the next level; all verified the same way,
and the original's demo level replays in the C port, one pass of the
original's main loop at a time, with the same result as the original code
after every pass). All game rules that do not draw are now in C, and so is
the drawing (milestone 10): the blitter, the full redraw of a screen, the
partial redraw of a frame, characters, messages and the two display pages.
The original's own drawing code runs in the simulator too, and the demo
level and random play on the other levels come out the same in the port,
frame for frame, in every byte of both hi-res pages. Around that there is a
simple model of the colours a monitor makes of those bytes, and a shell
(milestone 7): an SDL3 window with keys and sound effects, and a headless
player that writes frames as PNG files.

What that amounts to: the levels of your own checkout can be played. What
it does not: nothing has been compared with a real Apple II or an emulator;
the shell was written on a machine without a display and has never been
seen running; the title sequence, the scenes with the princess, the
endings, music and saved games are not ported; the colours are a model,
checked by eye. See `docs/milestones.md`, `docs/compatibility-matrix.md`
and `modern-port/README.md`.

## Conventions

Every statement about the original is labelled **confirmed** (read in the
source or verified by parsing data), **inferred**, or **not yet verified**, and
cites `file:line` in the original repository at the commit above.
