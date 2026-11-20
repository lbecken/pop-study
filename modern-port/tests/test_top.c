/* test_top.c - the frame and level top level on synthetic data.
 *
 * New code; uses the made-up state of pop_fixture.h.  Expected values were
 * worked out by hand from TOPCTRL.S and SUBS.S (see pop_top.h for the line
 * ranges).  The same routines are compared with the original code on random
 * states (tests/fuzz_control.py), over random play (tests/trace_kidstep.py)
 * and over the original's own demo (tests/replay_demo.py) when the original
 * source is available.
 */

#include <string.h>

#include "pop_fixture.h"
#include "pop_mover.h"
#include "pop_test.h"
#include "pop_top.h"

static void test_static_and_cold(void)
{
    CHECK_EQ(pop_static(15), 0);                   /* standing */
    CHECK_EQ(pop_static(109), 0);                  /* crouching */
    CHECK_EQ(pop_static(185), 0);                  /* dead */
    CHECK_EQ(pop_static(9), 1);                    /* running */
    CHECK_EQ(pop_cold(185), 0);
    CHECK_EQ(pop_cold(177), 0);                    /* impaled */
    CHECK_EQ(pop_cold(15), 1);                     /* standing is not "cold" */
}

static void test_timers(void)
{
    static const uint8_t vibes[] = { 0xFF, 0xFF, 0x00, 0xFF, 0x00 };
    unsigned i;
    setup(3, POP_FACE_LEFT);
    g.ag.weightless = 17;                          /* wtlessflash: steady, then flickering */
    for (i = 0; i < sizeof vibes; i++) {
        pop_wtlessflash(&g);                       /* 16, 15: on; 14, 13, 12: alternate */
        CHECK_EQ(g.vibes, vibes[i]);
    }
    g.ag.weightless = 1;
    pop_wtlessflash(&g);
    CHECK_EQ(g.ag.weightless, 0);
    CHECK_EQ(g.vibes, 0);                          /* over */

    g.mergetimer = 2;                              /* misctimers: 2, 1, then -1 for good */
    pop_misctimers(&g);
    CHECK_EQ(g.mergetimer, 1);
    pop_misctimers(&g);
    CHECK_EQ(g.mergetimer, 0xFF);
    pop_misctimers(&g);
    CHECK_EQ(g.mergetimer, 0xFF);

    setup(3, POP_FACE_LEFT);                       /* level 8: the mouse comes after a wait */
    g.level = 8;
    g.ch.scrn = 16;
    g.kid = g.ch;
    g.exitopen = 149;
    pop_misctimers(&g);
    CHECK_EQ(g.exitopen, 150);
    CHECK_EQ(g.shad.id, 0);
    pop_misctimers(&g);
    CHECK_EQ(g.exitopen, 151);
    CHECK_EQ(g.shad.id, 24);                       /* the mouse, as the "opponent" */
    CHECK_EQ(g.shad.posn, POP_SQ_MSCURRY);
}

static void dead_kid(void)
{
    setup(3, POP_FACE_LEFT);
    g.ch.posn = 185;
    g.ch.life = 0;
    g.MinLeft = g.SecLeft = 0xFF;
}

static void test_death(void)
{
    int i;
    dead_kid();
    pop_ctrlplayer(&g);                            /* first frame dead and still: the song */
    CHECK_EQ(g.SongCue, POP_SONG_ACCID);
    CHECK_EQ(g.ch.life, 1);
    g.SongCue = 0;                                 /* (played) */
    for (i = 0; i < 3; i++) pop_ctrlplayer(&g);
    CHECK_EQ(g.ch.life, POP_DEADENOUGH);
    CHECK_EQ(g.message, 0);
    pop_ctrlplayer(&g);                            /* then "Press button to continue" */
    CHECK_EQ(g.message, POP_MSG_CONT);
    CHECK_EQ(g.msgtimer, 255);
    CHECK(strchr(hooklog, 'R') == NULL);
    CHECK_EQ(g.leave, POP_LEAVE_NONE);
    g.BTN1 = 0xFF;
    nhooks = 0;
    pop_ctrlplayer(&g);
    CHECK(strchr(hooklog, 'R') != NULL);           /* button: the level starts again */
    CHECK_EQ(g.leave, POP_LEAVE_RESTART);

    dead_kid();                                    /* the message ran out: game over */
    g.ch.life = POP_DEADENOUGH;
    g.message = POP_MSG_CONT;
    g.msgtimer = 1;
    pop_ctrlplayer(&g);
    CHECK(strchr(hooklog, 'A') != NULL);

    dead_kid();                                    /* no time left: lost */
    g.ch.life = POP_DEADENOUGH;
    g.MinLeft = g.SecLeft = 0;
    pop_ctrlplayer(&g);
    CHECK(strchr(hooklog, 'L') != NULL);

    dead_kid();                                    /* still falling: nothing yet */
    g.ch.posn = 106;
    pop_ctrlplayer(&g);
    CHECK_EQ(g.ch.life, 0);
    CHECK_EQ(g.SongCue, 0);

    dead_kid();                                    /* dead while the screen is upside down */
    g.kid = g.ch;
    g.invert = 0xFF;
    pop_dokid(&g);
    CHECK_EQ(g.invert, 0);
    CHECK_EQ(g.redrawflg, 2);
    CHECK(strchr(hooklog, 'v') != NULL);
}

