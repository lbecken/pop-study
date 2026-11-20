/* pop_mover.c - see pop_mover.h.  New code; ports of original routines.
 *
 * The code follows the original branch by branch, with the original labels
 * in comments.  The 6502's Y register carries a block index from routine to
 * routine throughout MOVER.S; here that is g->ry.  The working copies of a
 * list entry ("trloc", "mobx" ... without an index) are element 0 of the
 * arrays.
 */

#include "pop_mover.h"

#include "pop_collide.h"
#include "pop_control.h"
#include "pop_world.h"

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))

#define TRLOC   (g->trloc[0])
#define TRSCRN  (g->trscrn[0])
#define TRDIREC (g->trdirec[0])
#define MOBX    (g->mobx[0])
#define MOBY    (g->moby[0])
#define MOBSCRN (g->mobscrn[0])
#define MOBVEL  (g->mobvel[0])
#define MOBTYPE (g->mobtype[0])
#define MOBLEVEL (g->moblevel[0])

enum {
    MAXTR = POP_TROBSPACE - 1, MAXMOB = POP_MOBSPACE - 1, MAXGATEVEL = 8,
    WIGGLETIME = 4, FF_ACCEL = 3, FF_TERMVEL = 29, CRUMBLETIME = 2, DISAPPEARTIME = 2,
    CRUSH_DIST = 30, LOOSEWIPE = 31, SPIKEWIPE = 31, SLICERWIPE = 63, PLATEWIPE = 16,
    EXITINC = 4, SLICERSYNC = 3
};

static const uint8_t gatevel[MAXGATEVEL + 1] = { 0, 0, 0, 20, 40, 60, 80, 100, 120 };
static const uint8_t gateinc[3] = { 0xFF, 4, 4 };  /* trdirec 0, 1, 2: down, up, up and jam */

uint8_t pop_rnd(pop_game *g)
{
    g->RNDseed = u8(g->RNDseed * 5u + 23u);
    return g->RNDseed;
}

uint8_t pop_unindex(uint8_t index, uint8_t *row)
{
    uint8_t x = 0;
    while (index >= 10) {
        index = u8(index - 10);
        x++;
    }
    *row = x;
    return index;
}

void pop_markfred(pop_game *g, uint8_t a, int *carry)
{
    if (*carry) return;
    *pop_rb(g, POP_RB_FRED, g->ry) = a;
}

void pop_markmove(pop_game *g, uint8_t a, int *carry)
{
    if (*carry) {                              /* ]os: the top line, from the screen above */
        if (g->ry < 10) g->rb[POP_RB_TOP + g->ry] = a;
        return;
    }
    *pop_rb(g, POP_RB_MOVE, g->ry) = a;
}

/* --- lists ------------------------------------------------------------------------------- */

static uint8_t searchtrob(pop_game *g)
{
    uint8_t x;
    if (g->numtrans > MAXTR) { g->unmodelled = 1; return 0; }
    for (x = g->numtrans; x != 0; x--) {
        if (g->trloc[x] == TRLOC && g->trscrn[x] == TRSCRN) return x;
    }
    return 0;
}

void pop_addtrob(pop_game *g)
{
    uint8_t x = searchtrob(g);
    if (g->unmodelled) return;
    if (x != 0) {                              /* :chdir already listed: new direction */
        g->trdirec[x] = TRDIREC;
        return;
    }
    if (g->numtrans == MAXTR) return;          /* list full: the trigger fails */
    x = ++g->numtrans;
    g->trdirec[x] = TRDIREC;
    g->trloc[x] = TRLOC;
    g->trscrn[x] = TRSCRN;
}

static void savemob(pop_game *g, uint8_t x)
{
    if (x > MAXMOB) { g->unmodelled = 1; return; }
    g->mobx[x] = MOBX;
    g->moby[x] = MOBY;
    g->mobscrn[x] = MOBSCRN;
    g->mobvel[x] = MOBVEL;
    g->mobtype[x] = MOBTYPE;
    g->moblevel[x] = MOBLEVEL;
}

static void loadmob(pop_game *g, uint8_t x)
{
    if (x > MAXMOB) { g->unmodelled = 1; return; }
    MOBX = g->mobx[x];
    MOBY = g->moby[x];
    MOBSCRN = g->mobscrn[x];
    MOBVEL = g->mobvel[x];
    MOBTYPE = g->mobtype[x];
    MOBLEVEL = g->moblevel[x];
}

static void addamob(pop_game *g)
{
    if (g->nummob == MAXMOB) return;
    if (g->nummob > MAXMOB) { g->unmodelled = 1; return; }
    g->nummob++;
    savemob(g, g->nummob);
}

static void stopobj(pop_game *g) { TRDIREC = 0xFF; }

/* --- "is it on screen" for the redraw marks ---------------------------------------------- */

