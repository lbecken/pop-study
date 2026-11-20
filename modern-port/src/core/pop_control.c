/* pop_control.c - see pop_control.h.  New code; ports of original routines.
 *
 * The code follows the original branch by branch, with the original labels
 * in comments.  Where a branch looks odd it is because the original does
 * that; docs/port/player-control.md lists the cases worth knowing.
 */

#include "pop_control.h"

#include "pop_collide.h"
#include "pop_mover.h"
#include "pop_world.h"

#define NEG(v) (((v) & 0x80u) != 0)        /* bmi / "fresh press" / pressed */

/* CTRL.S 37-52 */
enum {
    DEATH_VELOCITY = 33, OOF_VELOCITY = 22, GRAB_REACH = 0xF8 /* -8 */, GRAB_SPEED = 32,
    GRAB_LEAD = 25, STUN_TIME = 12, JUMPUP_REACH = 0, JUMPUP_ANGLE = 0xFA /* -6 */,
    JUMP_BACK_THRES = 6, STEP_OFF_FWD = 3, STEP_OFF_BACK = 8, SWORD_THRES = 90,
    SWORD_THRES_N = 0xF6 /* -10 */, BLOCK_THRES = 32, GRACE_PERIOD = 9, GD_PATIENCE = 15,
    GCLIMB_THRES = 6, STAIR_THRES = 30
};
/* CTRL.S 1746-1750 */
enum { RJ_CHANGE = 4, RJ_LOOKAHEAD = 1, RJ_LEAD_DIST = 14, RJ_MAX_FUJ_BAK = 8, RJ_MAX_FUJ_FWD = 2 };
enum { THINNER = 3, GATE_MARGIN = 6, EST_WIDTH = 13, SPIKE_EXT = 5, FCHECKMARK = 0x40,
       FTHINMARK = 0x20 };

#define u8(v) ((uint8_t)(v))                 /* 8-bit wrap-around arithmetic */

/* --- helpers ---------------------------------------------------------------------- */

static void anim_sound(void *ctx, uint8_t s) { pop_addsound(&((pop_game *)ctx)->sq, s); }
static void anim_slicers(void *ctx) { pop_addslicers(ctx); }
static void anim_upstairs(void *ctx) { pop_goneupstairs(ctx); }
static void anim_potion(void *ctx)
{
    pop_game *g = ctx;
    if (g->hooks.potion_effect) g->hooks.potion_effect(g->hooks.ctx);
}

void pop_g_jumpseq(pop_game *g, uint8_t seq_no)
{
    if (pop_jumpseq(g->seq, &g->ch, seq_no) != POP_SEQ_OK) {
        g->unmodelled = 1;
    }
}

void pop_g_animchar(pop_game *g)
{
    pop_anim_hooks h = { g, anim_sound, anim_slicers, anim_upstairs, anim_potion };
    if (pop_animchar(g->seq, &g->ch, &g->ag, &h) != POP_SEQ_OK) {
        g->unmodelled = 1;
    }
}

void pop_g_addsound(pop_game *g, uint8_t sound) { pop_addsound(&g->sq, sound); }

static void addslicers(pop_game *g) { pop_addslicers(g); }

/* clrall, CTRL.S 625-632: clrbtn is not touched; returns A = 1. */
uint8_t pop_clrall(pop_game *g)
{
    g->clrB = g->clrF = g->clrU = g->clrD = 0;
    return 1;
}

/* FACEJSTK, SPECIALK.S 788-797 */
void pop_facejstk(pop_game *g)
{
    uint8_t f = g->clrF;
    g->JSTKX = u8(0 - g->JSTKX);
    g->clrF = g->clrB;
    g->clrB = f;
}

/* DECSTR, MISC.S 691-735 */
uint8_t pop_decstr(pop_game *g, uint8_t amount)
{
    uint8_t *strength = g->ch.id ? &g->OppStrength : &g->KidStrength;
    uint8_t *change = g->ch.id ? &g->ChgOppStr : &g->ChgKidStr;
    if (amount >= *strength) {                 /* killkid / killopp */
        *change = u8(0 - *strength);
        return 0;
    }
    *change = u8(0 - amount);
    return *change;
}

/* GETSPIKES, MOVER.S 365-382: state of the spikes last read with rdblock. */
uint8_t pop_getspikes(pop_game *g)
{
    uint8_t s = pop_bluespec(g, g->ry);
    if (NEG(s)) return s == 0xFF ? 0 : 1;      /* jammed spikes are safe */
    if (s == 0) return 0;
    return s < SPIKE_EXT ? 2 : 0;
}

/* GETOPDIST, CTRLSUBS.S 2052-2102 */
uint8_t pop_getopdist(pop_game *g)
{
    uint8_t a;
    if (g->ch.scrn != g->op.scrn) {
        g->rx = 127;
        return 127;
    }
    if (g->op.x >= g->ch.x) {
        a = u8(g->op.x - g->ch.x);
        if (NEG(a)) a = 127;
    } else {
        a = u8(g->ch.x - g->op.x);
        if (NEG(a)) a = 127;
        a = u8(0 - a);
    }
    if (NEG(g->ch.face)) a = u8(0 - a);
    if (NEG(g->ch.face ^ g->op.face)) {        /* facing opposite ways */
        if (a < 127 - EST_WIDTH) a = u8(a + EST_WIDTH);
    }
    g->rx = a;
    return a;
}

/* CHECKLEDGE, CTRLSUBS.S 1814-1858.  In: blockid = the block that must be
 * clear; ledge_id and ry = rdblock results for the block to grab. */
uint8_t pop_checkledge(pop_game *g, uint8_t ledge_id)
{
    uint8_t tempstate = pop_bluespec(g, g->ry);
    uint8_t clear = g->blockid;
    if (clear == POP_BLOCK) return 0;
    if (clear == POP_PANELWOF && !NEG(g->ch.face)) return 0;
    if (pop_cmpspace(clear) != 0) return 0;
    if (ledge_id == POP_LOOSE) {
        /* `bit tempstate / bne :no` with A = 11: tests bits 0, 1 and 3 of the
         * loose floor's state, not whether it is non-zero. */
        if ((POP_LOOSE & tempstate) != 0) return 0;
    }
    if (ledge_id == POP_PANELWIF && NEG(g->ch.face)) return 0;
    if (pop_cmpspace(ledge_id) == 0) return 0;
    return 1;
}

