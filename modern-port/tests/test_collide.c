/* test_collide.c - collision routines on synthetic data.
 *
 * New code; uses the made-up state of pop_fixture.h.  Expected values were
 * worked out by hand from COLL.S (see pop_collide.h for the line ranges).
 * The same routines are compared with the original code on random states
 * (tests/fuzz_control.py) and inside the multi-frame kid trace
 * (tests/trace_kidstep.py) when the original source is available.
 *
 * Geometry used below: column c spans x = 58+14c .. 71+14c; barriers are
 * drawn 7 further right, so a solid block in column 5 occupies 135..148 and
 * a gate in column 5 only 147..148.  The fixture's image is 7 units wide:
 * a character at x facing right covers x-7..x, facing left x..x+7.
 */

#include <string.h>

#include "pop_collide.h"
#include "pop_fixture.h"
#include "pop_test.h"

/* The part of a frame that feeds collision detection. */
static void edges_and_barriers(void)
{
    pop_setupchar(&g);
    pop_rereadblocks(&g);
    pop_getedges(&g);
    pop_checkbarr(&g);
}

/* Kid in column 4 facing `face`, a `piece` in column 5; one frame to record
 * where he was, then he is moved to x and the barriers are checked again. */
static void walk_into(uint8_t piece, uint8_t spec, uint8_t face, uint8_t x)
{
    setup(4, face);
    put(1, 1, 5, piece, spec);
    edges_and_barriers();
    g.ch.x = x;
    edges_and_barriers();
}

static void test_edges(void)
{
    setup(4, POP_FACE_RIGHT);
    pop_setupchar(&g);
    pop_getedges(&g);
    CHECK_EQ(g.ch.x, 121);
    CHECK_EQ(g.CDLeftEj, 114);
    CHECK_EQ(g.CDRightEj, 121);
    CHECK_EQ(g.imheight, 40);
    setup(4, POP_FACE_LEFT);
    pop_setupchar(&g);
    pop_getedges(&g);
    CHECK_EQ(g.CDLeftEj, 121);
    CHECK_EQ(g.CDRightEj, 128);
}

