/* test_control.c - player control and floor routines on synthetic data.
 *
 * New code; the level, frame table, sequence table and image sizes below
 * were made up for these tests.  Expected values were worked out by hand
 * from the original routines cited in pop_control.h.  The same routines are
 * compared with the original code on random states (tests/fuzz_control.py)
 * and over many consecutive frames (tests/trace_kidstep.py) when the
 * original source is available.
 */

#include <string.h>

#include "pop_control.h"
#include "pop_test.h"
#include "pop_world.h"
#include "pop_fixture.h"

static void test_setup_is_what_the_tests_assume(void)
{
    setup(3, POP_FACE_LEFT);
    CHECK_EQ(g.ch.block_x, 3);
    CHECK_EQ(pop_getunderft(&g), POP_FLOOR);
    CHECK_EQ(pop_getinfront(&g), POP_FLOOR);
    CHECK_EQ(pop_getabove(&g), POP_SPACE);
    setup(6, POP_FACE_RIGHT);
    CHECK_EQ(pop_getunderft(&g), POP_FLOOR);
    CHECK_EQ(pop_getinfront(&g), POP_SPACE);       /* column 7 */
    CHECK_EQ(g.unmodelled, 0);
}

static void test_flags(void)
{
    setup(3, POP_FACE_LEFT);
    g.clrF = g.clrB = g.clrU = g.clrD = 0xFF;
    g.clrbtn = 0xFF;
    CHECK_EQ(pop_clrall(&g), 1);                   /* CTRL.S 625-632 */
    CHECK_EQ(g.clrF | g.clrB | g.clrU | g.clrD, 0);
    CHECK_EQ(g.clrbtn, 0xFF);                      /* the button flag is left alone */

    g.JSTKX = 0x01;
    g.clrF = 0xFF;
    g.clrB = 0x01;
    pop_facejstk(&g);                              /* SPECIALK.S 788-797 */
    CHECK_EQ(g.JSTKX, 0xFF);
    CHECK_EQ(g.clrF, 0x01);
    CHECK_EQ(g.clrB, 0xFF);
}

static void test_decstr(void)
{
    setup(3, POP_FACE_LEFT);                       /* MISC.S 691-735 */
    CHECK_EQ(pop_decstr(&g, 1), 0xFF);
    CHECK_EQ(g.ChgKidStr, 0xFF);                   /* -1, applied later by the caller's caller */
    CHECK_EQ(g.KidStrength, 3);                    /* not changed here */
    CHECK_EQ(pop_decstr(&g, 100), 0);              /* fatal: lose exactly what is left */
    CHECK_EQ(g.ChgKidStr, 0xFD);
    CHECK_EQ(pop_decstr(&g, 3), 0);                /* equal counts as fatal */
    g.ch.id = 4;                                   /* anyone else uses the opponent meter */
    g.OppStrength = 2;
    CHECK_EQ(pop_decstr(&g, 1), 0xFF);
    CHECK_EQ(g.ChgOppStr, 0xFF);
    CHECK_EQ(g.ChgKidStr, 0xFD);
}

static void test_getspikes(void)
{
    static const uint8_t spec[] = { 0, 1, 4, 5, 0x7F, 0x80, 0xA0, 0xFF };
    static const uint8_t want[] = { 0, 2, 2, 0, 0, 1, 1, 0 };     /* MOVER.S 365-382 */
    unsigned i;
    setup(3, POP_FACE_LEFT);
    for (i = 0; i < sizeof spec; i++) {
        put(1, 1, 3, POP_SPIKES, spec[i]);
        (void)pop_getunderft(&g);
        CHECK_EQ(pop_getspikes(&g), want[i]);
    }
}