static void test_setinitials(void)
{
    setup(3, POP_FACE_LEFT);
    g.blueprint[POP_INFO] = 2;                     /* one screen */
    put(1, 0, 0, POP_GATE, 1);                     /* editor: "up" */
    put(1, 0, 1, POP_GATE, 2);                     /* "down" */
    put(1, 0, 2, POP_LOOSE, 7);
    put(1, 0, 3, POP_FLASK, 3);                    /* kind 3 */
    put(1, 0, 4, POP_SPIKES, 9);                   /* not a gadget: left as it is */
    pop_setinitials(&g);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 0], POP_GMAXVAL);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 1], 0);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 2], 0);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 3], 0x60);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 4], 9);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_level_start(void)
{
    setup(3, POP_FACE_LEFT);
    g.blueprint[POP_INFO] = 2;
    g.blueprint[POP_KIDSTART_SCRN] = 1;
    g.blueprint[POP_KIDSTART_BLOCK] = 14;          /* row 1, column 4 */
    g.blueprint[POP_KIDSTART_FACE] = 0x00;         /* stored inverted: he will face left */
    g.blueprint[POP_GD_BLOCK] = 12;                /* a guard in row 1, column 2 */
    g.blueprint[POP_GD_PROG] = 1;
    g.blueprint[POP_GD_FACE] = 0x00;
    put(1, 0, 8, POP_EXIT, 0);                     /* the door he came in by */
    g.level = 2;
    g.origstrength = 5;
    g.numtrans = 7;                                /* leftovers of the level before */
    g.SongCue = 9;
    memset(&g.kid, 0, sizeof g.kid);
    pop_restart_level(&g);

    CHECK_EQ(g.kid.scrn, 1);
    CHECK_EQ(g.kid.block_x, 4);
    CHECK_EQ(g.kid.block_y, 1);
    CHECK_EQ(g.kid.x, 114 + 14);                   /* middle of his block */
    CHECK_EQ(g.kid.y, 118);
    CHECK_EQ(g.kid.face, POP_FACE_LEFT);
    CHECK_EQ(g.kid.posn, POP_SQ_TURN);             /* one frame into the "turn" sequence */
    CHECK_EQ(g.kid.life, 0xFF);
    CHECK_EQ(g.KidStrength, 5);
    CHECK_EQ(g.MaxKidStr, 5);
    CHECK_EQ(g.gotsword, 1);
    CHECK_EQ(g.SongCue, 0);
    CHECK_EQ(g.message, POP_MSG_LEVEL);
    CHECK_EQ(g.msgtimer, POP_LEVELTIMER - 1);      /* (the first frame has been drawn) */

    CHECK_EQ(g.VisScrn, 1);                        /* FirstFrame entered his screen */
    CHECK_EQ(g.cutplan, 0);                        /* and drew it whole */
    CHECK_EQ(g.CUTTIMER, 2);
    CHECK(strcmp(hooklog, "zSzQP") == 0);
    CHECK_EQ(g.shad.id, 2);                        /* with its guard */
    CHECK_EQ(g.shad.x, 86 + 14);
    CHECK_EQ(g.shad.posn, POP_SQ_ALERTSTAND);

    CHECK_EQ(g.numtrans, 1);                       /* only the entrance, slamming shut */
    CHECK_EQ(g.trloc[1], 8);
    CHECK_EQ(g.trdirec[1], 3);
    CHECK_EQ(g.blueprint[POP_BLUESPEC + 8], POP_EMAXVAL);
    CHECK_EQ(g.unmodelled, 0);

    setup(3, POP_FACE_LEFT);                       /* level 1 starts without the sword ... */
    g.blueprint[POP_INFO] = 2;
    g.blueprint[POP_KIDSTART_SCRN] = 1;
    g.blueprint[POP_KIDSTART_BLOCK] = 14;
    g.level = 0;                                   /* ... and the demo with 4 units */
    pop_startkid(&g);
    CHECK_EQ(g.KidStrength, 4);
}