/* INDEXBLOCK, CTRLSUBS.S 615-631 */
int pop_indexblock(pop_game *g)
{
    uint8_t y = g->tempblocky;
    if (NEG(y)) {                              /* :above */
        g->ry = g->tempblockx;
        return 1;
    }
    if (y >= 3 || g->tempblockx >= 10) {       /* :os */
        g->ry = 30;
        return 1;
    }
    g->ry = u8(g->tempblockx + 10 * y);
    return 0;
}

uint8_t *pop_rb(pop_game *g, unsigned base, uint8_t y)
{
    static uint8_t sink;
    if (base + y >= POP_RB_SIZE) {
        g->unmodelled = 1;
        sink = 0;
        return &sink;
    }
    return &g->rb[base + y];
}

/* MARKWIPE, CTRLSUBS.S 1744-1757.  A, Y and a set carry are preserved. */
void pop_markwipe(pop_game *g, uint8_t a, int *carry)
{
    uint8_t y = g->ry;
    if (*carry) return;
    if (*pop_rb(g, POP_RB_WIPE, y) == 0 || g->height >= *pop_rb(g, POP_RB_WHITE, y)) {
        *pop_rb(g, POP_RB_WHITE, y) = g->height;   /* keep the larger wipe height */
    }
    *pop_rb(g, POP_RB_WIPE, y) = a;
    *carry = 0;
}

/* MARKRED, CTRLSUBS.S 1728-1737 */
void pop_markred(pop_game *g, uint8_t a, int *carry)
{
    uint8_t y = g->ry;
    if (*carry) {                              /* ]os: top line from the screen above? */
        if (y < 10) g->rb[POP_RB_TOP + y] = a;
        return;
    }
    *pop_rb(g, POP_RB_RED, y) = a;
}

/* REMOVEOBJ, SUBS.S 1415-1432.  In: ry / blueprint from rdblock. */
void pop_removeobj(pop_game *g, uint8_t lastpotion)
{
    int carry = 0;
    size_t i = (size_t)(g->blue - 1u) * 30u + g->ry;
    g->lastpotion = lastpotion;
    g->clrbtn = 1;
    if (g->blue && POP_BLUESPEC + i < POP_LEVEL_SIZE) {
        g->blueprint[POP_BLUETYPE + i] = POP_FLOOR;
        g->blueprint[POP_BLUESPEC + i] = 0;
    } else {
        g->unmodelled = 1;
    }
    g->height = 35;
    pop_markwipe(g, 2, &carry);
    pop_markred(g, 2, &carry);
}

/* --- SETUPCHAR, GETEDGES, GETFWDDIST ------------------------------------------------- */

/* DIMCHAR: width in bytes and height in lines of a character image. */
static void dimchar(pop_game *g, uint8_t image, uint8_t table, uint8_t *w, uint8_t *h)
{
    const pop_imgdims *d = g->dims;
    if (!d || table >= POP_CHTABLES || image == 0 || image > d->count[table]) {
        g->unmodelled = 1;                     /* the original reads stray memory */
        *w = *h = 0;
        return;
    }
    *w = d->wh[table][2u * (image - 1u)];
    *h = d->wh[table][2u * (image - 1u) + 1u];
}

void pop_setupchar(pop_game *g)
{
    uint8_t lo, hi;
    unsigned doubled;
    g->FCharCU = 0;                            /* zerocrop */
    g->FCharCL = 0;
    g->FCharCR = 40;
    g->FCharCD = 192;
    pop_getframeinfo(g);
    g->FCharFace = g->ch.face;
    /* decodeim */
    g->FCharTable = u8((((g->Fsword & 0xC0u) >> 1) + (g->Fimage & 0x80u)) >> 5);
    g->FCharImage = u8((g->Fimage & 0x7Fu) | g->timebomb);

    lo = u8(pop_add_char_x(&g->ch, g->Fdx) - POP_SCRN_LEFT);
    doubled = (unsigned)lo << 1;               /* asl FCharX / rol FCharX+1 */
    lo = u8(doubled);
    hi = u8(doubled >> 8);
    if (hi != 0 && lo >= 0xF0) hi = 0xFF;      /* a small negative x */
    g->FCharY = u8(g->Fdy + g->ch.y);
    if (!NEG(g->Fcheck ^ g->FCharFace)) {      /* high bits match: odd dot */
        lo = u8(lo + 1);
        if (lo == 0) hi = u8(hi + 1);
    }
    g->FCharX = (uint16_t)(lo | ((uint16_t)hi << 8));
}

void pop_getedges(pop_game *g)
{
    uint8_t w, h, a, thin;
    dimchar(g, g->FCharImage, g->FCharTable, &w, &h);
    g->imheight = h;
    if (w >= 16) g->unmodelled = 1;            /* Mult7 has 16 entries */
    g->imwidth = u8((7u * w + 1u) >> 1);
    a = u8((((g->FCharX >> 8) & 1u) << 7) | ((g->FCharX & 0xFFu) >> 1));
    a = u8(a + POP_SCRN_LEFT);                 /* back to 140-wide units */
    if (!NEG(g->ch.face)) a = u8(a - g->imwidth);
    g->leftej = a;
    g->rightej = u8(a + g->imwidth);

    a = u8(g->FCharY - g->imheight + 1);
    if (a >= 192) a = 0;
    g->topej = a;
    a = pop_getblocky(a);
    if (a == 3) a = 0xFF;                      /* off screen: call it -1 */
    g->topblock = a;
    g->bottomblock = pop_getblocky(g->FCharY);
    g->leftblock = pop_getblockx(g, g->leftej);
    g->rightblock = pop_getblockx(g, g->rightej);

    thin = (g->Fcheck & FTHINMARK) ? THINNER : 0;
    g->CDLeftEj = u8(g->leftej + thin);
    g->CDRightEj = u8(g->rightej - thin);
}

