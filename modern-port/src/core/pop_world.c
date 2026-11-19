/* pop_world.c - see pop_world.h.  New code; ports of original routines. */

#include "pop_world.h"


/* --- tables (TABLES.S) -----------------------------------------------------------
 *
 * The original looks these up in tables built by assembler loops; the rules
 * below reproduce them and are checked against the assembled tables.
 */

/* BlockTable / PixelTable, index = x 0-255: 14-unit columns, column 0 at x=58. */
static uint8_t block_table(uint8_t x) { return (uint8_t)((x + 12u) / 14u - 5u); }
static uint8_t pixel_table(uint8_t x) { return (uint8_t)((x + 12u) % 14u); }

/* BlockEdge (20), then BlockTop, BlockBot, FloorY, BlockAy (5 each), which
 * follow it in memory: an index past 19 reads into them, as it would on the
 * original. */
static int edge_tables(unsigned i, uint8_t *out)
{
    if (i < 20) {
        *out = (uint8_t)(14u * i - 12u);
        return 1;
    }
    i -= 20;
    if (i < 20) {
        /* index within each 5-entry table corresponds to block row -1..3 */
        unsigned t = i / 5, r = i % 5;
        int bot = POP_SCRN_BOT - POP_BLOCK_HEIGHT * (int)(3u - r);     /* BlockBot */
        int v;
        switch (t) {
        case 0:  v = bot - POP_BLOCK_HEIGHT + 1; break;                /* BlockTop */
        case 1:  v = bot; break;                                       /* BlockBot */
        case 2:  v = bot - POP_VERT_DIST; break;                       /* FloorY   */
        default: v = bot - 3; break;                                   /* BlockAy  */
        }
        *out = (uint8_t)v;
        return 1;
    }
    return 0;
}

/* --- map and blueprint ------------------------------------------------------- */

/* GETLEFT etc.: `beq ]rts / asl / asl / tax / lda MAP-4,x`.  The index is
 * 8-bit; screen numbers above 24 read whatever follows MAP in the blueprint. */
static uint8_t map_entry(pop_game *g, uint8_t scrn, unsigned which)
{
    uint8_t x;
    if (scrn == 0) {
        return 0;
    }
    x = (uint8_t)(scrn << 2);
    g->rx = x;
    return g->blueprint[POP_MAP - 4u + which + x];
}

uint8_t pop_getleft(pop_game *g, uint8_t scrn)  { return map_entry(g, scrn, 0); }
uint8_t pop_getright(pop_game *g, uint8_t scrn) { return map_entry(g, scrn, 1); }
uint8_t pop_getup(pop_game *g, uint8_t scrn)    { return map_entry(g, scrn, 2); }
uint8_t pop_getdown(pop_game *g, uint8_t scrn)  { return map_entry(g, scrn, 3); }

void pop_calcblue(pop_game *g, uint8_t scrn)
{
    /* The original indexes Mult30, which has 32 entries; screen 0 selects the
     * editor's menu data and anything above 32 reads past the table. */
    if (scrn == 0 || scrn > 32) {
        g->unmodelled = 1;
        g->blue = 0;
        return;
    }
    g->blue = scrn;
}

static size_t blue_index(const pop_game *g, uint8_t y)
{
    return (size_t)(g->blue - 1u) * 30u + y;
}

uint8_t pop_bluetype(const pop_game *g, uint8_t y)
{
    size_t i = POP_BLUETYPE + blue_index(g, y);
    return (g->blue && i < POP_LEVEL_SIZE) ? g->blueprint[i] : 0;
}

uint8_t pop_bluespec(const pop_game *g, uint8_t y)
{
    size_t i = POP_BLUESPEC + blue_index(g, y);
    return (g->blue && i < POP_LEVEL_SIZE) ? g->blueprint[i] : 0;
}

void pop_set_bluespec(pop_game *g, uint8_t y, uint8_t value)
{
    size_t i = POP_BLUESPEC + blue_index(g, y);
    if (g->blue && i < POP_LEVEL_SIZE) {
        g->blueprint[i] = value;
    }
}

