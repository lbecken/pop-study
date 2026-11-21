/* test_present.c - from page bytes to pixels, on made-up bytes.
 *
 * New code.  The dot extraction is exact and checked as such; the colour
 * rules are checked against themselves: these tests pin down the model
 * of docs/port/presentation.md, they do not show that an Apple II looks
 * like it.
 */

#include <string.h>

#include "pop_present.h"
#include "pop_test.h"

static pop_gfx v;
static uint8_t rgb[POP_SCREEN_W * POP_SCREEN_H * 3];

static void line(int page, int y, int column, uint8_t value)
{
    /* the address of a screen line: see docs/port/blitter.md section 2 */
    unsigned a = ((unsigned)(y & 7) << 10) + ((unsigned)((y >> 3) & 7) << 7) + (unsigned)(y >> 6) * 0x28u
                 + (page == 2 ? 0x2000u : 0u);
    v.hr.vram[a + (unsigned)column] = value;
}

static const uint8_t *px(int x, int y) { return rgb + 3 * (y * POP_SCREEN_W + x); }
static int is(const uint8_t *p, int r, int g, int b) { return p[0] == r && p[1] == g && p[2] == b; }
static int white(int x, int y) { return is(px(x, y), 255, 255, 255); }
static int black(int x, int y) { return is(px(x, y), 0, 0, 0); }
static int same(int x0, int x1, int y) { return memcmp(px(x0, y), px(x1, y), 3) == 0; }

static void setup(void)
{
    pop_gfx_init(&v);
    v.sw_hires = 1;
}

static void test_dots(void)
{
    uint8_t dots[POP_SCREEN_W], high[POP_HR_COLS];
    setup();
    line(1, 5, 0, 0x01);                       /* bit 0 is the leftmost dot */
    line(1, 5, 1, 0xC0);                       /* bit 6 the seventh; bit 7 is not a dot */
    line(1, 191, 39, 0x40);
    pop_present_dots(&v, 1, 5, dots, high);
    CHECK_EQ(dots[0], 1); CHECK_EQ(dots[1], 0);
    CHECK_EQ(dots[13], 1); CHECK_EQ(dots[12], 0);
    CHECK_EQ(high[0], 0); CHECK_EQ(high[1], 1);
    pop_present_dots(&v, 1, 191, dots, NULL);
    CHECK_EQ(dots[279], 1);
    pop_present_dots(&v, 2, 5, dots, high);    /* the other page is another 8,192 bytes */
    CHECK_EQ(dots[0], 0);
}

static void test_mono(void)
{
    setup();
    line(1, 0, 0, 0x05);                       /* dots 0 and 2 */
    CHECK_EQ(pop_present(&v, POP_PRESENT_MONO, rgb), 0);
    CHECK(white(0, 0)); CHECK(black(1, 0)); CHECK(white(2, 0)); CHECK(black(3, 0));
    CHECK(black(0, 1));
    v.sw_page2 = 1;                            /* page 2 is on the screen: nothing there */
    pop_present(&v, POP_PRESENT_MONO, rgb);
    CHECK(black(0, 0));
}

static void test_colour(void)
{
    setup();
    line(1, 0, 0, 0x01);                       /* a lone dot in an even column */
    line(1, 1, 0, 0x02);                       /* in an odd column */
    line(1, 2, 0, 0x81);                       /* the same two with bit 7: the other pair of colours */
    line(1, 3, 0, 0x82);
    line(1, 4, 0, 0x03);                       /* two side by side */
    line(1, 5, 0, 0x15);                       /* alternate dots: 0, 2, 4 */
    line(1, 6, 0, 0x40);                       /* across a byte boundary: dots 6 and 7 */
    line(1, 6, 1, 0x01);
    line(1, 7, 0, 0x40);                       /* dots 6 and 8, the second byte with bit 7 */
    line(1, 7, 1, 0x82);
    CHECK_EQ(pop_present(&v, POP_PRESENT_COLOUR, rgb), 0);
    CHECK(!white(0, 0) && !black(0, 0));       /* four different colours */
    CHECK(!white(1, 1) && !black(1, 1));
    CHECK(memcmp(px(0, 0), px(1, 1), 3) != 0);
    CHECK(memcmp(px(0, 0), px(0, 2), 3) != 0);
    CHECK(memcmp(px(1, 1), px(1, 3), 3) != 0);
    CHECK(memcmp(px(0, 2), px(1, 3), 3) != 0);
    CHECK(black(1, 0)); CHECK(black(0, 1));
    CHECK(white(0, 4)); CHECK(white(1, 4)); CHECK(black(2, 4));       /* a pair is white */
    /* alternate dots make a solid colour: the dark dots between take it */
    CHECK(same(0, 1, 5)); CHECK(same(1, 2, 5)); CHECK(same(2, 3, 5)); CHECK(same(3, 4, 5));
    CHECK(same(0, 0, 5) && !white(0, 5) && !black(0, 5));
    CHECK(memcmp(px(0, 5), px(0, 0), 3) == 0); /* the colour of a lone even dot */
    CHECK(black(5, 5));
    CHECK(white(6, 6)); CHECK(white(7, 6));    /* neighbours across the byte boundary */
    /* the colour that runs through a dark dot is that of the dot to its left */
    CHECK(memcmp(px(7, 7), px(6, 7), 3) == 0);
    CHECK(memcmp(px(8, 7), px(6, 7), 3) != 0); /* (dot 8 is in a byte with bit 7) */
}

static void test_other_screens(void)
{
    int x, y, ok = 1;
    setup();
    line(1, 10, 3, 0x7F);
    v.sw_text = 1;                             /* the blank screen of a redraw: spaces */
    v.scrncolor = 0xA0;
    CHECK_EQ(pop_present(&v, POP_PRESENT_COLOUR, rgb), 0);
    for (y = 0; y < POP_SCREEN_H; y++) for (x = 0; x < POP_SCREEN_W; x++) ok = ok && black(x, y);
    CHECK(ok);
    v.scrncolor = 0xC1;                        /* text that is not spaces: no picture for it */
    CHECK_EQ(pop_present(&v, POP_PRESENT_COLOUR, rgb), 1);
    v.sw_text = 0;
    v.sw_hires = 0;                            /* a flash: lo-res, every cell the same byte */
    v.scrncolor = 0x11;
    CHECK_EQ(pop_present(&v, POP_PRESENT_COLOUR, rgb), 0);
    CHECK(!black(0, 0)); CHECK(same(0, 279, 0));
    CHECK(memcmp(px(0, 0), px(100, 191), 3) == 0);          /* one colour all over */
    v.scrncolor = 0xF0;                        /* two nibbles: blocks of four lines */
    pop_present(&v, POP_PRESENT_COLOUR, rgb);
    CHECK(black(0, 0)); CHECK(black(0, 3)); CHECK(white(0, 4)); CHECK(white(0, 7)); CHECK(black(0, 8));
    v.sw_hires = 1;
    pop_present(&v, POP_PRESENT_MONO, rgb);
    CHECK(white(21, 10)); CHECK(white(27, 10)); CHECK(black(28, 10));
}

int main(void)
{
    RUN(test_dots);
    RUN(test_mono);
    RUN(test_colour);
    RUN(test_other_screens);
    return pop_test_summary();
}
