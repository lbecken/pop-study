/* pop_auto.c - see pop_auto.h.  New code; ports of original routines.
 *
 * The code follows the original branch by branch, with the original labels
 * in comments; docs/port/opponents.md explains what the behaviour amounts to.
 */

#include "pop_auto.h"

#include "pop_collide.h"
#include "pop_control.h"
#include "pop_mover.h"
#include "pop_world.h"

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))

/* Edges at which a character passes to the next screen, AUTO.S 49-54. */
enum {
    TOP_CUT_EDGE_PL = 10, TOP_CUT_EDGE_MI = 0xF0, BOT_CUT_EDGE = 191 + 24,
    LEFT_CUT_EDGE = 58 - 4, RIGHT_CUT_EDGE = 197 + 4, SCRN_RIGHT = 197, SCRN_WIDTH = 140
};

/* --- generated tables ------------------------------------------------------------------- */

/* One of the fight tables, indexed as the original does: the tables follow
 * each other in memory, so an index past the end of one reads the next. */
static uint8_t fight(pop_game *g, unsigned table, uint8_t x)
{
    if (!g->autodata || !g->autodata->fight || table + x >= POP_FT_SIZE) {
        g->unmodelled = 1;
        return 0;
    }
    return g->autodata->fight[table + x];
}

/* A guard-start entry of screen `scrn`: `lda GdStart...-1,x`. */
static uint8_t *gd(pop_game *g, unsigned table, uint8_t scrn)
{
    static uint8_t sink;
    size_t i = (size_t)table - 1u + scrn;
    if (i >= POP_LEVEL_SIZE) {
        g->unmodelled = 1;
        sink = 0;
        return &sink;
    }
    return &g->blueprint[i];
}

/* `lda FloorY+1,x`: the index does not wrap. */
static uint8_t floor_of_row(pop_game *g, uint8_t row)
{
    if (row == 0xFF) {
        g->unmodelled = 1;
        return 0;
    }
    return pop_floory(g, u8(row + 1));
}

/* --- what the programs "press" (AUTO.S 822-888) ---------------------------------------------- */

static void do_release(pop_game *g)
{
    g->clrF = g->clrB = g->clrU = g->clrD = g->clrbtn = 0;
    g->JSTKX = g->JSTKY = g->btn = 0;
}
static void do_fwd(pop_game *g)   { g->clrF = 0xFF; g->JSTKX = 0xFF; }        /* DoAdvance */
static void do_back(pop_game *g)  { g->clrB = 0xFF; g->JSTKX = 1; }           /* DoRetreat */
static void do_up(pop_game *g)    { g->clrU = 0xFF; g->JSTKY = 0xFF; }        /* DoBlock */
static void do_down(pop_game *g)  { g->clrD = 0xFF; g->JSTKY = 1; }           /* DoTurn */
static void do_press(pop_game *g) { g->clrbtn = 0xFF; g->btn = 0xFF; }        /* DoStrike */
static void do_dropguard(pop_game *g) { g->clrD = 0xFF; do_back(g); }
static void do_engarde(pop_game *g)   { g->clrD = 0xFF; do_fwd(g); }

/* rndp: a random number; the tables are then indexed by guardprog. */
static uint8_t rndp(pop_game *g) { return pop_rnd(g); }

/* --- small routines from MISC.S, SUBS.S, CTRLSUBS.S, TOPCTRL.S ------------------------------- */

void pop_vanishchar(pop_game *g)
{
    g->ch.face = POP_NO_CHAR;
    g->ch.action = 0;
    g->ch.life = 0;
    g->ChgOppStr = u8(0 - g->OppStrength);
}

void pop_rechargemeter(pop_game *g)
{
    g->ChgKidStr = u8(g->MaxKidStr - g->KidStrength);
}

void pop_boostmeter(pop_game *g)
{
    if (g->MaxKidStr < POP_MAXMAXSTR) g->MaxKidStr++;
    pop_rechargemeter(g);
}

static void mark_from(pop_game *g, uint8_t y, int count)      /* Mark3 / Mark2 / Mark1 */
{
    for (; count > 0; count--, y++) {
        int carry = 0;
        g->ry = y;
        g->height = 4;
        pop_markwipe(g, 2, &carry);
        pop_markred(g, 2, &carry);
    }
    g->ry = u8(y - 1);
}

void pop_markkidmeter(pop_game *g) { mark_from(g, 20, 3); }
void pop_markoppmeter(pop_game *g) { mark_from(g, 28, 2); }

void pop_markmeters(pop_game *g)
{
    pop_markkidmeter(g);
    pop_markoppmeter(g);
}

void pop_deadenemy(pop_game *g)
{
    if (g->level == 0) {                       /* the demo goes on to its second part */
        g->milestone = 1;
        g->PreRecPtr = 0;
        g->PlayCount = 0;
    } else if (g->level == 13) {               /* :wingame the vizier is dead */
        pop_cuesong(g, POP_SONG_UPSTAIRS, 25);
        g->lightcolor = 0xFF;
        g->lightning = 10;
        g->exitopen = 1;
        g->timerequest = 4;
        (void)pop_rdblock(g, 24, 0, 0);        /* the plate that opens the way out */
        pop_pushpp(g);
    } else if (g->ch.id != 1) {                /* (no tune for the shadow) */
        pop_cuesong(g, POP_SONG_VICT, 25);
    }
}

void pop_deathsong(pop_game *g)
{
    uint8_t song = POP_SONG_ACCID;
    if (g->shad.id == 1) song = POP_SONG_SHADOW;
    else if (g->heroic != 0) song = POP_SONG_HEROIC;
    pop_cuesong(g, song, 255);
}