/* DBarr, COLL.S 925-983: distance to the barrier in block tempblockx;
 * negative if it is behind the character, $FF if there is none. */
static uint8_t dbarr(pop_game *g)
{
    static const uint8_t bar_l[5] = { 0, 12, 2, 0, 0 };    /* COLL.S 55 */
    static const uint8_t bar_r[5] = { 0, 0, 9, 11, 0 };    /* COLL.S 56 */
    uint8_t code;
    if (g->tempobjid == POP_GATE) {
        /* gatebarr?: an open enough gate is no barrier */
        if (u8((pop_bluespec(g, g->ry) >> 2) + GATE_MARGIN) >= g->imheight) return 0xFF;
    }
    g->blockedge = u8(pop_getblockej(g, g->tempblockx) + POP_ANGLE);
    code = pop_cmpbarr(g->tempobjid);
    if (code == 0) return 0xFF;
    if (NEG(g->ch.face)) {                     /* :checkL */
        uint8_t right = u8(g->blockedge + 13 - bar_r[code]);
        return u8(g->CDLeftEj - right);
    }
    return u8(u8(g->blockedge + bar_l[code]) - g->CDRightEj);
}

uint8_t pop_getfwddist(pop_game *g)
{
    uint8_t a;
    pop_getbaseblock(g);
    pop_setupchar(g);
    pop_getedges(g);

    g->tempobjid = pop_getunderft(g);
    if (pop_cmpbarr(g->tempobjid) != 0) {
        g->tempblockx = g->ch.block_x;
        a = dbarr(g);
        if (!NEG(a)) goto tobarr;
    }
    /* :nextb */
    a = g->tempobjid = pop_getinfront(g);
    if (a == POP_PANELWOF && !NEG(g->ch.face)) goto to_eob;
    if (pop_cmpbarr(a) != 0) {
        g->tempblockx = g->infrontx;
        a = dbarr(g);
        if (!NEG(a)) goto tobarr;
    }
    /* :nobarr */
    a = g->tempobjid = pop_getinfront(g);
    if (a == POP_LOOSE) goto to_eob;
    if (a == POP_PRESSPLATE || a == POP_SWORD || a == POP_FLASK) {   /* :toEOB1 */
        a = pop_getdist(g);
        if (a == 0) goto fullstep;
        g->rx = 0;
        goto done;
    }
    if (pop_cmpspace(a) == 0) goto to_eob;
fullstep:
    a = 11;
    g->rx = 2;
    goto done;
to_eob:
    a = pop_getdist(g);
    g->rx = 0;
    goto done;
tobarr:
    if (a >= 14) goto fullstep;
    g->rx = 1;
done:
    g->ry = g->tempobjid;
    return a;
}

/* --- fighting -------------------------------------------------------------------------- */

static int ready_posn(uint8_t x) { return x == 158 || x == 170 || x == 171; }

void pop_dostrike(pop_game *g, uint8_t x)
{
    uint8_t seq;
    if (x == 157 || ready_posn(x) || x == 165) {
        seq = g->ch.id ? POP_SQ_STRIKE : POP_SQ_FASTSTRIKE;   /* kid is fast */
    } else if (x == 150 || x == 161) {
        seq = POP_SQ_BLOCKTOSTRIKE;
    } else {
        return;
    }
    g->clrbtn = 1;
    pop_g_jumpseq(g, seq);
}

void pop_doretreat(pop_game *g)
{
    if (!ready_posn(g->ch.posn)) return;
    g->clrB = 1;
    pop_g_jumpseq(g, POP_SQ_RETREAT);
}

void pop_doadvance(pop_game *g)
{
    if (!ready_posn(g->ch.posn)) return;
    g->clrF = 1;
    pop_g_jumpseq(g, g->ch.id ? POP_SQ_ADVANCE : POP_SQ_FASTADVANCE);
}

void pop_doblock(pop_game *g)
{
    uint8_t x = g->ch.posn, op;
    if (x == 167) {                            /* :3 strike-to-block */
        g->clrU = 1;
        pop_g_jumpseq(g, POP_SQ_STRIKEBLOCK);
        return;
    }
    if (!(ready_posn(x) || x == 168 || x == 165)) return;
    /* :2 from ready position */
    if (pop_getopdist(g) >= BLOCK_THRES) goto blockmiss;
    op = g->op.posn;
    if (g->ch.id != 0) {                       /* enemy sees kid 1 frame ahead */
        if (op != 152) return;
        goto doit;
    }
    if (op == 168) return;                     /* 1 frame too early: wait */
    if (op == 151 || op == 152 || op == 162) goto doit;
    if (op != 153) goto blockmiss;
    g->clrU = 1;                               /* 1 frame too late: skip 1 frame */
    pop_g_jumpseq(g, POP_SQ_READYBLOCK);
    pop_g_animchar(g);
    return;
blockmiss:
    if (g->ch.id != 0) {                       /* enemy doesn't waste blocks */
        pop_doretreat(g);
        return;
    }
doit:
    g->clrU = 1;
    pop_g_jumpseq(g, POP_SQ_READYBLOCK);
}

void pop_doengarde(pop_game *g)
{
    g->clrF = pop_clrall(g);
    g->clrbtn = 1;
    g->ch.sword = 2;
    if (g->ch.id == 0) {
        g->offguard = 0;
        pop_g_jumpseq(g, POP_SQ_ENGARDE);
    } else if (g->ch.id == 1) {
        pop_g_jumpseq(g, POP_SQ_ENGARDE);
    } else {
        pop_g_jumpseq(g, POP_SQ_GUARDENGARDE);
    }
}

void pop_guardctrl(pop_game *g)
{
    if (g->ch.posn != 166) return;             /* standing alert */
    if (!NEG(g->clrD)) return;
    if (NEG(g->clrF)) {
        pop_doengarde(g);
        return;
    }
    g->clrD = 1;
    pop_g_jumpseq(g, POP_SQ_ALERTTURN);
}

