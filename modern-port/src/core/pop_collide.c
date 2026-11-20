/* pop_collide.c - see pop_collide.h.  New code; ports of original routines.
 *
 * The code follows the original branch by branch, with the original labels
 * in comments; docs/port/collision.md explains the scheme.
 */

#include "pop_collide.h"

#include "pop_control.h"
#include "pop_mover.h"
#include "pop_world.h"

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))

/* Distance from either edge of a block to the barrier in it, by barrier
 * code (COLL.S 55-56): 0 clear, 1 panel/gate, 2 (flask, unused: CMPBARR no
 * longer returns it), 3 mirror/slicer, 4 block. */
static const uint8_t bar_l[5] = { 0, 12, 2, 0, 0 };
static const uint8_t bar_r[5] = { 0, 0, 9, 11, 0 };

enum { SCRN_WIDTH = 140, GATE_MARGIN = 6, OOF_VELOCITY = 22 };

/* The eight buffers are 16 bytes apart in memory and ten are used; an index
 * of 10-15 lands in the unused part.  SNabove is the last and only ten
 * bytes long: beyond it are other variables. */
static uint8_t *cd_at(pop_game *g, unsigned buf, uint8_t x)
{
    static uint8_t sink;
    if (x >= 16) {
        g->unmodelled = 1;
        sink = 0;
        return &sink;
    }
    return &g->CD[buf][x];
}

static uint8_t *sn_at(pop_game *g, unsigned buf, uint8_t x)
{
    static uint8_t sink;
    if (x >= 16 || (buf == POP_CD_ABOVE && x >= 10)) {
        g->unmodelled = 1;
        sink = 0;
        return &sink;
    }
    return &g->SN[buf][x];
}

int pop_gatebarr(pop_game *g)
{
    return u8((pop_bluespec(g, g->ry) >> 2) + GATE_MARGIN) >= g->imheight;
}

/* getleftbar / getrightbar: x of the barrier's edge in block (x, y) of a
 * screen, given blockedge for that block.  *is_barrier = the carry. */
static uint8_t getleftbar(pop_game *g, uint8_t scrn, uint8_t x, uint8_t y, int *is_barrier)
{
    uint8_t code = pop_cmpbarr(pop_rdblock(g, scrn, x, y));
    if (code == 0) {
        *is_barrier = 0;
        return 255;
    }
    *is_barrier = 1;
    return u8(g->blockedge + bar_l[code]);
}

static uint8_t getrightbar(pop_game *g, uint8_t scrn, uint8_t x, uint8_t y, int *is_barrier)
{
    uint8_t code = pop_cmpbarr(pop_rdblock(g, scrn, x, y));
    if (code == 0) {
        *is_barrier = 0;
        return 0;
    }
    *is_barrier = 1;
    return u8(g->blockedge + 13 - bar_r[code]);
}

/* --- CHECKBARR ---------------------------------------------------------------------- */

static void init_cd_bufs(pop_game *g)
{
    uint8_t this_y = g->BlockYthis, last_y = g->BlockYlast;
    unsigned src, x;
    if (this_y == last_y || u8(this_y + 3) == last_y || u8(this_y - 3) == last_y) {
        src = POP_CD_THIS;                     /* same row (or the same row of another screen) */
    } else if (u8(this_y + 1) == last_y || u8(this_y - 2) == last_y) {
        src = POP_CD_ABOVE;                    /* he moved down a row... */
    } else {
        src = POP_CD_BELOW;
    }
    for (x = 0; x < 10; x++) {
        g->SN[POP_CD_LAST][x] = g->SN[src][x];
        g->CD[POP_CD_LAST][x] = g->CD[src][x];
        g->SN[POP_CD_THIS][x] = 0xFF;
        g->SN[POP_CD_ABOVE][x] = 0xFF;
        g->SN[POP_CD_BELOW][x] = 0xFF;
    }
}

