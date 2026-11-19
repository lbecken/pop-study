/* test_seq.c - unit tests for pop_seq.c on synthetic tables.
 *
 * New code; the tables below were written for these tests and contain no
 * original data.
 */

#include <string.h>

#include "pop_seq.h"
#include "pop_test.h"

#define ORG 0x3000u

/* Three sequences behind a 3-entry pointer table.
 *
 *   3000  dw walk, idle, fall
 *   3006  idle:  act 0 / 15 / goto idle
 *   300C  walk:  act 1 / 1 / chx 3
 *   3011  cyc:   2 / chx -1 / tap 1 / 3 / chx 2 / chy -4 / goto cyc
 *   301E  fall:  setfall -1,2 / ifwtless idle / aboutface / 7 / goto idle
 */
static const uint8_t TABLE[] = {
    0x0C, 0x30, 0x06, 0x30, 0x1E, 0x30,
    /* 3006 */ 0xF9, 0x00, 0x0F, 0xFF, 0x06, 0x30,
    /* 300C */ 0xF9, 0x01, 0x01, 0xFB, 0x03,
    /* 3011 */ 0x02, 0xFB, 0xFF, 0xF2, 0x01, 0x03, 0xFB, 0x02, 0xFA, 0xFC,
               0xFF, 0x11, 0x30,
    /* 301E */ 0xF8, 0xFF, 0x02, 0xF7, 0x06, 0x30, 0xFE, 0x07, 0xFF, 0x06, 0x30,
};
static const pop_seqtab TAB = { TABLE, sizeof TABLE, ORG };

/* --- hook recorder ----------------------------------------------------------- */

typedef struct {
    char log[64];       /* one letter per call, in call order */
    int  n;
    uint8_t sounds[8];
    int  n_sounds;
} recorder;

static void rec(recorder *r, char ch) { if (r->n < 63) r->log[r->n++] = ch; }
static void on_sound(void *ctx, uint8_t s)
{
    recorder *r = ctx;
    rec(r, 's');
    if (r->n_sounds < 8) r->sounds[r->n_sounds++] = s;
}
static void on_slicers(void *ctx) { rec(ctx, 'c'); }
static void on_upstairs(void *ctx) { rec(ctx, 'u'); }
static void on_potion(void *ctx) { rec(ctx, 'p'); }

static pop_anim_hooks hooks_for(recorder *r)
{
    pop_anim_hooks h;
    memset(r, 0, sizeof *r);
    h.ctx = r;
    h.add_sound = on_sound;
    h.add_slicers = on_slicers;
    h.gone_upstairs = on_upstairs;
    h.potion_effect = on_potion;
    return h;
}

static pop_char kid_at(uint8_t x, uint8_t face)
{
    pop_char c;
    memset(&c, 0, sizeof c);
    c.x = x;
    c.y = 181;
    c.face = face;
    c.block_y = 2;
    c.life = 0xFF;
    return c;
}

/* --- ADDCHARX ---------------------------------------------------------------- */

static void test_add_char_x(void)
{
    pop_char c = kid_at(100, POP_FACE_RIGHT);
    CHECK_EQ(pop_add_char_x(&c, 5), 105);
    CHECK_EQ(pop_add_char_x(&c, 0xFD), 97);          /* -3 */
    c.face = POP_FACE_LEFT;
    CHECK_EQ(pop_add_char_x(&c, 5), 95);
    CHECK_EQ(pop_add_char_x(&c, 0xFD), 103);         /* -(-3) */
    CHECK_EQ(pop_add_char_x(&c, 0), 100);
    CHECK_EQ(pop_add_char_x(&c, 0x80), 228);         /* -(-128) is still $80 */

    c.x = 2;                                          /* wraparound, no clamping */
    CHECK_EQ(pop_add_char_x(&c, 5), 253);
    c.x = 254;
    c.face = POP_FACE_RIGHT;
    CHECK_EQ(pop_add_char_x(&c, 5), 3);

    c.face = 0x80;                                    /* only bit 7 is tested */
    c.x = 100;
    CHECK_EQ(pop_add_char_x(&c, 5), 95);
    c.face = 0x7F;
    CHECK_EQ(pop_add_char_x(&c, 5), 105);
    CHECK_EQ(c.x, 100);                               /* input is not modified */
}

/* --- JUMPSEQ / GETSEQ ---------------------------------------------------------- */

static void test_jumpseq(void)
{
    pop_char c = kid_at(100, POP_FACE_LEFT);
    CHECK_EQ(pop_jumpseq(&TAB, &c, 1), POP_SEQ_OK);
    CHECK_EQ(c.seq, 0x300C);
    CHECK_EQ(pop_jumpseq(&TAB, &c, 2), POP_SEQ_OK);
    CHECK_EQ(c.seq, 0x3006);
    CHECK_EQ(pop_jumpseq(&TAB, &c, 3), POP_SEQ_OK);
    CHECK_EQ(c.seq, 0x301E);
    CHECK_EQ(c.posn, 0);                              /* frame is not changed */

    /* 8-bit index: (129-1)*2 = 256 -> 0, so 129 aliases sequence 1 */
    CHECK_EQ(pop_jumpseq(&TAB, &c, 129), POP_SEQ_OK);
    CHECK_EQ(c.seq, 0x300C);
    /* (0-1)*2 = $FE: far outside this small table */
    c.seq = 0x1234;
    CHECK_EQ(pop_jumpseq(&TAB, &c, 0), POP_SEQ_ERR_BOUNDS);
    CHECK_EQ(c.seq, 0x1234);                          /* unchanged on error */
}