void pop_fightctrl(pop_game *g)
{
    uint8_t a, x;
    if (g->ch.action >= 2) return;             /* must be on level ground */

    a = pop_getunderft(g);
    if (a != POP_LOOSE && g->EnemyAlert < 2) goto dropgd;
    /* :skip */
    a = pop_getopdist(g);
    if (a < SWORD_THRES) goto onalert;
    if (a < 128) goto dropgd;
    if (a >= 0xFC) goto onalert;               /* overlapping */
    pop_g_jumpseq(g, POP_SQ_TURNENGARDE);      /* DoTurnEng */
    return;

dropgd:
    if (g->ch.id == 0) {
        g->heroic = 0;
    } else if (g->ch.id >= 2) {
        goto onalert;                          /* guard: remain en garde */
    }
    if (g->ch.posn != 171) return;             /* wait for ready posn */
    g->ch.sword = 0;
    pop_g_jumpseq(g, POP_SQ_RESHEATHE);
    return;

onalert:
    x = g->ch.posn;
    if (x == 161) {                            /* successful block */
        if (!NEG(g->clrbtn)) {
            pop_g_jumpseq(g, POP_SQ_RETREAT);
            return;
        }
    } else if (!NEG(g->clrbtn)) {
        goto nostrike;
    }
    /* :bts */
    if (g->ch.id == 0) g->gdtimer = GD_PATIENCE;
    pop_dostrike(g, x);
    if (g->clrbtn == 1) return;                /* struck */
nostrike:
    if (NEG(g->clrD)) {                        /* down: lower the sword */
        if (!ready_posn(g->ch.posn)) return;
        g->clrD = 1;
        g->ch.sword = 0;
        if (g->ch.id == 0) {
            g->offguard = 1;
            g->refract = GRACE_PERIOD;
            g->heroic = 0;
            pop_g_jumpseq(g, POP_SQ_FASTSHEATHE);
        } else if (g->ch.id == 1) {
            pop_g_jumpseq(g, POP_SQ_RESHEATHE);
        } else {
            pop_g_jumpseq(g, POP_SQ_GOALERTSTAND);
        }
        return;
    }
    if (NEG(g->clrU)) pop_doblock(g);
    else if (NEG(g->clrF)) pop_doadvance(g);
    else if (NEG(g->clrB)) pop_doretreat(g);
}

/* --- actions --------------------------------------------------------------------------- */

void pop_dostandjump(pop_game *g)
{
    g->clrU = g->clrF = 1;
    pop_g_jumpseq(g, POP_SQ_STANDJUMP);
}

void pop_docrouch(pop_game *g)
{
    pop_g_jumpseq(g, POP_SQ_STOOP);
    g->clrD = pop_clrall(g);
}

void pop_doturn(pop_game *g)
{
    g->clrB = pop_clrall(g);
    /* if the enemy is behind you, draw as you turn */
    if (g->gotsword != 0 && g->EnemyAlert >= 2 && NEG(pop_getopdist(g))
        && pop_getdist(g) >= 2) {
        g->ch.sword = 2;
        g->offguard = 0;
        pop_g_jumpseq(g, POP_SQ_TURNDRAW);
        return;
    }
    pop_g_jumpseq(g, POP_SQ_TURN);
}

void pop_dostepfwd(pop_game *g)
{
    uint8_t a;
    g->clrF = g->clrbtn = 1;
    a = pop_getfwddist(g);
    if (a == 0) {
        if (g->rx != 1 && g->ch.repeat != 0) { /* first time at an edge: test with foot */
            g->ch.repeat = 0;
            pop_g_jumpseq(g, POP_SQ_TESTFOOT);
            return;
        }
        a = 11;                                /* barrier: step through; edge: step off */
    }
    g->ch.repeat = a;
    pop_g_jumpseq(g, u8(a + POP_SQ_STEPFWD1 - 1));
}

void pop_dostartrun(pop_game *g)
{
    /* very close to a barrier (other than a slicer): step instead */
    (void)pop_getfwddist(g);
    if (g->rx == 1 && g->ry != POP_SLICER) {
        if (pop_getfwddist(g) < 8) {
            if (NEG(g->clrF)) pop_dostepfwd(g);
            return;
        }
    }
    pop_g_jumpseq(g, POP_SQ_STARTRUN);
}

void pop_dojumphigh(pop_game *g)
{
    uint8_t a, ztemp, x;
    g->clrU = pop_clrall(g);
    a = pop_getfwddist(g);
    if (a < 4 && g->rx == 1) {                 /* close to a barrier: step back */
        g->ch.x = pop_add_char_x(&g->ch, u8(a - 3));
    }
    ztemp = pop_facedx(g, JUMPUP_REACH);
    a = u8(pop_getbasex(g) + JUMPUP_ANGLE + ztemp);   /* where the hand touches */
    x = pop_getblockx(g, a);
    a = pop_rdblock(g, g->ch.scrn, x, u8(g->ch.block_y - 1));
    if (a == POP_BLOCK || pop_cmpspace(a) != 0) {
        pop_g_jumpseq(g, POP_SQ_JUMPUP);       /* touch the ceiling */
    } else {
        pop_g_jumpseq(g, POP_SQ_HIGHJUMP);     /* no ceiling above */
    }
}

void pop_dojumphang(pop_game *g)
{
    uint8_t atemp, a;
    (void)pop_getaboveinf(g);
    atemp = pop_getdist(g);                    /* distance to the front of the block */
    if (atemp >= 4) goto longjump;
    a = pop_getfwddist(g);
    if (a < 4 && g->rx == 1) goto longjump;    /* close to a wall: step back, do Long */
    g->ch.x = pop_add_char_x(&g->ch, atemp);
    pop_g_jumpseq(g, POP_SQ_JUMPHANGMED);
    return;
longjump:
    g->ch.x = pop_add_char_x(&g->ch, u8(atemp - 4));   /* "Long" adds 4 to CharX */
    pop_g_jumpseq(g, POP_SQ_JUMPHANGLONG);
}