/* ]above: not on the visible screen.  If it is on the screen above, a
 * bottom-row piece maps to the top line (carry set, Y = 0-9).  Otherwise the
 * routine returns from the compare as it stands: the carry is whether trscrn
 * is at least scrnAbove, and Y is untouched. */
static int vis_above(pop_game *g)
{
    if (TRSCRN != g->scrnAbove) return TRSCRN >= g->scrnAbove;
    g->ry = u8(TRLOC - 20);
    return 1;
}

static int vis_no(pop_game *g)                 /* ]no */
{
    g->ry = 30;
    return 1;
}

static int check(pop_game *g)                  /* check: the piece itself */
{
    if (TRSCRN != g->VisScrn) return vis_above(g);
    g->ry = TRLOC;
    return g->ry >= 30;
}

static int checkright(pop_game *g)             /* the piece to its right */
{
    uint8_t y;
    if (TRSCRN == g->VisScrn) {
        y = TRLOC;
        g->ry = y;
        if (y == 9 || y == 19 || y == 29) return vis_no(g);
        g->ry = u8(y + 1);
        return 0;
    }
    if (TRSCRN != g->scrnLeft) return vis_above(g);
    y = TRLOC;                                 /* on the screen to the left */
    g->ry = y;
    if (y != 9 && y != 19 && y != 29) return vis_no(g);
    g->ry = u8(y - 9);                         /* the left-edge piece of this screen */
    return 0;
}

static int checkabover(pop_game *g)            /* the piece above and to the right */
{
    uint8_t y;
    if (TRSCRN == g->VisScrn) {
        y = TRLOC;
        g->ry = y;
        if (y < 10) {                          /* :above top row: the top line */
            g->ry = u8(y + 1);
            return 1;
        }
        if (y == 19 || y == 29) return vis_no(g);
        g->ry = u8(y - 9);
        return 0;
    }
    if (TRSCRN == g->scrnLeft) {
        y = TRLOC;
        g->ry = y;
        if (y == 9) {                          /* :yes0 */
            g->ry = 0;
            return 1;
        }
        if (y != 19 && y != 29) return vis_no(g);
        g->ry = u8(y - 19);
        return 0;
    }
    if (TRSCRN == g->scrnBelow) {
        y = TRLOC;
        g->ry = y;
        if (y >= 9) return vis_no(g);
        g->ry = u8(y + 21);
        return 0;
    }
    if (TRSCRN != g->scrnBelowL) return TRSCRN >= g->scrnBelowL;   /* `bne ]rts` */
    g->ry = TRLOC;
    if (g->ry != 9) return vis_no(g);
    g->ry = 20;
    return 0;
}

static void redtrobj(pop_game *g)
{
    int c = check(g);
    pop_markred(g, 2, &c);
    pop_markwipe(g, 2, &c);
    c = checkright(g);
    pop_markred(g, 2, &c);
    pop_markwipe(g, 2, &c);
}

static void redexit(pop_game *g)               /* also redtorch */
{
    int c = checkright(g);
    pop_markmove(g, 2, &c);
}

static void redflask(pop_game *g)              /* also redsword */
{
    int c = check(g);
    pop_markmove(g, 2, &c);
}

static void redloose(pop_game *g)
{
    g->trobcount++;
    g->height = LOOSEWIPE;
    redtrobj(g);
}

static void redgate(pop_game *g)
{
    int c = checkright(g);                     /* the piece to the right of the gate */
    pop_markmove(g, 2, &c);
    pop_markfred(g, 2, &c);
    c = checkabover(g);                        /* and to the right of the gate's panel */
    pop_markmove(g, 2, &c);
}

static void redspikes(pop_game *g)
{
    g->trobcount++;
    g->height = SPIKEWIPE;
    redtrobj(g);
}

static void redslicer(pop_game *g)
{
    int c;
    g->trobcount++;
    g->height = SLICERWIPE;
    c = check(g);
    pop_markred(g, 2, &c);
    pop_markwipe(g, 2, &c);
}

static void redplate(pop_game *g)
{
    g->height = PLATEWIPE;
    redtrobj(g);
}

/* --- triggers --------------------------------------------------------------------------------- */

void pop_trigslicer(pop_game *g, uint8_t state)
{
    uint8_t s = pop_bluespec(g, g->ry);
    g->mv_state = state;
    if (s != 0 && s < POP_SLICER_RET) return;  /* in mid-slice: don't interfere */
    TRLOC = g->ry;
    pop_set_bluespec(g, g->ry, state);
    TRSCRN = g->VisScrn;
    TRDIREC = 1;
    pop_addtrob(g);
}

void pop_closeexit(pop_game *g, uint8_t scrn)
{
    TRLOC = g->ry;
    TRSCRN = scrn;
    pop_set_bluespec(g, g->ry, POP_EMAXVAL);   /* all the way open ... */
    TRDIREC = 3;                               /* ... and coming down fast */
    pop_addtrob(g);
}

void pop_smashmirror(pop_game *g)
{
    pop_set_bluespec(g, g->ry, 86);
}

