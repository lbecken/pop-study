/* pop_present.h - from page bytes to a picture.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Everything before this file ends in bytes: two hi-res pages and a few
 * soft switches, compared byte for byte with what the original code
 * leaves.  This file turns them into pixels, and that step is NOT taken
 * from the Prince of Persia source: what a monitor shows for a hi-res
 * byte is a property of the Apple II's video circuit and of the NTSC
 * signal.  The rules below are the usual simplified description of it
 * (docs/port/presentation.md gives them and their limits).  They are a
 * presentation filter (decision D3); nothing has been compared with a
 * real machine or an emulator.
 */
#ifndef POP_PRESENT_H
#define POP_PRESENT_H

#include "pop_gfx.h"

enum { POP_SCREEN_W = 280, POP_SCREEN_H = 192 };

typedef enum pop_present_mode {
    POP_PRESENT_MONO,            /* a lit dot is white: the page as a monochrome monitor shows it */
    POP_PRESENT_COLOUR           /* artifact colours */
} pop_present_mode;

/* The 280 dots of screen line y of a hi-res page (1 or 2), 0 or 1 each,
 * left to right; `high`, if not NULL, gets bit 7 of each of the 40 bytes.
 * This much is exact. */
void pop_present_dots(const pop_gfx *v, int page, int y, uint8_t dots[POP_SCREEN_W],
                      uint8_t high[POP_HR_COLS]);

/* What the monitor is shown for the display state in v (the page selected,
 * hi-res or lo-res, graphics or text): 280 x 192 pixels, three bytes each
 * (red, green, blue), top line first.  Returns 0, or 1 if the state is one
 * this model has no picture for (text other than a screen of spaces),
 * which is then black. */
int pop_present(const pop_gfx *v, pop_present_mode mode, uint8_t *rgb);

#endif /* POP_PRESENT_H */
