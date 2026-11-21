/* pop_lists.c - the display lists and DRAWALL.  See pop_gfx.h.
 * New code; ports of original routines (GRAFIX.S), original labels in
 * comments.
 */

#include <string.h>

#include "pop_gfx.h"

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))

enum { IMG_BASE = 0x6000 };              /* the load address image pointers assume */

void pop_gfx_init(pop_gfx *v)
{
    memset(v, 0, sizeof *v);
    pop_hires_init(&v->hr);
    v->purpleflag = 1;
    v->dummy[0] = v->dummy[1] = POP_MAXPEEL;
}

void pop_gfx_cells_in(pop_gfx *v, const pop_game *g)
{
    v->IMAGE = g->height;
    v->OFFSET = g->OFFSET;
}

void pop_gfx_cells_out(const pop_gfx *v, pop_game *g)
{
    g->height = v->IMAGE;
    g->OFFSET = v->OFFSET;
}

pop_image pop_imgtab_image(const pop_imgtab *t, uint8_t number)
{
    pop_image im = { NULL, 0 };
    size_t at = 2u * number, p;          /* setimage: pointer at 2n - 1, 2n */
    if (!t->data || number == 0 || at >= t->size) return im;
    p = (size_t)t->data[at - 1] | ((size_t)t->data[at] << 8);
    if (p < IMG_BASE || p - IMG_BASE >= t->size) return im;
    im.bytes = t->data + (p - IMG_BASE);
    im.size = t->size - (p - IMG_BASE);  /* (reading on into the next image is "modelled") */
    return im;
}

/* --- adding ------------------------------------------------------------------------------ */

void pop_zerolsts(pop_gfx *v)
{
    v->genCLS = 0;
    v->wipeX[0] = v->bgX[0] = v->midX[0] = v->fgX[0] = v->msgX[0] = 0;
}

void pop_gfx_zeropeels(pop_gfx *v)
{
    v->peelX[0] = v->peelX[POP_MAXPEEL] = 0;
}

void pop_zeropeel(pop_gfx *v)
{
    if (v->hr.PAGE != 0) {                     /* :page2 */
        v->peelX[POP_MAXPEEL] = 0;
        v->hr.PEELBUF = POP_PEELBUF2;
    } else {
        v->peelX[0] = 0;
        v->hr.PEELBUF = POP_PEELBUF1;
    }
}

void pop_addback(pop_gfx *v)
{
    uint8_t x = u8(v->bgX[0] + 1);
    if (x >= POP_MAXBACK) return;              /* list full ("shouldn't happen") */
    v->bgX[x] = v->XCO;
    if (v->YCO >= 192) return;                 /* below the screen, or wrapped above it */
    v->bgY[x] = v->YCO;
    v->bgIMG[x] = v->IMAGE;
    v->bgOP[x] = v->OPACITY;
    v->bgX[0] = x;
}

void pop_addfore(pop_gfx *v)
{
    uint8_t x = u8(v->fgX[0] + 1);
    if (x >= POP_MAXFORE) return;
    v->fgX[x] = v->XCO;
    if (v->YCO >= 192) return;
    v->fgY[x] = v->YCO;
    v->fgIMG[x] = v->IMAGE;
    v->fgOP[x] = v->OPACITY;
    v->fgX[0] = x;
}

void pop_addmsg(pop_gfx *v)
{
    uint8_t x = u8(v->msgX[0] + 1);
    if (x >= POP_MAXMSG) return;
    v->msgX[x] = v->XCO;
    v->msgOFF[x] = v->OFFSET;
    v->msgY[x] = v->YCO;
    v->msgIMG[x] = v->IMAGE;
    v->msgOP[x] = v->OPACITY;
    v->msgX[0] = x;
}

void pop_addwipe(pop_gfx *v, uint8_t colour)
{
    uint8_t x = u8(v->wipeX[0] + 1);
    if (x >= POP_MAXWIPE) return;
    v->wipeCOL[x] = colour;
    v->wipeX[x] = v->XCO;
    v->wipeY[x] = v->YCO;
    v->wipeH[x] = v->IMAGE;                    /* height */
    v->wipeW[x] = v->width;
    v->wipeX[0] = x;
}

static int mid_common(pop_gfx *v, uint8_t typ, uint8_t *slot)
{
    uint8_t x = u8(v->midX[0] + 1);
    if (x >= POP_MAXMID) return 0;
    v->midTYP[x] = typ;
    v->midX[x] = v->XCO;
    v->midOFF[x] = v->OFFSET;
    v->midY[x] = v->YCO;
    v->midIMG[x] = v->IMAGE;
    v->midTAB[x] = v->TABLE;
    *slot = x;
    return 1;
}

