/* pop_frameadv.c - SURE: a whole screen from the level, as display lists.
 * See pop_gfx.h.  New code; ports of original routines (FRAMEADV.S, and two
 * of GAMEBG.S), original labels in comments.
 *
 * A block of the level is drawn as sections: A (the floor piece itself), B
 * (what the piece to its left shows above the floor line), C (what the
 * piece below-left shows through an opening), D (the floor's front edge),
 * and a front piece that goes over characters.  Rows are done bottom to
 * top, columns left to right; the routines hand each other values through
 * the variables of pop_gfx, and a few paths rely on what an earlier block
 * left there.
 */

#include "pop_gfx.h"

#include "pop_top.h"
#include "pop_world.h"

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))

/* BlockBot, TABLES.S: the bottom line of rows -1 to 3. */
static const uint8_t BLOCKBOT[5] = { 2, 65, 128, 191, 254 };

/* table[index] of the piece tables.  Like indexed addressing, an index past
 * a table's end reads what follows it; outside the block it is unmodelled. */
static uint8_t tab(pop_gfx *v, uint16_t table, unsigned index)
{
    size_t at = (size_t)table + index;
    if (!v->bg || at >= v->bg->size) { v->unmodelled = 1; return 0; }
    return v->bg->image[at];
}

#define T(name, index) tab(v, v->bg->name, (index))
#define K(name) (v->bg->name)

/* calcblue for a screen that may be 0: the original then points at
 * unrelated memory, which is all right as long as nothing is read. */
static void blue(pop_game *g, uint8_t scrn)
{
    if (scrn == 0) g->blue = 0;
    else pop_calcblue(g, scrn);
}

/* getobjid1: the piece at block y of the selected screen, as it is to be
 * drawn; sets `state`. */
static uint8_t getobjid1(pop_gfx *v, pop_game *g, uint8_t y)
{
    uint8_t id;
    if (g->blue == 0) { v->unmodelled = 1; return POP_SPACE; }
    v->state = pop_bluespec(g, y);
    id = pop_bluetype(g, y) & POP_IDMASK;
    if (id == POP_PRESSPLATE) {                /* down, if its link says so */
        return (g->blueprint[POP_LINKMAP + v->state] & 0x1F) >= 2 ? POP_DPRESSPLATE
                                                                   : POP_PRESSPLATE;
    }
    if (id == POP_UPRESSPLATE) {
        if ((g->blueprint[POP_LINKMAP + v->state] & 0x1F) >= 2) {
            v->state = 0;
            return POP_FLOOR;                  /* a pressed one looks like floor */
        }
        return POP_UPRESSPLATE;
    }
    return id;
}

static uint8_t getobjid(pop_gfx *v, pop_game *g, uint8_t y)
{
    if (g->SCRNUM == 0) return POP_SPACE;      /* GOnull: `state` stays as it was */
    return getobjid1(v, g, y);
}

/* add: to the background list (setback) or, as a byte-aligned piece, to
 * the mid list (setmid, addmidezfast). */
static void add(pop_gfx *v)
{
    if (v->addmid_sel) {
        v->OFFSET = 0;                         /* ADDMIDEZ */
        pop_addmidezo(v, POP_MID_FASTLAY);
    } else {
        pop_addback(v);
    }
}

static void maddfore(pop_gfx *v)               /* mask, then OR */
{
    v->OPACITY = POP_OP_MASK;
    pop_addfore(v);
    v->OPACITY = POP_OP_ORA;
    pop_addfore(v);
}

/* --- neighbours ------------------------------------------------------------------------------ */

static void getprev(pop_gfx *v, pop_game *g)   /* the right-hand column of the screen to the left */
{
    static const uint8_t last[3] = { 9, 19, 29 };
    int i;
    if (g->scrnLeft == 0) {                    /* nothing there: solid blocks */
        for (i = 0; i < 3; i++) { v->PREV[i] = POP_BLOCK; v->sprev[i] = 0; }
        return;
    }
    pop_calcblue(g, g->scrnLeft);
    for (i = 0; i < 3; i++) {
        v->PREV[i] = getobjid1(v, g, last[i]);
        v->sprev[i] = v->state;
    }
}

static void getbelow(pop_gfx *v, pop_game *g, uint8_t scrn_below, uint8_t scrn_below_l)
{
    uint8_t x = v->rowno, y;
    int i;
    if (x < 2) {                               /* the row below is on this screen */
        v->BELOW[0] = v->PREV[x + 1];
        v->SBELOW[0] = v->sprev[x + 1];
        y = u8(v->yindex + 10);
        for (i = 1; i < 10; i++) {
            v->BELOW[i] = getobjid(v, g, y);
            v->SBELOW[i] = v->state;
            y++;
        }
        return;
    }
    if (scrn_below == 0) {                     /* nothing below: floor (SBELOW not refreshed) */
        for (i = 1; i < 10; i++) v->BELOW[i] = POP_FLOOR;
    } else {
        pop_calcblue(g, scrn_below);
        for (i = 8; i >= 0; i--) {
            v->BELOW[i + 1] = getobjid(v, g, u8(i));
            v->SBELOW[i + 1] = v->state;
        }
    }
    if (scrn_below_l == 0) {
        v->BELOW[0] = (g->level == 12) ? POP_SPACE : POP_BLOCK;      /* (level 12's open void) */
    } else {
        pop_calcblue(g, scrn_below_l);
        v->BELOW[0] = getobjid(v, g, 9);
        v->SBELOW[0] = v->state;
    }
    blue(g, g->SCRNUM);
}

