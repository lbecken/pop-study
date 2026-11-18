# Run-cycle analysis

Status: milestone 3. The kid's run is reconstructed tick by tick from the
original data and drawn by a byte-level model of the original blitter.

Citations are `FILE.S:lines` in `01 POP Source/Source` of the original
repository at commit `25243550`. **Confirmed** = read in the source or computed
from the original data by the tools in this repository; **observed** = read off
the rendered pictures by eye; **inferred** = deduction; **derived** = arithmetic
on confirmed numbers under a stated assumption.

## Reproducing the pictures

```bash
python3 tools/extract_assets/render_run_cycle.py --source ../pop-original --out build/run_cycle
```

Writes to `build/run_cycle/` (git-ignored, derived from original art):

| File | Shows |
| --- | --- |
| `timeline.png` | one band per game tick, all bands showing the same window of the screen, so sideways drift is real movement |
| `pivot.png` | the same frames in cells aligned on the foot position |
| `mask_demo.png` | one frame over a filled page: where the mask clears the background |
| `run_cycle.json` | every number in the tables below |

Options: `--sequence runstop|runjump|runturn|…`, `--face right`, `--start-x`,
`--ticks`, `--scale`.

Markers: red = base x (the supporting foot), blue = `CharX`, green = the line
under `CharY`, grey = block edges.

What is and is not modelled: the sequence table moves the character
(`chx`/`chy`), and each frame is positioned and drawn by ports of the original
routines. Player input, collisions, gravity and screen edges are not modelled,
so this is the run on an open floor.

## From game coordinates to the screen

These routines were ported literally, 8-bit wraparound included
(`tools/extract_assets/pop_charpos.py`, `pop_hires.py`).

| Step | Routine | Rule (**confirmed**) |
| --- | --- | --- |
| direction | `ADDCHARX`, `CTRLSUBS.S:353-363` | forward = −x when `CharFace` is `$FF` (left), +x when `$00` (right) |
| draw x | `SETUPCHAR`, `CTRLSUBS.S:804-840` | `FCharX = 2 × (CharX ± Fdx − 58)`, plus 1 when bit 7 of `Fcheck` equals bit 7 of `CharFace` |
| draw y | same | `FCharY = CharY + Fdy`; it is the image's **bottom** line |
| byte/offset | `CVTX`, `GRAFIX.S:909-957` | column = `FCharX div 7`, shift = `FCharX mod 7` (checked against `ByteTable`/`OffsetTable`) |
| draw mode | `DrawNormal`, `GAMEBG.S:432-437` | kid is drawn in mask mode; mirror flag set when facing right (`GRAFIX.S:362-366`) |
| facing left | `LayMask`, `HIRES.S:833-998` | image occupies dots `[FCharX, FCharX + 7×width)` |
| facing right | `MLayMask`, `HIRES.S:1356-1533` | column −= width; bytes read right-to-left and bit-reversed: image occupies `[FCharX − 7×width, FCharX)` |
| foot | `GETBASEX`, `CTRLSUBS.S:329-340` | base x = `CharX ± (Fdx − foot)`: `foot` units in from the image's leading edge |
| floor | `FloorY`, `TABLES.S` | a standing character has `CharY` = 55, 118 or 181 for the three rows |

Consequences:

- **`CharX` is not the foot.** It is the image's leading edge (before `Fdx`).
  The foot is `foot` units inside the image. What stays still on the floor is
  base x, not `CharX`.
- **Right-facing is an exact mirror.** The "odd dot" rule flips with the facing,
  and the mirrored blitter anchors on the opposite edge. Rendering the run
  facing right from `CharX' = 255 − CharX` gives, dot for dot, the mirror image
  of the left-facing run (**confirmed** by test for all 24 ticks).
- **The mask is a one-dot outline, horizontally only.** Each image dot clears
  itself and its left and right neighbours on the background, within its own
  image byte; nothing is cleared above or below, and the halo does not extend
  into the neighbouring image byte (`MASKTAB`, `HRTABLES.S:219-244`). The mask is computed from the
  image at draw time; no mask data is stored.