void pop_kill0(pop_game *g)
{
    if (!NEG(g->ch.life) || g->ch.scrn != 0) return;
    pop_g_addsound(g, POP_SND_SPLAT);
    (void)pop_decstr(g, 100);
    g->msgtimer = 0;
    g->ch.life = 0;
    g->ch.posn = 185;
}

void pop_chgmeters(pop_game *g)
{
    uint8_t a;
    if (g->level == 12 && (g->op.id | g->ch.id) == 1) {      /* kid against his shadow: */
        if (NEG(g->ChgKidStr)) g->ChgOppStr = g->ChgKidStr;  /* what hurts one hurts both */
        else if (NEG(g->ChgOppStr)) g->ChgKidStr = g->ChgOppStr;
    }
    a = u8(g->KidStrength + g->ChgKidStr);
    if (a <= g->MaxKidStr) g->KidStrength = a;
    a = u8(g->OppStrength + g->ChgOppStr);
    if (a <= g->MaxOppStr) g->OppStrength = a;
}

void pop_unholy(pop_game *g)
{
    if (g->level != 12) return;
    if ((g->op.id | g->ch.id) != 1) return;    /* kid and shadow */
    if (!NEG(g->ch.life) || NEG(g->op.life)) return;         /* I live, the other is dead */
    g->lightcolor = 0xFF;
    g->lightning = 5;
    pop_g_addsound(g, POP_SND_SPLAT);
    (void)pop_decstr(g, 100);
}

void pop_firstguard(pop_game *g)
{
    if (g->EnemyAlert < 2) return;
    if (g->ch.sword != 0 || g->op.sword == 0) return;
    if (g->op.action >= 2) return;
    if (g->ch.face == g->op.face) return;
    if (pop_getopdist(g) < 0xF1) return;       /* only when within 15 behind... */
    g->ch.y = floor_of_row(g, g->ch.block_y);  /* bump off the guard */
    pop_g_jumpseq(g, POP_SQ_BUMP);
    pop_g_animchar(g);
}

void pop_potioneffect(pop_game *g)
{
    uint8_t x = g->lastpotion;
    if (g->ch.id != 0 || x == 0) return;
    if (NEG(x)) {                              /* the sword */
        g->gotsword = 1;
        pop_cuesong(g, POP_SONG_SWORD, 25);
        g->lightcolor = 0xFF;
        g->lightning = 3;
    } else if (x == 1) {                       /* recharge one unit */
        if (g->KidStrength == g->MaxKidStr) return;
        g->lightcolor = 0x99;
        g->lightning = 2;
        pop_cuesong(g, POP_SONG_SHORTPOT, 25);
        g->ChgKidStr = 1;
    } else if (x == 2) {                       /* one unit more, and full */
        g->lightcolor = 0x99;
        g->lightning = 5;
        pop_cuesong(g, POP_SONG_POTION, 25);
        pop_boostmeter(g);
    } else if (x == 3) {                       /* weightless */
        pop_cuesong(g, POP_SONG_SHORTPOT, 25);
        g->ag.weightless = 200;
        g->vibes = 3;
    } else if (x == 4) {                       /* upside down */
        g->invert ^= 0xFF;
        g->redrawflg = 2;
        if (g->hooks.invert_y) g->hooks.invert_y(g->hooks.ctx);
    } else if (x == 5) {                       /* poison */
        pop_g_addsound(g, POP_SND_SPLAT);
        g->ChgKidStr = 0xFF;
    }
}

void pop_mouserescue(pop_game *g)
{
    pop_loadkid(g);
    g->ch.id = 24;
    g->ch.x = 200;
    g->ch.block_y = 0;
    g->ch.y = floor_of_row(g, 0);
    g->ch.face = 0xFF;
    g->ch.life = 0xFF;
    g->OppStrength = 1;
    pop_g_jumpseq(g, POP_SQ_MSCURRY);
    pop_g_animchar(g);
    pop_saveshad(g);
}

void pop_stabchar(pop_game *g)
{
    uint8_t seq;
    if (!NEG(g->ch.life)) return;              /* already dead */
    if (g->ch.sword != 2) {                    /* :DL defenceless: one thrust kills */
        (void)pop_decstr(g, 100);
        goto killed;
    }
    if (g->ch.id == 4) goto wounded;           /* the skeleton has no life points */
    if (pop_decstr(g, 1) != 0) goto wounded;
killed:
    if (pop_getbehind(g) == POP_SPACE) {       /* killed at an edge: knocked off it */
        uint8_t d = pop_getdist(g);
        if (d >= 4) {
            g->ch.x = pop_add_char_x(&g->ch, u8(d - 14));
            g->ch.block_y++;
            pop_g_jumpseq(g, POP_SQ_FIGHTFALL);
            goto splat;
        }
    }
    seq = POP_SQ_STABKILL;
    goto land;
wounded:
    seq = POP_SQ_STABBED;
land:
    pop_g_jumpseq(g, seq);
    g->ch.y = floor_of_row(g, g->ch.block_y);
    g->ch.y_vel = 0;
splat:
    pop_g_addsound(g, POP_SND_SPLAT);
    pop_g_animchar(g);
}

/* getreflect: turn g->ch into the kid's mirror image. */
static void getreflect(pop_game *g)
{
    uint8_t d;
    g->mirrx = u8(pop_getblockej(g, g->ch.block_x) + POP_ANGLE + 3);
    d = pop_getdist(g);
    if (!NEG(g->ch.face)) d = u8((d ^ 0xFFu) + 14);          /* distance to the back of the block */
    g->dmirr = u8(d - 2);
    g->ch.x = u8(2u * g->mirrx - g->ch.x);
    g->ch.face ^= 0xFF;
}

