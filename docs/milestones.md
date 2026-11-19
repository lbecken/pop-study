# Milestone plan

Small, testable steps. Each ends with a commit and a recorded test command.
Nothing is ported before it has been read and described.

## Directory layout

```
docs/                 analysis; one file per topic, citations to the original
assets/manifest.json  hashed inventory of the original (metadata only)
tools/
  extract_assets/     Python: image tables, Merlin data assembler, frame/sequence
                      decoders, blitter model, run-cycle renderer; later level
                      parser
  extract_audio/      Python: sound-effect simulator (later)
modern-port/
  CMakeLists.txt
  src/core/           C17, no I/O, no SDL: animation, player, collision, rooms, enemies
  src/assets/         loaders for generated data files
  src/audio/          tone model + SDL3 backend (later)
  src/platform/       SDL3 window, input, renderer (later)
  tools/              command-line helpers
  tests/              C tests, C-versus-Python comparisons, later replay fixtures
tests/                pytest for the tools
build/                generated, git-ignored
  extracted/          decoded images
  data/               frames.json, sequences.json
  run_cycle/          timeline.png, pivot.png, mask_demo.png, run_cycle.json
  port/               CMake build tree; port/data/ holds seqtable.bin
```

The original checkout stays outside this repository and is never written to.

## Milestones

| # | Deliverable | Test | Status |
| --- | --- | --- | --- |
| 1 | Verified inventory; image-table decoder; `manifest.json`; Phase 1 docs (first pass) | 14 pytest cases; contact sheet inspected | **done** |
| 2 | `FRAMEDEF.S` and `SEQTABLE.S` assembled and decoded to JSON: frames, swords, sequences, per-tick dx/dy, traces; `docs/animation-data.md` | 51 pytest cases; 114 entry points resolve; all labels and branches aligned; run cycle sums to 26; every referenced image exists | **done** |
| 3 | Run frames drawn at their real positions by a byte-level blitter model (mask and mirror modes) with ports of the position routines; timeline and pivot sheets; `docs/run-cycle-analysis.md` | 90 pytest cases; blitter tables equal `HRTABLES.S` byte for byte; right-facing run is an exact mirror; supporting foot stays within 2 units per stance | **done** |
| 4 | First C17 code: `ADDCHARX`, `JUMPSEQ`, `GETSEQ`, `ANIMCHAR` in `modern-port/src/core`; CMake build that generates `seqtable.bin` from the user's checkout; `docs/port/sequence-interpreter.md` | 4 CTest tests: synthetic unit tests, loader tests, original-table tests, and 45,600 ticks compared C against Python; GCC and Clang with `-Werror`, ASan/UBSan, valgrind | **done** |
| 5 | Level blueprint parser and a static room render to PNG (needs `BGDATA.S`, `FRAMEADV.S` analysis) | Compare against trusted screenshots | proposed next |
| 6 | Sound: cycle-counted `tone` simulator → WAV + JSON; `docs/audio-analysis.md` | Frequencies checked by hand for two effects | |
| 7 | SDL3 shell: window, fixed timestep, nearest-neighbour scaling, plays the run cycle with keyboard input | Manual + deterministic headless trace | |
| 8 | Player control and physics: `GenCtrl` standing/running/turning/jumping, `gravity`, `checkfloor` | Input replay → state trace fixtures | |
| 9 | Collision, then TROBs/MOBs, then opponents and sword fighting, one subsystem per milestone | Replay traces per subsystem | |
| 10 | Blitter fidelity: masks, mirroring, colour model | Pixel comparison with emulator captures | |
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

**D5. Test oracle.**
*Default:* hand-derived expectations from reading the source, plus emulator
comparison when a disk image is available.
*Alternative (recommended if it works, see unknown P1):* assemble the original
modules with a Merlin-compatible assembler and run individual routines in a
6502 simulator, giving exact reference outputs for every ported routine.