void pop_dojumpedge(pop_game *g)
{
    (void)pop_getabove(g);
    g->ch.x = pop_add_char_x(&g->ch, u8(pop_getdist(g) - 10));
    pop_g_jumpseq(g, POP_SQ_JUMPBACKHANG);
}

void pop_dojumpup(pop_game *g)
{
    g->clrU = pop_clrall(g);
    g->blockid = pop_getabove(g);
    if (pop_checkledge(g, pop_getaboveinf(g)) != 0) {
        pop_dojumphang(g);
        return;
    }
    g->blockid = pop_getabovebeh(g);
    if (pop_checkledge(g, pop_getabove(g)) != 0) {     /* possible from one space back? */
        if (pop_getdist(g) < JUMP_BACK_THRES) goto jumphi;   /* too far to fudge */
        if (pop_cmpspace(pop_getbehind(g)) == 0) {     /* no floor behind */
            pop_dojumpedge(g);
            return;
        }
        g->ch.x = pop_add_char_x(&g->ch, u8(pop_getdist(g) - 14));
        pop_rereadblocks(g);
        pop_dojumphang(g);
        return;
    }
jumphi:
    pop_dojumphigh(g);
}

void pop_dorunjump(pop_game *g)
{
    uint8_t ztemp, blockx, a;
    if (g->ch.posn < 7) return;                /* must be in full run */
    g->bufindex = 0;
    ztemp = pop_add_char_x(&g->ch, RJ_CHANGE); /* projected CharX */
    blockx = pop_getblockxp(g, ztemp);
    for (;;) {
        unsigned fx = u8(g->ch.face + 1);
        if (fx > 1) { g->unmodelled = 1; return; }
        blockx = u8(blockx + (fx ? 1 : 0xFF));
        a = pop_rdblock(g, g->ch.scrn, blockx, g->ch.block_y);
        if (a == POP_SPIKES || pop_cmpspace(a) == 0) break;      /* :done */
        g->bufindex++;
        if (g->bufindex >= RJ_LOOKAHEAD + 1) goto noedge;        /* no edge in sight */
    }
    /* units to the end of the floor, less the distance the jump covers */
    a = pop_getdist1(g, ztemp);
    a = u8(a + 14u * g->bufindex - RJ_LEAD_DIST);
    if (a < u8(0 - RJ_MAX_FUJ_BAK) && a >= RJ_MAX_FUJ_FWD) {
        if (a < 0x80) return;                  /* still too far: wait a frame */
        a = 0xFD;                              /* jumped too late: will miss the edge */
    }
    g->ch.x = pop_add_char_x(&g->ch, u8(a + RJ_CHANGE));
noedge:
    g->clrU = pop_clrall(g);
    pop_g_jumpseq(g, POP_SQ_RUNJUMP);
}

void pop_pickitup(pop_game *g, uint8_t objid)
{
    if (g->ch.posn != 109) {                   /* crouch first, then pick up */
        uint8_t a = pop_getfwddist(g);
        if (g->rx != 2) g->ch.x = pop_add_char_x(&g->ch, a);
        if (!NEG(g->ch.face)) g->ch.x = pop_add_char_x(&g->ch, 0xFE);
        pop_docrouch(g);
        return;
    }
    if (objid == POP_SWORD) {
        pop_removeobj(g, 0xFF);
        pop_g_jumpseq(g, POP_SQ_PICKUPSWORD);
    } else {
        pop_removeobj(g, pop_bluespec(g, g->ry) >> 5);   /* potion # */
        pop_g_jumpseq(g, POP_SQ_DRINKPOTION);
    }
}

uint8_t pop_trypickup(pop_game *g)
{
    uint8_t a = pop_getunderft(g);
    if (a == POP_FLASK || a == POP_SWORD) {
        if (pop_cmpspace(pop_getbehind(g)) == 0) return 0;
        g->ch.x = pop_add_char_x(&g->ch, 0xF2);          /* one block back */
        pop_rereadblocks(g);
    }
    a = pop_getinfront(g);
    if (a == POP_FLASK || a == POP_SWORD) {
        pop_pickitup(g, a);
        return 1;
    }
    return 0;
}

/* --- handlers --------------------------------------------------------------------------- */

static void stairs(pop_game *g)                /* Stairs, CTRL.S 1249-1259 */
{
    g->ch.x = u8(pop_getblockej(g, g->tempblockx) + 10);
    g->ch.face = POP_FACE_LEFT;
    pop_g_jumpseq(g, POP_SQ_CLIMBSTAIRS);
}

static void standing_up(pop_game *g)
{
    /* in front of open stairs? */
    if (pop_getunderft(g) == POP_EXIT || pop_getbehind(g) == POP_EXIT
        || pop_getinfront(g) == POP_EXIT) {
        if ((pop_bluespec(g, g->ry) >> 2) >= STAIR_THRES) {
            stairs(g);
            return;
        }
    }
    if (NEG(g->JSTKX)) pop_dostandjump(g);
    else pop_dojumpup(g);                      /* straight up: grab a ledge if you can */
}

static void standing_down(pop_game *g)
{
    g->clrD = 1;
    /* facing an edge: "down" means step off */
    if (pop_cmpspace(pop_getinfront(g)) == 0 && pop_getdist(g) < STEP_OFF_FWD) {
        g->ch.x = pop_add_char_x(&g->ch, 5);
        pop_rereadblocks(g);
        return;
    }
    /* back to an edge: climb down and hang from the ledge */
    if (pop_cmpspace(pop_getbehind(g)) == 0 && pop_getdist(g) >= STEP_OFF_BACK) {
        g->blockid = pop_getbehind(g);
        if (pop_checkledge(g, pop_getunderft(g)) != 0) {
            if (NEG(g->ch.face) && pop_getunderft(g) == POP_GATE
                && (pop_bluespec(g, g->ry) >> 2) < GCLIMB_THRES) {
                goto crouch;
            }
            g->ch.x = pop_add_char_x(&g->ch, u8(pop_getdist(g) - 9));
            pop_g_jumpseq(g, POP_SQ_CLIMBDOWN);
            return;
        }
    }
crouch:
    pop_docrouch(g);
}