static void test_checkbarr(void)
{
    setup(4, POP_FACE_RIGHT);
    put(1, 1, 5, POP_BLOCK, 0);
    edges_and_barriers();
    CHECK_EQ(g.collideL, 0xFF);                    /* first frame: nothing to compare with */
    CHECK_EQ(g.collideR, 0xFF);
    CHECK_EQ(g.begrange, 2);                       /* one column left of his left edge's (3) */
    CHECK_EQ(g.endrange, 6);                       /* two right of his right edge, exclusive */
    CHECK_EQ(g.SN[POP_CD_THIS][5], 1);
    CHECK_EQ(g.CD[POP_CD_THIS][5], 0xF0);          /* short of its right edge, not past its left */
    CHECK_EQ(g.CD[POP_CD_THIS][4], 0x00);          /* no barrier: its "edges" are 255 and 0 */
    CHECK_EQ(g.SN[POP_CD_THIS][7], 0xFF);          /* outside the range: no data */

    g.ch.x = 138;                                  /* 3 units into the block */
    edges_and_barriers();
    CHECK_EQ(g.CD[POP_CD_LAST][5], 0xF0);
    CHECK_EQ(g.CD[POP_CD_THIS][5], 0xFF);          /* overlapping */
    CHECK_EQ(g.collideL, 5);                       /* crossed its left edge this frame */
    CHECK_EQ(g.collideR, 0xFF);

    edges_and_barriers();                          /* still inside next frame: not a new collision */
    CHECK_EQ(g.collideL, 0xFF);

    setup(4, POP_FACE_RIGHT);                      /* turning (action 7) is collision-proof */
    put(1, 1, 5, POP_BLOCK, 0);
    edges_and_barriers();
    g.ch.x = 138;
    g.ch.action = 7;
    edges_and_barriers();
    CHECK_EQ(g.collideL, 0xFF);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_too_fast_to_notice(void)
{
    /* The range checked each frame reaches one column past his leading
     * edge.  A barrier further away than that has no "last frame" entry, so
     * arriving inside it in one step is not detected. */
    setup(3, POP_FACE_RIGHT);                      /* x = 107: range ends before column 5 */
    put(1, 1, 5, POP_BLOCK, 0);
    edges_and_barriers();
    CHECK_EQ(g.SN[POP_CD_THIS][5], 0xFF);
    g.ch.x = 138;
    edges_and_barriers();
    CHECK_EQ(g.CD[POP_CD_THIS][5], 0xFF);
    CHECK_EQ(g.collideL, 0xFF);
}

static void test_ground_bump(void)
{
    walk_into(POP_BLOCK, 0, POP_FACE_RIGHT, 138);
    pop_collisions(&g);
    CHECK_EQ(g.ch.x, 135);                         /* back to the wall's face */
    CHECK_EQ(g.ch.posn, POP_SQ_BUMP);              /* from a standing frame: soft */
    CHECK_EQ(g.sq.count, 1);
    CHECK_EQ(g.sq.items[0], POP_SND_SMACKWALL);
    CHECK_EQ(g.ag.alertguard, 1);
    CHECK_EQ(g.collX, 5);

    walk_into(POP_BLOCK, 0, POP_FACE_RIGHT, 138);
    g.ch.posn = 41;                                /* mid running jump: hard */
    pop_collisions(&g);
    CHECK_EQ(g.ch.posn, POP_SQ_HARDBUMP);
    CHECK_EQ(g.ch.x, 135);

    walk_into(POP_BLOCK, 0, POP_FACE_RIGHT, 138);
    g.ch.y_vel = 25;                               /* coming down fast: no bump sequence */
    pop_collisions(&g);
    CHECK_EQ(g.ch.x, 130);                         /* 135, then 5 back */
    CHECK_EQ(g.ch.posn, 15);
    CHECK_EQ(g.sq.count, 0);

    walk_into(POP_BLOCK, 0, POP_FACE_RIGHT, 138);
    g.ch.life = 0;                                 /* dead: left alone */
    pop_collisions(&g);
    CHECK_EQ(g.ch.x, 138);

    walk_into(POP_BLOCK, 0, POP_FACE_RIGHT, 138);
    g.ch.action = 2;                               /* hanging: passes */
    pop_collisions(&g);
    CHECK_EQ(g.ch.x, 138);

    walk_into(POP_BLOCK, 0, POP_FACE_RIGHT, 138);
    g.AMtimer = 2;                                 /* walls off: a click and a count */
    pop_collisions(&g);
    CHECK_EQ(g.ch.x, 138);
    CHECK_EQ(g.AMtimer, 1);
    CHECK(strcmp(hooklog, "k") == 0);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_facing_matters(void)
{
    /* Backing into a wall is not stopped: the left edge of a barrier only
     * stops a character facing right (COLL.S 467-476) ... */
    walk_into(POP_BLOCK, 0, POP_FACE_LEFT, 131);   /* covers 131..138 */
    CHECK_EQ(g.collideL, 5);
    pop_collisions(&g);
    CHECK_EQ(g.ch.x, 131);
    CHECK_EQ(g.sq.count, 0);

    /* ... unless his sword is drawn; then he is put back and steps away */
    walk_into(POP_BLOCK, 0, POP_FACE_LEFT, 131);
    g.ch.sword = 2;
    pop_collisions(&g);
    CHECK_EQ(g.ch.posn, POP_SQ_BUMPENGBACK);
    CHECK_EQ(g.ch.x, 127);                         /* 131 - 3, then 1 "forward" (left) */
    CHECK_EQ(g.sq.count, 0);                       /* no sound for this one */
}

static void test_air_bump(void)
{
    walk_into(POP_BLOCK, 0, POP_FACE_RIGHT, 138);
    put(1, 1, 4, POP_SPACE, 0);                    /* nothing to stand on before the wall */
    pop_collisions(&g);
    CHECK_EQ(g.ch.x, 131);                         /* 135, then 4 back */
    CHECK_EQ(g.ch.posn, POP_SQ_BUMPFALL);
    CHECK_EQ(g.sq.items[0], POP_SND_SMACKWALL);

    walk_into(POP_BLOCK, 0, POP_FACE_RIGHT, 138);
    put(1, 1, 4, POP_SPACE, 0);
    g.ch.action = 4;                               /* already falling: just stop moving */
    g.ch.x_vel = 3;
    pop_collisions(&g);
    CHECK_EQ(g.ch.x_vel, 0);
    CHECK_EQ(g.ch.posn, 15);
    CHECK_EQ(g.sq.count, 1);
}

static void test_checkcoll(void)
{
    static const uint8_t gate_spec[] = { 0, 132, 136, 188 };
    static const uint8_t gate_bars[] = { 1, 1, 0, 0 };      /* (spec/4)+6 >= 40 lines: clear */
    unsigned i;
    for (i = 0; i < sizeof gate_spec; i++) {
        setup(4, POP_FACE_RIGHT);
        put(1, 1, 5, POP_GATE, gate_spec[i]);
        pop_setupchar(&g);
        pop_getedges(&g);
        CHECK_EQ(pop_getinfront(&g), POP_GATE);
        CHECK_EQ(pop_checkcoll(&g, POP_GATE), gate_bars[i]);
        CHECK_EQ(pop_gatebarr(&g), !gate_bars[i]);
    }
    CHECK_EQ(g.blockedge, 0);                      /* not set when there is no collision */

    setup(4, POP_FACE_RIGHT);
    put(1, 1, 5, POP_SLICER, POP_SLICER_EXT);
    (void)pop_getinfront(&g);
    CHECK_EQ(pop_checkcoll(&g, POP_SLICER), 1);    /* blades shut */
    CHECK_EQ(g.blockedge, 135);
    put(1, 1, 5, POP_SLICER, 1);
    CHECK_EQ(pop_checkcoll(&g, POP_SLICER), 0);
    /* A slicer with blood on it (bit 7) is compared unmasked here, so it
     * never counts as a barrier (COLL.S 564-567); CHECKSLICE does mask. */
    put(1, 1, 5, POP_SLICER, POP_SLICER_EXT | 0x80);
    CHECK_EQ(pop_checkcoll(&g, POP_SLICER), 0);

    CHECK_EQ(pop_checkcoll(&g, POP_FLASK), 0);
    CHECK_EQ(pop_checkcoll(&g, POP_BLOCK), 1);

    setup(4, POP_FACE_LEFT);                       /* the mirror */
    put(1, 1, 3, POP_MIRROR, 0);
    (void)pop_getinfront(&g);
    CHECK_EQ(pop_checkcoll(&g, POP_MIRROR), 1);    /* standing: solid */
    CHECK_EQ(g.createshad, 0);
    g.ch.posn = 41;                                /* running jump, facing left */
    CHECK_EQ(pop_checkcoll(&g, POP_MIRROR), 0);
    CHECK_EQ(g.createshad, 0xFF);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 13], 86);  /* SMASHMIRROR's mark on the piece */
    g.ch.face = POP_FACE_RIGHT;                    /* not from the other side */
    CHECK_EQ(pop_checkcoll(&g, POP_MIRROR), 1);
    g.ch.face = POP_FACE_LEFT;
    g.ch.id = 1;                                   /* and only the kid */
    CHECK_EQ(pop_checkcoll(&g, POP_MIRROR), 1);
}

