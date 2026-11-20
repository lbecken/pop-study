/* pop_hires.h - the blitter: images onto an Apple II hi-res page.
 *
 * New code for the pop-study reconstruction; no original code or data.
 * The images themselves are original data and are not in this repository:
 * the caller supplies them from the user's own checkout.
 *
 * Ported routines (01 POP Source/Source/HIRES.S, original commit 25243550);
 * each is compared with the original routine, run in a two-bank 6502
 * simulator, byte for byte over both hi-res pages (tests/fuzz_hires.py).
 *
 * (line each routine starts at)
 *   CLS 206          PREPREP 311      CROP 375         LAYRSAVE 542
 *   LAY 658          LayGen 681       LayMask 833      LayXOR 1008
 *   MLAY 1186        MLayGen 1202     MLayMask 1356    MLayXOR 1541
 *   PEEL 1721        FASTLAY 1740     fastlaySTA 1825  FASTMASK 1892
 *   FASTBLACK 2005   COPYSCRN 2060    INVERTY 2098
 *
 * The page is kept as the bytes of memory it occupies, in the interleaved
 * Apple II order, because the original computes addresses and a write that
 * runs past the end of a screen line lands on another line (or in a "screen
 * hole").  Bit 7 of a byte is not a dot; bits 0-6 are, left to right.
 *
 * See docs/port/blitter.md.
 */
#ifndef POP_HIRES_H
#define POP_HIRES_H

#include <stddef.h>
#include <stdint.h>

enum { POP_HR_COLS = 40, POP_HR_ROWS = 192, POP_HR_PAGE = 0x2000 };

/* OPACITY, HIRES.S 74-80.  Bit 7 added to any of them mirrors the image. */
enum {
    POP_OP_AND = 0, POP_OP_ORA = 1, POP_OP_STA = 2, POP_OP_EOR = 3, POP_OP_MASK = 4,
    POP_OP_VISMASK = 5, POP_OP_MIRROR = 0x80
};

/* Memory the blitter writes to, at its original main-memory addresses:
 * hi-res pages 1 and 2, and the two save-under ("peel") buffers. */
enum {
    POP_VRAM_BASE = 0x2000, POP_VRAM_SIZE = 0x4000,          /* $2000-$5FFF */
    POP_PEEL_BASE = 0xD000, POP_PEEL_SIZE = 0x1000,          /* $D000-$DFFF */
    POP_PEELBUF1 = 0xD000, POP_PEELBUF2 = 0xD800             /* EQ.S 12-13 */
};

/* An image: width in bytes, height in lines, then width x height bytes,
 * bottom line first, each with bit 7 set.  `size` is how many bytes may be
 * read from `bytes` (header included). */
typedef struct pop_image {
    const uint8_t *bytes;
    size_t size;
} pop_image;

typedef struct pop_hires {
    uint8_t vram[POP_VRAM_SIZE];
    uint8_t peelmem[POP_PEEL_SIZE];
    uint8_t ylo[POP_HR_ROWS], yhi[POP_HR_ROWS];      /* line addresses; INVERTY swaps them */

    /* The parameters, HIRES.S 148-170 (zero page $00-$17). */
    uint8_t PAGE;                        /* $00 page 1, $20 page 2 */
    uint8_t XCO, YCO, OFFSET;            /* byte column, bottom line, 0-6 dots right */
    uint8_t OPACITY;
    uint8_t LEFTCUT, RIGHTCUT, TOPCUT, BOTCUT;       /* usable area: 0, 40, 0, 192 = all */
    uint16_t PEELBUF;                    /* first free byte of the peel buffer */
    uint16_t PEELIMG;                    /* what LAYRSAVE saved; high byte 0 = nothing */
    uint8_t PEELXCO, PEELYCO;

    /* Set when the original would read or write outside the memory above,
     * or outside the image it was given. */
    uint8_t unmodelled;
} pop_hires;

void pop_hires_init(pop_hires *h);       /* pages zeroed, line table upright, full screen */

void pop_hr_cls(pop_hires *h);           /* CLS: fill the page PAGE selects with $80 */

/* LAY: draw an image by OPACITY, shifted, mirrored and cropped as asked. */
void pop_hr_lay(pop_hires *h, pop_image im);

/* LAYRSAVE: save what is under the area LAY would cover (one byte wider)
 * to the peel buffer.  Sets PEELIMG, PEELXCO, PEELYCO, advances PEELBUF. */
void pop_hr_layrsave(pop_hires *h, pop_image im);

/* PEEL: put a saved area back.  In: XCO, YCO, and the address LAYRSAVE
 * returned in PEELIMG. */
void pop_hr_peel(pop_hires *h, uint16_t peelimg);

/* FASTLAY: byte-aligned, no cropping at the sides, OPACITY 0-3. */
void pop_hr_fastlay(pop_hires *h, pop_image im);
void pop_hr_fastmask(pop_hires *h, pop_image im);            /* FASTMASK */

/* FASTBLACK: fill width x height bytes with `colour`. */
void pop_hr_fastblack(pop_hires *h, uint8_t width, uint8_t height, uint8_t colour);

/* COPYSCRN: copy one page to the other; arguments are the high bytes of
 * the page addresses ($20 or $40). */
void pop_hr_copyscrn(pop_hires *h, uint8_t from, uint8_t to);

void pop_hr_inverty(pop_hires *h);       /* INVERTY: turn the line table upside down */

/* The 40 bytes of screen line y (0 = top) of page 1 or 2, upright. */
const uint8_t *pop_hr_row(const pop_hires *h, int page, int y);

#endif /* POP_HIRES_H */