void pop_standing(pop_game *g)
{
    /* fresh button click: pick up an object? */
    if (NEG(g->clrbtn) && NEG(g->btn)) {
        if (pop_trypickup(g) != 0) return;
    }
    if (g->ch.id != 0) {                       /* shadow: down & fwd to go en garde */
        if (NEG(g->clrD) && NEG(g->clrF)) {
            pop_doengarde(g);
            return;
        }
    } else if (g->gotsword != 0) {             /* kid: go en garde if an opponent is near */
        uint8_t a;
        if (g->offguard != 0 && !NEG(g->btn)) goto btnup;   /* off guard: button draws */
        if (g->EnemyAlert < 2) goto safe;
        a = pop_getopdist(g);
        if (a < SWORD_THRES_N && a >= SWORD_THRES) goto safe;
        g->heroic = 1;                         /* :danger */
        if (a >= 0xFA) {                       /* behind you */
            pop_doturn(g);
            return;
        }
        if (g->op.id == 1) {                   /* let the shadow land */
            if (g->op.action == 3) goto safe;
            if (g->op.posn >= 107 && g->op.posn < 118) goto safe;
        }
        pop_doengarde(g);
        return;
safe:
        g->offguard = 0;
    }
    if (NEG(g->btn)) {                         /* standing, button down */
        if (NEG(g->clrB)) pop_doturn(g);
        else if (NEG(g->clrU)) standing_up(g);
        else if (NEG(g->clrD)) standing_down(g);
        else if (NEG(g->JSTKX) && NEG(g->clrF)) pop_dostepfwd(g);
        return;
    }
btnup:
    if (NEG(g->clrF)) pop_dostartrun(g);
    else if (NEG(g->clrB)) pop_doturn(g);
    else if (NEG(g->clrU)) standing_up(g);
    else if (NEG(g->clrD)) standing_down(g);
    else if (NEG(g->JSTKX)) pop_dostartrun(g);
}

void pop_crouching(pop_game *g)
{
    if (NEG(g->clrbtn)) {
        if (pop_trypickup(g) != 0) return;
    }
    if (g->JSTKY != 1) {                       /* no longer holding down */
        pop_g_jumpseq(g, POP_SQ_STANDUP);
        return;
    }
    if (NEG(g->clrF)) {
        g->clrF = 1;
        pop_g_jumpseq(g, POP_SQ_CRAWL);
    }
}

void pop_starting(pop_game *g)
{
    if (NEG(g->JSTKY) && NEG(g->JSTKX)) pop_dostandjump(g);
}

void pop_stjumpup(pop_game *g)
{
    if (NEG(g->JSTKX) || NEG(g->clrF)) pop_dostandjump(g);
}

void pop_turning(pop_game *g)
{
    if (NEG(g->btn) || !NEG(g->JSTKX) || NEG(g->JSTKY)) return;
    pop_g_jumpseq(g, POP_SQ_TURNRUN);          /* still forward: turn into a run */
}

void pop_arunning(pop_game *g)
{
    if (g->JSTKX == 0) {                       /* centred: stop running */
        if (g->ch.posn != 7 && g->ch.posn != 11) return;
        g->clrF = pop_clrall(g);
        pop_g_jumpseq(g, POP_SQ_RUNSTOP);
        return;
    }
    if (!NEG(g->JSTKX)) {                      /* back: turn around */
        g->clrB = pop_clrall(g);
        pop_g_jumpseq(g, POP_SQ_RUNTURN);
        return;
    }
    if (NEG(g->JSTKY)) {                       /* up: running jump */
        if (NEG(g->clrU)) pop_dorunjump(g);
        return;
    }
    if (NEG(g->clrD)) {                        /* down: dive and roll */
        g->clrD = 1;
        pop_g_jumpseq(g, POP_SQ_RDIVEROLL);
    }
}

void pop_hanging(pop_game *g)
{
    uint8_t a;
    if (g->stunned == 0 && NEG(g->JSTKY)) {    /* :climbup */
        g->clrU = pop_clrall(g);
        g->clrbtn = 1;
        a = pop_getabove(g);
        if (a == POP_MIRROR || a == POP_SLICER) {
            if (g->ch.face == 0) goto fail;    /* can only mount a mirror facing left */
        } else if (a == POP_GATE) {
            /* can only mount a closed gate facing right */
            if (g->ch.face != 0 && (pop_bluespec(g, g->ry) >> 2) < GCLIMB_THRES) goto fail;
        }
        pop_g_jumpseq(g, POP_SQ_CLIMBUP);
        return;
fail:
        pop_g_jumpseq(g, POP_SQ_CLIMBFAIL);
        return;
    }
    if (NEG(g->btn)) {
        if (g->ch.action != 6) {               /* not yet hanging straight */
            a = pop_getunderft(g);
            if (a == POP_BLOCK
                || (g->ch.face == 0xFF && (a == POP_PANELWIF || a == POP_PANELWOF))) {
                pop_g_jumpseq(g, POP_SQ_HANGSTRAIGHT);
                return;
            }
        }
        if (pop_cmpspace(pop_getabove(g)) != 0) return;   /* ledge still there: keep swinging */
    }
    /* :drop */
    g->clrD = pop_clrall(g);
    if (pop_cmpspace(pop_getbehind(g)) == 0 && pop_cmpspace(pop_getunderft(g)) == 0) {
        pop_g_jumpseq(g, POP_SQ_HANGFALL);
        return;
    }
    a = pop_getunderft(g);
    if (a == POP_BLOCK || (NEG(g->ch.face) && (a == POP_PANELWOF || a == POP_PANELWIF))) {
        g->ch.x = pop_add_char_x(&g->ch, 0xF9);           /* sheer wall: -7 */
    }
    pop_g_jumpseq(g, POP_SQ_HANGDROP);
}