static void get_cdata(pop_game *g, unsigned buf)
{
    uint8_t x = g->begrange;
    int barrier;
    g->blockedge = u8(pop_getblockej(g, g->begrange) + POP_ANGLE);
    do {
        uint8_t a, v;
        g->bufindex = x;
        /* left edge of the barrier against the right edge of the character */
        a = getleftbar(g, g->ch.scrn, g->bufindex, g->blocky, &barrier);
        v = (a < g->CDRightEj) ? 0x0F : 0x00;
        /* right edge of the barrier against his left edge */
        a = getrightbar(g, g->ch.scrn, g->bufindex, g->blocky, &barrier);
        if (a > g->CDLeftEj) v |= 0xF0;
        *cd_at(g, buf, g->tempblockx) = v;
        *sn_at(g, buf, g->tempblockx) = g->tempscrn;
        g->blockedge = u8(g->blockedge + 14);
        x = u8(g->bufindex + 1);
    } while (x != g->endrange);
}

void pop_checkbarr(pop_game *g)
{
    uint8_t a;
    int x;
    g->collideL = g->collideR = 0xFF;
    if (g->ch.action == 7) return;             /* turning: collision-proof */

    g->BlockYthis = g->ch.block_y;
    init_cd_bufs(g);
    g->BlockYlast = g->BlockYthis;

    a = u8(pop_getblockxp(g, g->CDRightEj) + 2);
    if (a >= 11) a = 11;
    g->endrange = a;                           /* last block in range + 1 */
    g->begrange = u8(pop_getblockxp(g, g->CDLeftEj) - 1);

    g->blocky = g->BlockYthis;
    get_cdata(g, POP_CD_THIS);
    g->blocky = u8(g->BlockYthis + 1);
    get_cdata(g, POP_CD_BELOW);
    g->blocky = u8(g->BlockYthis - 1);
    get_cdata(g, POP_CD_ABOVE);

    /* a nybble that was 0 last frame and is not now is a collision */
    for (x = 9; x >= 0; x--) {
        uint8_t now = g->CD[POP_CD_THIS][x], before = g->CD[POP_CD_LAST][x];
        if (NEG(g->SN[POP_CD_THIS][x])) continue;            /* no data this frame */
        if (g->SN[POP_CD_THIS][x] != g->SN[POP_CD_LAST][x]) continue;
        if ((before & 0x0F) == 0 && (now & 0x0F) != 0) g->collideL = u8(x);
        if ((before & 0xF0) == 0 && (now & 0xF0) != 0) g->collideR = u8(x);
    }
}

/* --- COLLISIONS ----------------------------------------------------------------------- */

static uint8_t adjust_scrn(const pop_game *g, uint8_t a)
{
    uint8_t s = g->tempscrn;
    if (s == g->VisScrn) return a;
    if (s == g->scrnLeft || s == g->scrnBelowL) return u8(a - SCRN_WIDTH);
    if (s == g->scrnRight || s == g->scrnBelowR) return u8(a + SCRN_WIDTH);
    return a;
}

int pop_checkcoll(pop_game *g, uint8_t objid)
{
    if (objid == POP_FLASK) return 0;          /* not really a barrier */
    if (objid == POP_GATE) {
        if (pop_gatebarr(g)) return 0;         /* open enough */
    } else if (objid == POP_SLICER) {
        if (pop_bluespec(g, g->ry) != POP_SLICER_EXT) return 0;   /* blades open */
    } else if (objid == POP_MIRROR) {
        /* the kid passes through the mirror from the right only in a running jump */
        if (g->ch.id == 0 && g->ch.posn >= 39 && g->ch.posn < 44 && NEG(g->ch.face)) {
            pop_smashmirror(g);
            g->createshad = 0xFF;
            return 0;
        }
    }
    /* :c1 */
    g->blockedge = u8(adjust_scrn(g, pop_getblockej(g, g->tempblockx)) + POP_ANGLE);
    return 1;
}