/* --- sections ---------------------------------------------------------------------------------- */

static int checkc(const pop_gfx *v)            /* is the block open at the bottom? */
{
    uint8_t o = v->objid;
    return o == POP_SPACE || o == POP_PILLARTOP || o == POP_PANELWOF || o >= POP_ARCHTOP1;
}

static void dodrawc(pop_gfx *v)
{
    uint8_t x = v->BELOW[v->colno], y, img;
    if (x == POP_BLOCK) {
        y = v->SBELOW[v->colno];
        if (y >= K(numblox)) y = 0;
        img = T(blockc, y);
        if (img == 0) return;
    } else {
        img = T(piecec, x);
        if (img == 0) return;
        if (img == K(panelc0)) {               /* a panel: which of its patterns */
            y = v->SBELOW[v->colno];
            if (y >= K(numpans)) return;
            img = T(panelc, y);
            if (img == 0) return;
        }
    }
    v->IMAGE = img;
    v->XCO = v->blockxco;
    v->YCO = v->Dy;
    v->OPACITY = POP_OP_ORA;
    add(v);
}

static void domaskb(pop_gfx *v)                /* (XCO is whatever it was) */
{
    uint8_t m = T(maskb, v->PRECED);
    if (m == 0) return;
    v->IMAGE = m;
    v->YCO = v->Dy;
    v->OPACITY = POP_OP_AND;
    add(v);
}

static void drawc(pop_gfx *v)
{
    if (!checkc(v)) return;
    dodrawc(v);
    domaskb(v);
}

static void drawgatec(pop_gfx *v)
{
    uint8_t gate;
    v->YCO = v->Dy;
    v->IMAGE = K(gatecmask);
    v->OPACITY = POP_OP_AND;
    add(v);
    gate = v->SBELOW[v->colno];
    if (gate >= K(gmaxval)) gate = K(gmaxval);
    v->gateposn = gate >> 2;
    v->IMAGE = T(gate8c, v->gateposn & 7);
    v->OPACITY = POP_OP_ORA;
    add(v);
}

static void drawmc(pop_gfx *v)
{
    uint8_t o = v->objid;
    if (o != POP_SPACE && o != POP_PANELWOF && o != POP_PILLARTOP) return;
    if (v->BELOW[v->colno] == POP_GATE) drawgatec(v);
}

static void drawb_at(pop_gfx *v, uint8_t y)    /* :cont2 */
{
    v->YCO = y;
    v->XCO = v->blockxco;
    v->OPACITY = POP_OP_ORA;
    add(v);
}

static void drawb(pop_gfx *v, const pop_game *g)
{
    uint8_t x = v->PRECED, y, img;
    if (v->objid == POP_BLOCK) return;
    if (x == POP_SPACE) {
        y = v->spreced;
        if (y >= K(numbpans) + 1) return;
        img = T(spaceb, y);
        if (img == 0) return;
        v->IMAGE = img;
        drawb_at(v, u8(T(spaceby, y) + v->Ay));
        return;
    }
    if (x == POP_FLOOR) {
        y = v->spreced;
        if (y >= K(numbpans) + 1) y = 0;
        img = T(floorb, y);
        if (img == 0) return;
        v->IMAGE = img;
        drawb_at(v, u8(T(floorby, y) + v->Ay));
        return;
    }
    if (x == POP_BLOCK) {
        y = v->spreced;
        if (y >= K(numblox)) y = 0;
        img = T(blockb, y);
        if (img == 0) { v->unmodelled = 1; return; }         /* (falls into :floor) */
        v->IMAGE = img;
        drawb_at(v, u8(T(pieceby, x) + v->Ay));
        return;
    }
    img = T(pieceb, x);
    if (img == K(panelb0)) {
        y = v->spreced;
        if (y >= K(numpans)) return;
        img = T(panelb, y);
        if (img == 0) return;
        v->IMAGE = img;
        drawb_at(v, u8(T(pieceby, x) + v->Ay));
        return;                                /* no stripe after a panel */
    }
    if (img != 0) {
        v->IMAGE = img;
        drawb_at(v, u8(T(pieceby, x) + v->Ay));
    }
    if (g->BGset1 != 1) return;                /* the stripe: palace only */
    img = T(bstripe, v->PRECED);
    if (img == 0) return;
    v->IMAGE = img;
    drawb_at(v, u8(v->Ay - 32));
}

static uint8_t getloosey(const pop_gfx *v, uint8_t a)        /* frame of a loose floor */
{
    if (!NEG(a)) return a;
    a &= 0x7F;
    if (a >= K(Ffalling) + 1) a = 1;
    return a;
}

static uint8_t getpiecea(pop_gfx *v, uint8_t x)
{
    if (x == POP_LOOSE) return T(loosea, getloosey(v, v->state));
    return T(piecea, x);
}

static void adda1(pop_gfx *v, uint8_t img, uint8_t x)
{
    v->IMAGE = img;
    v->XCO = v->blockxco;
    v->YCO = u8(v->Ay + T(pieceay, x));
    v->OPACITY = POP_OP_ORA;
    add(v);
}