void pop_reflection(pop_game *g)
{
    uint8_t top;
    pop_loadkid(g);
    pop_getframeinfo(g);
    if (g->createshad == 0xFF) {               /* CreateShad: it steps out as the shadow */
        getreflect(g);
        g->createshad = 0;
        g->ch.id = 1;
        pop_g_addsound(g, POP_SND_MIRRORCRACK);
        pop_saveshad(g);
        g->MaxOppStr = g->OppStrength = g->MaxKidStr;        /* it takes his strength ... */
        g->KidStrength = 1;                                  /* ... and leaves him one unit */
        pop_markmeters(g);
        return;
    }
    if (pop_getunderft(g) != POP_MIRROR) return;
    getreflect(g);
    if (NEG(g->dmirr)) return;                 /* wrong side of the mirror */
    pop_setupchar(g);
    top = pop_blocktop(g, u8(g->ch.block_y + 1));
    if (top >= g->FCharY) return;
    g->FCharCU = top;                          /* crop to the mirror */
    g->FCharCL = u8((g->ch.block_x << 2) + 1);
    if (g->hooks.add_refl_obj) g->hooks.add_refl_obj(g->hooks.ctx);
}

void pop_bonesrise(pop_game *g)
{
    uint8_t id;
    int carry = 0;
    if (g->level != 3 || g->shad.face != POP_NO_CHAR || g->VisScrn != POP_SKELSCRN) return;
    if (g->exitopen == 0) return;
    if (g->kid.block_x != POP_SKELTRIG && g->kid.block_x != POP_SKELTRIG + 1) return;

    id = pop_rdblock(g, g->VisScrn, POP_SKELX, POP_SKELY);   /* the pile of bones goes */
    pop_set_bluetype(g, g->ry, POP_FLOOR);
    g->height = 24;
    pop_markred(g, 2, &carry);
    pop_markwipe(g, 2, &carry);
    g->ry++;
    pop_markred(g, 2, &carry);
    pop_markwipe(g, 2, &carry);
    if (id != POP_BONES) return;

    g->ch.scrn = g->VisScrn;                   /* ... and a live skeleton stands there */
    g->ch.block_y = POP_SKELY;
    g->ch.y = floor_of_row(g, POP_SKELY);
    g->ch.block_x = POP_SKELX;
    g->ch.x = u8(pop_getblockej(g, POP_SKELX) + POP_ANGLE + 7);
    g->ch.face = 0xFF;
    pop_g_jumpseq(g, POP_SQ_ARISE);
    pop_g_animchar(g);
    g->guardprog = POP_SKELPROG;
    g->ch.life = 0xFF;
    g->OppStrength = 3;
    g->ag.alertguard = 0;
    g->refract = 0;
    g->ch.x_vel = g->ch.y_vel = 0;
    g->ch.sword = 2;
    g->ch.id = 4;
    pop_saveshad(g);
}

/* CHECKALERT: EnemyAlert = 2 if kid and opponent stand on one clear stretch
 * of floor, 1 if they can see each other across a gap, slicer or shut gate,
 * 0 if they cannot. */
void pop_checkalert(pop_game *g)
{
    uint8_t lo, hi;
    if (g->shad.id == 24) return;              /* the mouse */
    g->EnemyAlert = 0;
    if (g->shad.id == 1 && g->level != 12) return;           /* the shadow fights only on 12 */
    if (g->kid.posn == 0 || (g->kid.posn >= 219 && g->kid.posn < 229)) return;   /* on the stairs */
    if (g->shad.face == POP_NO_CHAR) return;
    if (!NEG(g->kid.life & g->shad.life)) return;            /* one of them is dead */
    if (g->kid.scrn != g->shad.scrn || g->kid.block_y != g->shad.block_y) return;
    g->EnemyAlert = 2;

    /* scan the blocks between them, by the x of their middles */
    lo = u8(pop_getblockej(g, g->kid.block_x) + 7);
    hi = u8(pop_getblockej(g, g->shad.block_x) + 7);
    if (hi < lo) { uint8_t t = lo; lo = hi; hi = t; }
#define RD(x) pop_rdblock(g, g->kid.scrn, pop_getblockxp(g, (x)), g->kid.block_y)
    if (RD(lo) == POP_SLICER) lo = u8(lo + 14);              /* a slicer at the left end: skip */
    if (RD(hi) == POP_GATE) hi = u8(hi - 14);                /* a gate at the right end: skip */
    if (hi < lo) return;
    for (;;) {
        uint8_t id;
        if (lo > hi) return;
        id = RD(lo);
        if (id == POP_BLOCK || id == POP_PANELWIF || id == POP_PANELWOF) {
            g->EnemyAlert = 0;                 /* a wall blocks the view */
            return;
        }
        if (id == POP_LOOSE || id == POP_SLICER
            || (id == POP_GATE && pop_bluespec(g, g->ry) < 28 * 4)
            || (id != POP_GATE && pop_cmpspace(id) == 0)) {
            g->EnemyAlert = 1;                 /* an obstacle, but they see each other */
        }
        lo = u8(lo + 14);
        if (lo == 0) return;
    }
#undef RD
}

/* --- the programs (AUTO.S 160-819) ----------------------------------------------------------- */

static void maybe_advance(pop_game *g)
{
    if (g->guardprog != 0 && g->gdtimer != 0) return;        /* (guard 0 is too dumb to care) */
    if (rndp(g) >= fight(g, POP_FT_ADV, g->guardprog)) return;
    do_fwd(g);
}