void pop_genctrl(pop_game *g)
{
    uint8_t x = g->ch.posn, a;
    if (!NEG(g->ch.life)) {                    /* dead: if standing, collapse */
        if (x == 15 || x == 166 || x == 158 || x == 171) pop_g_jumpseq(g, POP_SQ_DROPDEAD);
        return;
    }
    a = g->ch.action;
    if (a == 5 || a == 4) {                    /* mid-bump or falling */
        (void)pop_clrall(g);
        return;
    }
    if (g->ch.sword == 2) { pop_fightctrl(g); return; }
    if (g->ch.id >= 2) { pop_guardctrl(g); return; }

    if (x == 15) pop_standing(g);
    else if (x == 48) pop_turning(g);
    else if (x >= 50 && x < 53) pop_standing(g);      /* end of turn, crouch */
    else if (x < 4) pop_starting(g);
    else if (x >= 67 && x < 70) pop_stjumpup(g);
    else if (x < 15) pop_arunning(g);
    else if (x >= 87 && x < 100) pop_hanging(g);
    else if (x == 109) pop_crouching(g);
}

/* UserCtrl, CTRL.S 610-622: the handlers think in "forward/back"; for a
 * character facing right the stick is mirrored around the call. */
void pop_userctrl(pop_game *g)
{
    if (!NEG(g->ch.face)) {
        pop_facejstk(g);
        pop_genctrl(g);
        pop_facejstk(g);
    } else {
        pop_genctrl(g);
    }
}

/* PLAYERCTRL, CTRL.S 554-582.  Reading the input device is not ported: the
 * get_input hook stands for LoadSelect / getselect / clrjstk. */
void pop_playerctrl(pop_game *g)
{
    if (NEG(g->ch.life) && g->KidStrength == 0) g->ch.life = 0;   /* out of strength: dead */
    if (g->stunned != 0) g->stunned--;
    if (g->level == 0) {                       /* the demo plays itself */
        if (g->hooks.demo_ctrl) g->hooks.demo_ctrl(g->hooks.ctx);
        pop_genctrl(g);
        return;
    }
    if (g->hooks.get_input) g->hooks.get_input(g->hooks.ctx);
    pop_userctrl(g);
    if (g->hooks.save_input) g->hooks.save_input(g->hooks.ctx);
}

/* --- floor, falling, landing --------------------------------------------------------------- */

static uint8_t floor_under(pop_game *g)        /* FloorY,x with x = CharBlockY + 1 */
{
    return pop_floory(g, u8(g->ch.block_y + 1));
}

uint8_t pop_insideblock(pop_game *g)
{
    uint8_t a;
    if (pop_getdist(g) < 8 && pop_getinfront(g) != POP_BLOCK) {   /* :bumpfwd */
        a = u8(pop_getdist(g) + 4);
    } else if (pop_getbehind(g) == POP_BLOCK) {                   /* bump 2 back */
        a = u8((u8(pop_getdist(g) + 14) ^ 0xFFu) + 8);
    } else {
        a = u8((pop_getdist(g) ^ 0xFFu) + 8);
    }
    g->ch.x = pop_add_char_x(&g->ch, a);       /* :reland */
    pop_rereadblocks(g);
    return pop_getunderft(g);
}

void pop_doimpale(pop_game *g)
{
    pop_jamspikes(g);

    g->ch.y = floor_under(g);                  /* align with the floor */
    g->ch.x = u8(pop_getblockej(g, g->tempblockx) + 10);
    g->ch.x = pop_add_char_x(&g->ch, 8);       /* centre on the spikes */
    g->ch.y_vel = 0;
    pop_g_addsound(g, POP_SND_IMPALED);
    (void)pop_decstr(g, 100);
    pop_g_jumpseq(g, POP_SQ_IMPALE);
    pop_g_animchar(g);
}

void pop_checkimpale(pop_game *g)
{
    uint8_t x = g->ch.posn;
    if (pop_rdblock(g, g->ch.scrn, g->ch.block_x, g->ch.block_y) != POP_SPIKES) return;
    if (x < 7) return;
    if (x < 15) {                              /* running: must be springing */
        if (pop_getspikes(g) < 2) return;
    } else if (x == 43 || x == 26) {           /* landing from a jump */
        if (pop_getspikes(g) == 0) return;
    } else {
        return;
    }
    pop_doimpale(g);
}

void pop_hitflr(pop_game *g)
{
    uint8_t a, seq;
    g->ch.y = floor_under(g);                  /* align with the floor */
    if (pop_getunderft(g) == POP_SPIKES) goto hitspikes;

    /* landed too close to an edge?  move back a little */
    if (pop_cmpspace(pop_getinfront(g)) == 0 && pop_getdist(g) < 4) {
        g->ch.x = pop_add_char_x(&g->ch, 0xFD);
    }
    addslicers(g);
    if (!NEG(g->ch.life)) goto hardland;       /* dead before he hits the ground */

    if (pop_getdist(g) >= 12 && pop_getbehind(g) == POP_SPIKES) goto hitspikes;
    if (pop_getunderft(g) != POP_SPIKES) goto notspikes;
hitspikes:
    if (pop_getspikes(g) != 0) {
        pop_doimpale(g);
        return;
    }
notspikes:
    a = g->ch.y_vel;
    if (a < OOF_VELOCITY) goto softland;
    if (a >= DEATH_VELOCITY) goto hardland;
    /* :medland */
    if (g->ch.id == 1) goto softland;          /* shadow lands easy */
    if (g->ch.id >= 2) goto hardland;          /* guards can't survive 2 stories */
    if (pop_decstr(g, 1) == 0) goto hdland1;
    pop_g_addsound(g, POP_SND_SPLAT);
    seq = POP_SQ_MEDLAND;
    goto doland;
hardland:
    (void)pop_decstr(g, 100);
hdland1:
    pop_g_addsound(g, POP_SND_SPLAT);
    seq = POP_SQ_HARDLAND;
    goto doland;
softland:
    if (g->ch.id >= 2 || g->ch.sword == 2) {   /* guard always lands en garde */
        g->ch.sword = 2;
        seq = POP_SQ_LANDENGARDE;
    } else {
        seq = POP_SQ_SOFTLAND;
    }
doland:
    pop_g_jumpseq(g, seq);
    pop_g_animchar(g);
    g->ch.y_vel = 0;
}

