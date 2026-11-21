/* pop_present.c - from page bytes to a picture.  See pop_present.h. */

#include <string.h>

#include "pop_present.h"

typedef struct rgb { uint8_t r, g, b; } rgb;

/* Conventional approximations of the colours of an NTSC Apple II; not
 * measured, and no two monitors agreed. */
static const rgb BLACK = { 0, 0, 0 }, WHITE = { 255, 255, 255 };
static const rgb VIOLET = { 255, 68, 253 }, GREEN = { 20, 245, 60 };
static const rgb BLUE = { 20, 207, 253 }, ORANGE = { 255, 106, 60 };
/* the 16 lo-res colours, by number */
static const rgb LORES[16] = {
    { 0, 0, 0 }, { 227, 30, 96 }, { 96, 78, 189 }, { 255, 68, 253 },
    { 0, 163, 96 }, { 156, 156, 156 }, { 20, 207, 253 }, { 208, 195, 255 },
    { 96, 114, 3 }, { 255, 106, 60 }, { 156, 156, 156 }, { 255, 160, 208 },
    { 20, 245, 60 }, { 208, 221, 141 }, { 114, 255, 208 }, { 255, 255, 255 },
};

static void put(uint8_t *out, rgb c)
{
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
}

void pop_present_dots(const pop_gfx *v, int page, int y, uint8_t dots[POP_SCREEN_W],
                      uint8_t high[POP_HR_COLS])
{
    const uint8_t *row = pop_hr_row(&v->hr, page, y);
    int b, k;
    for (b = 0; b < POP_HR_COLS; b++) {
        for (k = 0; k < 7; k++) dots[7 * b + k] = (uint8_t)((row[b] >> k) & 1u);    /* bit 0 leftmost */
        if (high) high[b] = (uint8_t)(row[b] >> 7);
    }
}

/* The colour of a single lit dot: by the parity of its column and by bit 7
 * of the byte it is in. */
static rgb lone(int x, uint8_t highbit)
{
    if (highbit) return (x & 1) ? ORANGE : BLUE;
    return (x & 1) ? GREEN : VIOLET;
}

static void hires_line(const pop_gfx *v, int page, int y, pop_present_mode mode, uint8_t *out)
{
    uint8_t dots[POP_SCREEN_W], high[POP_HR_COLS];
    int x;
    pop_present_dots(v, page, y, dots, high);
    for (x = 0; x < POP_SCREEN_W; x++) {
        int left = x > 0 && dots[x - 1], right = x < POP_SCREEN_W - 1 && dots[x + 1];
        rgb c = BLACK;
        if (mode == POP_PRESENT_MONO) {
            if (dots[x]) c = WHITE;
        } else if (dots[x]) {
            /* two lit dots side by side are white; one alone is coloured */
            c = (left || right) ? WHITE : lone(x, high[x / 7]);
        } else if (left && right) {
            /* a dark dot between two lit ones: the colour runs through
             * (a row of alternate dots is a solid colour) */
            c = lone(x - 1, high[(x - 1) / 7]);
        }
        put(out + 3 * x, c);
    }
}

int pop_present(const pop_gfx *v, pop_present_mode mode, uint8_t *out)
{
    int x, y, bad = 0;
    if (v->sw_text) {
        /* The text page holds one value throughout.  A screen of spaces
         * is black; glyphs are not modelled. */
        memset(out, 0, (size_t)POP_SCREEN_W * POP_SCREEN_H * 3u);
        return v->scrncolor != 0xA0;
    }
    if (!v->sw_hires) {
        /* Lo-res: each text cell is two blocks, 7 dots by 4 lines, coloured
         * by the low (upper block) and high nibble of its byte. */
        for (y = 0; y < POP_SCREEN_H; y++) {
            rgb c = LORES[(y & 4) ? (v->scrncolor >> 4) : (v->scrncolor & 15)];
            if (mode == POP_PRESENT_MONO) c = (c.r | c.g | c.b) ? WHITE : BLACK;
            for (x = 0; x < POP_SCREEN_W; x++) put(out + 3 * (y * POP_SCREEN_W + x), c);
        }
        return bad;
    }
    for (y = 0; y < POP_SCREEN_H; y++) {
        hires_line(v, v->sw_page2 ? 2 : 1, y, mode, out + 3 * y * POP_SCREEN_W);
    }
    return bad;
}
