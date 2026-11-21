/* test_hires.c - the blitter on small made-up images.
 *
 * New code; no original data.  Expected bytes were worked out by hand from
 * HIRES.S and the rules of its tables (see pop_hires.h for line ranges).
 * The same routines are compared with the original blitter, byte for byte
 * over whole pages, in tests/fuzz_hires.py when the original source is
 * available.
 *
 * A hi-res byte holds 7 dots, bit 0 leftmost; bit 7 is not a dot.
 */

#include <string.h>

#include "pop_hires.h"
#include "pop_test.h"

static pop_hires h;

static pop_image image(const uint8_t *bytes, size_t size)
{
    pop_image im;
    im.bytes = bytes;
    im.size = size;
    return im;
}

#define IMAGE(name) image(name, sizeof name)

static uint8_t at(int y, int col) { return pop_hr_row(&h, 1, y)[col]; }

static void blank(void)                            /* page 1 "black": every byte $80 */
{
    pop_hires_init(&h);
    pop_hr_cls(&h);
}

static void test_page_layout(void)
{
    pop_hires_init(&h);
    CHECK_EQ(pop_hr_row(&h, 1, 0) - h.vram, 0x0000);       /* $2000 */
    CHECK_EQ(pop_hr_row(&h, 1, 1) - h.vram, 0x0400);       /* lines interleave by 8 ... */
    CHECK_EQ(pop_hr_row(&h, 1, 8) - h.vram, 0x0080);       /* ... by 64 ... */
    CHECK_EQ(pop_hr_row(&h, 1, 64) - h.vram, 0x0028);      /* ... and by thirds */
    CHECK_EQ(pop_hr_row(&h, 1, 191) - h.vram, 0x1FD0);
    CHECK_EQ(pop_hr_row(&h, 2, 0) - h.vram, 0x2000);       /* page 2 at $4000 */
    h.PAGE = 0x20;
    pop_hr_cls(&h);
    CHECK_EQ(pop_hr_row(&h, 2, 100)[39], 0x80);
    CHECK_EQ(pop_hr_row(&h, 1, 100)[39], 0x00);
    CHECK_EQ(h.unmodelled, 0);
}

static void test_store_and_shift(void)
{
    static const uint8_t dot02[] = { 1, 1, 0x85 };         /* 1 x 1: dots 0 and 2 */
    blank();
    h.XCO = 2; h.YCO = 5; h.OPACITY = POP_OP_STA;
    pop_hr_lay(&h, IMAGE(dot02));
    CHECK_EQ(at(5, 2), 0x85);
    CHECK_EQ(at(5, 3), 0x80);                              /* the spill byte: nothing spilled */
    CHECK_EQ(at(4, 2), 0x80);

    blank();
    h.XCO = 2; h.YCO = 5; h.OPACITY = POP_OP_STA; h.OFFSET = 3;
    pop_hr_lay(&h, IMAGE(dot02));
    CHECK_EQ(at(5, 2), 0xA8);                              /* dots 3 and 5 */
    CHECK_EQ(at(5, 3), 0x80);

    blank();
    h.XCO = 2; h.YCO = 5; h.OPACITY = POP_OP_STA; h.OFFSET = 6;
    pop_hr_lay(&h, IMAGE(dot02));
    CHECK_EQ(at(5, 2), 0xC0);                              /* dot 6 ... */
    CHECK_EQ(at(5, 3), 0x82);                              /* ... and dot 1 of the next byte */

    memset(h.vram, 0xFF, sizeof h.vram);                   /* on white: the dots left and */
    pop_hr_lay(&h, IMAGE(dot02));                          /* right of the image survive */
    CHECK_EQ(at(5, 2), 0xFF);                              /* 6 dots kept + dot 6 */
    CHECK_EQ(at(5, 3), 0xC2);                              /* dot 1; dots 0 and 2-5 cleared; 6 kept */

    blank();                                               /* OR and AND */
    h.XCO = 2; h.YCO = 5; h.OPACITY = POP_OP_ORA;
    h.vram[pop_hr_row(&h, 1, 5) - h.vram + 2] = 0x90;
    pop_hr_lay(&h, IMAGE(dot02));
    CHECK_EQ(at(5, 2), 0x95);
    h.OPACITY = POP_OP_AND;
    pop_hr_lay(&h, IMAGE(dot02));
    CHECK_EQ(at(5, 2), 0x85);
    CHECK_EQ(h.unmodelled, 0);
}