static void test_prepcut(void)
{
    setup(3, POP_FACE_LEFT);
    g.kid = g.ch;
    g.blueprint[POP_MAP + 4 * 1 + 0] = 1;          /* screen 2: screen 1 to its left */
    g.blueprint[POP_MAP + 4 * 1 + 3] = 3;          /* screen 3 below it */
    g.blueprint[POP_MAP + 4 * 2 + 0] = 4;          /* and 4 left of that */
    g.cutscrn = 1;                                 /* already there: nothing */
    pop_prepcut(&g);
    CHECK_EQ(g.cutplan, 0);
    g.cutscrn = 0;                                 /* never to screen 0 */
    pop_prepcut(&g);
    CHECK_EQ(g.VisScrn, 1);
    g.cutscrn = 2;
    pop_prepcut(&g);
    CHECK_EQ(g.VisScrn, 2);
    CHECK_EQ(g.cutplan, 1);
    CHECK_EQ(g.scrnLeft, 1);
    CHECK_EQ(g.scrnBelow, 3);
    CHECK_EQ(g.scrnBelowL, 4);
    CHECK_EQ(g.scrnRight, 0);

    setup(3, POP_FACE_LEFT);                       /* level 14, screen 5: the end of the game */
    g.level = 14;
    g.cutscrn = 5;
    pop_prepcut(&g);
    CHECK(strcmp(hooklog, "W") == 0);
    CHECK_EQ(g.cutplan, 0);
}

static void test_frame(void)
{
    int i;
    setup(3, POP_FACE_LEFT);                       /* a quiet frame: nothing changes */
    g.kid = g.ch;
    g.MinLeft = g.SecLeft = 0xFF;
    pop_g_jumpseq(&g, POP_SQ_STAND);
    g.kid = g.ch;
    for (i = 0; i < 5; i++) pop_nextframe(&g);
    CHECK_EQ(g.kid.x, 107);
    CHECK_EQ(g.kid.y, 118);
    CHECK_EQ(g.kid.posn, POP_SQ_STAND);
    CHECK_EQ(g.unmodelled, 0);

    g.MinLeft = g.SecLeft = 0;                     /* time is up */
    nhooks = 0;
    pop_nextframe(&g);
    CHECK(strchr(hooklog, 'L') != NULL);

    setup(3, POP_FACE_LEFT);                       /* level 12: leaving by screen 23 */
    g.level = 12;
    g.ch.scrn = 23;
    g.kid = g.ch;
    g.VisScrn = g.cutscrn = 23;
    g.NextLevel = 12;
    pop_nextframe(&g);
    CHECK_EQ(g.NextLevel, 13);
    CHECK_EQ(g.skipmessage, 1);
    CHECK_EQ(g.level, 13);                         /* straight on, with no time message */
    CHECK_EQ(g.timerequest, 0);
    CHECK_EQ(g.leave, POP_LEAVE_RESTART);
    CHECK(strcmp(hooklog, "R") == 0);

    setup(3, POP_FACE_LEFT);                       /* the demo: guard dead, the kid runs off */
    g.level = 0;
    g.milestone = 1;
    pop_democtrl(&g);
    CHECK_EQ(g.JSTKX, 0xFF);
    CHECK_EQ(g.clrF, 0xFF);
    CHECK_EQ(g.clrbtn, 1);
}

int main(void)
{
    RUN(test_static_and_cold);
    RUN(test_timers);
    RUN(test_death);
    RUN(test_setinitials);
    RUN(test_level_start);
    RUN(test_prepcut);
    RUN(test_frame);
    return pop_test_summary();
}