static void adda(pop_gfx *v)
{
    uint8_t x = v->objid, img = getpiecea(v, x);
    if (img == 0) return;
    adda1(v, img, x);
}

static void addamask(pop_gfx *v)
{
    uint8_t m = T(maska, v->objid);
    if (m == 0) return;
    v->IMAGE = m;
    v->XCO = v->blockxco;
    v->YCO = v->Ay;
    v->OPACITY = POP_OP_AND;
    add(v);
}

static void drawa(pop_gfx *v)
{
    uint8_t p = v->PRECED;
    if (p == POP_ARCHTOP1) {
        if (v->objid == POP_PANELWOF) { adda1(v, K(archpanel), v->objid); return; }
        adda(v);
        return;
    }
    if (p == POP_PANELWIF || p == POP_PANELWOF || p == POP_PILLARTOP || p == POP_BLOCK) {
        addamask(v);                           /* the left neighbour intrudes: mask first */
    }
    adda(v);
}

static int drawd(pop_gfx *v)                   /* returns whether it added anything */
{
    uint8_t x = v->objid, y, img;
    v->OPACITY = POP_OP_STA;
    if (x == POP_BLOCK) {
        y = v->state;
        if (y >= K(numblox)) y = 0;
        img = T(blockd, y);
        if (img == 0) { v->unmodelled = 1; return 0; }
    } else {
        if (x == POP_PANELWOF) v->OPACITY = POP_OP_ORA;
        img = T(pieced, x);
        if (img == 0) return 0;
    }
    v->IMAGE = img;
    v->XCO = v->blockxco;
    v->YCO = v->Dy;
    add(v);
    return 1;
}

/* --- pieces with state ------------------------------------------------------------------------ */

static void drawloosed(pop_gfx *v)
{
    uint8_t img = T(loosed, getloosey(v, v->state));
    if (img == 0) return;
    v->IMAGE = img;
    v->XCO = v->blockxco;
    v->YCO = v->Dy;
    v->OPACITY = POP_OP_STA;
    add(v);
}

static void drawlooseb(pop_gfx *v)             /* (XCO is whatever it was) */
{
    uint8_t y = getloosey(v, v->spreced);
    v->IMAGE = K(looseb);
    v->YCO = u8(v->Ay + T(looseby, y));
    v->OPACITY = POP_OP_ORA;
    add(v);
}

static void spike(pop_gfx *v, uint8_t img)
{
    if (img == 0) return;
    v->IMAGE = img;
    v->XCO = v->blockxco;
    v->YCO = u8(v->Ay - 1);
    v->OPACITY = POP_OP_ORA;
    add(v);
}

static uint8_t spikeframe(const pop_gfx *v, uint8_t s) { return NEG(s) ? K(spikeExt) : s; }

static void drawspikea(pop_gfx *v) { spike(v, T(spikea, spikeframe(v, v->state))); }
static void drawspikeb(pop_gfx *v) { spike(v, T(spikeb, spikeframe(v, v->spreced))); }

static void drawtorchb(pop_gfx *v)             /* drawtorchb, with setupflame (GAMEBG.S 735) */
{
    uint8_t x;
    if (v->blockxco == 0) return;              /* no flame on a torch in the left column */
    v->XCO = v->blockxco;
    v->YCO = v->Ay;
    x = v->spreced;
    if (x < POP_TORCHLAST + 1) {
        v->IMAGE = T(torchflame, x);
        v->XCO = u8(v->XCO + 1);
        v->YCO = u8(v->YCO - 43);
        v->OPACITY = POP_OP_STA;
        v->TABLE = 0;                          /* (the low byte of bgtable1's address) */
    }                                          /* else: added with whatever image was set last */
    pop_addback(v);
}

static void drawflaska(pop_gfx *v)             /* drawflaska, with setupflask (GAMEBG.S 675) */
{
    uint8_t x = v->state, kind = x & 0xE0;
    v->XCO = v->blockxco;
    v->YCO = v->Ay;
    v->OFFSET = 2;
    if (kind == 0) {                           /* empty */
        x = 0;
    } else {
        if (kind == 0x40) {                    /* the tall flask */
            v->YCO = u8(v->YCO - 4);
        } else if (kind > 0x40) {
            v->OFFSET++;
            v->YCO = u8(v->YCO - 4);
        }
        x &= 0x1F;
        if (x >= POP_BUBBLAST + 1) x = 0;
    }
    v->IMAGE = T(bubble, x);
    v->XCO = u8(v->XCO + 2);
    v->YCO = u8(v->YCO - 14);
    v->OPACITY = POP_OP_STA;
    v->TABLE = 0;                              /* (the low byte of bgtable2's address) */
    pop_addmidezo(v, POP_MID_LAY);
}

static void drawsworda(pop_gfx *v)
{
    v->IMAGE = (v->state == 1) ? K(swordgleam1) : K(swordgleam0);
    v->XCO = v->blockxco;
    v->YCO = v->Ay;
    v->OPACITY = POP_OP_STA;
    add(v);
}

static uint8_t slicerindex(pop_gfx *v)
{
    uint8_t x = v->state & 0x7F;
    if (x >= K(slicerRet)) x = K(slicerRet);
    return u8(T(slicerseq, x) - 1);
}