static void test_mask(void)
{
    static const uint8_t dot3[] = { 1, 1, 0x88 };
    pop_hires_init(&h);
    memset(h.vram, 0xFF, sizeof h.vram);                   /* a white screen */
    h.XCO = 7; h.YCO = 50; h.OPACITY = POP_OP_MASK;
    pop_hr_lay(&h, IMAGE(dot3));
    /* the dot itself is drawn; its two neighbours are cleared; the rest stays */
    CHECK_EQ(at(50, 7), 0xEB);
    CHECK_EQ(at(50, 8), 0xFF);
    CHECK_EQ(at(50, 6), 0xFF);
    CHECK_EQ(h.unmodelled, 0);
}

static void test_outline(void)
{
    static const uint8_t dot0[] = { 1, 1, 0x81 };
    blank();
    h.XCO = 4; h.YCO = 9; h.OPACITY = POP_OP_EOR;
    pop_hr_lay(&h, IMAGE(dot0));                           /* OR, then XOR one dot further right */
    CHECK_EQ(at(9, 4), 0x83);
    CHECK_EQ(at(9, 5), 0x80);
}

static void test_mirror(void)
{
    static const uint8_t left_dot[] = { 2, 1, 0x81, 0x80 };        /* a dot at the far left */
    blank();
    h.XCO = 10; h.YCO = 3; h.OPACITY = POP_OP_STA | POP_OP_MIRROR;  /* XCO: just right of it */
    pop_hr_lay(&h, IMAGE(left_dot));
    CHECK_EQ(at(3, 8), 0x80);
    CHECK_EQ(at(3, 9), 0xC0);                              /* now at the far right */
    CHECK_EQ(at(3, 10), 0x80);
    CHECK_EQ(h.OPACITY, POP_OP_STA);                       /* (the mirror bit is dropped) */
}