void pop_trigflask(pop_game *g, uint8_t scrn)
{
    TRLOC = g->ry;
    TRSCRN = scrn;
    TRDIREC = 1;
    pop_set_bluespec(g, g->ry, u8((pop_rnd(g) & 7u) | pop_bluespec(g, g->ry)));
    pop_addtrob(g);
}

void pop_trigsword(pop_game *g, uint8_t scrn)
{
    TRLOC = g->ry;
    TRSCRN = scrn;
    TRDIREC = 1;
    pop_set_bluespec(g, g->ry, pop_rnd(g) & 0x1Fu);
    pop_addtrob(g);
}

void pop_trigtorch(pop_game *g, uint8_t scrn)
{
    TRLOC = g->ry;
    TRSCRN = scrn;
    TRDIREC = 1;
    pop_set_bluespec(g, g->ry, pop_rnd(g) & 0x0Fu);
    pop_addtrob(g);
}

static void spikes_to_list(pop_game *g, uint8_t direc)       /* ]cont */
{
    uint8_t y = g->ry;
    TRDIREC = direc;
    TRLOC = y;
    TRSCRN = g->tempscrn;                      /* from rdblock */
    pop_addtrob(g);
    redspikes(g);
    pop_g_addsound(g, POP_SND_GATEDOWN);
}

void pop_trigspikes(pop_game *g)
{
    uint8_t s = pop_bluespec(g, g->ry);
    if (s == 0) {                              /* fully retracted: spring them */
        spikes_to_list(g, 1);
        return;
    }
    if (!NEG(s)) return;                       /* in motion */
    if (s == 0xFF) return;                     /* jammed */
    pop_set_bluespec(g, g->ry, POP_SPIKETIMER);/* out: restart their timer */
}

void pop_jamspikes(pop_game *g)
{
    pop_set_bluespec(g, g->ry, 0xFF);
    spikes_to_list(g, 0xFF);                   /* "stop object" */
}

void pop_breakloose1(pop_game *g, uint8_t state)
{
    uint8_t s;
    g->mv_state = state;
    if (pop_bluetype(g, g->ry) & POP_REQMASK) return;        /* blocked below */
    s = pop_bluespec(g, g->ry);
    if (!NEG(s) && s != 0) return;             /* already triggered (wiggling is fine) */
    pop_set_bluespec(g, g->ry, state);
    TRLOC = g->ry;
    TRSCRN = g->tempscrn;
    TRDIREC = 0;
    pop_addtrob(g);
    redloose(g);
}

void pop_breakloose(pop_game *g) { pop_breakloose1(g, 1); }

/* gettimer / chgtimer / getscrn: fields of a link entry (LINKLOC, LINKMAP). */
static uint8_t gettimer(const pop_game *g, uint8_t x)
{
    return g->blueprint[POP_LINKMAP + x] & 0x1Fu;
}

static void chgtimer(pop_game *g, uint8_t x, uint8_t t)
{
    uint8_t *m = &g->blueprint[POP_LINKMAP + x];
    *m = u8((*m & 0xE0u) | (t & 0x1Fu));
}

static uint8_t getscrn(const pop_game *g, uint8_t x)
{
    unsigned lo = (g->blueprint[POP_LINKLOC + x] & 0x60u) >> 2;
    unsigned hi = g->blueprint[POP_LINKMAP + x] & 0xE0u;
    return u8((hi + lo) >> 3);
}

static void triggate(pop_game *g)
{
    uint8_t a = pop_bluespec(g, g->ry);        /* current gate position */
    if (g->pptype == POP_UPRESSPLATE) {        /* :raise */
        TRDIREC = 1;
        if (a == 0xFF) { stopobj(g); return; } /* jammed open */
        if (a < POP_GMAXVAL) return;
        pop_set_bluespec(g, g->ry, POP_GATETIMER);           /* open: restart its timer */
        stopobj(g);
        return;
    }
    if (g->pptype == POP_RUBBLE) {             /* :jam open */
        TRDIREC = 2;
        if (a < POP_GMAXVAL) return;
        pop_set_bluespec(g, g->ry, 0xFF);      /* "jammed open" */
        stopobj(g);
        return;
    }
    if (a == 0) { stopobj(g); return; }        /* :lower already at the bottom */
    TRDIREC = 3;                               /* down fast */
}

/* trigger: walk the list of things the plate just pushed is linked to. */
static void trigger(pop_game *g)
{
    for (;;) {
        uint8_t x = g->linkindex, loc = g->blueprint[POP_LINKLOC + x], id;
        if (loc == 0xFF) return;               /* linked to nothing */
        TRLOC = loc & 0x1Fu;
        TRSCRN = getscrn(g, x);
        pop_calcblue(g, TRSCRN);
        g->ry = TRLOC;
        id = pop_bluetype(g, g->ry) & POP_IDMASK;
        if (id == POP_GATE) {                  /* trigobj */
            triggate(g);
        } else if (id == POP_EXIT) {           /* openexit: it only opens */
            TRDIREC = (pop_bluespec(g, g->ry) != 0) ? 0xFF : 1;
        }
        if (!NEG(TRDIREC)) pop_addtrob(g);
        x = g->linkindex;
        g->linkindex++;
        if (g->blueprint[POP_LINKLOC + x] & 0x80u) return;   /* last entry */
    }
}