static void drawslicera(pop_gfx *v)
{
    uint8_t x = v->xsave = slicerindex(v), img = 0;
    v->XCO = v->blockxco;
    v->YCO = v->Ay;
    if (NEG(v->state)) img = T(slicerbot2, x); /* bloodied */
    if (img == 0) img = T(slicerbot, x);
    if (img != 0) {
        v->IMAGE = img;
        v->OPACITY = POP_OP_ORA;
        add(v);
    }
    img = T(slicertop, x);
    if (img == 0) return;
    v->IMAGE = img;
    v->YCO = u8(v->Ay - T(slicergap, x));
    v->OPACITY = POP_OP_ORA;
    add(v);
}

static void drawslicerf(pop_gfx *v)
{
    uint8_t x = v->xsave = slicerindex(v), img;
    v->XCO = v->blockxco;
    v->YCO = v->Ay;
    img = T(slicerfrnt, x);
    if (img == 0) return;
    v->IMAGE = img;
    maddfore(v);
}

static void drawexitb(pop_gfx *v, pop_game *g)
{
    uint8_t a;
    v->IMAGE = K(stairs);
    v->YCO = u8(v->Ay - 12);
    if (v->blockxco >= 36) return;             /* it cannot stick out on the right */
    v->XCO = u8(v->blockxco + 1);
    v->OPACITY = POP_OP_STA;
    /* on the screen the kid starts on it is the way in: no stairs */
    if (g->SCRNUM != g->blueprint[POP_KIDSTART_SCRN]) add(v);
    a = u8(v->Dy - 67);
    if (a >= 192) return;
    v->blockthr = a;
    v->gateposn = v->spreced >> 2;
    a = u8(v->Ay - 14 - v->gateposn);
    g->doortop = a;                            /* for CROPCHAR */
    for (;;) {                                 /* the door, in strips of 4 lines */
        v->YCO = a;
        v->IMAGE = K(doormask);
        v->OPACITY = POP_OP_AND;
        add(v);
        v->IMAGE = K(door);
        v->OPACITY = POP_OP_ORA;
        add(v);
        a = u8(v->YCO - 4);
        if (a < v->blockthr) break;
    }
    a = u8(v->Ay - 64);
    if (a >= 192) return;
    v->YCO = a;
    v->IMAGE = K(toprepair);
    v->OPACITY = POP_OP_STA;
    add(v);
}

static uint8_t setupdgb(pop_gfx *v)
{
    uint8_t gate = v->spreced;
    v->blockthr = u8(v->Dy - 62);
    if (gate >= K(gmaxval)) gate = K(gmaxval);
    v->gateposn = u8((gate >> 2) + 1);
    v->gatebot = u8(v->Ay - v->gateposn);
    return v->gatebot;
}

static void restorebot(pop_gfx *v)             /* what the gate's bottom piece hides */
{
    v->IMAGE = T(pieceb, POP_GATE);
    v->YCO = u8(T(pieceby, POP_GATE) + v->Ay);
    v->XCO = v->blockxco;
    v->OPACITY = POP_OP_STA;
    add(v);
    if (checkc(v)) dodrawc(v);
    drawa(v);
}

static void drawgateb(pop_gfx *v)
{
    uint8_t a = setupdgb(v), h;
    if (u8(a + 12) < v->Ay) {
        v->YCO = v->gatebot;
        v->IMAGE = K(gatebotSTA);
        v->OPACITY = POP_OP_STA;
        pop_addback(v);
    } else {                                   /* the bottom piece crosses the floor line */
        restorebot(v);
        v->YCO = u8(v->gatebot - 2);
        v->IMAGE = K(gatebotORA);
        v->OPACITY = POP_OP_ORA;
        pop_addback(v);
    }
    v->OPACITY = POP_OP_STA;
    v->IMAGE = K(gateB1);
    a = u8(v->gatebot - 12);
    for (;;) {                                 /* the bars, 8 lines a piece */
        v->YCO = a;
        if (a >= 192) return;
        if (a < 7) break;                      /* would stick out of the block */
        if (u8(a - 7) < v->blockthr) break;
        pop_addback(v);
        a = u8(v->YCO - 8);
        if (a == 0) break;
    }
    h = u8(v->YCO - v->blockthr + 1);          /* the top piece: 1 to 8 lines */
    if (h == 0 || h >= 9) return;
    v->IMAGE = T(gate8b, h - 1u);
    pop_addback(v);
}

static void drawgatebf(pop_gfx *v)             /* the bars again, over the character */
{
    uint8_t a;
    (void)setupdgb(v);
    v->OPACITY = POP_OP_ORA;
    v->YCO = u8(v->gatebot - 2);
    v->IMAGE = K(gatebotORA);
    pop_addfore(v);
    v->IMAGE = K(gateB1);
    a = u8(v->gatebot - 12);
    for (;;) {
        v->YCO = a;
        if (a >= 192) return;
        if (a < 7) return;
        if (u8(a - 7) < v->blockthr) return;
        pop_addfore(v);
        a = u8(v->YCO - 8);
        if (a == 0) return;
    }
}

