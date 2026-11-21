/* pop_hires.c - see pop_hires.h.  New code; ports of original routines.
 *
 * The code follows HIRES.S routine by routine, with the original labels in
 * comments.  The original patches table addresses and opcodes into its
 * inner loops; here those are locals.  Its lookup tables are computed by
 * rule (docs/run-cycle-analysis.md shows the rules give the original tables
 * byte for byte).
 */

#include "pop_hires.h"

#include <string.h>

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))
#define u16(v) ((uint16_t)(v))

/* --- tables (HRTABLES.S), indexed by a byte with bit 7 set, minus $80 ---------------- */

static uint8_t SHIFT[7][128], CARRY[7][128], MIRROR[128], MASKTAB[128];
static const uint8_t AMASKS[7] = { 0x80, 0x81, 0x83, 0x87, 0x8F, 0x9F, 0xBF };
static const uint8_t BMASKS[7] = { 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0 };
static int tables_ready;

static void make_tables(void)
{
    unsigned n, b, k;
    for (b = 0; b < 128; b++) {
        unsigned m = 0;
        for (n = 0; n < 7; n++) {
            SHIFT[n][b] = u8(((b << n) & 0x7F) | 0x80);      /* n dots to the right */
            CARRY[n][b] = u8(b >> (7 - n));                  /* what spills into the next byte */
        }
        for (k = 0; k < 7; k++) if (b & (1u << k)) m |= 1u << (6 - k);
        MIRROR[b] = u8(m | 0x80);
        /* keep the screen dots that are neither under an image dot nor next to one */
        MASKTAB[b] = u8(0x80 | (~(b | (b << 1) | (b >> 1)) & 0x7F));
    }
    tables_ready = 1;
}

/* A table entry for the byte `v`.  The original indexes `table-$80` with a
 * byte that has bit 7 set; one without would read some other table. */
static uint8_t tab(pop_hires *h, const uint8_t *table, uint8_t v)
{
    if (!NEG(v)) { h->unmodelled = 1; return 0x80; }
    return table[v & 0x7F];
}

/* --- memory ------------------------------------------------------------------------------ */

static uint8_t rd(pop_hires *h, uint16_t a)
{
    if (a >= POP_VRAM_BASE && a < POP_VRAM_BASE + POP_VRAM_SIZE) return h->vram[a - POP_VRAM_BASE];
    if (a >= POP_PEEL_BASE && a < POP_PEEL_BASE + POP_PEEL_SIZE) return h->peelmem[a - POP_PEEL_BASE];
    h->unmodelled = 1;
    return 0;
}

static void wr(pop_hires *h, uint16_t a, uint8_t v)
{
    if (a >= POP_VRAM_BASE && a < POP_VRAM_BASE + POP_VRAM_SIZE) h->vram[a - POP_VRAM_BASE] = v;
    else if (a >= POP_PEEL_BASE && a < POP_PEEL_BASE + POP_PEEL_SIZE) h->peelmem[a - POP_PEEL_BASE] = v;
    else h->unmodelled = 1;
}

/* YLO,y and YHI,y.  The tables have 192 entries and lie one after the
 * other, followed by SHIFT0; an index of 192 or more reads on into the
 * next table, as it does in the original. */
static uint8_t ylo(const pop_hires *h, uint8_t y)
{
    return y < POP_HR_ROWS ? h->ylo[y] : h->yhi[y - POP_HR_ROWS];
}

static uint8_t yhi(const pop_hires *h, uint8_t y)
{
    return y < POP_HR_ROWS ? h->yhi[y] : u8(0x80 | (y - POP_HR_ROWS));   /* SHIFT0 */
}

/* Address of byte XCO of line y: YLO + XCO, the carry going into YHI + PAGE. */
static uint16_t line_at(const pop_hires *h, uint8_t y, uint8_t xco)
{
    unsigned lo = (unsigned)ylo(h, y) + xco;
    unsigned hi = (unsigned)yhi(h, y) + h->PAGE + (lo >> 8);
    return u16(((hi & 0xFF) << 8) | (lo & 0xFF));
}