/* handler, CTRLSUBS.S 162-178: follow off-screen coordinates to the screen
 * they belong to.  Block x is tested as a signed byte, then as >= 10. */
static void handler(pop_game *g)
{
    for (;;) {
        uint8_t a = g->tempblockx;
        if (a & 0x80u) {                              /* offleft */
            g->tempblockx = (uint8_t)(a + 10u);
            g->tempscrn = pop_getleft(g, g->tempscrn);
            continue;
        }
        if (a >= 10) {                                /* offrt */
            g->tempblockx = (uint8_t)(a - 10u);
            g->tempscrn = pop_getright(g, g->tempscrn);
            continue;
        }
        a = g->tempblocky;
        if (a & 0x80u) {                              /* offtop */
            g->tempblocky = (uint8_t)(a + 3u);
            g->tempscrn = pop_getup(g, g->tempscrn);
            continue;
        }
        if (a >= 3) {                                 /* offbot */
            g->tempblocky = (uint8_t)(a - 3u);
            g->tempscrn = pop_getdown(g, g->tempscrn);
            continue;
        }
        return;
    }
}

uint8_t pop_rdblock1(pop_game *g)
{
    uint8_t y, id;
    handler(g);
    if (g->tempscrn == 0) {                           /* :nullscrn */
        g->rx = POP_BLOCK;                            /* ry is left as it was */
        return POP_BLOCK;
    }
    pop_calcblue(g, g->tempscrn);
    y = (uint8_t)(10u * g->tempblocky + g->tempblockx);   /* Mult10,y + tempblockx */
    id = pop_bluetype(g, y) & POP_IDMASK;
    g->rx = id;
    g->ry = y;
    return id;
}

uint8_t pop_rdblock(pop_game *g, uint8_t scrn, uint8_t x, uint8_t y)
{
    g->tempscrn = scrn;
    g->tempblockx = x;
    g->tempblocky = y;
    g->ry = y;                                        /* the Y register at entry */
    return pop_rdblock1(g);
}

/* --- around the character ---------------------------------------------------------- */

/* `ldx CharFace / inx / ... adc plus1,x` (or minus1,x): plus1 = -1, 1 and
 * minus1 = 1, -1 sit next to each other, so faces 1 and 2 read on into
 * minus1.  Other values index into code and are not modelled. */
static uint8_t face_step(pop_game *g, int behind)
{
    static const uint8_t steps[4] = { 0xFF, 0x01, 0x01, 0xFF };   /* plus1, minus1 */
    unsigned x = (uint8_t)(g->ch.face + 1u) + (behind ? 2u : 0u);
    if (x > 3) {
        g->unmodelled = 1;
        return 0;
    }
    return steps[x];
}

uint8_t pop_getunderft(pop_game *g)
{
    return pop_rdblock(g, g->ch.scrn, g->ch.block_x, g->ch.block_y);
}

uint8_t pop_getinfront(pop_game *g)
{
    g->infrontx = (uint8_t)(g->ch.block_x + face_step(g, 0));
    return pop_rdblock(g, g->ch.scrn, g->infrontx, g->ch.block_y);
}

uint8_t pop_get2infront(pop_game *g)
{
    uint8_t step = face_step(g, 0);
    return pop_rdblock(g, g->ch.scrn, (uint8_t)(g->ch.block_x + step + step), g->ch.block_y);
}

uint8_t pop_getbehind(pop_game *g)
{
    g->behindx = (uint8_t)(g->ch.block_x + face_step(g, 1));
    return pop_rdblock(g, g->ch.scrn, g->behindx, g->ch.block_y);
}

uint8_t pop_getabove(pop_game *g)
{
    g->abovey = (uint8_t)(g->ch.block_y - 1u);
    return pop_rdblock(g, g->ch.scrn, g->ch.block_x, g->abovey);
}

uint8_t pop_getaboveinf(pop_game *g)
{
    g->infrontx = (uint8_t)(g->ch.block_x + face_step(g, 0));
    g->abovey = (uint8_t)(g->ch.block_y - 1u);
    return pop_rdblock(g, g->ch.scrn, g->infrontx, g->abovey);
}