static void pushpp1(pop_game *g)
{
    uint8_t t;
    g->linkindex = pop_bluespec(g, g->ry);
    t = gettimer(g, g->linkindex);
    if (t == 31) return;                       /* permanently down */
    if (t < 2) {                               /* a fresh plate */
        chgtimer(g, g->linkindex, POP_PPTIMER);
        TRLOC = g->ry;
        TRSCRN = g->tempscrn;
        TRDIREC = 1;
        pop_addtrob(g);
        redplate(g);
        g->ag.alertguard = 1;
        pop_g_addsound(g, POP_SND_PLATEDOWN);
    } else {                                   /* already down: restart the timer */
        chgtimer(g, g->linkindex, POP_PPTIMER);
    }
    trigger(g);
}

void pop_pushpp(pop_game *g)
{
    g->pptype = pop_bluetype(g, g->ry) & POP_IDMASK;
    pushpp1(g);
}

void pop_jampp(pop_game *g)
{
    g->pptype = pop_bluetype(g, g->ry) & POP_IDMASK;
    if (g->pptype == POP_PRESSPLATE) {
        pop_set_bluetype(g, g->ry, POP_DPRESSPLATE);
    } else {
        pop_set_bluetype(g, g->ry, POP_FLOOR);
        pop_set_bluespec(g, g->ry, 0);         /* (pushpp1 then reads link 0) */
        g->pptype = POP_RUBBLE;
    }
    pushpp1(g);
}

/* --- animating the list ------------------------------------------------------------------------- */

void pop_addlowersound(pop_game *g, uint8_t state)
{
    uint8_t y;
    if ((state & 1u) == 0) return;             /* every other frame */
    if (!(g->level == 3 && TRSCRN == 2)) {     /* (always audible there) */
        if (TRSCRN == g->scrnLeft) {
            y = g->ry = TRLOC;
            if (y != 9 && y != 19 && y != 29) return;        /* visible at the left edge? */
        } else if (TRSCRN == g->VisScrn) {
            y = g->ry = TRLOC;
            if (y == 9 || y == 19 || y == 29) return;
        } else {
            return;
        }
    }
    pop_g_addsound(g, POP_SND_LOWERINGGATE);
}

void pop_mirappear(pop_game *g)
{
    if (g->level != 4) return;
    (void)pop_rdblock(g, POP_MIRSCRN, POP_MIRX, POP_MIRY);
    pop_set_bluetype(g, g->ry, POP_MIRROR);
}

/* Coming down fast: the direction doubles as an index into gatevel. */
static int slam(pop_game *g)                   /* returns 1 when it has hit the bottom */
{
    uint8_t x = TRDIREC, before = g->mv_state;
    if (x < MAXGATEVEL) {
        x++;
        TRDIREC = x;
    }
    if (x > MAXGATEVEL) { g->unmodelled = 1; return 0; }
    g->mv_state = u8(before - gatevel[x]);
    return before < gatevel[x];
}

static void animexit(pop_game *g)
{
    uint8_t x = TRDIREC;
    if (!NEG(x)) {
        if (x >= 3) {                          /* :downfast */
            if (slam(g)) {
                stopobj(g);
                g->mv_state = 0;
                pop_g_addsound(g, POP_SND_GATESLAM);
            }
        } else {
            pop_g_addsound(g, POP_SND_RAISINGEXIT);
            g->mv_state = u8(g->mv_state + EXITINC);
            if (g->mv_state >= POP_EMAXVAL) {  /* :stop open */
                stopobj(g);
                pop_g_addsound(g, POP_SND_GATEDOWN);
                pop_cuesong(g, POP_SONG_STAIRS, 15);
                g->exitopen = 1;
                pop_mirappear(g);
            }
        }
    }
    redexit(g);
}

static void animgate(pop_game *g)
{
    uint8_t x = TRDIREC, a;
    if (NEG(x)) goto cont;                     /* stopped */
    if (x >= 3) {                              /* :downfast */
        if (slam(g)) {
            g->mv_state = 0;
            stopobj(g);
            pop_g_addsound(g, POP_SND_GATESLAM);
        }
        goto cont;
    }
    a = g->mv_state;
    if (a == 0xFF) goto stop;                  /* jammed open */
    a = u8(a + gateinc[x]);
    g->mv_state = a;
    if (x == 0) {                              /* :goingdown */
        if (a == 0) goto stop;
        if (a < POP_GMAXVAL) pop_addlowersound(g, a);        /* (above that: still waiting) */
        goto cont;
    }
    if (a >= POP_GMAXVAL) {                    /* :attop */
        if (x >= 2) {                          /* jam at the top */
            g->mv_state = 0xFF;
            goto stop;
        }
        g->mv_state = POP_GATETIMER;           /* pause, then start to close */
        TRDIREC = 0;
        return;
    }
    pop_g_addsound(g, POP_SND_RAISINGGATE);
    goto cont;
stop:
    stopobj(g);
    pop_g_addsound(g, POP_SND_GATEDOWN);
cont:
    redgate(g);
}