static void maybe_block(pop_game *g)
{
    uint8_t p = g->op.posn;
    if (p != 152 && p != 153 && p != 162) return;            /* opponent is not striking */
    if (rndp(g) >= fight(g, g->justblocked ? POP_FT_IMPBLOCK : POP_FT_BLOCK, g->guardprog)) {
        return;
    }
    do_up(g);
}

static void maybe_strike(pop_game *g)
{
    unsigned table;
    if (g->op.posn == 169 || g->op.posn == 151) return;      /* he is starting a strike */
    table = (g->ch.posn == 161 || g->ch.posn == 150) ? POP_FT_RESTRIKE : POP_FT_STRIKE;
    if (rndp(g) >= fight(g, table, g->guardprog)) return;
    do_press(g);
}

static void gen_fight(pop_game *g)             /* en garde, face to face, in reach */
{
    uint8_t d = pop_getopdist(g);
    if (d < POP_BLOCKTHRES1 || d >= POP_BLOCKRANGE2) { maybe_advance(g); return; }
    maybe_block(g);
    if (g->refract != 0) return;
    d = pop_getopdist(g);
    if (d < POP_STRIKERANGE1 || d >= POP_STRIKERANGE2) { maybe_advance(g); return; }
    maybe_strike(g);
}

static void in_range(pop_game *g)
{
    if (g->op.sword == 2) { gen_fight(g); return; }
    if (g->refract != 0) return;               /* he is unarmed or off guard: maul him */
    if (pop_getopdist(g) < POP_STRIKERANGE2) do_press(g);
    else do_fwd(g);
}

/* FollowKid: he has dropped out of the fight.  Advance to the edge; follow
 * him down only for a one-storey drop onto safe floor while he is there. */
static void follow_kid(pop_game *g)
{
    uint8_t id;
    if (g->op.action == 2 || g->op.action == 6) return;      /* he is hanging on the ledge */
    id = pop_getinfront(g);
    if (pop_cmpbarr(id) != 0) goto stopped;
    if (pop_cmpspace(id) != 0) { do_fwd(g); return; }
    (void)pop_getinfront(g);                   /* :atedge look one row down */
    g->tempblocky++;
    id = pop_rdblock1(g);
    if (id == POP_SPIKES || id == POP_LOOSE || pop_cmpbarr(id) != 0) goto stopped;
    if (pop_cmpspace(id) == 0) goto stopped;
    if (u8(g->ch.block_y + 1) != g->op.block_y) goto stopped;
    do_fwd(g);
    return;
stopped:
    g->droppedout = 0;
    do_back(g);                                /* so as to kill him if he climbs back up */
}

static void en_garde(pop_game *g)
{
    uint8_t p = g->ch.posn, d;
    if (p == 166 || p < 150) return;           /* wait until ready */
    if (g->EnemyAlert < 2) {
        if (g->EnemyAlert == 1) return;        /* in sight across a gap or barrier: stay put */
        if (g->droppedout != 0) { follow_kid(g); return; }
        if (g->ch.id == 4) return;             /* (a skeleton never lowers its guard) */
        do_dropguard(g);
        return;
    }
    /* :ea2 a clear stretch of floor between them.  Let a stunned kid recover. */
    d = pop_getopdist(g);
    if (!NEG(d) && d >= 12 && g->op.posn >= 102 && g->op.posn < 118 && g->op.action == 5) return;

    d = pop_getopdist(g);
    if (d >= POP_TOOFAR) {                     /* :outofrange */
        if (g->refract != 0) return;
        if (g->ch.face != g->op.face) {
            p = g->op.posn;
            if (p >= 7 && p < 15) {            /* :runwait he is running at me */
                if (pop_getopdist(g) < POP_RUNTHRES) do_press(g);
                return;
            }
            if (p >= 34 && p < 44) {           /* :jumpwait or jumping past: cut him down */
                if (pop_getopdist(g) < POP_JUMPTHRES) do_press(g);
                return;
            }
        }
        /* :nojump advance, but only onto solid floor */
        if (pop_cmpspace(pop_getinfront(g)) != 0 && pop_cmpspace(pop_get2infront(g)) != 0) {
            do_fwd(g);
        } else {
            do_back(g);
        }
        return;
    }
    if (d < (g->ch.sword >= 2 ? POP_TOOCLOSE : POP_OFFGUARDTHRES)) {   /* :tooclose to hit him */
        if (g->ch.face == g->op.face) do_back(g);
        else do_fwd(g);
        return;
    }
    in_range(g);
}

static void alert(pop_game *g)                 /* Alert: not en garde */
{
    uint8_t d;
    if (!NEG(g->kid.life)) return;             /* the kid is dead: relax */
    d = pop_getopdist(g);
    if (g->op.block_y == g->ch.block_y && d >= 0xF8) goto eng;   /* right on top of me */
    if (g->ag.alertguard != 0) {               /* a sound: look round */
        g->ag.alertguard = 0;
        if (d < 128) goto eng;
        if (d >= 0xFC) return;                 /* overlapping: stand still */
        do_down(g);                            /* DoTurn */
        return;
    }
    if (d >= 128) return;                      /* he is behind me */
eng:
    if (g->EnemyAlert == 0) return;
    if (g->level == 13 && g->SongCue != 0) return;           /* the vizier waits for his music */
    do_engarde(g);
}

static void guard_prog(pop_game *g)
{
    if (g->ch.sword >= 2) en_garde(g);
    else alert(g);
}

