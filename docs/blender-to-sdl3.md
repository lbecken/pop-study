# From Blender to the reconstruction: a pipeline for replacement sprites

Milestone 12 (optional). A proposal for how newly made character art could
be used with the port in place of the original's, and what such art has to
supply to work. **Nothing in this document has been tried**: there is no
Blender in this environment, and no replacement art exists. It is derived
from what the earlier milestones established about how the original's
frames are stored, placed and timed; each requirement cites where that was
established. Read it as a specification to test, not as a tested recipe.

## 1. Why one would

The original artwork cannot be redistributed with the port (see
source-inventory.md, "Licensing": a public source release is not an
open-source licence). The port therefore reads images from the user's own
checkout. Replacement art would let the port be shown, or shipped, without
any of the original's.

Two rules, from the project's brief, shape everything below:

- **Original and new assets are never mixed without labels.** A set of
  replacement tables lives in its own directory with a provenance file
  (section 6); the port loads either the original's directory or a
  replacement directory, never some of each silently.
- **The game rules are not changed to suit new art.** New art has to fit
  the numbers the rules already use.

## 2. What a character frame is, to the game

Established in animation-data.md, run-cycle-analysis.md and
port/frame-drawing.md; all **confirmed** there against the source and, for
the drawing, against the original code.

| Property | Where it lives | Consequence for new art |
| --- | --- | --- |
| A frame is one 1-bit picture, 7 dots to the byte; the run's frames are 2 to 5 bytes wide and 38 to 41 lines high | image tables `IMG.CHTAB1`–`5` (graphics-formats.md) | Art is delivered as 1-bit bitmaps of exactly the original frame's size in bytes and lines. Sizes matter to the rules: see below |
| Which picture a frame number shows | frame table (`FRAMEDEF.S`): table and image number per frame | The replacement must provide the same image *numbers* in the same *tables*; the frame table is game data and stays |
| Where it is drawn | `Fdx`, `Fdy` per frame; the image's bottom-left corner (facing left) is the origin; an "odd dot" bit | The picture must be composed inside its box so that the feet are where the original's are: section 3 |
| The supporting foot | `foot` per frame (5 bits of `Fcheck`), in 140-wide units from the leading edge | The foot on the ground must be at that x within the picture, or feet slide and the character stands beside ledges |
| The mirror image | made by the blitter, exactly | Only left-facing art is needed |
| The outline | computed at draw time from the picture: one dot left and right of every lit dot, within its byte | Art needs no mask. Thin single-dot features get a black halo; design with it |
| Colour | none stored: a dot's colour is its column's parity (port/presentation.md) | "White" needs two lit dots side by side; alternate dots give a solid colour. The same drawing shifted by one dot changes colour, which the game uses for guards |
| Size, as the rules see it | `GETEDGES` asks for the image's width and height and marks blocks, crops and collides by them (port/frame-drawing.md §6) | Width in bytes and height in lines per image must equal the original's, or collisions and redraw marks change |
| Timing | one frame per game tick, order and root motion from the sequence table | No in-betweens. A 24 fps render must be *picked from*, not resampled into, the original's tick list |

## 3. Matching the geometry

For each frame the replacement needs these numbers, all of which the
project's extraction already produces (`frames.json`, `sequences.json`,
`run_cycle.json`; animation-data.md, tools in `tools/extract_assets`):

- the box: `width_bytes × 7` dots by `height` lines;
- the foot column inside the box: `foot × 2` dots from the leading (left)
  edge, give or take the odd-dot bit;
- the ground line: the bottom row of the box, shifted by `Fdy`;
- for sequences, the root displacement per tick (`chx`), which is chosen in
  the original so that the supporting foot stays within 1–2 units during a
  stance (run-cycle-analysis.md, "Foot contacts").

Proposed set-up in Blender, so that renders land in those boxes without
hand work:

1. An orthographic camera, side view, with a pixel grid of 280 × 192 for
   the 140 × 192-unit playfield; pixel aspect is not square on the real
   machine (a dot is narrower than a line is high), so model the character
   to look right at roughly 0.9 : 1 and judge in the port, not in the
   viewport.
2. A ground plane at the floor line and a vertical guide at the foot
   column. For each frame, a per-frame camera offset (driven from the
   exported numbers) puts the frame's box around the character with the
   foot guide at `foot`.
3. The rig is posed *to* the original's poses frame by frame (the
   extracted pictures as background images). This is tracing, deliberately:
   the rules read the animation (which frames accept input, which have a
   foot down), so the poses carry game meaning. A freely animated cycle
   would have to be re-timed into the same frame count and stance pattern,
   with unequal left and right steps of about 14 and 12 units for the run.
4. Render each frame to a 1-bit image: flat emission shading, no
   anti-aliasing, a threshold; then check lone dots and pairs against the
   colour rule (a white figure needs even widths of lit dots).

The steps most likely to need iteration are 3 (readable poses at 40 lines)
and 4 (colour fringes).

## 4. From renders to something the port loads

The simplest target is the original's own container: an image table with
the same count, the same per-image sizes and new pixels. The port and every
tool then work unchanged.

Needed, not yet written:

- `make_imgtable`: a directory of 1-bit images named by image number, plus
  the sizes expected, → a table in the `IMG.*` format (count, pointers from
  `$6000`, per image width, height and rows bottom-up with bit 7 set).
  Its test is ready-made: decoding an original table and encoding it again
  must give the original bytes up to the table's end.
- an option of `pop_app` / `pop_sdl` to take the image directory separately
  from the levels (`--images <dir>`), refusing a directory that has no
  provenance file.

## 5. Checking a replacement set

Because the game's tests compare page bytes with the original, they cannot
pass with other pictures and are not the right tests. What can be checked
mechanically:

| Check | Why |
| --- | --- |
| Every image has the original's width and height | the rules use them |
| The game state after every frame of the demo and of the traces is the same with either set (`replay_demo.py`-style, state only, no pages) | art must not change behaviour; this would catch a size or table mistake |
| Per stance, the foot column moves no more than the original's 2 units (the run-cycle sheet tool, run on the new set) | feet must not slide |
| No lit dot outside the box; lone dots listed for review | colour and outline surprises |

And by eye: contact sheets and the demo rendered by `render_demo.py` from
the replacement directory.

## 6. Provenance

A replacement directory carries a `PROVENANCE.txt` naming, for every table:
who made it, with what, when, under what licence, and from which reference
(for traced poses: "posed against frames extracted from the user's own
copy of …"). Tracing poses from the original pictures makes the result a
derived work of them in some sense; whether that is acceptable for a given
use is a legal question this document does not answer, and is a reason to
keep the label honest.

Renders, screenshots and videos made with a replacement set should say so.

## 7. Background art, sound

Out of scope here. Background pieces are tied to the piece tables
(port/redraw.md) by position and size in the same way and could follow the
same route. Sound effects are speaker toggle lists (audio-analysis.md), not
samples; replacing them is a different problem.
