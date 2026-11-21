/* pop_screen.c - messages over the picture, and what the monitor is
 * shown.  See pop_gfx.h.
 * New code; ports of original routines (GAMEBG.S, SUBS.S, TOPCTRL.S),
 * original labels in comments.
 */

#include "pop_gfx.h"
#include "pop_loop.h"

#define u8(v) ((uint8_t)(v))

/* --- messages ----------------------------------------------------------------------------------- */

/* Where each box goes and which image it is: line, byte column, dot
 * offset, image of the second background table (GAMEBG.S 162-172). */
enum { MY = 90, LOWMY = 153, HICONTY = 73, LOWCONTY = 168 };
typedef struct box { uint8_t y, x, offset, image; } box;
static const box CONTBOX  = { HICONTY, 13, 0, 0x7C };       /* "Press button to continue" */
static const box MSGBOX   = { MY, 15, 0, 0x7B };            /* an empty box */
static const box LEVELMSG = { MY - 5, 16, 3, 0x7A };        /* "LEVEL" */
static const box TIMELEFT = { MY, 11, 0, 0x7D };            /* "MINUTES LEFT" */
static const box SECONDS  = { MY - 5, 14, 0, 0x7F };        /* "SECONDS" */

/* Numbers 0-12 as images (GAMEBG.S 177-181): tens, units. */
static const uint8_t DIGIT1[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x71, 0x71, 0x71 };
static const uint8_t DIGIT2[13] = { 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
                                    0x70, 0x71, 0x72 };

static void setupimage(pop_gfx *v, const box *b)
{
    v->YCO = b->y;
    v->XCO = b->x;
    v->OFFSET = b->offset;
    v->IMAGE = b->image;
}

static void superim1(pop_gfx *v)               /* drawn over the picture, which is saved under it */
{
    v->OPACITY = POP_OP_STA | 0x40;
    pop_addmsg(v);
}

static uint8_t digit2(pop_gfx *v, uint8_t x)
{
    if (x >= sizeof DIGIT2) { v->unmodelled = 1; return 0; }  /* (the original reads on) */
    return DIGIT2[x];
}

static uint8_t getlevelno(const pop_game *g)
{
    return g->level >= 13 ? 12 : g->level;
}

void pop_printlevel(pop_gfx *v, const pop_game *g)
{
    uint8_t x, a;
    setupimage(v, &MSGBOX);
    superim1(v);
    setupimage(v, &LEVELMSG);
    if (getlevelno(g) >= 10) v->OFFSET = 0;    /* two digits: the word moves 3 dots left */
    v->OPACITY = POP_OP_ORA;
    pop_addmsg(v);
    v->XCO = u8(v->XCO + 6);
    x = getlevelno(g);
    a = DIGIT1[x];
    if (a != 0) {
        v->IMAGE = a;
        v->OPACITY = POP_OP_ORA;
        pop_addmsg(v);
        v->XCO = u8(v->XCO + 1);
    }
    v->IMAGE = DIGIT2[x];
    v->OPACITY = POP_OP_ORA;
    pop_addmsg(v);
}

void pop_timeleftmsg(pop_gfx *v, pop_game *g)
{
    uint8_t temp, y;
    setupimage(v, &TIMELEFT);
    /* in the last minute the box keeps out of the kid's way: lower when
     * he is on the middle row and not falling (by the time last worked out) */
    if (g->MinLeft < 2 && g->kid.action != 3 && g->kid.action != 4 && g->kid.block_y == 1) {
        v->YCO = LOWMY;
    }
    superim1(v);
    v->YCO = u8(v->YCO - 5);
    v->XCO = u8(v->XCO + 1);
    v->OPACITY = POP_OP_ORA;
    pop_getminleft(g);
    temp = g->MinLeft;                         /* BCD */
    if (temp < 2) temp = g->SecLeft;
    if ((temp >> 4) != 0) {
        v->IMAGE = digit2(v, temp >> 4);
        pop_addmsg(v);
    }
    v->XCO = u8(v->XCO + 1);
    v->IMAGE = digit2(v, temp & 0x0F);
    pop_addmsg(v);
    if (g->MinLeft >= 2) return;
    y = v->YCO;                                /* "SECONDS" over "MINUTES" */
    setupimage(v, &SECONDS);
    v->YCO = y;
    v->OPACITY = POP_OP_STA;
    pop_addmsg(v);
}

void pop_continuemsg(pop_gfx *v, const pop_game *g)
{
    setupimage(v, &CONTBOX);
    if ((g->kid.block_x & 1) == 0) v->YCO = LOWCONTY;        /* (high or low by where he lies) */
    superim1(v);
}

void pop_show_message(pop_gfx *v, pop_game *g, uint8_t kind)
{
    pop_gfx_cells_in(v, g);
    switch (kind) {
    case POP_SHOW_CONTINUE: pop_continuemsg(v, g); break;
    case POP_SHOW_LEVEL:    pop_printlevel(v, g); break;
    case POP_SHOW_TIME:     pop_timeleftmsg(v, g); break;
    default: break;
    }
    pop_gfx_cells_out(v, g);
}

/* --- the screen --------------------------------------------------------------------------------- */

/* The display soft switches: $C054 / $C055 page 1 / 2, $C056 / $C057
 * lo-res / hi-res, $C050 / $C051 graphics / text.  (The original touches
 * them with `lda`.) */
static void note(pop_gfx *v, char what)
{
    if (v->nswlog + 1u < sizeof v->swlog) {
        v->swlog[v->nswlog++] = what;
        v->swlog[v->nswlog] = 0;
    }
}

static void page2(pop_gfx *v, uint8_t on) { v->sw_page2 = on; note(v, on ? '2' : '1'); }
static void hires(pop_gfx *v, uint8_t on) { v->sw_hires = on; note(v, on ? 'h' : 'l'); }
static void text(pop_gfx *v, uint8_t on)  { v->sw_text = on;  note(v, on ? 't' : 'g'); }

void pop_lrclse(pop_gfx *v, uint8_t value)
{
    v->scrncolor = value;                      /* (only filled anew if the value changes) */
}

void pop_cut_begin(pop_gfx *v)
{
    v->hr.PAGE = 0x20;
    pop_lrclse(v, 0xA0);                       /* spaces */
    page2(v, 0);
    text(v, 1);
}

void pop_cut_copy(pop_gfx *v, pop_game *g)
{
    uint8_t to, from;
    v->hr.PAGE = 0;
    to = u8(v->hr.PAGE + 0x20);                /* COPYSCRN, GRAFIX.S 1369: to the page drawn on */
    from = to ^ 0x60;
    v->width = to;                             /* (IMAGE + 1 and IMAGE carry the two addresses) */
    g->height = v->IMAGE = from;
    pop_hr_copyscrn(&v->hr, from, to);
}

void pop_pageflip(pop_gfx *v, const pop_game *g)
{
    if (v->hr.PAGE == 0) {
        v->hr.PAGE = 0x20;
        page2(v, 0);                           /* show page 1, draw on page 2 */
    } else {
        v->hr.PAGE = 0;
        page2(v, 1);
    }
    hires(v, 1);
    text(v, 0);
    v->sw_an3 = g->vibes != 0 ? 0 : 1;         /* $C05E / $C05F */
}

void pop_doflashon(pop_gfx *v, uint8_t colour)
{
    pop_lrclse(v, colour);
    page2(v, 0);
    hires(v, 0);                               /* the lo-res screen: one colour all over */
}

void pop_doflashoff(pop_gfx *v)
{
    if (v->hr.PAGE == 0) page2(v, 1);
    hires(v, 1);
}