/* chgshadposn: put the shadow at one of its scripted places. */
static void chgshadposn(pop_game *g, unsigned which)
{
    const uint8_t *p;
    if (!g->autodata || !g->autodata->shadpos || which + 8 > POP_SHADPOS_SIZE) {
        g->unmodelled = 1;
        return;
    }
    p = g->autodata->shadpos + which;
    g->ch.posn = p[0];
    g->ch.x = p[1];
    g->ch.y = p[2];
    g->ch.face = p[3];
    g->ch.block_x = p[4];
    g->ch.block_y = p[5];
    g->ch.action = p[6];
    pop_g_jumpseq(g, p[7]);
    g->ch.id = 1;
    g->PlayCount = 0;
}

static void csps(pop_game *g, unsigned which)
{
    chgshadposn(g, which);
    g->guardprog = 3;
    g->MaxOppStr = g->OppStrength = POP_SHADSTRENGTH;
    pop_saveshad(g);
}

static void final_shad(pop_game *g)            /* level 12 */
{
    uint8_t d, p;
    if (g->ch.scrn == POP_SWORDSCRN && g->shadowaction == 0) {
        if (g->op.x >= 150) {                  /* hold him at the top until the kid arrives */
            csps(g, POP_SHADPOS_12);
            return;
        }
        g->shadowaction = 1;
    }
    if (g->ch.sword >= 2) {                    /* :fight */
        if (g->offguard != 0 && g->refract == 0) do_down(g); /* kid put up his sword: so do I */
        else en_garde(g);
        return;
    }
    if (g->op.sword < 2 && g->offguard != 0) { /* :face swords down, face to face */
        d = pop_getopdist(g);
        if (NEG(d)) {                          /* :merge they walk into each other */
            g->lightcolor = 0xFF;
            g->lightning = 10;
            pop_boostmeter(g);
            pop_cuesong(g, POP_SONG_REJOIN, 85);
            g->mergetimer = 42;
            g->ch.id = 0;
            pop_savekid(g);                    /* the shadow becomes the kid */
            pop_vanishchar(g);
            return;
        }
        if (g->EnemyAlert != 2) return;
        p = g->op.posn;                        /* he starts towards me: I towards him */
        if ((p >= 3 && p < 15) || (p >= 127 && p < 133)) do_fwd(g);
        return;
    }
    /* :hostile */
    if (g->EnemyAlert >= 2 && pop_getopdist(g) < POP_SWORDTHRES) {
        if (g->ch.posn == 15) do_engarde(g);   /* draw on him */
        return;
    }
    if (NEG(pop_getopdist(g))) do_back(g);     /* turn to face him */
}

static void shadow_prog(pop_game *g)
{
    if (g->level == 4) {                       /* out of the mirror and away */
        if (g->ch.scrn != 4) return;
        if (g->ch.x < 80) pop_vanishchar(g);
        else do_fwd(g);
    } else if (g->level == 5) {                /* the thief: when the gate opens, take the potion */
        const pop_autodata *a = g->autodata;
        if (g->ch.scrn != POP_FLASKSCRN) return;
        if (g->PlayCount == 0) {
            (void)pop_rdblock(g, POP_FLASKSCRN, 1, 0);
            if (pop_bluespec(g, g->ry) < 20) return;
            g->PreRecPtr = 0;
        }
        if (!a || !a->shadprog5) { g->unmodelled = 1; return; }
        pop_autoplayback(g, a->shadprog5, a->shadprog5_len);
        if (g->ch.x < 15) pop_vanishchar(g);
    } else if (g->level == 6) {                /* when the kid jumps the chasm, shut the gate on him */
        if (g->ch.scrn != 1) return;
        if (g->kid.posn != 43 || g->kid.x >= 0x80) return;
        do_press(g);
        do_fwd(g);
    } else if (g->level == 12) {
        final_shad(g);
    }
}

static void mouse_prog(pop_game *g)
{
    if (g->ch.face == POP_NO_CHAR) return;
    if (g->ch.action != 0) {                   /* still running in */
        if (g->ch.x >= 166) return;
        pop_g_jumpseq(g, POP_SQ_MLEAVE);
        pop_g_animchar(g);
        return;
    }
    if (g->ch.x >= 200) pop_vanishchar(g);
}

void pop_autoctrl(pop_game *g)
{
    uint8_t id;
    do_release(g);
    id = g->ch.id;
    if (id == 0) { guard_prog(g); return; }    /* KidProg: the kid in the demo */
    if (g->justblocked != 0) g->justblocked--;
    if (g->gdtimer != 0) g->gdtimer--;
    if (g->refract != 0) g->refract--;
    if (id == 24) mouse_prog(g);
    else if (id == 4) { g->ch.sword = 2; guard_prog(g); }    /* SkelProg */
    else if (id < 2) shadow_prog(g);
    else guard_prog(g);                        /* guards, and the vizier on level 13 */
}

void pop_shadctrl(pop_game *g)
{
    if (g->ch.id == 24) {
        pop_autoctrl(g);
        return;
    }
    if (NEG(g->ch.life) && g->OppStrength == 0) {            /* his strength has run out */
        g->ch.life = 0;
        pop_deadenemy(g);
    }
    if (!NEG(g->ch.life) && g->ch.id == 1) {   /* a dead shadow just vanishes */
        pop_vanishchar(g);
        return;
    }
    if (g->ManCtrl != 0) {                     /* played by hand, with the other device */
        if (g->hooks.desel_input) g->hooks.desel_input(g->hooks.ctx);
        pop_userctrl(g);
        if (g->hooks.save_desel) g->hooks.save_desel(g->hooks.ctx);
        return;
    }
    pop_autoctrl(g);
    pop_genctrl(g);
}