static void animplate(pop_game *g)
{
    uint8_t x, t;
    if (NEG(TRDIREC)) return;
    x = g->mv_state;                           /* the plate's link index */
    t = u8(gettimer(g, x) - 1);
    chgtimer(g, x, t);
    if (t >= 2) return;                        /* the timer stops at 1 */
    pop_g_addsound(g, POP_SND_PLATEUP);
    stopobj(g);
    redplate(g);
}

static void animslicer(pop_game *g)
{
    uint8_t n, row;
    if (!NEG(TRDIREC)) {
        n = u8((g->mv_state & 0x7Fu) + 1);
        if (n >= POP_SLICETIMER + 1) n = 1;    /* wrap around */
        g->mv_state = u8(n | (g->mv_state & 0x80u));         /* keep the "bloodied" bit */
        if (n == POP_SLICER_EXT) pop_g_addsound(g, POP_SND_JAWSCLASH);

        /* A slicer keeps going only on the visible screen, on the kid's
         * row, while he is alive (or it has blood on it); otherwise it is
         * purged as soon as it is open. */
        (void)pop_unindex(TRLOC, &row);
        if (!(TRSCRN == g->VisScrn && row == g->kid.block_y
              && (NEG(g->kid.life) || (g->mv_state & 0x80u)))) {
            if ((g->mv_state & 0x7Fu) >= POP_SLICER_RET) stopobj(g);   /* :os */
        }
    }
    if ((g->mv_state & 0x7Fu) >= POP_SLICER_RET) return;     /* open: nothing to redraw */
    redslicer(g);
}

uint8_t pop_getflaskframe(uint8_t frame)
{
    frame = u8(frame + 1);
    return frame >= POP_BUBB_LAST + 1 ? 1 : frame;
}

uint8_t pop_getflameframe(pop_game *g, uint8_t state)
{
    uint8_t r;
    g->mv_state = state;
    r = pop_rnd(g);
    if (r != state && r < POP_TORCH_LAST + 1) return r;      /* any other frame will do */
    r = u8(state + 1);                         /* else the next one */
    return r < POP_TORCH_LAST + 1 ? r : 0;
}

static void animflask(pop_game *g)
{
    uint8_t potion;
    if (NEG(TRDIREC)) return;
    if (TRSCRN != g->VisScrn) { stopobj(g); return; }
    potion = g->mv_state & 0xE0u;
    g->mv_state = u8(pop_getflaskframe(g->mv_state & 0x1Fu) | potion);
    redflask(g);
}

static void animsword(pop_game *g)
{
    if (TRSCRN != g->VisScrn) { stopobj(g); return; }
    g->mv_state--;
    if (g->mv_state == 0) g->mv_state = u8((pop_rnd(g) & 0x3Fu) + 40);   /* next gleam */
    redflask(g);
}

static void animtorch(pop_game *g)
{
    if (NEG(TRDIREC)) return;
    if (TRSCRN != g->VisScrn) { stopobj(g); return; }
    g->mv_state = pop_getflameframe(g, g->mv_state);
    redexit(g);
}

static void animspikes(pop_game *g)
{
    uint8_t a;
    if (NEG(TRDIREC)) goto done;
    a = g->mv_state;
    if (NEG(a)) {                              /* :timerloop out, counting down */
        g->mv_state--;
        if ((g->mv_state & 0x7Fu) != 0) return;
        g->mv_state = POP_SPIKE_EXT + 1;       /* time's up: first retracting frame */
        goto done;
    }
    g->mv_state++;                             /* next frame */
    if (a == POP_SPIKE_EXT) {
        g->mv_state = POP_SPIKETIMER;          /* fully out: start the timer */
    } else if (a == POP_SPIKE_RET) {
        g->mv_state = 0;                       /* fully back: ready again */
        stopobj(g);
    }
done:
    redspikes(g);
}

/* makespace: the piece becomes empty space.  Returns the state for it: on
 * the palace background set, 1 (a striped back wall). */
static uint8_t makespace(pop_game *g)
{
    pop_set_bluetype(g, g->ry, POP_SPACE);
    return g->BGset1 == 1 ? 1 : 0;
}