static void test_neighbouring_screen(void)
{
    /* A barrier seen on the screen to the right is 140 units further on. */
    setup(4, POP_FACE_RIGHT);
    g.tempscrn = 2;
    g.tempblockx = 0;
    g.scrnRight = 2;
    CHECK_EQ(pop_checkcoll(&g, POP_BLOCK), 1);
    CHECK_EQ(g.blockedge, 58 + 7 + 140);
    g.scrnRight = 0;
    g.scrnLeft = 2;
    CHECK_EQ(pop_checkcoll(&g, POP_BLOCK), 1);
    CHECK_EQ(g.blockedge, (uint8_t)(58 + 7 - 140));
}

static void test_checkgate(void)
{
    setup(5, POP_FACE_RIGHT);                      /* standing in a gateway as it shuts */
    put(1, 1, 5, POP_GATE, 0);
    pop_setupchar(&g);
    pop_getedges(&g);
    g.CD[POP_CD_THIS][5] = g.CD[POP_CD_LAST][5] = 0xFF;
    pop_checkgate(&g);
    CHECK_EQ(g.ch.x, 135 - 5);                     /* knocked to the left */
    CHECK_EQ(g.sq.items[0], POP_SND_SMACKWALL);

    setup(5, POP_FACE_RIGHT);                      /* gate still high enough: nothing */
    put(1, 1, 5, POP_GATE, 188);
    pop_setupchar(&g);
    pop_getedges(&g);
    g.CD[POP_CD_THIS][5] = g.CD[POP_CD_LAST][5] = 0xFF;
    pop_checkgate(&g);
    CHECK_EQ(g.ch.x, 135);

    setup(5, POP_FACE_RIGHT);                      /* running through: not checked */
    put(1, 1, 5, POP_GATE, 0);
    pop_setupchar(&g);
    pop_getedges(&g);
    g.CD[POP_CD_THIS][5] = g.CD[POP_CD_LAST][5] = 0xFF;
    g.ch.posn = 9;
    pop_checkgate(&g);
    CHECK_EQ(g.ch.x, 135);
    CHECK_EQ(g.sq.count, 0);
}