static void test_getopdist(void)
{
    setup(3, POP_FACE_RIGHT);                      /* CTRLSUBS.S 2052-2102 */
    g.op.scrn = 1;
    g.op.face = POP_FACE_RIGHT;
    g.ch.x = 100;
    g.op.x = 120;
    CHECK_EQ(pop_getopdist(&g), 20);               /* ahead */
    g.op.face = POP_FACE_LEFT;
    CHECK_EQ(pop_getopdist(&g), 33);               /* facing each other: + estwidth */
    g.ch.face = POP_FACE_LEFT;
    CHECK_EQ(pop_getopdist(&g), 0xEC);             /* now behind: -20, same facing */
    g.op.x = 80;
    g.op.face = POP_FACE_RIGHT;
    CHECK_EQ(pop_getopdist(&g), 33);
    g.op.x = 250;
    g.ch.x = 60;                                   /* 190 apart: clamped to 127 first */
    g.ch.face = g.op.face = POP_FACE_RIGHT;
    CHECK_EQ(pop_getopdist(&g), 127);
    g.op.scrn = 2;
    CHECK_EQ(pop_getopdist(&g), 127);              /* another screen */
}

static void test_checkledge(void)
{
    setup(3, POP_FACE_LEFT);                       /* CTRLSUBS.S 1814-1858 */
    (void)pop_getunderft(&g);                      /* ry -> the floor piece, state 0 */
    g.blockid = POP_SPACE;
    CHECK_EQ(pop_checkledge(&g, POP_FLOOR), 1);
    CHECK_EQ(pop_checkledge(&g, POP_SPACE), 0);    /* nothing to hold */
    g.blockid = POP_BLOCK;
    CHECK_EQ(pop_checkledge(&g, POP_FLOOR), 0);    /* no room above the ledge */
    g.blockid = POP_FLOOR;
    CHECK_EQ(pop_checkledge(&g, POP_FLOOR), 0);
    g.blockid = POP_SPACE;
    CHECK_EQ(pop_checkledge(&g, POP_PANELWIF), 0); /* facing left */
    g.ch.face = POP_FACE_RIGHT;
    CHECK_EQ(pop_checkledge(&g, POP_PANELWIF), 1);
    g.blockid = POP_PANELWOF;
    CHECK_EQ(pop_checkledge(&g, POP_FLOOR), 0);    /* facing right */

    /* A loose floor: refused when its state has any of bits 0, 1 or 3 set -
     * the test is `bit` against the piece id 11, not "state non-zero". */
    g.blockid = POP_SPACE;
    put(1, 1, 3, POP_LOOSE, 0);
    CHECK_EQ(pop_checkledge(&g, POP_LOOSE), 1);
    put(1, 1, 3, POP_LOOSE, 1);
    CHECK_EQ(pop_checkledge(&g, POP_LOOSE), 0);
    put(1, 1, 3, POP_LOOSE, 4);
    CHECK_EQ(pop_checkledge(&g, POP_LOOSE), 1);
    put(1, 1, 3, POP_LOOSE, 0x84);
    CHECK_EQ(pop_checkledge(&g, POP_LOOSE), 1);
}

static void test_redraw_marks(void)
{
    int carry;
    setup(3, POP_FACE_LEFT);
    g.tempblockx = 4;                              /* INDEXBLOCK, CTRLSUBS.S 615-631 */
    g.tempblocky = 2;
    CHECK_EQ(pop_indexblock(&g), 0);
    CHECK_EQ(g.ry, 24);
    g.tempblocky = 0xFF;                           /* the row above the screen */
    CHECK_EQ(pop_indexblock(&g), 1);
    CHECK_EQ(g.ry, 4);
    g.tempblocky = 3;
    CHECK_EQ(pop_indexblock(&g), 1);
    CHECK_EQ(g.ry, 30);

    g.ry = 7;                                      /* MARKWIPE, CTRLSUBS.S 1744-1757 */
    g.height = 20;
    carry = 0;
    pop_markwipe(&g, 2, &carry);
    CHECK_EQ(g.rb[POP_RB_WIPE + 7], 2);
    CHECK_EQ(g.rb[POP_RB_WHITE + 7], 20);
    g.height = 10;                                 /* a smaller wipe keeps the larger height */
    pop_markwipe(&g, 2, &carry);
    CHECK_EQ(g.rb[POP_RB_WHITE + 7], 20);
    g.height = 35;
    pop_markwipe(&g, 2, &carry);
    CHECK_EQ(g.rb[POP_RB_WHITE + 7], 35);
    carry = 1;                                     /* off screen: nothing */
    g.ry = 8;
    pop_markwipe(&g, 2, &carry);
    CHECK_EQ(g.rb[POP_RB_WIPE + 8], 0);
    CHECK_EQ(carry, 1);

    pop_markred(&g, 2, &carry);                    /* MARKRED: carry set -> top line */
    CHECK_EQ(g.rb[POP_RB_TOP + 8], 2);
    CHECK_EQ(g.rb[POP_RB_RED + 8], 0);
    carry = 0;
    pop_markred(&g, 3, &carry);
    CHECK_EQ(g.rb[POP_RB_RED + 8], 3);
}