static void drawmb(pop_gfx *v, pop_game *g)
{
    switch (v->PRECED) {
    case POP_GATE:   drawgateb(v); break;
    case POP_SPIKES: drawspikeb(v); break;
    case POP_LOOSE:  drawlooseb(v); break;
    case POP_TORCH:  drawtorchb(v); break;
    case POP_EXIT:   drawexitb(v, g); break;
    default: break;
    }
}

static void drawmd(pop_gfx *v)
{
    if (v->objid == POP_LOOSE) drawloosed(v);
}

static void drawma(pop_gfx *v)
{
    switch (v->objid) {
    case POP_SPIKES: drawspikea(v); break;
    case POP_SLICER: drawslicera(v); break;
    case POP_FLASK:  drawflaska(v); break;
    case POP_SWORD:  drawsworda(v); break;
    default: break;
    }
}

static void drawfrnt(pop_gfx *v, const pop_game *g)
{
    uint8_t x, img = 0, s;
    int plain = 0;
    /* DrawGateBF?: the kid stands in the gate's block, left of the bars */
    if (v->PRECED == POP_GATE && v->rowno == g->kid.block_y
        && u8(v->colno - 1) == g->kid.block_x && g->scrnRight != g->kid.scrn) {
        drawgatebf(v);
    }
    x = v->objid;
    if (x == POP_SLICER) { drawslicerf(v); return; }
    if (x == POP_FLASK) {
        s = v->state & 0xE0;
        if (s != 0xA0 && s >= 0x40) img = K(specialflask);
    }
    if (img == 0) {
        img = T(fronti, x);
        if (img == 0) return;
    }
    v->IMAGE = img;
    v->YCO = u8(v->Ay + T(fronty, x));
    v->XCO = u8(v->blockxco + T(frontx, x));
    if (x >= POP_ARCHTOP2) {
        plain = 1;
    } else if (g->BGset1 != 1 && x == POP_POSTS) {
        plain = 1;                             /* dungeon: posts are stored, not masked */
    } else if (x == POP_BLOCK) {
        uint8_t y = v->state;
        if (y >= K(numblox)) y = 0;
        v->IMAGE = T(blockfr, y);
        plain = 1;
    }
    if (plain) {
        v->OPACITY = POP_OP_STA;
        pop_addfore(v);
    } else {
        maddfore(v);
    }
}

/* --- a block, a screen ------------------------------------------------------------------------ */

static void redblocksure(pop_gfx *v, pop_game *g)
{
    drawc(v);
    drawmc(v);
    drawb(v, g);
    drawmb(v, g);
    (void)drawd(v);
    drawmd(v);
    drawa(v);
    drawma(v);
    drawfrnt(v, g);
}

static void reddsure(pop_gfx *v, pop_game *g)    /* D-sections of the row above */
{
    drawc(v);
    drawmc(v);
    drawb(v, g);
    (void)drawd(v);
    drawmd(v);
    drawfrnt(v, g);
}

/* --- what SURE and FAST have in common: the walk over the blocks -------------------------------- */

static void startrow(pop_gfx *v, pop_game *g, int row)
{
    v->rowno = u8(row);
    v->Dy = BLOCKBOT[row + 1];
    v->Ay = u8(v->Dy - 3);
    v->yindex = u8(row * 10);
    v->PRECED = v->PREV[row];
    v->spreced = v->sprev[row];
    getbelow(v, g, g->scrnBelow, g->scrnBelowL);
}

/* the bottom row of the screen above: only its D-sections show, 3 lines
 * high at the top */
static void starttop(pop_gfx *v, pop_game *g)
{
    v->rowno = 2;
    v->Dy = 2;
    v->Ay = 0xFF;
    v->yindex = 20;
    v->PRECED = 0;                             /* (spreced keeps its last value) */
    getbelow(v, g, g->SCRNUM, g->scrnLeft);    /* "below" that row is this screen */
}

static void startblock(pop_gfx *v, uint8_t col)
{
    v->colno = col;
    v->XCO = v->blockxco = u8(col * 4);
}

static void nextblock(pop_gfx *v)
{
    v->PRECED = v->objid;
    v->spreced = v->state;
    v->yindex++;
}

void pop_sure(pop_gfx *v, pop_game *g)
{
    int row;
    uint8_t col;
    v->genCLS = 1;
    v->addmid_sel = 0;                         /* setback */
    getprev(v, g);
    blue(g, g->SCRNUM);
    for (row = 2; row >= 0; row--) {           /* bottom row first */
        startrow(v, g, row);
        for (col = 0; col < 10; col++) {
            startblock(v, col);
            v->objid = getobjid(v, g, v->yindex);
            redblocksure(v, g);
            nextblock(v);
        }
    }
    starttop(v, g);
    blue(g, g->scrnAbove);
    for (col = 0; col < 10; col++) {
        startblock(v, col);
        if (g->scrnAbove == 0) v->objid = POP_FLOOR;         /* (`state` keeps its value) */
        else v->objid = getobjid1(v, g, v->yindex);
        reddsure(v, g);
        nextblock(v);
    }
}

/* --- objects: characters, swords, impact stars, falling floors ---------------------------------- */