static void test_getseq(void)
{
    pop_char c = kid_at(100, POP_FACE_LEFT);
    uint8_t b = 0;
    c.seq = 0x3006;
    CHECK_EQ(pop_getseq(&TAB, &c, &b), POP_SEQ_OK);
    CHECK_EQ(b, 0xF9);
    CHECK_EQ(c.seq, 0x3007);

    c.seq = (uint16_t)(ORG + sizeof TABLE - 1);       /* last byte is readable */
    CHECK_EQ(pop_getseq(&TAB, &c, &b), POP_SEQ_OK);
    CHECK_EQ(b, 0x30);
    CHECK_EQ(pop_getseq(&TAB, &c, &b), POP_SEQ_ERR_BOUNDS);
    c.seq = 0x2FFF;                                   /* below the origin */
    CHECK_EQ(pop_getseq(&TAB, &c, &b), POP_SEQ_ERR_BOUNDS);
    CHECK_EQ(c.seq, 0x2FFF);
}

/* --- ANIMCHAR ------------------------------------------------------------------ */

static void test_tick_semantics(void)
{
    /* Operands written after a frame number run at the start of the next
     * call: chx 3 belongs to the tick that shows frame 2. */
    recorder r;
    pop_anim_hooks h = hooks_for(&r);
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(100, POP_FACE_RIGHT);
    static const uint8_t frames[] = { 1, 2, 3, 2, 3, 2 };
    static const uint8_t xs[]     = { 100, 103, 102, 104, 103, 105 };
    static const uint8_t ys[]     = { 181, 181, 181, 177, 177, 173 };
    int i;

    CHECK_EQ(pop_jumpseq(&TAB, &c, 1), POP_SEQ_OK);
    for (i = 0; i < 6; i++) {
        CHECK_EQ(pop_animchar(&TAB, &c, &g, &h), POP_SEQ_OK);
        CHECK_EQ(c.posn, frames[i]);
        CHECK_EQ(c.x, xs[i]);
        CHECK_EQ(c.y, ys[i]);
    }
    CHECK_EQ(c.action, 1);
    CHECK_EQ(g.alertguard, 1);
    CHECK_EQ(r.n_sounds, 2);                          /* frame 3 shown twice */
    CHECK_EQ(r.sounds[0], POP_SND_FOOTSTEP);
    CHECK_EQ(c.seq, 0x3012);                          /* just past the frame byte */
}

static void test_facing_left_mirrors_chx(void)
{
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(100, POP_FACE_LEFT);
    static const uint8_t xs[] = { 100, 97, 98, 96 };
    int i;
    CHECK_EQ(pop_jumpseq(&TAB, &c, 1), POP_SEQ_OK);
    for (i = 0; i < 4; i++) {
        CHECK_EQ(pop_animchar(&TAB, &c, &g, NULL), POP_SEQ_OK);   /* NULL hooks allowed */
        CHECK_EQ(c.x, xs[i]);
    }
}

static void test_ifwtless_and_aboutface(void)
{
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(100, POP_FACE_LEFT);

    CHECK_EQ(pop_jumpseq(&TAB, &c, 3), POP_SEQ_OK);
    CHECK_EQ(pop_animchar(&TAB, &c, &g, NULL), POP_SEQ_OK);
    CHECK_EQ(c.posn, 7);                              /* not weightless: falls through */
    CHECK_EQ(c.face, POP_FACE_RIGHT);
    CHECK_EQ(c.x_vel, 0xFF);
    CHECK_EQ(pop_s8(c.x_vel), -1);
    CHECK_EQ(c.y_vel, 2);
    CHECK_EQ(pop_animchar(&TAB, &c, &g, NULL), POP_SEQ_OK);
    CHECK_EQ(c.posn, 15);
    CHECK_EQ(c.action, 0);

    c = kid_at(100, POP_FACE_LEFT);
    g.weightless = 1;                                 /* branch taken */
    CHECK_EQ(pop_jumpseq(&TAB, &c, 3), POP_SEQ_OK);
    CHECK_EQ(pop_animchar(&TAB, &c, &g, NULL), POP_SEQ_OK);
    CHECK_EQ(c.posn, 15);
    CHECK_EQ(c.face, POP_FACE_LEFT);                  /* aboutface was skipped */
    CHECK_EQ(c.y_vel, 2);                             /* setfall ran before the branch */
}