static void test_removeobj(void)
{
    setup(3, POP_FACE_LEFT);                       /* SUBS.S 1415-1432 */
    put(1, 1, 2, POP_FLASK, 0x40);
    CHECK_EQ(pop_getinfront(&g), POP_FLASK);       /* facing left: column 2 */
    g.clrbtn = 0xFF;
    pop_removeobj(&g, 2);
    CHECK_EQ(g.blueprint[POP_BLUETYPE + 12], POP_FLOOR);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 12], 0);
    CHECK_EQ(g.lastpotion, 2);
    CHECK_EQ(g.clrbtn, 1);
    CHECK_EQ(g.rb[POP_RB_WIPE + 12], 2);
    CHECK_EQ(g.rb[POP_RB_WHITE + 12], 35);
    CHECK_EQ(g.rb[POP_RB_RED + 12], 2);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_standing(void)
{
    setup(3, POP_FACE_LEFT);                       /* fresh "forward": start to run */
    g.JSTKX = 0xFF;
    g.clrF = 0xFF;
    pop_standing(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_STARTRUN));

    setup(3, POP_FACE_LEFT);                       /* fresh "back": turn */
    g.JSTKX = 0x01;
    g.clrB = 0xFF;
    pop_standing(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_TURN));
    CHECK_EQ(g.clrB, 1);                           /* used */

    setup(3, POP_FACE_LEFT);                       /* button + forward: a careful step */
    g.JSTKX = 0xFF;
    g.clrF = 0xFF;
    g.btn = 0xFF;
    pop_standing(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_STEPFWD1 + 11 - 1));   /* a full 11-unit step */
    CHECK_EQ(g.ch.repeat, 11);
    CHECK_EQ(g.clrF, 1);
    CHECK_EQ(g.clrbtn, 1);

    setup(3, POP_FACE_LEFT);                       /* fresh "down": crouch */
    g.JSTKY = 0x01;
    g.clrD = 0xFF;
    pop_standing(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_STOOP));
    CHECK_EQ(g.clrD, 1);

    setup(3, POP_FACE_LEFT);                       /* fresh "up", nothing above: jump */
    g.JSTKY = 0xFF;
    g.clrU = 0xFF;
    pop_standing(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_HIGHJUMP));

    setup(3, POP_FACE_LEFT);                       /* no input: nothing */
    pop_standing(&g);
    CHECK_EQ(g.ch.seq, 0);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_userctrl_mirrors_the_stick(void)
{
    /* The handlers think "negative x = forward", which is the raw stick for
     * a character facing left.  Facing right, UserCtrl flips the stick
     * around the call and flips it back (CTRL.S 610-622). */
    setup(3, POP_FACE_RIGHT);
    g.JSTKX = 0x01;                                /* stick right = forward */
    g.clrB = 0xFF;                                 /* "right" freshly pushed */
    pop_userctrl(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_STARTRUN));
    CHECK_EQ(g.JSTKX, 0x01);                       /* restored */
    CHECK_EQ(g.clrB, 0xFF);
}