void pop_hires_init(pop_hires *h)
{
    int y;
    if (!tables_ready) make_tables();
    memset(h, 0, sizeof *h);
    for (y = 0; y < POP_HR_ROWS; y++) {
        unsigned a = 0x2000u + ((unsigned)(y & 7) << 10) + ((unsigned)((y >> 3) & 7) << 7)
                     + (unsigned)(y >> 6) * 0x28u;
        h->ylo[y] = u8(a);
        h->yhi[y] = u8(a >> 8);
    }
    h->RIGHTCUT = POP_HR_COLS;
    h->BOTCUT = POP_HR_ROWS;
    h->PEELBUF = POP_PEELBUF1;
}

const uint8_t *pop_hr_row(const pop_hires *h, int page, int y)
{
    unsigned a = 0x2000u + ((unsigned)(y & 7) << 10) + ((unsigned)((y >> 3) & 7) << 7)
                 + (unsigned)(y >> 6) * 0x28u + (page == 2 ? 0x2000u : 0u);
    return h->vram + (a - POP_VRAM_BASE);
}

/* --- the state of one LAY call -------------------------------------------------------------- */

typedef struct lay {
    pop_image im;
    size_t ptr;                          /* IMAGE: offset of the current row in im.bytes */
    uint8_t WIDTH, HEIGHT, XCO, YCO;
    uint8_t TOPEDGE, VISWIDTH, OFFLEFT, OFFRIGHT, RMOST;
} lay;

static uint8_t img(pop_hires *h, const lay *s, size_t offset)
{
    size_t at = s->ptr + offset;
    if (at >= s->im.size) { h->unmodelled = 1; return 0x80; }
    return s->im.bytes[at];
}

/* PREPREP: width and height from the image header. */
static void prep(pop_hires *h, lay *s, pop_image im)
{
    s->im = im;
    s->ptr = 0;
    s->WIDTH = img(h, s, 0);
    s->HEIGHT = img(h, s, 1);
    s->ptr = 2;
    s->XCO = h->XCO;
    s->YCO = h->YCO;
}

/* CROP.  Returns 0 if nothing of the image is inside the usable area. */
static int crop(pop_hires *h, lay *s)
{
    uint8_t a = s->YCO, x;
    if (a < h->BOTCUT) {                       /* the bottom line is inside */
        a = u8(a - s->HEIGHT);                 /* the line above the top one */
        if (a < 191) {                         /* :topok */
            s->TOPEDGE = a;
            if (h->TOPCUT != 0 && u8(h->TOPCUT - 1) >= s->TOPEDGE) s->TOPEDGE = u8(h->TOPCUT - 1);
        } else {                               /* the top is above the screen */
            s->TOPEDGE = u8(h->TOPCUT - 1);
        }
    } else {                                   /* :botoff skip the rows below the area */
        a = u8(u8(a - s->HEIGHT) + 1);         /* top line */
        if (a >= h->BOTCUT) return 0;
        s->TOPEDGE = u8(a - 1);
        x = s->YCO;
        do {
            s->ptr += s->WIDTH;
            x--;
        } while (x >= h->BOTCUT);
        s->YCO = x;
    }
    /* :done the sides */
    a = s->XCO;
    if (NEG(a) || a < h->LEFTCUT) {            /* :leftoff */
        s->OFFLEFT = u8(h->LEFTCUT - s->XCO);
        a = u8(s->WIDTH - s->OFFLEFT);
        if (NEG(a)) return 0;
        s->VISWIDTH = a;                       /* (may be 0) */
        s->XCO = h->LEFTCUT;
        s->OFFRIGHT = s->RMOST = 0;            /* (assumes the right edge is inside) */
        return 1;
    }
    if (a >= h->RIGHTCUT) return 0;
    a = u8(a + s->WIDTH);                      /* the byte after the last */
    if (a < h->RIGHTCUT) {                     /* :bothok */
        s->VISWIDTH = s->WIDTH;
        s->OFFLEFT = s->OFFRIGHT = s->RMOST = 0;
        return 1;
    }
    s->RMOST = u8(a - h->RIGHTCUT);
    s->OFFRIGHT = u8(s->RMOST + 1);            /* (the carry byte is outside as well) */
    s->VISWIDTH = u8(h->RIGHTCUT - s->XCO);
    s->OFFLEFT = 0;
    return 1;
}