/* DoShad, TOPCTRL.S 701-740: one frame of the opponent.  Off the visible
 * screen he is controlled but not animated or moved. */
void pop_doshad(pop_game *g)
{
    if (g->shad.face == POP_NO_CHAR) return;
    pop_loadshadwop(g);
    pop_rereadblocks(g);
    pop_unholy(g);
    pop_shadctrl(g);
    if (g->ch.scrn == g->VisScrn) {
        pop_g_animchar(g);
        if (g->ch.x >= POP_SCRN_LEFT - 14 && g->ch.x < SCRN_RIGHT + 14) {
            pop_gravity(g);
            pop_addfall(g);
            pop_setupchar(g);
            pop_rereadblocks(g);
            pop_getedges(g);
            pop_enemycoll(g);
            pop_checkfloor(g);
            pop_checkpress(g);
            pop_checkspikes(g);
            pop_checkimpale(g);
            (void)pop_checkslice2(g);
        }
    }
    pop_saveshad(g);
}

void pop_autoplayback(pop_game *g, const uint8_t *prog, size_t len)
{
    uint8_t y, cmd;
    if (g->PlayCount >= 254) return;
    g->PlayCount++;
    y = g->PreRecPtr;
    if (y >= len) { g->unmodelled = 1; return; }
    if (g->PlayCount >= prog[y]) {             /* time for the next command */
        if ((size_t)y + 1 >= len) { g->unmodelled = 1; return; }
        cmd = prog[y + 1];
        g->PreRecPtr = u8(y + 2);
    } else {                                   /* not yet: repeat the last one */
        if (y == 0) { g->unmodelled = 1; return; }
        cmd = prog[y - 1];
    }
    switch (cmd) {
    case POP_CMD_ENDDEMO:
        if (g->hooks.attract_mode) g->hooks.attract_mode(g->hooks.ctx);
        break;
    case POP_CMD_CTR:     do_release(g); break;
    case POP_CMD_FWD:     do_fwd(g); break;
    case POP_CMD_BACK:    do_back(g); break;
    case POP_CMD_UP:      do_up(g); break;
    case POP_CMD_DOWN:    do_down(g); break;
    case POP_CMD_UPFWD:   do_up(g); do_fwd(g); break;
    case POP_CMD_PRESS:   do_press(g); break;
    case POP_CMD_RELEASE: g->btn = 0; break;
    default: break;                            /* (end of program: nothing more) */
    }
}

/* --- sword contact (AUTO.S 914-1040) ------------------------------------------------------------ */

static void test_strike(pop_game *g)
{
    uint8_t d;
    if (g->ch.sword != 2 || g->ch.block_y != g->op.block_y) return;
    if (g->ch.posn != 153 && g->ch.posn != 154) return;      /* the two frames of a thrust */

    d = pop_getopdist(g);
    if (d < POP_BLOCKRANGE2 && (g->op.posn == 161 || g->op.posn == 150)) {
        g->op.posn = 161;                      /* he blocks it: "successful block" */
        if (g->ch.id != 0) g->justblocked = POP_BLOCKTIME;   /* a guard blocks worse for a while */
        pop_g_jumpseq(g, POP_SQ_BLOCKEDSTRIKE);
        pop_g_animchar(g);
        return;
    }
    if (g->ch.posn != 154) return;             /* :nobloc full extension: does it reach? */
    d = pop_getopdist(g);
    if (d < (g->op.sword >= 2 ? POP_STRIKERANGE1 : POP_OFFGUARDTHRES)) return;
    if (d >= POP_STRIKERANGE2) return;
    g->op.action = 99;                         /* "stabbed" */
}

void pop_checkstrike(pop_game *g)
{
    if (g->kid.posn == 0) return;
    if (g->kid.posn >= 219 && g->kid.posn < 229) return;     /* on the staircase */
    pop_loadshadwop(g);
    test_strike(g);
    pop_saveshadwop(g);
    pop_loadkidwop(g);
    test_strike(g);
    pop_savekidwop(g);
}

void pop_checkstab(pop_game *g)
{
    if (g->shad.action == 99) {
        if (g->kid.action == 99) g->kid.action = 1;          /* both at once: the player wins */
        pop_loadshad(g);
        pop_stabchar(g);
        pop_saveshad(g);
        (void)rndp(g);
        g->refract = fight(g, POP_FT_REFRACT, g->guardprog);
    }
    if (g->kid.action != 99) return;
    pop_loadkid(g);
    pop_stabchar(g);
    pop_savekid(g);
}

/* --- from screen to screen (AUTO.S 1230-1738) ----------------------------------------------------- */

void pop_cut(pop_game *g, uint8_t dir)
{
    if (dir == 3) {                            /* Cdown */
        g->ch.scrn = pop_getdown(g, g->ch.scrn);
        g->ch.block_y = u8(g->ch.block_y - 3);
        g->ch.y = u8(g->ch.y - 189);
    } else if (dir == 1) {                     /* Cright */
        g->ch.scrn = pop_getright(g, g->ch.scrn);
        g->ch.x = u8(g->ch.x - SCRN_WIDTH);
    } else if (dir == 2) {                     /* Cup */
        g->ch.scrn = pop_getup(g, g->ch.scrn);
        g->ch.block_y = u8(g->ch.block_y + 3);
        g->ch.y = u8(g->ch.y + 189);
    } else {                                   /* Cleft */
        g->ch.scrn = pop_getleft(g, g->ch.scrn);
        g->ch.x = u8(g->ch.x + SCRN_WIDTH);
    }
}