- **Blocks are 14 units wide**; block 0 starts at x = 58, the screen's left
  edge (`BlockEdge`, `TABLES.S`).

The five lookup tables the blitter uses are regenerated here from closed-form
rules and match the assembled `HRTABLES.S` byte for byte (**confirmed** by
test), as do the row-address tables.

## The run, tick by tick

Facing left on the bottom row, starting from a stand at `CharX` = 168.
From `run_cycle.json`.

| Tick | Frame | Author's name | `chx` | `CharX` | `foot` | base x | Image w×h (bytes × lines) | Sound |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 15 | stand | – | 168 | 3 | 171 | 2×41 | |
| 1 | 1 | run-4 | 0 | 168 | 4 | 171 | 2×41 | |
| 2 | 2 | run-5 | 0 | 168 | 4 | 171 | 3×41 | |
| 3 | 3 | run-6 | 0 | 168 | 7 | 172 | 3×40 | |
| 4 | 4 | run-7 | 0 | 168 | 8 | 172 | 3×39 | |
| 5 | 5 | run-8 | 8 | 160 | 6 | 166 | 4×38 | |
| 6 | 6 | run-9 | 3 | 157 | 9 | 166 | 4×38 | |
| 7 | 7 | run-10 | 3 | 154 | 10 | 164 | 4×39 | |
| 8 | 8 | run-11 | 5 | 149 | 5 | 154 | 5×40 | |
| 9 | 9 | run-12 | 1 | 148 | 4 | 152 | 5×40 | footstep |
| 10 | 10 | run-13 | 2 | 146 | 7 | 153 | 4×39 | |
| 11 | 11 | run-14 | 4 | 142 | 11 | 153 | 4×38 | |
| 12 | 12 | run-15 | 5 | 137 | 3 | 140 | 5×40 | |
| 13 | 13 | run-16 | 2 | 135 | 3 | 138 | 5×40 | footstep |
| 14 | 14 | run-17 | 3 | 132 | 7 | 139 | 3×39 | |
| 15 | 7 | run-10 | 4 | 128 | 10 | 138 | 4×39 | |
| 16–23 | 8…14, 7 | | as 8–15 | 123…102 | | 128, 126, 127, 127, 114, 112, 113, 112 | | 17, 21 |

Frames 1–4 need `Fdx` = 1, 1, 3, 4 to keep the picture in place while `CharX`
does not move; every later run frame has `Fdx` = 0 (`FRAMEDEF.S:23-36`). `Fdy`
is 0 throughout, so the bottom image row is always on line 181.

## Findings

### Frame count and timing

- **Confirmed:** start-up is 6 frames shown once (1–6); the cycle is 8 frames
  (7–14). Every frame is shown for exactly one game tick; the table has no
  hold or repeat in the run.
- **Derived**, assuming the game's nominal 725 ticks per minute
  (`SPECIALK.S:68`; real tick length is not yet measured, unknown T1): one
  tick ≈ 83 ms, one cycle ≈ 0.66 s, about 181 steps per minute.

### Foot contacts

The base x column is the result worth having. It moves in plateaus:

| Ticks | Frames | Base x | Spread |
| --- | --- | --- | --- |
| 0–4 | 15, 1–4 | 171, 171, 171, 172, 172 | 1 |
| 5–7 | 5, 6, 7 | 166, 166, 164 | 2 |
| 8–11 | 8, 9, 10, 11 | 154, 152, 153, 153 | 2 |
| 12–15 | 12, 13, 14, 7 | 140, 138, 139, 138 | 2 |
| 16–19 | 8, 9, 10, 11 | 128, 126, 127, 127 | 2 |
| 20–23 | 12, 13, 14, 7 | 114, 112, 113, 112 | 2 |

- **Confirmed:** within the cycle, base x holds within 2 units (4 hi-res dots)
  for four consecutive ticks, then jumps to a new plateau. `CharX` meanwhile
  moves every tick. So the per-frame `chx` values are not an independent speed
  curve: they are what is needed to keep the supporting foot fixed on the floor
  given each frame's `foot` offset.
- **Inferred:** each plateau is one stance (one foot on the ground). The cycle
  is two stances of four frames: frames 8–11 on one foot, frames 12–14 and 7 on
  the other.
