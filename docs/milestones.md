# Milestone plan

Small, testable steps. Each ends with a commit and a recorded test command.
Nothing is ported before it has been read and described.

## Directory layout

```
docs/                 analysis; one file per topic, citations to the original
assets/manifest.json  hashed inventory of the original (metadata only)
tools/
  extract_assets/     Python: image tables, Merlin data assembler, frame/sequence
                      decoders, blitter model, run-cycle renderer, level parser,
                      room renderer
  extract_audio/      Python: sound effects by running the original sound module
  oracle/             Python: load the original modules and call their routines
modern-port/
  CMakeLists.txt
  src/core/           C17, no I/O, no SDL: animation, player, collision, rooms, enemies
  src/assets/         loaders for generated data files
  src/audio/          effect renderer (flip list to PCM); SDL3 backend later
  src/platform/       SDL3 window, input, renderer (later)
  tools/              command-line helpers
  tests/              C tests, C-versus-Python comparisons, later replay fixtures
tests/                pytest for the tools
build/                generated, git-ignored
  extracted/          decoded images
  data/               frames.json, sequences.json
  run_cycle/          timeline.png, pivot.png, mask_demo.png, run_cycle.json
  rooms/              levelNN/screenSS.png and .json, level.json, overview.png
  audio/              sfx.json, sfx.csv, sfx_NN_name.wav, sfx_all.wav
  port/               CMake build tree; port/data/ holds seqtable.bin, sfx.bin,
                      framedef.bin, imgdims.bin, autodata.bin
```

The original checkout stays outside this repository and is never written to.

## Milestones