/* Things that happen as a character passes a particular screen edge. */
static void mirrmusic(pop_game *g)             /* level 4: danger theme by the mirror */
{
    if (g->exitopen == 0 || g->exitopen == 77 || g->level != 4) return;
    if (g->ch.block_y != POP_MIRY || g->ch.scrn != 11) return;
    pop_cuesong(g, POP_SONG_DANGER, 50);
    g->exitopen = 77;                          /* so as not to repeat it */
}

static void milestone3(pop_game *g)            /* level 3: past the gate */
{
    if (g->level != 3 || g->ch.scrn != 7) return;
    g->milestone = 1;
    g->origstrength = g->MaxKidStr;
}

static void stealsword(pop_game *g)            /* level 12: the sword is gone */
{
    if (g->level != 12 || g->ch.scrn != 18) return;
    (void)pop_rdblock(g, POP_SWORDSCRN, POP_SWORDX, POP_SWORDY);
    pop_set_bluetype(g, g->ry, POP_FLOOR);
}

static void jaffmusic(pop_game *g)             /* level 13: the vizier's theme */
{
    if (g->level != 13 || g->exitopen != 0 || g->ch.scrn != 3) return;
    pop_cuesong(g, POP_SONG_JAFFAR, 25);
}

uint8_t pop_cutchar(pop_game *g)
{
    uint8_t y = g->ch.y, act = g->ch.action, p = g->ch.posn, dir;
    if (act != 5 && act != 4 && act != 3) {    /* (falling: only ever down) */
        if (y < TOP_CUT_EDGE_PL || y >= TOP_CUT_EDGE_MI) { dir = 2; goto cut; }
    }
    if (y >= BOT_CUT_EDGE) {                   /* :CUTDOWN */
        if (g->level == 6 && g->ch.scrn == 1) return 0xFF;   /* the plunge into the next level */
        dir = 3;
        goto cut;
    }
    /* no left/right cut while climbing up, standing up, striking, blocking or turning */
    if ((p >= 135 && p < 150) || (p >= 110 && p < 120) || (p >= 150 && p < 163)
        || (p >= 166 && p < 169) || act == 7) {
        return 0xFF;
    }
    if (g->ch.face != 0) {                     /* facing left */
        if (g->leftej <= LEFT_CUT_EDGE) goto left;
        if (g->leftej >= SCRN_RIGHT + 1) goto right;
        return 0xFF;
    }
    {                                          /* :faceR not through a panel at the edge */
        uint8_t id = pop_rdblock(g, g->ch.scrn, 9, g->ch.block_y);
        if (id != POP_PANELWIF && id != POP_PANELWOF && g->rightej >= RIGHT_CUT_EDGE) goto right;
    }
    if (g->rightej <= POP_SCRN_LEFT - 1) goto left;
    return 0xFF;
left:
    mirrmusic(g);
    milestone3(g);
    dir = 0;
    goto cut;
right:
    stealsword(g);
    jaffmusic(g);
    dir = 1;
cut:
    pop_cut(g, dir);
    return dir;
}

/* updateguard: the guard stays behind; note where, for when the kid returns. */
static void updateguard(pop_game *g)
{
    uint8_t s;
    if (g->shad.face == POP_NO_CHAR || g->shad.id == 1 || g->shad.id == 24) return;
    g->tempblockx = 0;                         /* (ADDGUARD will work the column out from x) */
    g->tempblocky = g->shad.block_y;
    (void)pop_indexblock(g);
    s = g->shad.scrn;
    *gd(g, POP_GD_BLOCK, s) = g->ry;
    *gd(g, POP_GD_X, s) = g->shad.x;
    *gd(g, POP_GD_FACE, s) = g->shad.face;
    *gd(g, POP_GD_PROG, s) = g->guardprog;
    if (NEG(g->shad.life)) {
        *gd(g, POP_GD_SEQH, s) = 0;            /* alive: he will start afresh */
    } else {
        *gd(g, POP_GD_SEQL, s) = u8(g->shad.seq);            /* dead: he lies as he fell */
        *gd(g, POP_GD_SEQH, s) = u8(g->shad.seq >> 8);
    }
    g->shad.face = POP_NO_CHAR;
    g->OppStrength = 0;
}

void pop_cutcheck(pop_game *g)
{
    uint8_t dir;
    int transfer;
    if (g->CUTTIMER != 0) {
        g->CUTTIMER--;
        return;
    }
    pop_loadkid(g);
    pop_setupchar(g);
    pop_getedges(g);
    dir = pop_cutchar(g);
    if (NEG(dir)) return;
    g->tempobjid = dir;                        /* (]cutdir shares the byte at $F2) */
    pop_savekid(g);
    g->cutscrn = g->ch.scrn;
    if (g->shad.face == POP_NO_CHAR) return;

    /* The guard: take him along (he is alive, en garde and close behind, and
     * no live guard waits on the new screen), or leave him. */
    transfer = 0;
    if (NEG(g->shad.life) && g->shad.sword == 2
        && !(*gd(g, POP_GD_BLOCK, g->kid.scrn) < 30 && *gd(g, POP_GD_SEQH, g->kid.scrn) == 0)) {
        if (dir == 0) transfer = g->shad.x < 256 - SCRN_WIDTH - 25;
        else if (dir == 1) transfer = g->shad.x >= SCRN_WIDTH + 25;
        else if (dir == 2) transfer = NEG(g->shad.block_y);
        else transfer = g->shad.block_y >= 3;
    }
    if (!transfer) {
        updateguard(g);
        return;
    }
    *gd(g, POP_GD_BLOCK, g->kid.scrn) = 0xFF;  /* transferguard */
    *gd(g, POP_GD_BLOCK, g->shad.scrn) = 0xFF;
    pop_loadshad(g);
    pop_cut(g, dir);
    pop_saveshad(g);
}