/* loadobj: an entry of the object list into the variables ADDMID reads. */
static uint8_t loadobj(pop_gfx *v, pop_game *g, uint8_t x)
{
    g->FCharX = (uint16_t)((g->FCharX & 0xFF00u) | g->objX[x]);      /* (the low byte only) */
    v->XCO = g->objX[x];
    v->OFFSET = g->objOFF[x];
    g->FCharY = v->YCO = g->objY[x];
    v->IMAGE = g->objIMG[x];
    v->TABLE = g->objTAB[x];
    g->FCharFace = g->objFACE[x];
    g->FCharCU = g->objCU[x];
    g->FCharCD = g->objCD[x];
    g->FCharCL = g->objCL[x];
    g->FCharCR = g->objCR[x];
    return g->objTYP[x];
}

/* A character image: saved under, then drawn, from a character table. */
static void addchar(pop_gfx *v, const pop_game *g, uint8_t opacity)
{
    v->OPACITY = opacity;
    pop_addmid(v, g, POP_MID_LAYSAVE | POP_MID_CHARTABLE);
}

static void drawkid(pop_gfx *v, const pop_game *g)       /* DRAWKID */
{
    if (g->backtolife != 0 && v->hr.PAGE == 0) return;   /* blinks while coming back to life */
    /* merging with the shadow: every other frame he is drawn as the shadow is */
    if (!NEG(g->mergetimer) && (g->mergetimer & 1)) addchar(v, g, POP_OP_EOR);
    else addchar(v, g, POP_OP_MASK);
}

static void drawguard(pop_gfx *v, const pop_game *g)     /* DRAWGUARD */
{
    if (g->GuardColor != 0) {                  /* DrawShifted: one dot right, the other colours */
        uint8_t a = u8(v->OFFSET + 1);         /* chgoffset */
        if (a >= 7) {
            v->XCO++;
            a = u8(a - 7);
        }
        v->OFFSET = a;
    }
    addchar(v, g, POP_OP_MASK);
}

/* DrawFF: a falling floor is drawn as its three sections, like the loose
 * floor it was, at the place the mover has it. */
static void drawff(pop_gfx *v, pop_game *g)
{
    g->FCharFace = 0xFF;                       /* not mirrored */
    g->FCharImage = v->IMAGE;                  /* the frame of the loose floor */
    v->YCO = u8(g->FCharY - 3);                /* A-section: mask, then piece */
    v->IMAGE = T(maska, POP_FLOOR);
    v->OPACITY = POP_OP_AND;
    pop_addmid(v, g, POP_MID_LAYSAVE);
    v->IMAGE = T(loosea, g->FCharImage);
    v->OPACITY = POP_OP_ORA;
    pop_addmid(v, g, POP_MID_LAY);
    v->IMAGE = T(loosed, g->FCharImage);       /* D-section */
    v->YCO = g->FCharY;
    v->OPACITY = POP_OP_STA;
    pop_addmid(v, g, POP_MID_LAYSAVE);
    v->XCO = u8(u8(g->FCharX) + 4);            /* B-section, in the next block */
    v->YCO = u8(g->FCharY - 4);
    v->IMAGE = K(looseb);
    v->OPACITY = POP_OP_ORA;
    pop_addmid(v, g, POP_MID_LAYSAVE);
}

static void drawobjx(pop_gfx *v, pop_game *g, uint8_t x)
{
    switch (loadobj(v, g, x)) {
    case POP_OBJ_KID:
    case POP_OBJ_REFLECT: drawkid(v, g); break;
    case POP_OBJ_SHAD:    addchar(v, g, POP_OP_EOR); break;          /* DRAWSHAD */
    case POP_OBJ_FF:      drawff(v, g); break;
    case POP_OBJ_SWORD:
    case POP_OBJ_COMIX:   addchar(v, g, POP_OP_MASK); break;         /* DRAWSWORD */
    case POP_OBJ_GD:      drawguard(v, g); break;
    default: break;
    }
}

/* sortlist: what is lower on the screen is to be drawn first, and an
 * opponent last.  One pass from the end of the list: the flag that would
 * ask for another (`switches`) is never set. */
static void sortlist(pop_gfx *v, const pop_game *g)
{
    uint8_t x = v->sortX[0];
    while (x != 1) {
        uint8_t obj1 = v->sortX[x], obj2 = v->sortX[x - 1];          /* compare */
        if (g->objTYP[obj1] != POP_OBJ_SHAD && g->objY[obj1] > g->objY[obj2]) {
            v->sortX[x] = obj2;
            v->sortX[x - 1] = obj1;
        }
        x--;
        if (x == 0) break;
    }
}

/* drawobjs: the objects indexed to v->yindex, onto the mid list. */
static void drawobjs(pop_gfx *v, pop_game *g)
{
    uint8_t n = g->objX[0], x = 1, y = 0;
    if (n == 0) return;
    do {
        if (x >= POP_MAXOBJ) { v->unmodelled = 1; return; }
        if (g->objINDX[x] == v->yindex) {
            y++;
            if (y >= sizeof v->sortX) { v->unmodelled = 1; return; }
            v->sortX[y] = x;
        }
        x++;
    } while (x <= n);
    if (y == 0) return;
    v->sortX[0] = y;
    sortlist(v, g);
    x = 1;
    do {
        v->xsave = x;
        drawobjx(v, g, v->sortX[x]);
        x = u8(v->xsave + 1);
    } while (x <= v->sortX[0]);
}