static void animfloor(pop_game *g)
{
    uint8_t a, col, row;
    if (NEG(TRDIREC)) goto red;
    a = ++g->mv_state;
    if (NEG(a)) {                              /* :wiggle only wiggling */
        if (g->level == 13) return;
        if (a >= WIGGLETIME + 0x80) {
            g->mv_state = 0;
            stopobj(g);
        }
        goto red;
    }
    if (a < POP_FFALLING) goto red;            /* loosetimer */

    /* the floor detaches: the piece becomes space and a MOB takes over */
    g->mv_state = makespace(g);
    stopobj(g);
    col = pop_unindex(TRLOC, &row);
    MOBX = u8(col << 2);
    MOBLEVEL = row;
    MOBY = pop_blockbot(g, u8(row + 1));
    MOBSCRN = TRSCRN;
    MOBVEL = 0;
    MOBTYPE = 0;
    addamob(g);
red:
    redloose(g);
}

static void animobj(pop_game *g, uint8_t x)
{
    uint8_t id;
    TRLOC = g->trloc[x];
    TRSCRN = g->trscrn[x];
    TRDIREC = g->trdirec[x];

    pop_calcblue(g, TRSCRN);
    g->ry = TRLOC;
    g->mv_state = pop_bluespec(g, g->ry);
    id = pop_bluetype(g, g->ry) & POP_IDMASK;

    if (id == POP_TORCH) animtorch(g);
    else if (id == POP_UPRESSPLATE || id == POP_PRESSPLATE) animplate(g);
    else if (id == POP_SPIKES) animspikes(g);
    else if (id == POP_LOOSE) animfloor(g);
    else if (id == POP_SPACE) { stopobj(g); redloose(g); }   /* animspace: it has fallen */
    else if (id == POP_SLICER) animslicer(g);
    else if (id == POP_GATE) animgate(g);
    else if (id == POP_EXIT) animexit(g);
    else if (id == POP_FLASK) animflask(g);
    else if (id == POP_SWORD) animsword(g);
    else stopobj(g);                           /* none of these: purge it */

    /* :done  The new state is stored through BlueSpec as it now stands.  On
     * level 4 MIRAPPEAR has just pointed it at the mirror's screen. */
    pop_set_bluespec(g, TRLOC, g->mv_state);
    g->ry = TRLOC;
}

void pop_animtrans(pop_game *g)
{
    uint8_t x, y;
    int clean = 0;
    g->trobcount = 0;
    if (g->numtrans == 0) return;
    if (g->numtrans > MAXTR) { g->unmodelled = 1; return; }
    for (x = g->numtrans; x != 0; x--) {
        uint8_t d;
        g->tempnt = x;
        animobj(g, x);
        d = TRDIREC;
        if (NEG(d)) {                          /* stopped: mark it for deletion */
            d = 0xFF;
            clean = 1;
        }
        g->trdirec[x] = d;
    }
    if (!clean) return;
    y = 0;                                     /* copy the list onto itself without them */
    for (x = 1; x <= g->numtrans; x++) {
        if (g->trdirec[x] == 0xFF) continue;
        y++;
        g->trdirec[y] = g->trdirec[x];
        g->trloc[y] = g->trloc[x];
        g->trscrn[y] = g->trscrn[x];
    }
    g->numtrans = y;
}

/* --- falling floors ------------------------------------------------------------------------------ */

static void markmob(pop_game *g)
{
    int c;
    if (MOBSCRN != g->VisScrn) return;
    g->height = LOOSEWIPE;
    c = pop_indexblock(g);
    pop_markred(g, 2, &c);
    pop_markwipe(g, 2, &c);
    g->tempblockx++;
    c = pop_indexblock(g);
    pop_markred(g, 2, &c);
    pop_markfred(g, 2, &c);
    pop_markwipe(g, 2, &c);
}

static void passthru(pop_game *g)              /* through a floor plane */
{
    MOBLEVEL++;
    if (MOBLEVEL < 3) return;
    MOBY = u8(MOBY - 192);                     /* ... and onto the next screen */
    MOBLEVEL = 0;
    MOBSCRN = pop_getdown(g, MOBSCRN);
}

/* makerubble: change the piece it landed on.  A pressure plate is pushed
 * first; an "up" plate is pushed as rubble, which jams its gates open. */
static void makerubble(pop_game *g)
{
    uint8_t id;
    g->tempblocky = MOBLEVEL;
    g->tempblockx = MOBX >> 2;
    g->tempscrn = MOBSCRN;
    id = pop_rdblock1(g);
    if (id == POP_PRESSPLATE || id == POP_UPRESSPLATE) {
        if (id == POP_UPRESSPLATE) pop_set_bluetype(g, g->ry, POP_RUBBLE);
        pop_pushpp(g);
        (void)pop_rdblock1(g);
    } else if (id != POP_FLOOR && id != POP_SPIKES && id != POP_FLASK && id != POP_TORCH) {
        return;                                /* can't turn this piece into rubble */
    }
    pop_set_bluetype(g, g->ry, POP_RUBBLE);
    markmob(g);
}