void pop_cutguard(pop_game *g)
{
    if (g->shad.face == POP_NO_CHAR || g->shad.y < BOT_CUT_EDGE) return;
    if (g->shad.id == 1) {                     /* the shadow vanishes */
        if (g->shad.action != 4) return;
        pop_loadshad(g);
        pop_vanishchar(g);
        pop_saveshad(g);
        return;
    }
    if (g->shad.id == 4) {                     /* the skeleton lands on the screen below */
        g->shad.scrn = pop_getdown(g, g->shad.scrn);
        if (g->shad.scrn == 3) {
            pop_g_addsound(g, POP_SND_SPLAT);
            g->shad.x = 0x85;
            g->shad.block_y = 1;
            g->shad.face = 0;
            g->shad.life = 0xFF;
            updateguard(g);
            return;
        }
    }
    pop_deadenemy(g);                          /* ]RemoveGd a guard is gone for good */
    *gd(g, POP_GD_BLOCK, g->VisScrn) = 0xFF;
    g->shad.face = POP_NO_CHAR;
    g->OppStrength = 0;
    g->ChgOppStr = 0xFF;
}

/* INITIALGUARDS, SUBS.S 1674-1690: once per level.  The level file gives
 * each guard's block only; his x and "fresh start" mark are set here. */
void pop_initialguards(pop_game *g)
{
    uint8_t scrn, row;
    for (scrn = 24; scrn != 0; scrn--) {
        uint8_t b = *gd(g, POP_GD_BLOCK, scrn);
        if (b >= 30) continue;
        *gd(g, POP_GD_X, scrn) = u8(pop_getblockej(g, pop_unindex(b, &row)) + POP_ANGLE + 7);
        *gd(g, POP_GD_SEQH, scrn) = 0;
    }
}

static void add_normal_guard(pop_game *g)
{
    uint8_t v = g->VisScrn, b = *gd(g, POP_GD_BLOCK, v), row, seqh, prog;
    if (b >= 30) return;                       /* no guard on this screen */
    g->ch.scrn = v;
    (void)pop_unindex(b, &row);
    g->ch.block_y = row;
    g->ch.y = floor_of_row(g, row);
    g->ch.x = *gd(g, POP_GD_X, v);
    g->ch.block_x = pop_getblockxp(g, g->ch.x);
    g->ch.face = *gd(g, POP_GD_FACE, v);
    g->ch.id = (g->level == 3) ? 4 : 2;        /* skeleton on level 3, else guard */
    seqh = *gd(g, POP_GD_SEQH, v);
    if (seqh == 0) {                           /* a fresh start */
        if (g->ch.id == 4) {
            g->ch.sword = 2;
            pop_g_jumpseq(g, POP_SQ_LANDENGARDE);
        } else {
            g->ch.sword = 0;
            pop_g_jumpseq(g, POP_SQ_ALERTSTAND);
        }
    } else {                                   /* as he was left */
        g->ch.seq = (uint16_t)(*gd(g, POP_GD_SEQL, v) | ((uint16_t)seqh << 8));
    }
    pop_g_animchar(g);
    if (g->ch.posn == 185 || g->ch.posn == 177 || g->ch.posn == 178) {   /* killed, impaled, halved */
        g->ch.life = 1;
        g->OppStrength = 0;
    } else {
        g->ch.life = 0xFF;
        g->ag.alertguard = 0;
        g->refract = 0;
        g->justblocked = 0;
        /* getgdstrength.  guardprog still holds the previous guard's
         * program here: it is set a few lines further down. */
        g->MaxOppStr = g->OppStrength = u8(fight(g, POP_FT_BASICSTR, g->level)
                                           + fight(g, POP_FT_EXTRASTR, g->guardprog));
    }
    g->ch.x_vel = g->ch.y_vel = 0;
    g->ch.action = 1;
    prog = *gd(g, POP_GD_PROG, g->VisScrn);
    g->guardprog = prog < POP_NUMPROGS ? prog : 3;
    g->GuardColor = fight(g, POP_FT_BASICCOLOR, g->level)
        ^ fight(g, POP_FT_SPECIALCOLOR, g->guardprog);
    pop_saveshad(g);
}

void pop_addguard(pop_game *g)
{
    g->offguard = 0;
    if (g->level == 12) {                      /* the shadow drops in once the sword is taken */
        if (g->exitopen != 0 || g->mergetimer != 0 || g->VisScrn != POP_SWORDSCRN) return;
        g->ch.scrn = g->VisScrn;
        if (pop_rdblock(g, g->VisScrn, POP_SWORDX, POP_SWORDY) == POP_SWORD) return;
        g->shadowaction = 0;
        g->exitopen = 1;
        csps(g, POP_SHADPOS_12);
        return;
    }
    if (g->level == 6) {
        g->ch.scrn = g->VisScrn;
        if (g->VisScrn == 1) {                 /* waiting across the chasm */
            if (g->exitopen != 77) {
                pop_cuesong(g, POP_SONG_DANGER, 50);
                g->exitopen = 77;
            }
            csps(g, POP_SHADPOS_6A);
            return;
        }
    } else if (g->level == 5) {
        g->ch.scrn = g->VisScrn;
        if (g->VisScrn == POP_FLASKSCRN) {     /* by the potion, unless it is gone */
            if (pop_rdblock(g, g->VisScrn, POP_FLASKX, POP_FLASKY) == POP_FLASK) {
                csps(g, POP_SHADPOS_5);
            }
            return;
        }
    }
    add_normal_guard(g);
}
