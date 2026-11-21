# Port notes: from page bytes to a picture

Twelfth C17 subsystem (milestone 10d): what a monitor would show for the
bytes the drawing code leaves. Decision D3 (milestones.md): render the
hi-res page as dots, exactly, and apply a simple artifact-colour model as a
presentation filter, with monochrome as an option.

C sources: `modern-port/src/video/pop_present.h`, `pop_present.c`;
`modern-port/tools/pop_png.h` (a PNG writer without a library);
`modern-port/tools/render_demo.py`.

**This is the one part of the port that is not derived from the Prince of
Persia source.** Up to here everything is bytes that were compared with
what the original code leaves in memory (frame-drawing.md). How those
bytes look is decided by the Apple II's video circuit and the NTSC signal,
which the game's source does not describe and which nothing in this
project has measured. Sections 1 and 2 are exact. Section 3 is a model,
written from the standard descriptions of Apple II hi-res graphics as I
know them, with no reference at hand to cite line by line and no emulator
or hardware to compare with: treat every statement in it as **inferred**.

## 1. Which bytes are on the screen

The game leaves four soft switches and one byte (frame-drawing.md §5;
`pop_gfx.sw_page2`, `sw_hires`, `sw_text`, `scrncolor`):

| State | Shown | In play |
| --- | --- | --- |
| graphics, hi-res | hi-res page 1 or 2 | always, except the two cases below |
| text | the text page, which holds one value throughout | the blank screen while a new screen is built: spaces |
| graphics, lo-res | the same page read as 40 × 48 colour blocks | a flash: one colour all over |

Within one pass of the main loop the state can change more than once
(`pop_gfx.swlog`); `pop_present` shows the state as it is when called.

## 2. Dots (exact)

A hi-res page is 192 lines of 40 bytes, at addresses that are not in line
order (blitter.md §2). Each byte holds seven dots, bit 0 leftmost; bit 7 is
not a dot. `pop_present_dots` returns the 280 dots of a line and the
forty bit-7 values. The flip potion needs nothing here: it swaps the
blitter's table of line addresses, the picture in memory really is upside
down, and the screen shows memory as it is.

`POP_PRESENT_MONO` paints a lit dot white: the page as a monochrome monitor
shows it, apart from the half-dot shift of section 4.

## 3. Colours (a model)

On a colour monitor a hi-res dot has no colour of its own; colour comes
from where a dot is on the line. The rules `pop_present` applies, for each
line separately:

| Dots | Pixel |
| --- | --- |
| lit, and the dot to its left or right is lit | white |
| lit, both neighbours dark | a colour: by the parity of its column and by bit 7 of its byte (table below) |
| dark, both neighbours lit | the colour of the dot to its left: the colour runs through, so that a row of alternate dots is a solid colour |
| dark otherwise | black |

| Bit 7 of the byte | even column | odd column |
| --- | --- | --- |
| 0 | violet | green |
| 1 | blue | orange |

RGB values are conventional approximations (`pop_present.c`), as are the
sixteen lo-res colours used for a flash.

What can be said for the model: the pictures it gives of the demo look
like the game (blue-grey dungeon stone, orange flames and strength units,
a white kid with coloured edges), and the game's own source agrees with it
where it mentions colour at all: the impact star's x is made even or odd
"kid red, opps blue" (frame-drawing.md §4), and a guard of the other colour
is the same image one dot to the right.

## 4. What the model leaves out

- **The half-dot shift.** The dots of a byte with bit 7 set are displayed
  half a dot later than those of a byte without. The picture here is 280
  pixels wide and ignores it; a faithful one needs 560.
- **Edges.** Where a white area ends, a real screen shows a fringe of
  colour that depends on the parity of the last dot; the model shows it
  only when a single dot is left over.
- **Colour changes inside a run.** A dark dot between two lit ones takes
  the left one's colour even if the right one is in a byte with the other
  bit 7.
- **The signal.** No blur, no bleeding between lines, no difference between
  monitors; a real composite monitor is softer.
- **Text** other than a blank screen, which the game does not show during
  play, and the fourth switch (`$C05E`/`$C05F`), which the game sets from
  `vibes` in `PageFlip`: what it does on the machines the game ran on is
  **not yet verified** (unknown G19).
- **Time.** A picture is one state. How long each state is on the screen
  (a flash, a blank screen, the two pages in turn) is a matter for the
  shell (milestone 7; unknown T1 has what is known about frame lengths).

Next step for all of these: compare frames with an emulator's composite
output, which this environment does not have.

## 5. Looking at it

```
python3 modern-port/tools/render_demo.py build/port/pop_worldrun build/port/data \
        <original checkout> <out dir> [every=8] [mono|colour] [scale=2]
```

plays the demo level in the port alone (no original code runs) and writes
every eighth frame and a contact sheet as PNG files. The pictures are the
original game's artwork drawn from your own checkout; they are generated,
not part of this repository. With the original source configured, CTest
does this once into `<build>/demo_frames` (`render_demo`).

## 6. Checks

| Check | What it shows |
| --- | --- |
| `tests/test_present.c` (`present_unit`) | The dot extraction on made-up bytes (exact), and that the colour rules are the ones written above: it pins the model down, it does not validate it |
| `tests/replay_frames.py` | That the bytes being shown are the original's (frame-drawing.md §9) |
| by eye | Frames of the demo written by `render_demo.py` look like the game. Weak evidence, and the only kind there is for section 3 |