/* The patched instruction of the general routines: AND, ORA, EOR with the
 * screen byte, or (STA) the value as it is.  OPCODE, HRTABLES.S 321-326. */
static uint8_t combine(pop_hires *h, uint8_t opacity, uint8_t a, uint8_t screen)
{
    switch (opacity) {
    case POP_OP_AND: case POP_OP_MASK: return a & screen;
    case POP_OP_ORA: return a | screen;
    case POP_OP_STA: case POP_OP_VISMASK: return a;
    case POP_OP_EOR: return a ^ screen;
    default: h->unmodelled = 1; return a;      /* beyond the table */
    }
}

/* shiftoffset: one dot further right, or left if there is no room. */
static uint8_t shiftoffset(uint8_t x)
{
    return x >= 6 ? u8(x - 1) : u8(x + 1);
}

/* --- left to right -------------------------------------------------------------------------- */

static void lay_gen(pop_hires *h, pop_image im)
{
    lay s;
    const uint8_t *shift, *carry;
    uint8_t amask, bmask, op = h->OPACITY, y, yy, b, a, CARRYv;
    uint16_t base;
    prep(h, &s, im);
    if (!crop(h, &s)) return;
    if (h->OFFSET > 6) { h->unmodelled = 1; return; }
    shift = SHIFT[h->OFFSET];
    carry = CARRY[h->OFFSET];
    amask = AMASKS[h->OFFSET];
    bmask = BMASKS[h->OFFSET];
    y = s.YCO;
    do {                                       /* :nextline */
        base = line_at(h, y, s.XCO);
        yy = 0;
        if (s.OFFLEFT != 0) {                  /* (a) the first carry comes from the image */
            b = img(h, &s, (size_t)s.OFFLEFT - 1);
            CARRYv = tab(h, carry, b);
            s.ptr += s.OFFLEFT;
            s.WIDTH = s.VISWIDTH;
            if (s.VISWIDTH == 0) goto extra;
        } else {                               /* (b) ... from the screen */
            CARRYv = rd(h, base) & amask;
        }
        do {                                   /* :3 */
            b = img(h, &s, yy);
            a = tab(h, shift, b) | CARRYv;
            a = combine(h, op, a, rd(h, u16(base + yy)));
            wr(h, u16(base + yy), a);
            CARRYv = tab(h, carry, b);
            yy++;
        } while (yy < s.VISWIDTH);
        if (s.OFFRIGHT == 0) {
extra:                                         /* :4 the byte the shift spills into */
            a = u8((rd(h, u16(base + yy)) & bmask) | CARRYv);
            a = combine(h, op, a, rd(h, u16(base + yy)));
            wr(h, u16(base + yy), a);
        }
        s.ptr += s.WIDTH;                      /* :5 next line up */
        s.YCO--;
        y = s.YCO;
    } while (y != s.TOPEDGE);
}

static void lay_mask(pop_hires *h, pop_image im)
{
    lay s;
    const uint8_t *shift, *carry;
    uint8_t amask, bmask, op = h->OPACITY, y, yy, b, m, a, CARRYv, carryim, imbyte;
    uint16_t base;
    if (op > POP_OP_VISMASK) { h->unmodelled = 1; return; }  /* OPCODE,x beyond the table */
    prep(h, &s, im);
    if (!crop(h, &s)) return;
    if (h->OFFSET > 6) { h->unmodelled = 1; return; }
    shift = SHIFT[h->OFFSET];
    carry = CARRY[h->OFFSET];
    amask = AMASKS[h->OFFSET];
    bmask = BMASKS[h->OFFSET];
    y = s.YCO;
    do {
        base = line_at(h, y, s.XCO);
        yy = 0;
        if (s.OFFLEFT != 0) {                  /* (a) */
            b = img(h, &s, (size_t)s.OFFLEFT - 1);
            carryim = tab(h, carry, b);
            m = tab(h, MASKTAB, b);
            CARRYv = tab(h, carry, m);
            s.ptr += s.OFFLEFT;
            s.WIDTH = s.VISWIDTH;
            if (s.VISWIDTH == 0) goto extra;
        } else {                               /* (b) */
            CARRYv = amask;
            carryim = amask & rd(h, base);
        }
        do {                                   /* :inloop */
            b = img(h, &s, yy);
            imbyte = tab(h, shift, b) | carryim;             /* the image, shifted */
            carryim = tab(h, carry, b);
            m = tab(h, MASKTAB, b);
            a = tab(h, shift, m) | CARRYv;                   /* its mask, shifted */
            a = combine(h, op, a, rd(h, u16(base + yy)));
            wr(h, u16(base + yy), a | imbyte);
            CARRYv = tab(h, carry, m);
            yy++;
        } while (yy < s.VISWIDTH);
        if (s.OFFRIGHT == 0) {
extra:
            a = bmask | CARRYv;
            a = combine(h, op, a, rd(h, u16(base + yy)));
            wr(h, u16(base + yy), a | carryim);
        }
        s.ptr += s.WIDTH;
        s.YCO--;
        y = s.YCO;
    } while (y != s.TOPEDGE);
}

