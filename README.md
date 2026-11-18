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
| `docs/unknowns.md` | Open questions, each with the next inspection step |
| `docs/milestones.md` | Milestone plan, directory layout, decisions awaiting input |
| `assets/manifest.json` | Hashed inventory of all 582 original files (metadata only) |
| `tools/extract_assets/` | Image-table decoder, Merlin data assembler, frame/sequence extractor, blitter model, run-cycle renderer, manifest generator (Python 3, stdlib only) |
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
python3 tools/extract_assets/make_manifest.py --source ../pop-original --out assets/manifest.json
```

```bash
POP_SOURCE_DIR=../pop-original python3 -m pytest tests -q
```

Without `POP_SOURCE_DIR` the nine integration tests are skipped and only the
synthetic-data unit tests run. `build/` is git-ignored.

## Status

Done: milestone 1 (inventory, image-table extraction), milestone 2 (frame
definitions and animation sequences extracted and cross-checked) and milestone
3 (run cycle drawn at its real screen positions and analysed). The position
arithmetic and the mask blitter exist as tested Python models; no C code has
been written yet. See `docs/milestones.md`.

## Conventions

Every statement about the original is labelled **confirmed** (read in the
source or verified by parsing data), **inferred**, or **not yet verified**, and
cites `file:line` in the original repository at the commit above.