| # | Deliverable | Test | Status |
| --- | --- | --- | --- |
| 1 | Verified inventory; image-table decoder; `manifest.json`; Phase 1 docs (first pass) | 14 pytest cases; contact sheet inspected | **done** |
| 2 | `FRAMEDEF.S` and `SEQTABLE.S` assembled and decoded to JSON: frames, swords, sequences, per-tick dx/dy, traces; `docs/animation-data.md` | 51 pytest cases; 114 entry points resolve; all labels and branches aligned; run cycle sums to 26; every referenced image exists | **done** |
| 3 | Run frames drawn at their real positions by a byte-level blitter model (mask and mirror modes) with ports of the position routines; timeline and pivot sheets; `docs/run-cycle-analysis.md` | 90 pytest cases; blitter tables equal `HRTABLES.S` byte for byte; right-facing run is an exact mirror; supporting foot stays within 2 units per stance | **done** |
| 4 | First C17 code: `ADDCHARX`, `JUMPSEQ`, `GETSEQ`, `ANIMCHAR` in `modern-port/src/core`; CMake build that generates `seqtable.bin` from the user's checkout; `docs/port/sequence-interpreter.md` | 4 CTest tests: synthetic unit tests, loader tests, original-table tests, and 45,600 ticks compared C against Python; GCC and Clang with `-Werror`, ASan/UBSan, valgrind | **done** |
| 5 | Level blueprint parser; Python model of the full-screen redraw (`SURE` and the list blitters) rendering every screen to PNG; `docs/level-format.md`, `docs/room-rendering.md` | 113 pytest cases; all 299 reachable screens of 15 levels build and draw; screens line up across edges when laid out by the level map. Not compared with screenshots (none available) | **done** |
| 6 | Sound effects by executing the original `SOUND.S`: a 6502 assembler and cycle-counting simulator, flip lists as JSON/CSV, WAV previews; C sound queue and effect renderer; `docs/audio-analysis.md`. Side result: 18 of 22 original modules assemble and fit their memory slots | 131 pytest cases; 7 CTest tests incl. C renderer against Python, sample by sample; simulator timing equals the hand-derived loop formula for every effect | **done** |
| 7 | SDL3 shell: window, fixed timestep, nearest-neighbour scaling, plays the run cycle with keyboard input; SDL3 audio backend for the effect renderer | Manual + deterministic headless trace. Needs SDL3, which is not installed in the current environment | deferred until SDL3 is available |
| 8a | Oracle (original routines run in the 6502 simulator, 17 modules loaded) and the C world-query layer: block reading across screens, foot/distance/coordinate routines, piece classes, frame lookup, record copies, `GRAVITY`, `ADDFALL`; `docs/oracle.md`, `docs/port/world-queries.md` | 139 pytest cases; 9 CTest tests incl. 15,600 random cases per run, C against the original code; earlier milestones re-checked against the original | **done** |
| 8b | Player control in C: `PLAYERCTRL` (input left as a hook), `UserCtrl`, `GENCTRL`, the `standing`, `starting`, `stjumpup`, `turning`, `arunning`, `crouching`, `hanging` handlers, `FightCtrl`/`GuardCtrl` and every `Do…` action, with `GETFWDDIST`, `SETUPCHAR`, `GETEDGES`, `CHECKLEDGE`, `GETOPDIST` and the redraw marks; `docs/port/player-control.md` | Each routine fuzzed against the original through the oracle (all variables, sounds, level changes, redraw buffers, calls out); 143 unit checks on synthetic data; 35 deliberate bugs injected to test the test | **done** |
| 8c | Floor and falling in C: `CHECKFLOOR`, `onground`, `startfall`, `CDpatch`, `InsideBlock`, `falling`, `hitflr`, `fallon`, `CHECKIMPALE`, `DOIMPALE`; then the ported part of a whole kid frame run for many consecutive frames | Same fuzzing; plus `trace_kidstep.py`: 60-frame traces of random input, C against the original after every call; a scenario test on `LEVEL1` (run and stop) | **done** |
| 9a | Collision in C: `CHECKBARR` and its collision-data buffers, `COLLISIONS`, `CHECKCOLL`, the ground and air bumps, `CHECKGATE`, `CHECKSLICE`, `CHECKSLICE2`, `ENEMYCOLL`, `GoneUpstairs`; added to the kid-frame trace, where the kid now stops at walls; `docs/port/collision.md` | 8 more routines in the fuzz comparison (60 in all); the trace with collision steps; 109 unit checks; 24 more deliberate bugs | **done** |
| 9b | Moving level objects in C: the transition list and every piece's state machine (gates, exit, plates and their link chains, spikes, slicers, loose floors, torches, flasks, sword), falling floors with landing, rubble and crushing, and the triggers in the kid's frame (`CHECKPRESS`, `CHECKSPIKES`, `shakeloose`, `ADDSLICERS`); `docs/port/moving-objects.md` | 27 more routines in the fuzz comparison (87 in all); the kid-frame trace with the level reacting; 129 unit checks of whole timelines; 45 more deliberate bugs | **done** |
| 9c | Opponents in C: `AUTO.S` (guard and shadow programs, `CHECKSTRIKE`, `CHECKSTAB`, scripted movement, `CUTCHECK` and the guard hand-over, `ADDGUARD`), `MISC.S` (`STABCHAR`, `CHECKALERT`, `FIRSTGUARD`, potions, mirror shadow, skeleton, mouse), `SHADCTRL`, `DoShad`, strength meters, `INITIALGUARDS`; fight tables generated into `autodata.bin`; `docs/port/opponents.md` | 29 more routines in the fuzz comparison (116 in all); the frame trace with guards, fights, strength and screen changes; 128 unit checks; 80 more deliberate bugs | **done** |
| 9d-1 | The top of a frame and a level in C: `NextFrame`, `DoKid` with the kid's death handling, `misctimers`, `PrepCut`, level start (`RESTART` from the point the level is loaded, `SETINITIALS`, `STARTKID`, `entrance`, `FirstFrame`), `DemoCtrl` and the demo's script (generated data); `docs/port/game-frame.md` | 18 more routines in the fuzz comparison (134 in all); the frame trace now runs the original's own `NextFrame`; the original's demo level replayed from start to end, C against the original at every call; 90 unit checks; 89 more deliberate bugs | **done** |
| 9d-2 | The loop around the frame: input-device state (`getselect`, `clrjstk`, the per-device press flags, `KREAD`), the clock (`KEEPTIME`, `GETMINLEFT`, `SHOWTIME`), messages (`dispmsg`), song cues, flashes, `MainLoop`, `LoadNextLevel`; the state changes made inside the drawing path | 27 more routines in the fuzz comparison (161 in all), among them whole `MainLoop` passes; trace and demo replay now run `MainLoop` itself; 211 unit checks; 112 more deliberate bugs; decimal mode added to the simulator; `docs/port/game-loop.md` | **done** |
| 10a | The blitter in C (`HIRES.S`: `lay`, `fastlay`, `layrsave`, `peel`, masks, the five draw modes, mirroring, cropping) writing a 40 × 192-byte hi-res page. Needs the oracle extended to two memory banks, so that the original blitter itself runs in the simulator on the original image tables | Page bytes after each call, C against the original blitter, on random images, positions and modes: done, with 73 unit checks and 59 deliberate bugs. Side results: the Python blitter model of milestone 3 and the room model of milestone 5 now agree byte for byte with the original code's output (all 336 screens of the 15 levels); `docs/port/blitter.md` | **done** |
| 10b | Full-screen redraw in C (`SURE`, the image lists, `drawall`), replacing the Python model as the reference | The finished page and the three display lists for all 336 screens and the null screen of each level, and for random gadget states and pieces, C against the original code: done, with 122 unit checks and 105 deliberate bugs; piece tables generated into `bgdata.bin`; `docs/port/redraw.md` | **done** |
| 10c-1 | Partial redraw in C: `FAST` and the redraw buffers, wipes, floor pieces over a character, the top line, strength meters | The page, the five display lists, the redraw buffers and the meter flags after random marks, C against the original code: done, with unit checks and deliberate bugs; `docs/port/frame-drawing.md` | **done** |
| 10c-2 | The object list: characters with cropping, swords, impact stars, falling floors (`addchars`, `addmobs`, `drawobjs`); save-under across the two pages | The object list from random game states, and the page from random object lists, C against the original | in progress |
| 10c-3 | Messages, page flipping, the whole frame (`DoFast`, `DoCleanCut`) | The page after every frame of the demo, C against the original | |
| 10d | Presentation: 280 × 192 dot buffer and the artifact-colour filter of decision D3; frames written as PNG | Dot buffer derived from compared page bytes; colour model documented, checked by eye only (no emulator captures available) | |
| 11 | Verification pass: compatibility matrix, emulator comparison, profiling | `docs/verification.md` | |
| 12 | Optional: Blender → sprite-sheet pipeline document | – | |