- **Confirmed:** the two steps are unequal. Plateau means are 153.0 → 138.75 →
  127.0 → 112.75: steps of 14.25 and 11.75 units, summing to the 26 units per
  cycle. A cycle is therefore 1.86 blocks, not a whole number, so the same
  frame does not recur at the same place within a block.
- **Confirmed:** the residual foot slide inside a stance is at most 2 units,
  and is identical in every lap (the pattern is +0, −2, −1, −1 for the first
  stance and +0, −2, −1, −2 for the second).
- **Confirmed:** every run frame has the "floor check" bit set
  (`FRAMEDEF.S:23-36`), so the game treats the runner as on the ground on every
  tick; there is no airborne frame in the run as far as the rules are concerned.
  Compare the running jump below, where frames 39–43 have the bit clear.

### Footsteps

- **Confirmed:** the footstep sound is queued on the ticks showing frames 9 and
  13 (`SEQTABLE.S:170, 174`), which are the *second* tick of each stance, one
  tick after the contact frames 8 and 12, and four ticks apart.
- **Confirmed:** the same `tap` also sets `alertguard` (`COLL.S:1125-1127`), so
  footsteps are what guards hear.

### Poses

**Observed** in `pivot.png` (character facing left, so forward is left):

| Frame | Pose |
| --- | --- |
| 15 | standing upright |
| 1, 2 | upright, weight starting forward |
| 3, 4 | torso tips forward, knees flex |
| 5, 6 | drive: torso well forward, rear leg extending behind |
| 7 | legs gathered under the body |
| 8 | contact: supporting foot under the body, rear leg at full stretch behind; widest pose |
| 9 | weight over the supporting foot, rear lower leg lifted |
| 10 | passing: rear leg folded up behind, body over the foot |
| 11 | legs crossing, compact |
| 12 | contact on the other foot, long stretch; counterpart of 8 |
| 13 | counterpart of 9 |
| 14 | passing; counterpart of 10, the narrowest cycle frame |
| 7 | counterpart of 11 |

So the cycle reads as two half-cycles with matching phases:
contact (8, 12) → loading (9, 13) → passing (10, 14) → gather (11, 7).

### Silhouette

**Confirmed** from the image tables:

- Width follows the stride: 5 bytes (35 dots) at contact and loading, 4 bytes
  at gather, down to 3 bytes (21 dots) at one passing frame. The standing
  figure is 2 bytes (14 dots).
- Image height varies only between 38 and 40 lines in the cycle (41 standing).
  With the bottom row fixed on line 181, the top of the image moves over 3
  lines: lowest at frame 11 (38 lines), highest at the contact and loading
  frames (40).
- The two half-cycles are not copies: frame 10 is 4 bytes wide and its
  counterpart 14 is 3; frame 11 is 38 lines and its counterpart 7 is 39. The
  left and right steps were drawn (or traced) separately.

### Displacement

- **Confirmed:** 14 units during start-up, then 26 per cycle.
- **Derived** at the nominal tick rate: 26 units per 0.66 s ≈ 39 units/s ≈ 2.8
  blocks/s; the 140-unit screen is crossed in about 3.6 s.
- **Confirmed:** `CharX` does not move during frames 1–4. For the first four
  ticks after pushing forward, the kid leans but does not travel; the first
  displacement (8 units, the largest in the run) comes with frame 5.

### Leaving the run

| Exit | When allowed | What happens (**confirmed**) |
| --- | --- | --- |
| Stop (stick centred) | only on frames 7 and 11 (`CTRL.S:1382-1392`) | `runstop`: 8 frames (53–56, 49–52), `CharX` moves 2, 7, 0, 2, −2 = 9 units net, two footstep taps, then stand |
| Turn (stick back) | any run frame (`CTRL.S:1396-1401`) | `runturn`: 13 frames, 7 units of slide, ends in the run cycle facing the other way |
| Jump (stick up, fresh press) | frame 7 or later (`CTRL.S:1752-1755`) | `runjump`: 11 frames, 67 units, then rejoins the cycle at frame 7 |
| Dive roll (fresh down) | any run frame (`CTRL.S:1359-1370`) | `rdiveroll` |