void pop_addmid(pop_gfx *v, const pop_game *g, uint8_t typ)
{
    uint8_t x;
    if (!mid_common(v, typ, &x)) return;
    /* a character facing right is drawn mirrored */
    v->midOP[x] = u8(((g->FCharFace ^ 0xFF) & 0x80) | v->OPACITY);
    v->midCU[x] = g->FCharCU;
    v->midCD[x] = g->FCharCD;
    v->midCL[x] = g->FCharCL;
    v->midCR[x] = g->FCharCR;
    v->midX[0] = x;
}

void pop_addmidezo(pop_gfx *v, uint8_t typ)
{
    uint8_t x;
    if (!mid_common(v, typ, &x)) return;
    v->midOP[x] = v->OPACITY;
    v->midCU[x] = v->midCL[x] = 0;
    v->midCR[x] = 40;
    v->midCD[x] = 192;
    v->midX[0] = x;
}

void pop_addpeel(pop_gfx *v)
{
    uint8_t a, x;
    if ((v->hr.PEELIMG >> 8) == 0) return;     /* layrsave saved nothing */
    a = v->hr.PAGE;
    if (a != 0) {                              /* the second page's list: maxpeel on, */
        if (v->purpleflag > 1) { v->unmodelled = 1; return; }
        a = v->dummy[v->purpleflag];           /* by way of the copy-protection table */
    }
    if (a != 0 && a != POP_MAXPEEL) { v->unmodelled = 1; return; }
    x = a;
    a = u8(v->peelX[x] + 1);
    if (a >= POP_MAXPEEL) return;
    v->peelX[x] = a;
    x = u8(a + x);
    v->peelX[x] = v->hr.PEELXCO;
    v->peelY[x] = v->hr.PEELYCO;
    v->peelIMGL[x] = u8(v->hr.PEELIMG);
    v->peelIMGH[x] = u8(v->hr.PEELIMG >> 8);
}

/* --- drawing ----------------------------------------------------------------------------- */

/* The drawing routines pass their parameters to the blitter through the
 * same variables the adding routines use, and leave them there. */

/* setbgimg: bit 7 of the coded number chooses the table. */
static pop_image bgimage(pop_gfx *v, uint8_t coded)
{
    pop_image im = pop_imgtab_image(&v->bgtable[NEG(coded) ? 1 : 0], coded & 0x7F);
    v->IMAGE = coded & 0x7F;
    v->TABLE = 0;
    if (!im.bytes) v->unmodelled = 1;
    return im;
}

/* setcharimg: TABLE is the character table's number, 0-6. */
static pop_image chimage(pop_gfx *v, uint8_t table, uint8_t number)
{
    pop_image im = { NULL, 0 };
    if (table < 7) im = pop_imgtab_image(&v->chtable[table], number);
    v->IMAGE = number;
    v->TABLE = 0;
    if (!im.bytes) v->unmodelled = 1;
    return im;
}

static void place(pop_gfx *v, uint8_t x, uint8_t y)
{
    v->hr.XCO = v->XCO = x;
    v->hr.YCO = v->YCO = y;
}

static void mode(pop_gfx *v, uint8_t opacity)
{
    v->hr.OPACITY = v->OPACITY = opacity;
}

static void dogen(pop_gfx *v, const pop_game *g)       /* DOGEN */
{
    if (v->genCLS != 0) pop_hr_cls(&v->hr);
    if (g->BGset1 == 1) v->dummy[0] = 0;       /* "purple copy-protection" */
}

static void sngpeel(pop_gfx *v)                /* SNGPEEL: last in, first out */
{
    uint8_t base = v->hr.PAGE ? POP_MAXPEEL : 0, a = v->peelX[base], x;
    while (a != 0) {
        x = u8(a + base);
        if (x >= 2 * POP_MAXPEEL) { v->unmodelled = 1; return; }
        v->IMAGE = v->peelIMGL[x];
        v->width = v->peelIMGH[x];             /* (IMAGE + 1) */
        place(v, v->peelX[x], v->peelY[x]);
        mode(v, POP_OP_STA);
        pop_hr_peel(&v->hr, (uint16_t)(v->peelIMGL[x] | (v->peelIMGH[x] << 8)));
        a--;
    }
}