static void knockloose(pop_game *g)            /* lands on a loose floor: take it along */
{
    pop_set_bluespec(g, g->ry, makespace(g));
    MOBVEL >>= 1;
    savemob(g, g->tempnt);
    MOBY = u8(MOBY + 6);                       /* a second falling floor, just below */
    passthru(g);
    addamob(g);
    loadmob(g, g->tempnt);
    markmob(g);
}

/* SHAKEM1: In: tempscrn, tempblocky.  Every idle loose floor of that row
 * starts to wiggle (shakeit). */
static void shakem1(pop_game *g)
{
    int x;
    for (x = 9; x >= 0; x--) {
        g->tempblockx = u8(x);
        if (pop_rdblock1(g) == POP_LOOSE && pop_bluespec(g, g->ry) == 0) {
            pop_set_bluespec(g, g->ry, 0x80);
            TRLOC = g->ry;
            TRSCRN = g->tempscrn;
            TRDIREC = 1;
            pop_addtrob(g);
        }
    }
}

void pop_shakem(pop_game *g, uint8_t blocky)
{
    if (g->level == 13) return;
    g->tempblocky = blocky;
    g->tempscrn = g->VisScrn;
    shakem1(g);
}

static void mobfloor(pop_game *g)
{
    uint8_t a = MOBVEL, id;
    if (NEG(a)) return;                        /* crumbling */
    if (a < FF_TERMVEL) {
        a = u8(a + FF_ACCEL);
        MOBVEL = a;
    }
    a = u8(a + MOBY);
    MOBY = a;

    if (MOBSCRN == 0) {                        /* :null falling on the null screen */
        if (MOBY >= 192 + 17) MOBVEL = u8(0 - DISAPPEARTIME);
        return;
    }
    if (a >= 0xE2) return;                     /* "negative": still above the screen */
    if (MOBLEVEL == 0xFF) { g->unmodelled = 1; return; }     /* BlockAy+1,x does not wrap */
    if (a < pop_blockay(g, u8(MOBLEVEL + 1))) return;        /* not at the floor plane yet */

    /* passing through the floor plane: what is there? */
    g->tempblocky = MOBLEVEL;
    g->tempblockx = MOBX >> 2;
    g->tempscrn = MOBSCRN;
    id = pop_rdblock1(g);
    g->underFF = id;
    if (id == POP_SPACE) {
        passthru(g);
    } else if (id == POP_LOOSE) {
        knockloose(g);
        passthru(g);
    } else {                                   /* :crash lands on something solid */
        pop_g_addsound(g, POP_SND_LOOSECRASH);
        g->tempscrn = MOBSCRN;
        g->tempblocky = MOBLEVEL;
        shakem1(g);
        if (MOBLEVEL == 0xFF) { g->unmodelled = 1; return; }
        MOBY = pop_blockay(g, u8(MOBLEVEL + 1));
        MOBVEL = u8(0 - CRUMBLETIME);
        makerubble(g);
    }
}

/* crushchar: a falling floor has landed on the character (the kid). */
static void crushchar(pop_game *g)
{
    if (g->level != 13 && g->ch.posn >= 5 && g->ch.posn < 15) return;   /* running: escapes */
    if (g->ch.action >= 2 && g->ch.action != 7) return;      /* only on the ground */
    g->ch.y = pop_floory(g, u8(g->ch.block_y + 1));
    if (pop_decstr(g, 1) == 0) {
        pop_g_jumpseq(g, POP_SQ_HARDLAND);     /* that was his last unit */
        return;
    }
    if (g->ch.posn == 109) return;             /* already crouched */
    pop_g_jumpseq(g, POP_SQ_CRUSH);
}

static void checkcrush(pop_game *g)
{
    pop_loadkid(g);
    if (MOBSCRN != g->ch.scrn) return;
    if ((MOBX >> 2) != g->ch.block_x) return;
    if (MOBY >= g->ch.y) return;               /* below him altogether */
    if (u8(g->ch.y - CRUSH_DIST) >= MOBY) return;            /* still well above */
    crushchar(g);
    pop_savekid(g);
}

void pop_animmobs(pop_game *g)
{
    uint8_t x, y;
    if (g->nummob == 0) return;
    if (g->nummob > MAXMOB) { g->unmodelled = 1; return; }
    for (x = g->nummob; x != 0; x--) {
        g->tempnt = x;
        loadmob(g, x);
        if (MOBTYPE == 0) mobfloor(g);         /* animmob */
        if (NEG(MOBVEL)) MOBVEL++;             /* stopping: count up to -1 */
        checkcrush(g);
        x = g->tempnt;
        savemob(g, x);
    }
    y = 0;                                     /* delete those that have ceased to exist */
    for (x = 1; x <= g->nummob; x++) {
        if (g->mobvel[x] == 0xFF) continue;
        y++;
        g->mobvel[y] = g->mobvel[x];
        g->mobx[y] = g->mobx[x];
        g->moby[y] = g->moby[x];
        g->mobscrn[y] = g->mobscrn[x];
        g->mobtype[y] = g->mobtype[x];
        g->moblevel[y] = g->moblevel[x];
    }
    g->nummob = y;
}