void pop_fallon(pop_game *g)
{
    if (!NEG(g->btn & g->ch.life)) return;     /* button down and alive? */
    if (g->ch.y_vel >= GRAB_SPEED) return;     /* falling too fast */
    if (u8(g->ch.y + GRAB_LEAD) < floor_under(g)) return;   /* not within range yet */

    /* is there a ledge within reach? */
    g->savekidx = g->ch.x;
    g->ch.x = pop_add_char_x(&g->ch, GRAB_REACH);
    pop_rereadblocks(g);
    g->blockid = pop_getabove(g);
    if (pop_checkledge(g, pop_getaboveinf(g)) == 0) {
        g->ch.x = g->savekidx;
        pop_rereadblocks(g);
        return;
    }
    g->ch.x = pop_add_char_x(&g->ch, pop_getdist(g));     /* align with the block */
    g->ch.y = floor_under(g);
    g->ch.y_vel = 0;
    pop_g_jumpseq(g, POP_SQ_FALLHANG);
    pop_g_animchar(g);
    g->stunned = STUN_TIME;
}

void pop_cdpatch(pop_game *g)
{
    if (g->rjumpflag == 44 && pop_getdist(g) < 6) {       /* running jump */
        pop_g_jumpseq(g, POP_SQ_PATCHFALL);
        pop_g_animchar(g);
        pop_rereadblocks(g);
        return;
    }
    g->ch.x = pop_add_char_x(&g->ch, 0xFF);    /* :patchX */
    pop_rereadblocks(g);
}

void pop_startfall(pop_game *g)
{
    uint8_t a, seq;
    g->rjumpflag = 0;
    g->ch.sword = 0;                           /* so you can grab on */
    g->ch.block_y = u8(g->ch.block_y + 1);     /* the floor just below your feet */
    addslicers(g);

    a = g->ch.posn;                            /* the frame about to be replaced */
    g->rjumpflag = a;
    if (a == 9) seq = POP_SQ_STEPFALL;
    else if (a == 13) seq = POP_SQ_STEPFALL2;
    else if (a == 26) seq = POP_SQ_JUMPFALL;
    else if (a == 44) seq = POP_SQ_RJUMPFALL;
    else if (a >= 81 && a < 86) {              /* :hdropfall */
        g->ch.x = pop_add_char_x(&g->ch, 5);
        pop_rereadblocks(g);
        seq = POP_SQ_STEPFALL2;
    } else if (a >= 150 && a < 180) {          /* from a fighting stance */
        if (g->ch.id < 2) {
            g->droppedout = 1;                 /* for the guard's benefit */
            seq = POP_SQ_FIGHTFALL;
        } else if (NEG(g->ch.x_vel)) {         /* guard stepped off backwards */
            seq = POP_SQ_EFIGHTFALL;
        } else {
            g->droppedout = 0;
            seq = POP_SQ_EFIGHTFALLFWD;
        }
    } else {
        seq = POP_SQ_STEPFALL;
    }
    pop_g_jumpseq(g, seq);
    pop_g_animchar(g);                         /* advance 1 frame into the fall */
    pop_rereadblocks(g);
    if (pop_cmpwall(g, pop_getunderft(g)) == 0) {
        (void)pop_insideblock(g);              /* "inside" a block: bump him outside */
        return;
    }
    if (pop_cmpwall(g, pop_getinfront(g)) != 0) return;
    pop_cdpatch(g);
}

void pop_onground(pop_game *g)
{
    uint8_t a;
    if ((g->Fcheck & FCHECKMARK) == 0) return; /* this frame needs no floor */
    a = pop_getunderft(g);
    if (a == POP_BLOCK) a = pop_insideblock(g);
    if (pop_cmpspace(a) != 0) return;

    /* Level 12: phantom bridge.  Floorboards are created on the fly. */
    if (g->level == 12 && NEG(g->mergetimer) && g->ch.block_y == 0
        && (g->ch.scrn == 2 || (g->ch.scrn == 13 && g->tempblockx >= 6))) {
        int carry;
        size_t i = (size_t)(g->blue - 1u) * 30u + g->ry;
        if (g->blue && i < 720) g->blueprint[POP_BLUETYPE + i] = POP_FLOOR;
        else g->unmodelled = 1;
        carry = pop_indexblock(g);
        pop_markwipe(g, 2, &carry);
        pop_markred(g, 2, &carry);
        g->ry = u8(g->ry + 1);
        pop_markwipe(g, 2, &carry);
        pop_markred(g, 2, &carry);
        return;
    }
    pop_startfall(g);                          /* no floor underfoot */
}

void pop_falling(pop_game *g)
{
    uint8_t a;
    if (g->ch.y < floor_under(g)) {            /* hasn't reached the floor yet */
        pop_fallon(g);
        return;
    }
    a = pop_getunderft(g);                     /* passing through the floor plane */
    if (a == POP_BLOCK) a = pop_insideblock(g);
    if (pop_cmpspace(a) != 0) {
        pop_hitflr(g);
        return;
    }
    g->ch.block_y = u8(g->ch.block_y + 1);     /* fall through */
}

void pop_checkfloor(pop_game *g)
{
    uint8_t a = g->ch.action, x = g->ch.posn;
    if (a == 6) return;                        /* hanging */
    if (a == 5) {                              /* bumped */
        if (x == 109 || x == 185) pop_onground(g);     /* crouched or dead */
        return;
    }
    if (a == 4) { pop_falling(g); return; }    /* freefall */
    if (a == 3) {
        if (x >= 102 && x < 106) pop_fallon(g);
        return;
    }
    if (a == 2) return;                        /* hanging */
    pop_onground(g);                           /* 7, 0 or 1: on the ground */
}