static void test_remaining_instructions_and_hook_order(void)
{
    /* up / down / jaru / die / tap 0 / tap 2 / tap 3 / effect 1 / effect 2 /
     * nextlevel / jard / frame 9 */
    static const uint8_t t[] = {
        0xFD, 0xFC, 0xFC, 0xF5, 0xF6, 0xF2, 0x00, 0xF2, 0x02, 0xF2, 0x03,
        0xF3, 0x01, 0xF3, 0x02, 0xF1, 0xF4, 0x09,
    };
    pop_seqtab tab = { t, sizeof t, 0x4000 };
    recorder r;
    pop_anim_hooks h = hooks_for(&r);
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(100, POP_FACE_LEFT);

    c.seq = 0x4000;
    c.block_y = 0;
    CHECK_EQ(pop_animchar(&tab, &c, &g, &h), POP_SEQ_OK);
    CHECK_EQ(c.posn, 9);
    CHECK_EQ(c.block_y, 1);                           /* 0 -1 +1 +1 */
    CHECK_EQ(g.jarabove, 0xFF);                       /* jaru then jard: last wins */
    CHECK_EQ(g.alertguard, 1);
    CHECK(strcmp(r.log, "cccspu") == 0);              /* slicers x3, sound, potion, upstairs */
    CHECK_EQ(r.n_sounds, 1);                          /* tap 0 and tap 3 are silent */
    CHECK_EQ(r.sounds[0], POP_SND_SMACKWALL);
    CHECK_EQ(c.seq, 0x4000 + sizeof t);
}

static void test_tap_selectors(void)
{
    static const uint8_t t[] = { 0xF2, 0x03, 0x05, 0xF2, 0x00, 0x06 };
    pop_seqtab tab = { t, sizeof t, 0x4000 };
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(100, POP_FACE_LEFT);
    c.seq = 0x4000;
    CHECK_EQ(pop_animchar(&tab, &c, &g, NULL), POP_SEQ_OK);
    CHECK_EQ(g.alertguard, 0);                        /* selector 3 does nothing */
    CHECK_EQ(pop_animchar(&tab, &c, &g, NULL), POP_SEQ_OK);
    CHECK_EQ(g.alertguard, 1);                        /* selector 0 alerts without sound */
}

static void test_block_y_and_y_wrap(void)
{
    static const uint8_t t[] = { 0xFD, 0xFA, 0x7F, 0xFA, 0x7F, 0xFA, 0x7F, 0x01 };
    pop_seqtab tab = { t, sizeof t, 0x4000 };
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(100, POP_FACE_LEFT);
    c.seq = 0x4000;
    c.block_y = 0;
    c.y = 10;
    CHECK_EQ(pop_animchar(&tab, &c, &g, NULL), POP_SEQ_OK);
    CHECK_EQ(c.block_y, 0xFF);                        /* row -1, stored as $FF */
    CHECK_EQ(c.y, (10 + 3 * 127) & 0xFF);
}

static void test_errors(void)
{
    static const uint8_t spin[] = { 0xFF, 0x00, 0x40 };            /* goto self */
    static const uint8_t cut[] = { 0xFB };                         /* chx, operand missing */
    static const uint8_t away[] = { 0xFF, 0x00, 0x90 };            /* goto outside */
    pop_seqtab tab = { spin, sizeof spin, 0x4000 };
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(100, POP_FACE_LEFT);

    c.seq = 0x4000;
    c.posn = 15;
    CHECK_EQ(pop_animchar(&tab, &c, &g, NULL), POP_SEQ_ERR_NO_FRAME);
    CHECK_EQ(c.posn, 15);                             /* frame untouched */

    tab.data = cut;
    tab.size = sizeof cut;
    c.seq = 0x4000;
    CHECK_EQ(pop_animchar(&tab, &c, &g, NULL), POP_SEQ_ERR_BOUNDS);

    tab.data = away;
    tab.size = sizeof away;
    c.seq = 0x4000;
    CHECK_EQ(pop_animchar(&tab, &c, &g, NULL), POP_SEQ_ERR_BOUNDS);
    CHECK_EQ(c.seq, 0x9000);                          /* the goto itself succeeded */
}

static void test_every_non_instruction_byte_is_a_frame(void)
{
    uint8_t t[1];
    pop_seqtab tab = { t, 1, 0x4000 };
    pop_anim_globals g = { 0, 0, 0 };
    unsigned v;
    for (v = 0; v <= 0xF0; v++) {                     /* $00-$F0 are frames */
        pop_char c = kid_at(100, POP_FACE_LEFT);
        t[0] = (uint8_t)v;
        c.seq = 0x4000;
        c.posn = (uint8_t)(v ^ 0x55u);
        CHECK_EQ(pop_animchar(&tab, &c, &g, NULL), POP_SEQ_OK);
        CHECK_EQ(c.posn, v);
    }
}

int main(void)
{
    RUN(test_add_char_x);
    RUN(test_jumpseq);
    RUN(test_getseq);
    RUN(test_tick_semantics);
    RUN(test_facing_left_mirrors_chx);
    RUN(test_ifwtless_and_aboutface);
    RUN(test_remaining_instructions_and_hook_order);
    RUN(test_tap_selectors);
    RUN(test_block_y_and_y_wrap);
    RUN(test_errors);
    RUN(test_every_non_instruction_byte_is_a_frame);
    return pop_test_summary();
}