/* checkcoll1: CHECKCOLL for column x of the collision data. */
static int checkcoll1(pop_game *g, uint8_t x)
{
    uint8_t a = g->ch.block_y;
    g->tempblockx = x;
    if (NEG(a)) a = u8(a + 3);
    else if (a >= 3) a = u8(a - 3);
    g->tempblocky = a;
    g->tempscrn = *sn_at(g, POP_CD_THIS, x);
    return pop_checkcoll(g, pop_rdblock1(g));
}

static void bump_sound(pop_game *g)            /* BumpSound */
{
    g->ag.alertguard = 1;
    pop_g_addsound(g, POP_SND_SMACKWALL);
}

static void air_bump(pop_game *g)              /* AirBump: no floor at the barrier */
{
    g->ch.x = pop_add_char_x(&g->ch, 0xFC);
    if (g->ch.action == 4) {                   /* already falling: just rebound */
        g->ch.x_vel = 0;
    } else {
        pop_g_jumpseq(g, POP_SQ_BUMPFALL);
        pop_g_animchar(g);
    }
    bump_sound(g);
}

static void ground_bump(pop_game *g, uint8_t coll_face)
{
    uint8_t x = g->ch.block_y, floor, posn;
    /* `lda FloorY+1,x`: the index does not wrap, so row -1 reads far beyond
     * the table */
    if (x == 0xFF) {
        g->unmodelled = 1;
        return;
    }
    floor = pop_floory(g, u8(x + 1));
    if (g->ch.sword != 2 && u8(floor - g->ch.y) >= 15) {   /* well above the floor */
        air_bump(g);
        return;
    }
    g->ch.y = floor;
    if (g->ch.y_vel >= OOF_VELOCITY) {         /* let checkfloor take care of it */
        g->ch.x = pop_add_char_x(&g->ch, 0xFB);
        return;
    }
    g->ch.y_vel = 0;
    if (g->ch.life == 0) return;               /* :deadbump */

    if (g->ch.sword == 2) {                    /* :CollideEng */
        if (coll_face != g->ch.face) {
            pop_g_jumpseq(g, POP_SQ_BUMPENGFWD);
            pop_g_animchar(g);
            bump_sound(g);
        } else {                               /* backing into the barrier */
            pop_g_jumpseq(g, POP_SQ_BUMPENGBACK);
            pop_g_animchar(g);
            g->ch.x = pop_add_char_x(&g->ch, 1);
        }
        return;
    }
    posn = g->ch.posn;                         /* the frame he was in decides how hard */
    if (posn == 24 || posn == 25 || (posn >= 40 && posn < 43) || (posn >= 102 && posn < 107)) {
        pop_g_jumpseq(g, POP_SQ_HARDBUMP);
        pop_g_animchar(g);
        bump_sound(g);
    } else {
        pop_g_jumpseq(g, POP_SQ_BUMP);
        bump_sound(g);
        pop_g_animchar(g);
    }
}

/* collide: In: a = distance to move the character so that he touches the
 * barrier; coll_face = $FF for the left edge of a barrier, 0 for the right. */
static void collide(pop_game *g, uint8_t a, uint8_t coll_face)
{
    uint8_t id;
    if (!NEG(g->ch.life)) return;              /* dead: let him finish falling */
    if (g->ch.posn == 177) return;             /* impaled */
    g->ch.x = u8(g->ch.x + a);

    /* in mid-air or on the ground?  look at the floor on his side of the barrier */
    id = pop_rdblock1(g);
    if (NEG(coll_face)) {
        if (id == POP_BLOCK) {                 /* no floor in a block: use the one before it */
            g->tempblockx = u8(g->tempblockx - 1);
            id = pop_rdblock1(g);
        }
    } else if (id == POP_PANELWOF || id == POP_PANELWIF || id == POP_BLOCK) {
        g->tempblockx = u8(g->tempblockx + 1);
        if (g->tempscrn == 0 && g->tempblockx == 10) {   /* screen 0 block 10 = CharScrn block 0 */
            g->tempscrn = g->ch.scrn;
            g->tempblockx = 0;
        }
        id = pop_rdblock1(g);
    }
    if (pop_cmpspace(id) != 0) ground_bump(g, coll_face);
    else air_bump(g);
}