/* The "special XOR": OR the image in, then XOR with the image one dot
 * further over, which leaves its outline. */
static void lay_xor(pop_hires *h, pop_image im)
{
    lay s;
    const uint8_t *shift, *carry, *shift1, *carry1;
    uint8_t amask, x1, y, yy, b, a, CARRYv, carryim, imbyte;
    uint16_t base;
    prep(h, &s, im);
    if (!crop(h, &s)) return;
    if (h->OFFSET > 6) { h->unmodelled = 1; return; }
    shift = SHIFT[h->OFFSET];
    carry = CARRY[h->OFFSET];
    x1 = shiftoffset(h->OFFSET);
    shift1 = SHIFT[x1];
    carry1 = CARRY[x1];
    amask = AMASKS[x1];                        /* (of the second offset, as in the original) */
    y = s.YCO;
    do {
        base = line_at(h, y, s.XCO);
        yy = 0;
        if (s.OFFLEFT != 0) {
            b = img(h, &s, (size_t)s.OFFLEFT - 1);
            carryim = tab(h, carry1, b);
            CARRYv = tab(h, carry, b);
            s.ptr += s.OFFLEFT;
            s.WIDTH = s.VISWIDTH;
            if (s.VISWIDTH == 0) goto extra;
        } else {
            CARRYv = rd(h, base) & amask;
            carryim = 0;
        }
        do {
            b = img(h, &s, yy);
            imbyte = tab(h, shift1, b) | carryim;
            carryim = tab(h, carry1, b);
            a = tab(h, shift, b) | CARRYv;
            a |= rd(h, u16(base + yy));
            wr(h, u16(base + yy), u8((a ^ imbyte) | 0x80));
            CARRYv = tab(h, carry, b);
            yy++;
        } while (yy < s.VISWIDTH);
        if (s.OFFRIGHT == 0) {
extra:
            a = CARRYv | rd(h, u16(base + yy));
            wr(h, u16(base + yy), u8((a ^ carryim) | 0x80));
        }
        s.ptr += s.WIDTH;
        s.YCO--;
        y = s.YCO;
    } while (y != s.TOPEDGE);
}

/* --- mirrored: XCO is the byte after the right edge; the image is read
 *     right to left and every byte reversed ------------------------------------------------- */

/* The line's address for the mirrored routines: the low byte is stepped
 * by `inc` (no carry into the high byte) and XCO is added as the index. */
typedef struct mbase { uint8_t lo, hi; } mbase;

static mbase mline(const pop_hires *h, uint8_t y)
{
    mbase b;
    b.lo = ylo(h, y);
    b.hi = u8(yhi(h, y) + h->PAGE);
    return b;
}

static uint16_t mat(mbase b, uint8_t xco)
{
    return u16((((unsigned)b.hi << 8) | b.lo) + xco);
}