static void test_cropping(void)
{
    static const uint8_t two[] = { 2, 1, 0x81, 0x82 };
    static const uint8_t tall[] = { 1, 3, 0x81, 0x82, 0x84 };      /* bottom row first */
    blank();
    h.XCO = 0xFF; h.YCO = 7; h.OPACITY = POP_OP_STA;       /* one byte off the left edge */
    pop_hr_lay(&h, IMAGE(two));
    CHECK_EQ(at(7, 0), 0x82);
    CHECK_EQ(at(7, 1), 0x80);

    blank();
    h.XCO = 39; h.YCO = 7; h.OPACITY = POP_OP_STA;         /* one byte off the right edge */
    pop_hr_lay(&h, IMAGE(two));
    CHECK_EQ(at(7, 39), 0x81);
    CHECK_EQ(pop_hr_row(&h, 1, 7)[40], 0x80);              /* nothing past the line's end */

    blank();
    h.XCO = 40; h.YCO = 7;                                 /* wholly outside */
    pop_hr_lay(&h, IMAGE(two));
    CHECK_EQ(at(7, 39), 0x80);

    blank();
    h.XCO = 5; h.YCO = 192; h.OPACITY = POP_OP_STA;        /* bottom row below the screen */
    pop_hr_lay(&h, IMAGE(tall));
    CHECK_EQ(at(191, 5), 0x82);
    CHECK_EQ(at(190, 5), 0x84);
    CHECK_EQ(at(189, 5), 0x80);

    blank();
    h.XCO = 5; h.YCO = 1; h.OPACITY = POP_OP_STA;          /* top row above it */
    pop_hr_lay(&h, IMAGE(tall));
    CHECK_EQ(at(1, 5), 0x81);
    CHECK_EQ(at(0, 5), 0x82);

    blank();                                               /* one line taller than fits: the */
    {                                                      /* top "line" wraps to 255 */
        static uint8_t taller[2 + 7];
        memset(taller, 0x81, sizeof taller);
        taller[0] = 1; taller[1] = 7;
        h.XCO = 5; h.YCO = 5; h.OPACITY = POP_OP_STA;      /* 5 - 7 = -2 */
        pop_hr_lay(&h, image(taller, sizeof taller));
        CHECK_EQ(at(0, 5), 0x81);
        CHECK_EQ(at(5, 5), 0x81);
        CHECK_EQ(at(6, 5), 0x80);
        CHECK_EQ(h.unmodelled, 0);
        h.YCO = 6;                                         /* 6 - 7 = -1 = 255: exactly one above */
        pop_hr_lay(&h, image(taller, sizeof taller));
        CHECK_EQ(at(6, 5), 0x81);
        CHECK_EQ(at(7, 5), 0x80);
        CHECK_EQ(h.unmodelled, 0);
    }
    {                                                      /* (the same with the height that makes */
        static uint8_t wrap[2 + 70];                       /* YCO - height come out as 191) */
        memset(wrap, 0x81, sizeof wrap);
        wrap[0] = 1; wrap[1] = 70;
        blank();
        h.XCO = 5; h.YCO = 5; h.OPACITY = POP_OP_STA;      /* 5 - 70 = 191 mod 256 */
        pop_hr_lay(&h, image(wrap, sizeof wrap));
        CHECK_EQ(at(0, 5), 0x81);
        CHECK_EQ(at(6, 5), 0x80);
        CHECK_EQ(at(191, 5), 0x80);
        CHECK_EQ(h.unmodelled, 0);
    }

    blank();                                               /* wholly below: its top line is the */
    h.XCO = 5; h.YCO = 193; h.OPACITY = POP_OP_STA;        /* first one under the screen */
    pop_hr_lay(&h, IMAGE(two));
    CHECK_EQ(h.unmodelled, 0);
    CHECK_EQ(at(191, 5), 0x80);
    h.YCO = 192 + 1; h.BOTCUT = 190;                       /* (and under a smaller area) */
    pop_hr_lay(&h, IMAGE(tall));                           /* top line 191 >= 190 */
    CHECK_EQ(h.unmodelled, 0);
    CHECK_EQ(at(189, 5), 0x80);

    blank();                                               /* a usable area narrower than the screen */
    h.XCO = 9; h.YCO = 7; h.LEFTCUT = 10; h.OPACITY = POP_OP_STA;
    pop_hr_lay(&h, IMAGE(two));
    CHECK_EQ(at(7, 9), 0x80);
    CHECK_EQ(at(7, 10), 0x82);
    CHECK_EQ(h.unmodelled, 0);
}