void pop_collisions(pop_game *g)
{
    int barrier;
    uint8_t a;
    if (g->AMtimer != 0) {                     /* "antimatter": walls do not stop him */
        if (g->hooks.speaker_click) g->hooks.speaker_click(g->hooks.ctx);
        g->AMtimer--;
        return;
    }
    /* cases where he is allowed through a barrier */
    if (g->ch.action == 2 || g->ch.action == 6) return;      /* hanging */
    if (g->ch.posn >= 135 && g->ch.posn < 149) return;       /* climbing */

    if (!NEG(g->collideL)) {                   /* leftcoll */
        g->collX = g->collideL;
        if (g->ch.sword != 2 && g->ch.face != 0) return;     /* must be facing it */
        if (!checkcoll1(g, g->collX)) return;
        a = getleftbar(g, g->tempscrn, g->tempblockx, g->tempblocky, &barrier);
        collide(g, u8(a - g->CDRightEj), 0xFF);
        return;
    }
    if (!NEG(g->collideR)) {                   /* rightcoll */
        g->collX = g->collideR;
        if (g->ch.sword != 2 && !NEG(g->ch.face)) return;
        if (!checkcoll1(g, g->collX)) return;
        a = getrightbar(g, g->tempscrn, g->tempblockx, g->tempblocky, &barrier);
        collide(g, u8(a - g->CDLeftEj), 0x00);
    }
}

/* --- gate, slicers ---------------------------------------------------------------------- */

void pop_checkgate(pop_game *g)
{
    uint8_t x;
    if (g->ch.action != 7 && g->ch.posn != 15 && (g->ch.posn < 108 || g->ch.posn >= 111)) {
        return;                                /* only turning, standing or crouching */
    }
    if (pop_getunderft(g) != POP_GATE) {
        g->tempblockx = u8(g->tempblockx - 1);
        if (pop_rdblock1(g) != POP_GATE) return;
    }
    x = g->tempblockx;
    /* overlapping the gate's column both this frame and last? */
    if ((*cd_at(g, POP_CD_THIS, x) & *cd_at(g, POP_CD_LAST, x)) != 0xFF) return;
    if (pop_gatebarr(g)) return;               /* still open enough */
    bump_sound(g);

    g->collX = g->tempblockx;                  /* knock him to the side he is more on */
    (void)pop_getunderft(g);
    g->ch.x = u8(g->ch.x + (g->tempblockx > g->collX ? 5 : 0xFB));
}

/* ]slice: In: rdblock results for the slicer. */
static void slice(pop_game *g)
{
    pop_set_bluespec(g, g->ry, pop_bluespec(g, g->ry) | 0x80u);   /* blood on the blades */
    if (g->ch.posn == 178) return;             /* already cut in half */
    g->ch.x = u8(pop_getblockej(g, g->tempblockx) + 7);
    g->ch.x = pop_add_char_x(&g->ch, 8);       /* align with the slicer */
    g->ch.y = pop_floory(g, u8(g->ch.block_y + 1));
    (void)pop_decstr(g, 100);
    pop_g_addsound(g, POP_SND_SPLAT);
    pop_g_jumpseq(g, POP_SQ_HALVE);
    pop_g_animchar(g);
}

void pop_checkslice(pop_game *g)
{
    uint8_t x = 9;
    g->tempblocky = g->ch.block_y;
    do {
        g->tempblockx = x;
        if (*cd_at(g, POP_CD_THIS, x) == 0xFF) {             /* overlapping a barrier */
            g->tempscrn = *sn_at(g, POP_CD_THIS, x);
            if (pop_rdblock1(g) == POP_SLICER
                && (pop_bluespec(g, g->ry) & 0x7Fu) == POP_SLICER_EXT) {
                slice(g);
                return;
            }
        }
        x = u8(g->tempblockx - 1);
    } while (!NEG(x));
}