static void mlay_gen(pop_hires *h, pop_image im)
{
    lay s;
    const uint8_t *shift, *carry;
    uint8_t amask, bmask, op = h->OPACITY, y, yy, b, m, a, CARRYv;
    mbase base;
    uint16_t at;
    prep(h, &s, im);
    s.XCO = u8(s.XCO - s.WIDTH);
    if (!crop(h, &s)) return;
    if (h->OFFSET > 6) { h->unmodelled = 1; return; }
    shift = SHIFT[h->OFFSET];
    carry = CARRY[h->OFFSET];
    amask = AMASKS[h->OFFSET];
    bmask = BMASKS[h->OFFSET];
    y = s.YCO;
    do {                                       /* :0 */
        base = mline(h, y);
        if (s.OFFLEFT != 0) {                  /* the first carry comes from the image */
            yy = s.VISWIDTH;
            m = tab(h, MIRROR, img(h, &s, yy));
            CARRYv = tab(h, carry, m);
            yy--;
            if (NEG(yy)) goto extra;
        } else {
            CARRYv = rd(h, mat(base, s.XCO)) & amask;
            yy = u8(s.WIDTH - 1);
        }
        for (;;) {                             /* :3 */
            b = img(h, &s, yy);
            m = tab(h, MIRROR, b);
            a = tab(h, shift, m) | CARRYv;
            at = mat(base, s.XCO);
            a = combine(h, op, a, rd(h, at));
            wr(h, at, a);
            CARRYv = tab(h, carry, m);
            base.lo++;
            if (yy == s.RMOST) break;          /* the rest is off the right edge */
            yy--;
            if (NEG(yy)) break;
        }
        if (s.OFFRIGHT == 0) {                 /* :7 */
extra:                                         /* :4 */
            at = mat(base, s.XCO);
            a = u8((rd(h, at) & bmask) | CARRYv);
            a = combine(h, op, a, rd(h, at));
            wr(h, at, a);
        }
        s.ptr += s.WIDTH;
        s.YCO--;
        y = s.YCO;
    } while (y != s.TOPEDGE);
}

static void mlay_mask(pop_hires *h, pop_image im)
{
    lay s;
    const uint8_t *shift, *carry;
    uint8_t amask, bmask, op = h->OPACITY, y, yy, b, m, mm, a, CARRYv, carryim, imbyte;
    mbase base;
    uint16_t at;
    if (op > POP_OP_VISMASK) { h->unmodelled = 1; return; }
    prep(h, &s, im);
    s.XCO = u8(s.XCO - s.WIDTH);
    if (!crop(h, &s)) return;
    if (h->OFFSET > 6) { h->unmodelled = 1; return; }
    shift = SHIFT[h->OFFSET];
    carry = CARRY[h->OFFSET];
    amask = AMASKS[h->OFFSET];
    bmask = BMASKS[h->OFFSET];
    y = s.YCO;
    do {
        base = mline(h, y);
        if (s.OFFLEFT != 0) {
            yy = s.VISWIDTH;
            m = tab(h, MIRROR, img(h, &s, yy));
            carryim = tab(h, carry, m);
            mm = tab(h, MASKTAB, m);
            CARRYv = tab(h, carry, mm);
            yy--;
            if (NEG(yy)) goto extra;
        } else {
            CARRYv = amask;
            carryim = amask & rd(h, mat(base, s.XCO));
            yy = u8(s.WIDTH - 1);
        }
        for (;;) {
            b = img(h, &s, yy);
            m = tab(h, MIRROR, b);
            imbyte = tab(h, shift, m) | carryim;
            carryim = tab(h, carry, m);
            mm = tab(h, MASKTAB, m);
            a = tab(h, shift, mm) | CARRYv;
            at = mat(base, s.XCO);
            a = combine(h, op, a, rd(h, at));
            wr(h, at, a | imbyte);
            CARRYv = tab(h, carry, mm);
            base.lo++;
            if (yy == s.RMOST) break;
            yy--;
            if (NEG(yy)) break;
        }
        if (s.OFFRIGHT == 0) {
extra:
            at = mat(base, s.XCO);
            a = u8((rd(h, at) & bmask) | CARRYv);            /* (not quite as lay_mask does it) */
            a = combine(h, op, a, rd(h, at));
            wr(h, at, a | carryim);
        }
        s.ptr += s.WIDTH;
        s.YCO--;
        y = s.YCO;
    } while (y != s.TOPEDGE);
}