/* --- what a character sets off -------------------------------------------------------------------- */

void pop_checkpress(pop_game *g)
{
    uint8_t p = g->ch.posn, id;
    if ((p >= 87 && p < 100) || (p >= 135 && p < 141)) {     /* hanging or climbing */
        id = pop_getabove(g);
    } else {
        uint8_t a = g->ch.action;
        if (a != 7 && a != 5 && a >= 2) return;              /* not on the ground */
        if (p == 79) {                         /* :touchceil jumped up and touched it */
            if (pop_getabove(g) == POP_LOOSE) pop_breakloose(g);
            return;
        }
        if ((g->Fcheck & 0x40u) == 0) return;  /* foot isn't on the floor */
        id = pop_getunderft(g);
    }
    if (id == POP_UPRESSPLATE || id == POP_PRESSPLATE) {
        if (NEG(g->ch.life)) pop_pushpp(g);
        else pop_jampp(g);                     /* dead weight */
    } else if (id == POP_LOOSE) {
        g->ag.alertguard = 1;
        pop_breakloose(g);
    }
}

/* CHECKSPIKES: for every column his image covers, look down from his row
 * for spikes with nothing but space above them. */
void pop_checkspikes(pop_game *g)
{
    uint8_t right = pop_getblockxp(g, g->rightej), col;
    if (NEG(right)) return;
    col = pop_getblockxp(g, g->leftej);
    for (;;) {
        g->blockx = col;
        g->tempblockx = col;
        g->tempblocky = g->ch.block_y;
        g->tempscrn = g->ch.scrn;
        for (;;) {
            uint8_t id = pop_rdblock1(g);
            if (id == POP_SPIKES) {
                pop_trigspikes(g);
                break;
            }
            if (pop_cmpspace(id) != 0) break;
            if (g->tempscrn == 0 || g->tempscrn != g->ch.scrn) break;   /* not his screen */
            g->tempblocky++;                   /* one level below */
        }
        if (g->blockx == right) return;
        col = u8(g->blockx + 1);
    }
}

void pop_shakeloose(pop_game *g)
{
    uint8_t jar = g->ag.jarabove;
    if (jar == 0) return;
    g->ag.jarabove = 0;
    pop_shakem(g, NEG(jar) ? g->ch.block_y : u8(g->ch.block_y - 1));
}

/* ADDSLICERS.  Its counter is the SUBS.S local `tempstate`, which sits at
 * the same address ($F2) as COLL.S's `tempobjid`; the port keeps that, so
 * that tempobjid ends up as it does in the original. */
void pop_addslicers(pop_game *g)
{
    uint8_t state = POP_SLICETIMER, y, end;
    g->tempobjid = state;
    pop_calcblue(g, g->ch.scrn);
    g->ry = g->ch.block_y;
    if (g->ch.block_y >= 3) return;
    y = u8(g->ch.block_y * 10);
    end = u8(y + 10);
    for (; y < end; y++) {
        uint8_t s;
        g->ry = y;
        if ((pop_bluetype(g, y) & POP_IDMASK) != POP_SLICER) continue;
        s = pop_bluespec(g, y);
        if ((s & 0x7Fu) != 0 && (s & 0x7Fu) < POP_SLICER_RET) continue;   /* in mid-slice */
        pop_trigslicer(g, u8((s & 0x80u) | state));
        state = u8(state - SLICERSYNC);        /* getnextstate: each one out of step */
        if (state < POP_SLICER_RET) state = u8(state + POP_SLICETIMER + 1 - POP_SLICER_RET);
        g->tempobjid = state;
    }
    g->ry = end;
}

/* --- screen and level events ------------------------------------------------------------------------ */

void pop_addtorches(pop_game *g)
{
    int y;
    pop_calcblue(g, g->VisScrn);
    for (y = 29; y >= 0; y--) {
        uint8_t id = pop_bluetype(g, u8(y)) & POP_IDMASK;
        g->ry = u8(y);
        if (id == POP_TORCH) pop_trigtorch(g, g->VisScrn);
        else if (id == POP_FLASK) pop_trigflask(g, g->VisScrn);
        else if (id == POP_SWORD) pop_trigsword(g, g->VisScrn);
    }
}

void pop_crumble(pop_game *g)
{
    uint8_t x;
    if (g->level != 13) return;
    if (g->VisScrn != 23 && g->VisScrn != 16) return;
    g->tempscrn = g->scrnAbove;                /* blocks 2-7 of the bottom row of the screen above */
    g->tempblocky = 2;
    x = 7;
    do {
        g->tempblockx = x;
        if (pop_rdblock1(g) == POP_LOOSE) {    /* each after a random delay */
            pop_breakloose1(g, u8(0 - (pop_rnd(g) & 0x0Fu)));
        }
        x = u8(g->tempblockx - 1);
    } while (x >= 2);
}