uint8_t pop_getabovebeh(pop_game *g)
{
    g->behindx = (uint8_t)(g->ch.block_x + face_step(g, 1));
    g->abovey = (uint8_t)(g->ch.block_y - 1u);
    return pop_rdblock(g, g->ch.scrn, g->behindx, g->abovey);
}

/* GETBASEX: A = Fdx - (Fcheck & $1F), then ADDCHARX. */
uint8_t pop_getbasex(pop_game *g)
{
    uint8_t a = (uint8_t)(0u - (g->Fcheck & 0x1Fu));
    a = (uint8_t)(a + g->Fdx);
    return pop_add_char_x(&g->ch, a);
}

uint8_t pop_getblockx(pop_game *g, uint8_t x)
{
    g->ry = x;
    g->OFFSET = pixel_table(x);
    return block_table(x);
}

uint8_t pop_getblockxp(pop_game *g, uint8_t x)
{
    return pop_getblockx(g, (uint8_t)(x - POP_ANGLE));
}

/* GETDIST1: `lda CharFace / beq :facingright` - only an exact 0 faces right. */
uint8_t pop_getdist1(pop_game *g, uint8_t x)
{
    (void)pop_getblockxp(g, x);
    if (g->ch.face != POP_FACE_RIGHT) {
        return g->OFFSET;
    }
    return (uint8_t)(13u - g->OFFSET);
}

uint8_t pop_getdist(pop_game *g)
{
    return pop_getdist1(g, pop_getbasex(g));
}

void pop_getbaseblock(pop_game *g)
{
    g->ch.block_x = pop_getblockxp(g, pop_getbasex(g));
}

/* FACEDX: `bit CharFace / bmi ]rts / negate`.  The source comment says the
 * opposite, but the code returns dx unchanged when facing LEFT. */
uint8_t pop_facedx(const pop_game *g, uint8_t dx)
{
    if (g->ch.face & 0x80u) {
        return dx;
    }
    return (uint8_t)(0u - dx);
}

uint8_t pop_getblockej(pop_game *g, uint8_t block)
{
    uint8_t x = (uint8_t)(block + 5u), v = 0;
    g->rx = x;
    if (!edge_tables(x, &v)) {
        g->unmodelled = 1;
    }
    return v;
}

/* GETBLOCKY: X counts 3..0 while A < BlockTop+1,x; $FF if below all four. */
uint8_t pop_getblocky(uint8_t y)
{
    int x;
    for (x = 3; x >= 0; x--) {
        int top = POP_SCRN_BOT + 1 - POP_BLOCK_HEIGHT * (3 - x);       /* 3, 66, 129, 192 */
        if (y >= top) break;
    }
    return (uint8_t)x;
}

uint8_t pop_getblockyp(uint8_t y)
{
    int x;
    for (x = 3; x >= 0; x--) {
        int floor = POP_SCRN_BOT - POP_VERT_DIST - POP_BLOCK_HEIGHT * (2 - x);  /* 55..244 */
        if (y >= floor) break;
    }
    return (uint8_t)x;
}

/* --- piece classes ---------------------------------------------------------------- */

uint8_t pop_cmpspace(uint8_t id)
{
    if (id == POP_SPACE || id == POP_PILLARTOP || id == POP_PANELWOF || id == POP_BLOCK
        || id >= POP_ARCHTOP1) {
        return 0;
    }
    return 1;
}

uint8_t pop_cmpbarr(uint8_t id)
{
    if (id == POP_PANELWIF || id == POP_PANELWOF || id == POP_GATE) return 1;
    if (id == POP_MIRROR || id == POP_SLICER) return 3;
    if (id == POP_BLOCK) return 4;
    return 0;
}

uint8_t pop_cmpwall(const pop_game *g, uint8_t id)
{
    if (id == POP_BLOCK) return 0;
    if ((g->ch.face & 0x80u) && (id == POP_PANELWIF || id == POP_PANELWOF)) return 0;
    return 1;
}