static void drawwipe(pop_gfx *v)               /* DRAWWIPE */
{
    unsigned x;
    for (x = 1; x <= v->wipeX[0] && x < POP_MAXWIPE; x++) {
        v->IMAGE = v->wipeH[x];
        v->width = v->wipeW[x];
        place(v, v->wipeX[x], v->wipeY[x]);
        mode(v, v->wipeCOL[x]);
        pop_hr_fastblack(&v->hr, v->wipeW[x], v->wipeH[x], v->wipeCOL[x]);
    }
}

static void drawback(pop_gfx *v)               /* DRAWBACK */
{
    unsigned x;
    for (x = 1; x <= v->bgX[0] && x < POP_MAXBACK; x++) {
        pop_image im = bgimage(v, v->bgIMG[x]);
        if (!im.bytes) return;
        place(v, v->bgX[x], v->bgY[x]);
        mode(v, v->bgOP[x]);
        pop_hr_fastlay(&v->hr, im);
    }
}

static void drawfore(pop_gfx *v)               /* DRAWFORE */
{
    unsigned x;
    for (x = 1; x <= v->fgX[0] && x < POP_MAXFORE; x++) {
        pop_image im = bgimage(v, v->fgIMG[x]);
        if (!im.bytes) return;
        place(v, v->fgX[x], v->fgY[x]);
        if (v->fgOP[x] == POP_OP_MASK) {
            pop_hr_fastmask(&v->hr, im);
        } else {
            mode(v, v->fgOP[x]);
            pop_hr_fastlay(&v->hr, im);
        }
    }
}

static void setaddl(pop_gfx *v, unsigned x)    /* :setaddl */
{
    v->hr.OFFSET = v->OFFSET = v->midOFF[x];
    v->hr.LEFTCUT = v->midCL[x];
    v->hr.RIGHTCUT = v->midCR[x];
    v->hr.TOPCUT = v->midCU[x];
    v->hr.BOTCUT = v->midCD[x];
}

static void drawmid(pop_gfx *v)                /* DRAWMID */
{
    unsigned x;
    for (x = 1; x <= v->midX[0] && x < POP_MAXMID; x++) {
        uint8_t typ = v->midTYP[x];
        pop_image im = NEG(typ) ? chimage(v, v->midTAB[x], v->midIMG[x])
                                : bgimage(v, v->midIMG[x]);
        if (!im.bytes) return;
        place(v, v->midX[x], v->midY[x]);
        mode(v, v->midOP[x]);
        switch (typ & 0x7F) {
        case POP_MID_FASTLAY:
            pop_hr_fastlay(&v->hr, im);
            break;
        case POP_MID_LAY:
            setaddl(v, x);
            pop_hr_lay(&v->hr, im);
            break;
        case POP_MID_LAYSAVE:                  /* characters: save what is under them */
            setaddl(v, x);
            pop_hr_layrsave(&v->hr, im);
            pop_addpeel(v);
            pop_hr_lay(&v->hr, im);
            break;
        default:
            break;
        }
    }
}

static void drawmsg(pop_gfx *v)                /* DRAWMSG */
{
    unsigned x;
    for (x = 1; x <= v->msgX[0] && x < POP_MAXMSG; x++) {
        pop_image im = bgimage(v, v->msgIMG[x]);
        if (!im.bytes) return;
        place(v, v->msgX[x], v->msgY[x]);
        v->hr.OFFSET = v->OFFSET = v->msgOFF[x];
        v->hr.LEFTCUT = v->hr.TOPCUT = 0;
        v->hr.RIGHTCUT = 40;
        v->hr.BOTCUT = 192;
        mode(v, v->msgOP[x]);
        if (v->msgOP[x] & 0x40) {              /* bit 6: save under it first */
            mode(v, v->msgOP[x] & 0xBF);
            pop_hr_layrsave(&v->hr, im);
            pop_addpeel(v);
        }
        pop_hr_lay(&v->hr, im);
    }
}

void pop_drawall(pop_gfx *v, pop_game *g)
{
    pop_gfx_cells_in(v, g);
    dogen(v, g);
    sngpeel(v);                                /* the peel list set up two frames ago */
    pop_zeropeel(v);
    drawwipe(v);
    drawback(v);
    drawmid(v);
    drawfore(v);
    drawmsg(v);
    if (v->hr.unmodelled) v->unmodelled = 1;
    pop_gfx_cells_out(v, g);
}