Milestones 2–4 depend only on what is already in hand. From milestone 5 on, the
decisions below affect the work.

## Decisions awaiting input

Defaults are what I will assume unless told otherwise.

**D1. Where derived game data lives.** The port needs the sequence table, frame
definitions, levels and images.
*Default:* generate them at build time from the user's checkout into `build/`;
commit none of it. This keeps the repository free of original content at the
cost of a required setup step.
*Alternative:* commit generated JSON/headers for the tables (not the art), which
makes the port build stand-alone but redistributes data derived from the source.

*Clarification (milestone 9c).* "Data" here means tables that describe
content: animation, levels, images, sounds, and since 9c the opponents' fight
parameters and scripted movements (`autodata.bin`). Constants that are part
of a routine's logic (thresholds, the gate's drop speeds, the widths of
barriers within a block) are written in the C source next to the code that
uses them, each with its citation. The line between the two is a judgement;
say so if you want it drawn elsewhere.

**D2. Simulation rate.** The original has no fixed frame rate (see
`architecture.md` §2).
*Default:* fixed 725/60 ≈ 12.083 Hz, the constant the game's own clock uses,
configurable.
*Alternative:* calibrate to measured emulator timing per scene, including the
pauses caused by sound.

**D3. Colour rendering.**
*Default:* render a 280 × 192 dot buffer exactly as the blitter would and apply
a simple, documented hi-res artifact-colour model as a presentation filter, with
monochrome as an option.
*Alternative:* full NTSC signal simulation, or pre-coloured sprites.

**D4. Music.** Score data is absent from the repository.
*Default:* port the player logic from `MSYS.S` and the cue points, ship no
songs; the port stays silent where music would play unless the user supplies
song tables extracted from their own disk image.
*Alternative:* leave music out entirely.

**D5. Test oracle.** *In use.* The project's own assembler and 6502 simulator
assemble 19 of the 22 original modules (plus `GRAFIX.S` without its IIGS tail)
and run their routines (`docs/oracle.md`). Every routine ported to C from
milestone 8 on is compared with the original on random inputs, in addition to
hand-derived tests. The simulator is new
code with documented cycle counts; it has not itself been checked against an
independent emulator, and emulator comparison of the whole game still needs a
disk image.