- Frames 7 and 11 are the two "gather" frames, the last tick of each stance.
  Stopping is thus only possible at the end of a step; a centred stick on any
  other frame is ignored until the run reaches one (**confirmed** by the code;
  the *reason* is **inferred**: `runstop` begins from a matching pose).
- Stopping costs 9 units of travel, about two-thirds of a block. `runstop`
  frames 53–56 are the same four images that begin `runturn`.
- In the running jump, base x shows a first stance of 3 ticks (173, 173, 173),
  a second of 3 ticks (157, 156, 158), then a flight in which `chy` lifts and
  lowers the figure (−3, −9, −2, +11, +3 on ticks 7–11; net 0) and frames
  39–43 have the floor-check bit clear, and a landing at base x ≈ 113. Take-off to landing is 44 units, a
  little over three blocks. `DoRunjump` (`CTRL.S:1752` on) adjusts `CharX`
  before starting so that the take-off lands near a ledge; that adjustment is
  **not yet analysed**.

## How this differs from skeletal animation

What the data shows about the original technique (**confirmed** unless noted):

1. **A frame is a whole picture.** There are no limbs, joints or bones in the
   data. A pose exists only as one bitmap; nothing can be blended, retargeted
   or re-timed, and the run has exactly the 14 pictures it was given.
2. **Motion is authored per frame, by hand.** Root movement is a list of signed
   byte displacements in the sequence table. Foot planting is achieved by
   choosing each `chx` to cancel the change in that frame's `foot` offset, not
   by inverse kinematics. The 1–2 unit slide is the leftover of doing that in
   whole units.
3. **The pivot is per frame.** Each frame carries its own anchor (`foot`) and
   draw offset (`Fdx`, `Fdy`); the image's own corner is the draw origin. A
   skeletal system has one root transform and derives everything else.
4. **Game rules read the animation.** Control code branches on the frame
   number (`CharPosn`) to decide what input is accepted; the floor check, the
   collision width ("thin") and the footstep sound are per-frame flags. In a
   modern engine these would be state-machine conditions and animation events
   layered on top of clips; here the clip *is* the state.
5. **Fixed timing.** One picture per game tick, always. There is no
   interpolation and no playback rate; speed is distance per frame.
6. **Mirroring is free, asymmetry is real.** One facing is stored and the other
   is produced by the blitter, exactly. But within the cycle the left and right
   steps are different drawings with different step lengths, which a mirrored
   or procedurally generated walk would not have.

Not from this repository: the author has described elsewhere (his published
journals) that the animation was traced from filmed footage. Nothing in the
source states this, though the working files under `04 Support/DRAZ` have names
such as `PAC.RUN`, `PAC.GUY1` and `PAC.CLIMBUP` that would fit such source
sheets (**not yet verified**, their format is undecoded: unknown G5).

Implications for the port and for any replacement art:

- Reproduce the tables, do not resample them: frame order, per-tick `chx`,
  `foot`, `Fdx`, the odd-dot rule and the mirroring rule together determine
  where every dot lands.
- Replacement sprites (for example from the optional Blender pipeline) must
  supply, per frame, the same metadata the original has: an anchor at the
  supporting foot, and a root displacement chosen so that anchor stays fixed
  during a stance. Rendering a skeletal run with constant root velocity and
  dropping the frames in would make the feet slide.
- The 8-frame cycle with unequal steps of about 14 and 12 units is part of the
  look. A symmetric cycle would change how the run lines up with block edges.

## Limits of this analysis

- Tick duration is nominal, not measured (unknown T1).
- Pose descriptions are by eye from 1-bit pictures.
- No colour: dots are shown white. On an Apple II the same bytes produce
  artifact colours (unknown G3).
- Cropping at screen edges, the save-under ("peel") buffers and the other draw
  modes (`LayXOR` for the shadow, the shifted mode for guards) are not modelled.
- Nothing has been compared against the running game; there is no disk image in
  the repository. Internal consistency is strong (tables match byte for byte,
  mirror symmetry is exact, feet land on the floor line), but that is not the
  same as an emulator comparison.