static void test_slicer(void)
{
    setup(3, POP_FACE_LEFT);
    put(1, 1, 3, POP_SLICER, POP_SLICER_EXT);
    g.CD[POP_CD_THIS][3] = 0xFF;                   /* overlapping column 3's barrier */
    g.SN[POP_CD_THIS][3] = 1;
    pop_checkslice(&g);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 13], POP_SLICER_EXT | 0x80);   /* smeared */
    CHECK_EQ(g.ch.posn, POP_SQ_HALVE);
    CHECK_EQ(g.ChgKidStr, 0xFD);
    CHECK_EQ(g.sq.items[0], POP_SND_SPLAT);
    CHECK_EQ(g.ch.x, 107 - 8);                     /* centred on the blades */
    CHECK_EQ(g.ch.y, 118);

    setup(3, POP_FACE_LEFT);                       /* blades open: safe */
    put(1, 1, 3, POP_SLICER, 1);
    g.CD[POP_CD_THIS][3] = 0xFF;
    g.SN[POP_CD_THIS][3] = 1;
    pop_checkslice(&g);
    CHECK_EQ(g.ch.posn, 15);

    setup(3, POP_FACE_LEFT);                       /* the edge-based version, for opponents */
    put(1, 1, 3, POP_SLICER, POP_SLICER_EXT | 0x80);
    g.ch.x = 104;                                  /* covers 104..111; blades at 107..109 */
    pop_setupchar(&g);
    pop_rereadblocks(&g);
    pop_getedges(&g);
    CHECK_EQ(pop_checkslice2(&g), 1);
    CHECK_EQ(g.ch.posn, POP_SQ_HALVE);

    setup(2, POP_FACE_LEFT);                       /* one column away: not touching */
    put(1, 1, 3, POP_SLICER, POP_SLICER_EXT);
    pop_setupchar(&g);
    pop_rereadblocks(&g);
    pop_getedges(&g);
    CHECK_EQ(pop_checkslice2(&g), 0);
    CHECK_EQ(g.ch.posn, 15);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_enemycoll(void)
{
    setup(3, POP_FACE_LEFT);                       /* a guard backing into a wall-with-floor */
    put(1, 1, 3, POP_PANELWIF, 0);                 /* barrier at 119..120 */
    g.ch.id = 4;
    g.ch.sword = 2;
    g.ch.action = 1;
    g.ch.x = 115;                                  /* covers 115..122: 3 units inside */
    pop_rereadblocks(&g);
    pop_enemycoll(&g);
    CHECK_EQ(g.ch.x, 112);                         /* moved 3 "forward" (left) */
    CHECK_EQ(g.ch.posn, POP_SQ_BUMPENGBACK);

    setup(3, POP_FACE_LEFT);                       /* not en garde: not checked */
    put(1, 1, 3, POP_PANELWIF, 0);
    g.ch.id = 4;
    g.ch.action = 1;
    g.ch.x = 115;
    pop_rereadblocks(&g);
    pop_enemycoll(&g);
    CHECK_EQ(g.ch.x, 115);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_goneupstairs(void)
{
    setup(3, POP_FACE_LEFT);
    pop_goneupstairs(&g);
    CHECK_EQ(g.SongCue, POP_SONG_UPSTAIRS);
    CHECK_EQ(g.SongCount, 25);
    CHECK_EQ(g.NextLevel, 1);
    g.level = 4;                                   /* the mirror level has its own tune */
    pop_goneupstairs(&g);
    CHECK_EQ(g.SongCue, POP_SONG_SHADOW);
    g.SongCue = 0;
    g.level = 13;                                  /* no music */
    pop_goneupstairs(&g);
    CHECK_EQ(g.SongCue, 0);
    CHECK_EQ(g.NextLevel, 3);
}

int main(void)
{
    RUN(test_edges);
    RUN(test_checkbarr);
    RUN(test_too_fast_to_notice);
    RUN(test_ground_bump);
    RUN(test_facing_matters);
    RUN(test_air_bump);
    RUN(test_checkcoll);
    RUN(test_neighbouring_screen);
    RUN(test_checkgate);
    RUN(test_slicer);
    RUN(test_enemycoll);
    RUN(test_goneupstairs);
    return pop_test_summary();
}