static void mlay_xor(pop_hires *h, pop_image im)
{
    lay s;
    const uint8_t *shift, *carry, *shift1, *carry1;
    uint8_t amask, x1, y, yy, b, m, a, CARRYv, carryim, imbyte;
    mbase base;
    uint16_t at;
    prep(h, &s, im);
    s.XCO = u8(s.XCO - s.WIDTH);
    if (!crop(h, &s)) return;
    if (h->OFFSET > 6) { h->unmodelled = 1; return; }
    shift = SHIFT[h->OFFSET];
    carry = CARRY[h->OFFSET];
    x1 = shiftoffset(h->OFFSET);
    shift1 = SHIFT[x1];
    carry1 = CARRY[x1];
    amask = AMASKS[x1];
    y = s.YCO;
    do {
        base = mline(h, y);
        if (s.OFFLEFT != 0) {
            yy = s.VISWIDTH;
            m = tab(h, MIRROR, img(h, &s, yy));
            carryim = tab(h, carry1, m);
            CARRYv = tab(h, carry, m);
            yy--;
            if (NEG(yy)) goto extra;
        } else {
            CARRYv = amask & rd(h, mat(base, s.XCO));
            carryim = 0;
            yy = u8(s.WIDTH - 1);
        }
        for (;;) {
            b = img(h, &s, yy);
            m = tab(h, MIRROR, b);
            imbyte = tab(h, shift1, m) | carryim;
            carryim = tab(h, carry1, m);
            a = tab(h, shift, m) | CARRYv;
            at = mat(base, s.XCO);
            a |= rd(h, at);
            wr(h, at, u8((a ^ imbyte) | 0x80));
            CARRYv = tab(h, carry, m);
            base.lo++;
            if (yy == s.RMOST) break;
            yy--;
            if (NEG(yy)) break;
        }
        if (s.OFFRIGHT == 0) {
extra:
            at = mat(base, s.XCO);
            a = CARRYv | rd(h, at);
            wr(h, at, u8((a ^ carryim) | 0x80));
        }
        s.ptr += s.WIDTH;
        s.YCO--;
        y = s.YCO;
    } while (y != s.TOPEDGE);
}

void pop_hr_lay(pop_hires *h, pop_image im)
{
    uint8_t whole = h->OPACITY, op = whole & 0x7F;
    if (!tables_ready) make_tables();
    if (NEG(whole)) {                          /* MLAY; the routines see OPACITY without bit 7 */
        h->OPACITY = op;
        if (op == POP_OP_EOR) mlay_xor(h, im);
        else if (op >= POP_OP_MASK) mlay_mask(h, im);
        else mlay_gen(h, im);
        return;                                /* (OPACITY stays without its mirror bit) */
    }
    if (op == POP_OP_EOR) lay_xor(h, im);
    else if (op >= POP_OP_MASK) lay_mask(h, im);
    else lay_gen(h, im);
}

/* --- save-under ---------------------------------------------------------------------------------- */

void pop_hr_layrsave(pop_hires *h, pop_image im)
{
    lay s;
    uint8_t x;
    int y;
    uint16_t base;
    prep(h, &s, im);
    if (NEG(h->OPACITY)) s.XCO = u8(s.XCO - s.WIDTH);
    s.WIDTH++;                                 /* the byte a shift spills into */
    if (!crop(h, &s)) goto skip;
    h->PEELIMG = h->PEELBUF;
    h->PEELXCO = s.XCO;
    h->PEELYCO = s.YCO;
    if (s.VISWIDTH == 0) goto skip;
    wr(h, h->PEELBUF, s.VISWIDTH);             /* in image format: width, height, rows */
    wr(h, u16(h->PEELBUF + 1), u8(s.YCO - s.TOPEDGE));
    h->PEELBUF = u16(h->PEELBUF + 2);
    x = s.YCO;
    do {
        base = line_at(h, x, s.XCO);
        for (y = s.VISWIDTH - 1; y >= 0; y--) {
            wr(h, u16(h->PEELBUF + y), rd(h, u16(base + y)));
            if (y >= 0x80) break;              /* (`bpl`: a width over 128 copies one byte) */
        }
        h->PEELBUF = u16(h->PEELBUF + s.VISWIDTH);
        x--;
    } while (x != s.TOPEDGE);
    return;
skip:
    h->PEELIMG &= 0x00FF;                      /* high byte 0: nothing saved */
}

/* --- byte-aligned ------------------------------------------------------------------------------ */

typedef enum { FAST_OP, FAST_STA, FAST_MASK } fastkind;