/* --- only what changed ------------------------------------------------------------------------- */

/* The redraw buffers (pop_game.rb): a count per block of how many more
 * frames something there is to be redrawn; two, as a rule, one for each
 * display page. */
static uint8_t *rb(pop_game *g, unsigned buffer, uint8_t y)
{
    return &g->rb[(buffer + y) % POP_RB_SIZE];
}

/* metbufs: is any of blocks y ... y + n - 1 marked?  (The meters lie over
 * the bottom row: the kid's over blocks 20-22, the opponent's over 28-29.) */
static uint8_t metbufs(pop_game *g, uint8_t y, unsigned n)
{
    uint8_t a = 0;
    for (; n > 0; n--, y++) {
        a |= *rb(g, POP_RB_RED, y) | *rb(g, POP_RB_FLOOR, y) | *rb(g, POP_RB_HALF, y)
             | *rb(g, POP_RB_FRED, y) | *rb(g, POP_RB_WIPE, y);
    }
    return a;
}

static void wipe(pop_gfx *v, uint8_t lines)    /* ]wipe: black out the block's width */
{
    v->IMAGE = lines;                          /* height = IMAGE */
    v->width = 4;
    v->XCO = v->blockxco;
    v->YCO = v->Dy;
    pop_addwipe(v, 0x80);
}

static void wiped(pop_gfx *v)                  /* the D-section's three lines */
{
    uint8_t o = v->objid;
    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_PANELWOF || o == POP_BLOCK) return;
    wipe(v, 3);
}

static void drawflr(pop_gfx *v)                /* ]drawflr */
{
    addamask(v);
    adda(v);
    drawma(v);
    (void)drawd(v);
}

static void drawfloor(pop_gfx *v)              /* a floor piece over a character behind it */
{
    if (v->PRECED != 0) return;                /* only with empty space to the left */
    drawflr(v);
}

static void halfmask(pop_gfx *v)               /* drawhalf :sub */
{
    v->IMAGE = K(CUmask);
    v->XCO = v->blockxco;
    v->YCO = v->Ay;
    if (v->objid == POP_DPRESSPLATE) v->YCO++; /* ("quick trick") */
    v->OPACITY = POP_OP_AND;
    add(v);
}

static void drawhalf(pop_gfx *v, const pop_game *g)    /* drawfloor for a climb-up */
{
    uint8_t x = v->objid, img;
    if (v->PRECED != 0) return;
    if (x == POP_FLOOR || x == POP_TORCH || x == POP_DPRESSPLATE || x == POP_EXIT) {
        halfmask(v);
        img = K(CUpiece);
    } else {
        /* no half piece for this one: the whole floor piece */
        if (g->BGset1 != 1 || (x != POP_POSTS && x != POP_ARCHBOT)) { drawflr(v); return; }
        halfmask(v);
        img = K(CUpost);
    }
    v->IMAGE = img;
    v->OPACITY = POP_OP_ORA;
    add(v);
    (void)drawd(v);
}

static void redblockfast(pop_gfx *v, pop_game *g)
{
    uint8_t y = v->yindex;
    if (*rb(g, POP_RB_WIPE, y) != 0) {
        (*rb(g, POP_RB_WIPE, y))--;
        wipe(v, *rb(g, POP_RB_WHITE, y));      /* wipesq: as high as whitebuf says */
    }
    if (*rb(g, POP_RB_RED, y) != 0) {          /* the whole block */
        (*rb(g, POP_RB_RED, y))--;
        v->addmid_sel = 0;
        redblocksure(v, g);
        /* (`bpl`: the moving parts were redrawn with it; their mark stays) */
    } else if (*rb(g, POP_RB_MOVE, y) != 0) {  /* only its moving parts */
        (*rb(g, POP_RB_MOVE, y))--;
        v->addmid_sel = 0;
        drawmc(v);
        drawmb(v, g);
        drawma(v);
    }
    if (*rb(g, POP_RB_FLOOR, y) != 0) {        /* the floor piece, in front of a character */
        (*rb(g, POP_RB_FLOOR, y))--;
        v->addmid_sel = 1;
        drawfloor(v);
    } else if (*rb(g, POP_RB_HALF, y) != 0) {
        (*rb(g, POP_RB_HALF, y))--;
        v->addmid_sel = 1;
        drawhalf(v, g);
    }
    if (*rb(g, POP_RB_OBJ, y) != 0) {          /* the objects in this block */
        *rb(g, POP_RB_OBJ, y) = 0;
        drawobjs(v, g);
        v->XCO = v->blockxco;
    }
    if (*rb(g, POP_RB_FRED, y) != 0) {         /* the front piece */
        (*rb(g, POP_RB_FRED, y))--;
        drawfrnt(v, g);
    }
}

static void reddfast(pop_gfx *v, pop_game *g)
{
    uint8_t *mark = rb(g, POP_RB_TOP, v->colno);
    if (*mark == 0) return;
    (*mark)--;
    wiped(v);
    drawc(v);
    drawmc(v);
    drawb(v, g);
    if (drawd(v)) pop_addfore(v);              /* redrawd: on both planes */
    drawmd(v);
    drawfrnt(v, g);
}

/* --- strength meters (GAMEBG.S) ----------------------------------------------------------------- */