static int slice_here(pop_game *g, uint8_t id)                /* :slice? */
{
    int barrier;
    if (id != POP_SLICER) return 0;
    if ((pop_bluespec(g, g->ry) & 0x7Fu) != POP_SLICER_EXT) return 0;
    g->blockedge = u8(pop_getblockej(g, g->tempblockx) + POP_ANGLE);
    if (getleftbar(g, g->tempscrn, g->tempblockx, g->tempblocky, &barrier) >= g->CDRightEj) {
        return 0;
    }
    if (getrightbar(g, g->tempscrn, g->tempblockx, g->tempblocky, &barrier) <= g->CDLeftEj) {
        return 0;
    }
    (void)pop_rdblock1(g);
    slice(g);
    return 1;
}

int pop_checkslice2(pop_game *g)
{
    if (slice_here(g, pop_getunderft(g))) return 1;
    g->tempblockx = u8(g->tempblockx + 1);
    return slice_here(g, pop_rdblock1(g));
}

/* --- opponents ------------------------------------------------------------------------------ */

/* DBarr2: distance to the barrier behind the character (negative = he is
 * inside it).  The facing test is the reverse of DBarr's. */
static uint8_t dbarr2(pop_game *g)
{
    uint8_t code = pop_cmpbarr(g->tempobjid);
    if (code == 0) return 0xFF;
    if (NEG(g->ch.face)) {                     /* :checkR his back is to the right */
        return u8(u8(g->blockedge + bar_l[code]) - g->CDRightEj);
    }
    return u8(g->CDLeftEj - u8(g->blockedge + 13 - bar_r[code]));
}

void pop_enemycoll(pop_game *g)
{
    uint8_t a;
    if (g->AMtimer != 0) return;
    if (g->ch.action != 1) return;             /* must be on the ground */
    if (!NEG(g->ch.life)) return;              /* and alive */
    if (g->ch.sword < 2) return;               /* and en garde */

    a = pop_getunderft(g);
    if (a == POP_BLOCK || a == POP_PANELWIF) goto hit;
    if (a == POP_GATE && !pop_gatebarr(g)) goto hit;
    if (NEG(g->ch.face)) return;               /* facing right: the block behind too */
    g->tempblockx = u8(g->tempblockx - 1);
    a = pop_rdblock1(g);
    if (a == POP_PANELWIF) goto hit;
    if (a != POP_GATE || pop_gatebarr(g)) return;
hit:
    /* backing into a barrier while en garde: put him right at its edge */
    pop_setupchar(g);
    pop_getedges(g);
    g->tempobjid = pop_rdblock(g, g->tempscrn, g->tempblockx, g->tempblocky);
    if (!pop_checkcoll(g, g->tempobjid)) return;
    a = dbarr2(g);
    if (!NEG(a)) return;
    g->ch.x = pop_add_char_x(&g->ch, u8(0 - a));
    pop_g_jumpseq(g, POP_SQ_BUMPENGBACK);
    pop_g_animchar(g);
    pop_rereadblocks(g);
}

/* --- leaving the level ------------------------------------------------------------------------ */

/* CUESONG, SPECIALK.S 1332-1335. */
void pop_cuesong(pop_game *g, uint8_t song, uint8_t within)
{
    g->SongCue = song;
    g->SongCount = within;
}

void pop_goneupstairs(pop_game *g)
{
    if (g->level != 13) {                      /* no music for level 13 */
        pop_cuesong(g, (g->level == 4) ? POP_SONG_SHADOW : POP_SONG_UPSTAIRS, 25);
    }
    g->NextLevel++;
}