static void test_running_and_crouching(void)
{
    setup(3, POP_FACE_LEFT);                       /* stick centred on frame 7: stop */
    g.ch.posn = 7;
    g.clrU = 0xFF;
    pop_genctrl(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_RUNSTOP));
    CHECK_EQ(g.clrF, 1);
    CHECK_EQ(g.clrU, 0);

    setup(3, POP_FACE_LEFT);                       /* ... but not on frame 8 */
    g.ch.posn = 8;
    pop_genctrl(&g);
    CHECK_EQ(g.ch.seq, 0);

    setup(3, POP_FACE_LEFT);                       /* stick back while running: turn */
    g.ch.posn = 9;
    g.JSTKX = 0x01;
    pop_genctrl(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_RUNTURN));

    setup(3, POP_FACE_LEFT);                       /* crouched, stick released: stand up */
    g.ch.posn = 109;
    pop_genctrl(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_STANDUP));

    setup(3, POP_FACE_LEFT);                       /* crouched, down + fresh forward: crawl */
    g.ch.posn = 109;
    g.JSTKY = 0x01;
    g.clrF = 0xFF;
    pop_genctrl(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_CRAWL));

    setup(3, POP_FACE_LEFT);                       /* dead and standing: collapse */
    g.ch.life = 0;
    g.JSTKX = 0xFF;
    g.clrF = 0xFF;
    pop_genctrl(&g);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_DROPDEAD));

    setup(3, POP_FACE_LEFT);                       /* falling: input is discarded */
    g.ch.action = 4;
    g.clrF = g.clrU = 0xFF;
    pop_genctrl(&g);
    CHECK_EQ(g.ch.seq, 0);
    CHECK_EQ(g.clrF | g.clrU, 0);
}

static void test_playerctrl(void)
{
    setup(3, POP_FACE_LEFT);                       /* CTRL.S 554-582 */
    g.stunned = 2;
    pop_playerctrl(&g);
    CHECK_EQ(g.stunned, 1);
    CHECK(strcmp(hooklog, "io") == 0);             /* input read, then saved */
    CHECK_EQ(g.ch.life, 0xFF);

    setup(3, POP_FACE_LEFT);                       /* no strength left: dead this frame */
    g.KidStrength = 0;
    pop_playerctrl(&g);
    CHECK_EQ(g.ch.life, 0);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_DROPDEAD));

    setup(3, POP_FACE_LEFT);                       /* level 0 is the self-playing demo: */
    g.level = 0;                                   /* the script moves the stick, no input is read */
    pop_playerctrl(&g);
    CHECK_EQ(g.PlayCount, 1);
    CHECK(hooklog[0] == 0);
}

static void test_walking_off_an_edge(void)
{
    setup(7, POP_FACE_RIGHT);                      /* column 7 has no floor */
    CHECK_EQ(pop_getunderft(&g), POP_SPACE);
    g.ch.posn = 9;                                 /* mid-run */
    g.ch.sword = 2;
    pop_checkfloor(&g);                            /* -> onground -> startfall */
    CHECK_EQ(g.ch.block_y, 2);                     /* now relative to the floor below */
    CHECK_EQ(g.ch.posn, POP_SQ_STEPFALL);          /* one frame into the made-up sequence */
    CHECK_EQ(g.rjumpflag, 9);
    CHECK_EQ(g.ch.sword, 0);                       /* hands free to grab */
    CHECK_EQ(g.unmodelled, 0);

    setup(3, POP_FACE_RIGHT);                      /* floor underfoot: nothing happens */
    g.ch.posn = 9;
    pop_checkfloor(&g);
    CHECK_EQ(g.ch.block_y, 1);
    CHECK_EQ(g.ch.seq, 0);

    setup(7, POP_FACE_RIGHT);                      /* hanging (action 2): no floor check */
    g.ch.action = 2;
    pop_checkfloor(&g);
    CHECK_EQ(g.ch.block_y, 1);
}

static void land(uint8_t yvel, uint8_t id)
{
    setup(3, POP_FACE_LEFT);
    g.ch.action = 4;
    g.ch.id = id;
    g.ch.y = 125;                                  /* just past the floor line, 118 */
    g.ch.y_vel = yvel;
    pop_checkfloor(&g);                            /* -> falling -> hitflr */
}