/* --- frame information ------------------------------------------------------------ */

/* getfindex: (A - 1) * 5 as a 16-bit value; A - 1 wraps in 8 bits. */
static uint16_t findex(uint8_t a)
{
    return (uint16_t)((uint8_t)(a - 1u) * 5u);
}

void pop_getframe(pop_game *g, uint8_t frame)
{
    g->framepoint = (uint16_t)(findex(frame) + g->frames->fdef);
}

/* usealtsets, CTRLSUBS.S 1685-1715. */
static void usealtsets(pop_game *g)
{
    uint8_t id = g->ch.id, a = g->ch.posn;
    if (id == 0 || id == 24) {
        return;                                       /* kid and mouse: main set */
    }
    if (id >= 5) {                                    /* princess, vizier */
        g->framepoint = (uint16_t)(findex(a) + g->frames->altset2);
        return;
    }
    if (id >= 2) {
        if (a < 102) return;
        if (a < 107) a = (uint8_t)(a + 70u);          /* falling: 102-106 -> 172-176 */
    }
    if (a < 150 || a >= 190) {
        return;
    }
    g->framepoint = (uint16_t)(findex((uint8_t)(a - 149u)) + g->frames->altset1);
}

void pop_getframeinfo(pop_game *g)
{
    const pop_frametab *t = g->frames;
    size_t off;
    pop_getframe(g, g->ch.posn);
    usealtsets(g);
    off = (uint16_t)(g->framepoint - t->origin);
    if (g->framepoint < t->origin || off + 5u > t->size) {
        /* The original would read whatever lies beyond the table. */
        g->unmodelled = 1;
        g->Fimage = g->Fsword = g->Fdx = g->Fdy = g->Fcheck = 0;
        return;
    }
    g->Fimage = t->data[off];
    g->Fsword = t->data[off + 1];
    g->Fdx    = t->data[off + 2];
    g->Fdy    = t->data[off + 3];
    g->Fcheck = t->data[off + 4];
}

void pop_rereadblocks(pop_game *g)
{
    pop_getframeinfo(g);
    pop_getbaseblock(g);
}

/* --- falling ---------------------------------------------------------------------------- */

void pop_gravity(pop_game *g)
{
    uint8_t v, accel, limit;
    if (g->ch.action != 4) {
        return;
    }
    accel = g->ag.weightless ? POP_WTLESS_GRAVITY : POP_ACCEL_GRAVITY;
    limit = g->ag.weightless ? POP_WTLESS_TERM_VEL : POP_TERM_VELOCITY;
    v = (uint8_t)(g->ch.y_vel + accel);
    /* `cmp #limit / bcc :ok`: an unsigned compare, so an upward (negative)
     * velocity that is still "above" the limit as a byte is replaced by the
     * limit at once. */
    if (v >= limit) {
        v = limit;
    }
    g->ch.y_vel = v;
}

void pop_addfall(pop_game *g)
{
    g->ch.y = (uint8_t)(g->ch.y_vel + g->ch.y);
    if (g->ch.action != 4) {
        return;
    }
    g->ch.x = pop_add_char_x(&g->ch, g->ch.x_vel);
    pop_rereadblocks(g);
}

/* --- character records -------------------------------------------------------------- */

void pop_loadkid(pop_game *g)     { g->ch = g->kid; }
void pop_savekid(pop_game *g)     { g->kid = g->ch; }
void pop_loadshad(pop_game *g)    { g->ch = g->shad; }
void pop_saveshad(pop_game *g)    { g->shad = g->ch; }
void pop_loadkidwop(pop_game *g)  { g->ch = g->kid;  g->op = g->shad; }
void pop_savekidwop(pop_game *g)  { g->kid = g->ch;  g->shad = g->op; }
void pop_loadshadwop(pop_game *g) { g->ch = g->shad; g->op = g->kid; }
void pop_saveshadwop(pop_game *g) { g->shad = g->ch; g->kid = g->op; }