static void test_save_under(void)
{
    static const uint8_t block[] = { 2, 3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t before[POP_VRAM_SIZE];
    unsigned i;
    pop_hires_init(&h);
    for (i = 0; i < POP_VRAM_SIZE; i++) h.vram[i] = (uint8_t)(i * 7 + (i >> 8));
    memcpy(before, h.vram, sizeof before);
    h.XCO = 5; h.YCO = 20; h.OFFSET = 3; h.OPACITY = POP_OP_STA;
    pop_hr_layrsave(&h, IMAGE(block));
    CHECK_EQ(h.PEELIMG, POP_PEELBUF1);
    CHECK_EQ(h.PEELXCO, 5);
    CHECK_EQ(h.PEELYCO, 20);
    CHECK_EQ(h.peelmem[0], 3);                             /* one byte wider than the image */
    CHECK_EQ(h.peelmem[1], 3);
    CHECK_EQ(h.peelmem[2], before[pop_hr_row(&h, 1, 20) - h.vram + 5]);
    CHECK_EQ(h.PEELBUF, POP_PEELBUF1 + 2 + 3 * 3);
    CHECK(memcmp(before, h.vram, sizeof before) == 0);     /* saving changes nothing */
    pop_hr_lay(&h, IMAGE(block));
    CHECK(memcmp(before, h.vram, sizeof before) != 0);
    h.XCO = h.PEELXCO; h.YCO = h.PEELYCO;
    pop_hr_peel(&h, h.PEELIMG);
    CHECK(memcmp(before, h.vram, sizeof before) == 0);     /* and the background is back */

    h.XCO = 60; h.YCO = 20;                                /* nothing visible: nothing saved */
    pop_hr_layrsave(&h, IMAGE(block));
    CHECK_EQ(h.PEELIMG >> 8, 0);
    CHECK_EQ(h.unmodelled, 0);
    /* a list entry that points outside the two buffers (LAYRSAVE does not
     * stop at their end) is not followed */
    CHECK_EQ(h.unmodelled, 0);
    pop_hr_peel(&h, 0xE000);
    CHECK_EQ(h.unmodelled, 1);
    h.unmodelled = 0;
    pop_hr_peel(&h, 0xCFFF);
    CHECK_EQ(h.unmodelled, 1);
    h.unmodelled = 0;
}

static void test_fast_routines(void)
{
    static const uint8_t column[] = { 1, 5, 0x81, 0x82, 0x84, 0x88, 0x90 };
    static const uint8_t dot3[] = { 1, 1, 0x88 };
    blank();
    h.XCO = 6; h.YCO = 2; h.OPACITY = POP_OP_ORA;          /* taller than the room above it: */
    pop_hr_fastlay(&h, IMAGE(column));                     /* cut at the top line */
    CHECK_EQ(at(2, 6), 0x81);
    CHECK_EQ(at(1, 6), 0x82);
    CHECK_EQ(at(0, 6), 0x84);
    h.OPACITY = POP_OP_EOR;                                /* XOR takes it off again, */
    pop_hr_fastlay(&h, IMAGE(column));                     /* and bit 7 with it */
    CHECK_EQ(at(1, 6), 0x00);

    memset(h.vram, 0xFF, sizeof h.vram);
    h.XCO = 7; h.YCO = 50;
    pop_hr_fastmask(&h, IMAGE(dot3));                      /* the mask alone */
    CHECK_EQ(at(50, 7), 0xE3);

    blank();
    h.XCO = 4; h.YCO = 10;
    pop_hr_fastblack(&h, 2, 3, 0x2A);
    CHECK_EQ(at(10, 4), 0x2A);
    CHECK_EQ(at(8, 5), 0x2A);
    CHECK_EQ(at(7, 4), 0x80);
    CHECK_EQ(at(10, 6), 0x80);

    pop_hr_copyscrn(&h, 0x20, 0x40);                       /* page 1 to page 2 */
    CHECK_EQ(pop_hr_row(&h, 2, 10)[4], 0x2A);
    CHECK_EQ(h.unmodelled, 0);
}

static void test_upside_down(void)
{
    static const uint8_t dot0[] = { 1, 1, 0x81 };
    blank();
    pop_hr_inverty(&h);                                    /* the "flip" potion */
    h.XCO = 3; h.YCO = 0; h.OPACITY = POP_OP_STA;
    pop_hr_lay(&h, IMAGE(dot0));
    CHECK_EQ(at(191, 3), 0x81);                            /* line 0 is now the bottom one */
    CHECK_EQ(at(0, 3), 0x80);
    pop_hr_inverty(&h);                                    /* and back */
    pop_hr_lay(&h, IMAGE(dot0));
    CHECK_EQ(at(0, 3), 0x81);
}

static void test_outside_the_model(void)
{
    static const uint8_t bad[] = { 1, 1, 0x05 };           /* an image byte without bit 7 */
    static const uint8_t short_image[] = { 2, 2, 0x81 };   /* fewer bytes than it claims */
    blank();
    h.XCO = 3; h.YCO = 3; h.OPACITY = POP_OP_STA;
    pop_hr_lay(&h, IMAGE(bad));
    CHECK_EQ(h.unmodelled, 1);
    blank();
    h.XCO = 3; h.YCO = 3; h.OPACITY = POP_OP_STA;
    pop_hr_lay(&h, IMAGE(short_image));
    CHECK_EQ(h.unmodelled, 1);
    blank();
    h.XCO = 3; h.YCO = 3; h.OPACITY = POP_OP_STA; h.OFFSET = 7;
    pop_hr_lay(&h, IMAGE(short_image));
    CHECK_EQ(h.unmodelled, 1);
}

int main(void)
{
    RUN(test_page_layout);
    RUN(test_store_and_shift);
    RUN(test_mask);
    RUN(test_outline);
    RUN(test_mirror);
    RUN(test_cropping);
    RUN(test_save_under);
    RUN(test_fast_routines);
    RUN(test_upside_down);
    RUN(test_outside_the_model);
    return pop_test_summary();
}