static void test_landing(void)
{
    land(21, 0);                                   /* CTRL.S 140-242 */
    CHECK_EQ(g.ch.posn, POP_SQ_SOFTLAND);
    CHECK_EQ(g.ch.y, 118);                         /* put on the floor line */
    CHECK_EQ(g.ch.y_vel, 0);
    CHECK_EQ(g.ChgKidStr, 0);
    CHECK_EQ(g.sq.count, 0);

    land(22, 0);                                   /* OofVelocity: costs one unit */
    CHECK_EQ(g.ch.posn, POP_SQ_MEDLAND);
    CHECK_EQ(g.ChgKidStr, 0xFF);
    CHECK_EQ(g.sq.count, 1);
    CHECK_EQ(g.sq.items[0], POP_SND_SPLAT);

    land(33, 0);                                   /* DeathVelocity */
    CHECK_EQ(g.ch.posn, POP_SQ_HARDLAND);
    CHECK_EQ(g.ChgKidStr, 0xFD);                   /* all three units */
    CHECK_EQ(g.sq.items[0], POP_SND_SPLAT);

    land(25, 1);                                   /* the shadow lands easily */
    CHECK_EQ(g.ch.posn, POP_SQ_SOFTLAND);
    land(25, 2);                                   /* a guard does not survive it */
    CHECK_EQ(g.ch.posn, POP_SQ_HARDLAND);
    CHECK_EQ(g.ChgOppStr, 0xFD);
    land(5, 2);                                    /* a guard lands with sword drawn */
    CHECK_EQ(g.ch.posn, POP_SQ_LANDENGARDE);
    CHECK_EQ(g.ch.sword, 2);

    setup(3, POP_FACE_LEFT);                       /* one unit left: a medium fall kills */
    g.KidStrength = 1;
    g.ch.action = 4;
    g.ch.y = 125;
    g.ch.y_vel = 25;
    pop_checkfloor(&g);
    CHECK_EQ(g.ch.posn, POP_SQ_HARDLAND);
    CHECK_EQ(g.ChgKidStr, 0xFF);

    setup(7, POP_FACE_RIGHT);                      /* no floor here: keep falling */
    g.ch.action = 4;
    g.ch.y = 125;
    pop_checkfloor(&g);
    CHECK_EQ(g.ch.block_y, 2);
    CHECK_EQ(g.ch.seq, 0);
}

static void test_spikes(void)
{
    setup(3, POP_FACE_LEFT);                       /* DOIMPALE, CTRL.S 2058-2086 */
    put(1, 1, 3, POP_SPIKES, 2);                   /* springing */
    g.ch.posn = 9;                                 /* running over them */
    pop_checkimpale(&g);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 13], 0xFF);/* jammed */
    CHECK_EQ(g.numtrans, 1);                       /* listed as stopped, to be redrawn */
    CHECK_EQ(g.trloc[1], 13);
    CHECK_EQ(g.trdirec[1], 0xFF);
    CHECK_EQ(g.ch.posn, POP_SQ_IMPALE);
    CHECK_EQ(g.ChgKidStr, 0xFD);
    CHECK_EQ(g.sq.count, 2);
    CHECK_EQ(g.sq.items[1], POP_SND_IMPALED);
    CHECK_EQ(g.ch.y, 118);
    /* centred on the block: left edge 100, + 10, then 8 further "forward",
     * which for a character facing left is towards smaller x */
    CHECK_EQ(g.ch.x, 102);

    setup(3, POP_FACE_LEFT);                       /* standing among spikes is safe */
    put(1, 1, 3, POP_SPIKES, 2);
    pop_checkimpale(&g);
    CHECK_EQ(g.ch.seq, 0);

    setup(3, POP_FACE_LEFT);                       /* fully out (state 5+): safe to run */
    put(1, 1, 3, POP_SPIKES, 5);
    g.ch.posn = 9;
    pop_checkimpale(&g);
    CHECK_EQ(g.ch.seq, 0);
}

int main(void)
{
    RUN(test_setup_is_what_the_tests_assume);
    RUN(test_flags);
    RUN(test_decstr);
    RUN(test_getspikes);
    RUN(test_getopdist);
    RUN(test_checkledge);
    RUN(test_redraw_marks);
    RUN(test_removeobj);
    RUN(test_standing);
    RUN(test_userctrl_mirrors_the_stick);
    RUN(test_running_and_crouching);
    RUN(test_playerctrl);
    RUN(test_walking_off_an_edge);
    RUN(test_landing);
    RUN(test_spikes);
    return pop_test_summary();
}