/* FASTLAY, fastlaySTA and FASTMASK share everything but the inner
 * operation.  `ptr` is the offset of the image header in `bytes`. */
static void fast(pop_hires *h, const uint8_t *bytes, size_t size, int peel, fastkind kind)
{
    uint8_t width, height, start, top, x, y, b, a;
    size_t ptr = 2;
    uint16_t base;
    unsigned borrow;
    if (size < 2) { h->unmodelled = 1; return; }
    width = bytes[0];
    height = bytes[1];
    start = u8(width - 1);
    /* `sbc` with the carry the subtraction before it left: clear if width is 0 */
    borrow = (width == 0) ? 1u : 0u;
    if ((unsigned)h->YCO >= (unsigned)height + borrow) top = u8(h->YCO - height - borrow);
    else top = 0xFF;                           /* "limited Y-clipping": cut at the top line */
    x = h->YCO;
    do {
        base = line_at(h, x, h->XCO);
        y = start;
        do {                                   /* right to left */
            if (ptr + y >= size) { h->unmodelled = 1; b = 0x80; }
            else b = bytes[ptr + y];
            if (kind == FAST_STA) {
                a = b;
            } else if (kind == FAST_MASK) {
                a = tab(h, MASKTAB, b) & rd(h, u16(base + y));
            } else {
                a = combine(h, h->OPACITY, b, rd(h, u16(base + y)));
            }
            wr(h, u16(base + y), a);
            y--;
        } while (!NEG(y));
        ptr += width;
        x--;
    } while (x != top);
    (void)peel;
}

void pop_hr_fastlay(pop_hires *h, pop_image im)
{
    if (!tables_ready) make_tables();
    if (h->OPACITY > POP_OP_VISMASK) { h->unmodelled = 1; return; }
    fast(h, im.bytes, im.size, 0,
         (h->OPACITY == POP_OP_STA) ? FAST_STA : FAST_OP);
}

void pop_hr_fastmask(pop_hires *h, pop_image im)
{
    if (!tables_ready) make_tables();
    fast(h, im.bytes, im.size, 0, FAST_MASK);
}

void pop_hr_peel(pop_hires *h, uint16_t peelimg)
{
    /* (a list entry can point past the buffers: LAYRSAVE does not stop at their end) */
    if (peelimg < POP_PEEL_BASE || peelimg - POP_PEEL_BASE >= POP_PEEL_SIZE) {
        h->unmodelled = 1;
        return;
    }
    fast(h, h->peelmem + (peelimg - POP_PEEL_BASE),
         (size_t)(POP_PEEL_BASE + POP_PEEL_SIZE - peelimg), 1, FAST_STA);
}

void pop_hr_fastblack(pop_hires *h, uint8_t width, uint8_t height, uint8_t colour)
{
    uint8_t start = u8(width - 1), x, y;
    uint8_t top = u8(h->YCO - height - (width == 0 ? 1 : 0));    /* (no clipping here) */
    uint16_t base;
    x = h->YCO;
    do {
        base = line_at(h, x, h->XCO);
        y = start;
        do {
            wr(h, u16(base + y), colour);
            y--;
        } while (!NEG(y));
        x--;
    } while (x != top);
}

/* --- whole pages -------------------------------------------------------------------------------- */

void pop_hr_cls(pop_hires *h)
{
    unsigned i, at = ((unsigned)h->PAGE + 0x20u) << 8;
    for (i = 0; i < POP_HR_PAGE; i++) wr(h, u16(at + i), 0x80);
}

void pop_hr_copyscrn(pop_hires *h, uint8_t from, uint8_t to)
{
    unsigned i;
    for (i = 0; i < POP_HR_PAGE; i++) {
        wr(h, u16(((unsigned)to << 8) + i), rd(h, u16(((unsigned)from << 8) + i)));
    }
}

void pop_hr_inverty(pop_hires *h)
{
    int low = POP_HR_ROWS - 1, high = 0;
    for (; high < 96; high++, low--) {
        uint8_t t = h->ylo[low];
        h->ylo[low] = h->ylo[high];
        h->ylo[high] = t;
        t = h->yhi[low];
        h->yhi[low] = h->yhi[high];
        h->yhi[high] = t;
    }
}