/* Where the units of the two meters go: byte column and dot offset,
 * GAMEBG.S 93-97.  The kid's run left to right, the opponent's right to
 * left and mirrored. */
static const uint8_t KIDSTRX[12] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 };
static const uint8_t KIDSTROFF[12] = { 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4 };
static const uint8_t OPPSTRX[12] = { 39, 38, 37, 36, 35, 34, 32, 31, 30, 29, 28, 27 };
static const uint8_t OPPSTROFF[12] = { 5, 4, 3, 2, 1, 0, 6, 5, 4, 3, 2, 1 };

static void meterimg(pop_gfx *v, const uint8_t *xs, const uint8_t *offs, uint8_t unit)
{
    if (unit >= 12) { v->unmodelled = 1; return; }
    v->XCO = xs[unit];
    v->OFFSET = offs[unit];
    pop_addmsg(v);
}

/* How many units the next image shows: lines of 3, 2 or 1 while more than
 * one is left; 0 means "the last one", which is drawn on its own. */
static uint8_t meterline(uint8_t left)
{
    return left >= 4 ? 3 : left >= 3 ? 2 : left >= 2 ? 1 : 0;
}

void pop_drawkidmeter(pop_gfx *v, const pop_game *g)
{
    uint8_t left, n, unit;
    v->YCO = 191;
    v->OPACITY = POP_OP_STA;
    v->xsave = 0;                              /* units drawn so far */
    for (;;) {
        left = (uint8_t)(g->KidStrength - v->xsave);
        if (left == 0) break;
        n = meterline(left);
        if (n == 0) {                          /* :drawlast: it blinks when it is the only one */
            if (g->KidStrength >= 2 || v->hr.PAGE != 0) {
                v->IMAGE = K(bullet);
                unit = v->xsave;
                v->xsave++;
                meterimg(v, KIDSTRX, KIDSTROFF, unit);
            }
            break;
        }
        v->IMAGE = T(bline, n - 1u);
        unit = v->xsave;
        v->xsave = (uint8_t)(v->xsave + n);
        meterimg(v, KIDSTRX, KIDSTROFF, unit);
    }
    v->OPACITY = POP_OP_AND;                   /* :darkpart: empty units up to the maximum */
    v->IMAGE = K(blank);
    while (v->xsave < g->MaxKidStr) {
        meterimg(v, KIDSTRX, KIDSTROFF, v->xsave);
        v->xsave++;
        if (v->xsave == 0) break;
    }
}

void pop_drawoppmeter(pop_gfx *v, const pop_game *g)
{
    uint8_t left, n, unit;
    if (g->OppStrength == 0) return;
    if (g->shad.id == 24 || g->shad.id == 4) return;         /* no meter for mouse or skeleton */
    if (g->shad.id == 1 && g->level != 12) return;           /* the shadow's only on level 12 */
    v->YCO = 191;
    v->OPACITY = POP_OP_STA | POP_OP_MIRROR;
    v->xsave = 0;
    for (;;) {
        left = (uint8_t)(g->OppStrength - v->xsave);
        if (left == 0) break;
        n = meterline(left);
        if (n == 0) {
            if (g->OppStrength >= 2 || v->hr.PAGE != 0) {
                v->IMAGE = K(bullet);
                unit = v->xsave;
                v->xsave++;
                meterimg(v, OPPSTRX, OPPSTROFF, unit);
                return;                        /* (no empty unit after it) */
            }
            break;
        }
        v->IMAGE = T(bline, n - 1u);
        unit = v->xsave;
        v->xsave = (uint8_t)(v->xsave + n);
        meterimg(v, OPPSTRX, OPPSTROFF, unit);
    }
    v->OPACITY = POP_OP_AND | POP_OP_MIRROR;   /* :darkpart: one empty unit */
    v->IMAGE = K(blank);
    meterimg(v, OPPSTRX, OPPSTROFF, v->xsave);
}

void pop_fast(pop_gfx *v, pop_game *g)
{
    int row;
    uint8_t col;
    getprev(v, g);
    blue(g, g->SCRNUM);
    v->redkidmeter = metbufs(g, 20, 3);        /* a meter under a redrawn block is drawn again */
    v->redoppmeter = metbufs(g, 28, 2);
    v->yindex = 30;                            /* characters off the screen first */
    drawobjs(v, g);
    for (row = 2; row >= 0; row--) {
        startrow(v, g, row);
        for (col = 0; col < 10; col++) {
            startblock(v, col);
            v->objid = getobjid(v, g, v->yindex);
            redblockfast(v, g);
            nextblock(v);
        }
    }
    v->addmid_sel = 0;                         /* :cont setback */
    starttop(v, g);
    if (g->scrnAbove != 0) {                   /* (unlike SURE: nothing without a screen above) */
        pop_calcblue(g, g->scrnAbove);
        for (col = 0; col < 10; col++) {
            startblock(v, col);
            v->objid = getobjid1(v, g, v->yindex);
            reddfast(v, g);
            nextblock(v);
        }
    }
    v->yindex = 0xFF;                          /* impact stars on top of everything */
    drawobjs(v, g);
    if (v->redkidmeter != 0) pop_drawkidmeter(v, g);         /* UPDATEMETERS */
    if (v->redoppmeter != 0) pop_drawoppmeter(v, g);
}
