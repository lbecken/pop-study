/* test_loop.c - the game loop around a frame, on synthetic data.
 *
 * New code; uses the made-up state of pop_fixture.h.  Expected values were
 * worked out by hand from SPECIALK.S, SUBS.S, GRAFIX.S and TOPCTRL.S (see
 * pop_loop.h for the line ranges).  The same routines are compared with the
 * original code on random states (tests/fuzz_control.py) when the original
 * source is available.
 */

#include <string.h>

#include "pop_fixture.h"
#include "pop_loop.h"
#include "pop_mover.h"
#include "pop_test.h"
#include "pop_top.h"

static void set_fc(unsigned v)
{
    g.FrameCount[0] = (uint8_t)v;
    g.FrameCount[1] = (uint8_t)(v >> 8);
}

static unsigned fc(void) { return (unsigned)(g.FrameCount[0] | (g.FrameCount[1] << 8)); }

static void test_keyboard(void)
{
    setup(3, POP_FACE_LEFT);
    g.keypress = 0xEA;                             /* "j", a new press */
    pop_kread(&g);
    CHECK_EQ(g.kbdX, 0xFF);
    CHECK_EQ(g.kbdY, 0);
    g.keypress = 0x6A;                             /* the same key, no longer new ... */
    g.keydown = 0x80;                              /* ... but still held */
    pop_kread(&g);
    CHECK_EQ(g.kbdX, 0xFF);
    g.keydown = 0;                                 /* let go */
    pop_kread(&g);
    CHECK_EQ(g.kbdX, 0);
    g.keypress = 0xD5;                             /* "U": up and left; capitals count too */
    pop_kread(&g);
    CHECK_EQ(g.kbdX, 0xFF);
    CHECK_EQ(g.kbdY, 0xFF);
    g.keypress = 0xEF;                             /* "o": up and right */
    pop_kread(&g);
    CHECK_EQ(g.kbdX, 1);
    CHECK_EQ(g.kbdY, 0xFF);
    g.keypress = 0xEB;                             /* "k": down */
    pop_kread(&g);
    CHECK_EQ(g.kbdX, 0);
    CHECK_EQ(g.kbdY, 1);
    g.keypress = 0xA0;                             /* space: not a direction */
    pop_kread(&g);
    CHECK_EQ(g.kbdX | g.kbdY, 0);
}

static void test_devices(void)
{
    setup(3, POP_FACE_LEFT);
    g.kbdX = 1; g.kbdY = 0xFF; g.joyX = 0xFF; g.joyY = 1;
    g.BTN0 = 0x80; g.BTN1 = 0;
    pop_getselect(&g);                             /* keyboard selected: either button */
    CHECK_EQ(g.JSTKX, 1);
    CHECK_EQ(g.JSTKY, 0xFF);
    CHECK_EQ(g.btn, 0x80);
    pop_getdesel(&g);                              /* the other device */
    CHECK_EQ(g.JSTKX, 0xFF);
    CHECK_EQ(g.JSTKY, 1);
    CHECK_EQ(g.btn, 0x80);
    g.ManCtrl = 0xFF;                              /* two players: a button each */
    pop_getdesel(&g);
    CHECK_EQ(g.btn, 0);                            /* the joystick has button 1 */
    pop_getselect(&g);
    CHECK_EQ(g.btn, 0x80);
    g.joyon = 0xFF;                                /* joystick selected */
    pop_getselect(&g);
    CHECK_EQ(g.JSTKX, 0xFF);
    CHECK_EQ(g.btn, 0);
}

static void test_press_flags(void)
{
    setup(3, POP_FACE_LEFT);
    g.JSTKX = 0xFF;                                /* forward is pushed: a press */
    pop_clrjstk(&g);
    CHECK_EQ(g.clrF, 0xFF);
    CHECK_EQ(g.clrB, 0);
    g.JSTKX = 0;                                   /* released unused: still remembered */
    pop_clrjstk(&g);
    CHECK_EQ(g.clrF, 0xFF);
    g.clrF = 1;                                    /* the control code used it */
    g.JSTKX = 0xFF;
    pop_clrjstk(&g);                               /* still held: no new press */
    CHECK_EQ(g.clrF, 1);
    g.JSTKX = 0;
    pop_clrjstk(&g);                               /* released: ready for the next */
    CHECK_EQ(g.clrF, 0);
    g.JSTKX = 1; g.JSTKY = 1; g.btn = 0x80;        /* back, down and the button */
    pop_clrjstk(&g);
    CHECK_EQ(g.clrB, 0xFF);
    CHECK_EQ(g.clrD, 0xFF);
    CHECK_EQ(g.clrU, 0);
    CHECK_EQ(g.clrbtn, 0xFF);

    pop_saveselect(&g);                            /* kept per device */
    g.clrB = g.clrD = g.clrbtn = 0;
    pop_savedesel(&g);
    pop_loadselect(&g);
    CHECK_EQ(g.clrB, 0xFF);
    CHECK_EQ(g.clrbtn, 0xFF);
    pop_loaddesel(&g);
    CHECK_EQ(g.clrB, 0);
    pop_clearjoy(&g);                              /* directions forgotten, the button not */
    CHECK_EQ(g.clrSEL[1], 0);
    CHECK_EQ(g.clrSEL[3], 0);
    CHECK_EQ(g.clrSEL[4], 0xFF);
    pop_initinput(&g);
    CHECK_EQ(g.clrSEL[4], 0);
    CHECK_EQ(g.clrDESEL[1], 0);

    setup(3, POP_FACE_LEFT);                       /* the demo is interrupted by keys */
    g.level = 0;
    g.keypress = 0xA0;
    CHECK_EQ(pop_demokeys(&g), 0xFF);
    g.keypress = 0x9B;                             /* but not by ESC (pause) */
    CHECK_EQ(pop_demokeys(&g), 0);
    g.keypress = 0x20;                             /* nor by a key no longer new */
    CHECK_EQ(pop_demokeys(&g), 0);
    g.BTN1 = 0x80;
    CHECK_EQ(pop_demokeys(&g), 0xFF);
    g.level = 1;
    CHECK_EQ(pop_demokeys(&g), 0);
}

static void test_clock(void)
{
    CHECK_EQ(pop_timetable(0), 0);
    CHECK_EQ(pop_timetable(1), 3625);              /* 5 "minutes" of 725 frames */
    CHECK_EQ(pop_timetable(11), 39875);            /* 5 left */
    CHECK_EQ(pop_timetable(14), 42050);            /* 2 left */
    CHECK_EQ(pop_timetable(15), 42776);            /* the last minute begins */
    CHECK_EQ(pop_timetable(16), 43505);            /* 5 frames after the end */
    CHECK_EQ(pop_timetable(17), 65535);

    setup(3, POP_FACE_LEFT);
    g.SecLeft = 0xFF;
    set_fc(0);     pop_getminleft(&g); CHECK_EQ(g.MinLeft, 0x61);   /* before the first frame */
    set_fc(1);     pop_getminleft(&g); CHECK_EQ(g.MinLeft, 0x60);
    set_fc(725);   pop_getminleft(&g); CHECK_EQ(g.MinLeft, 0x60);
    set_fc(726);   pop_getminleft(&g); CHECK_EQ(g.MinLeft, 0x59);   /* decimal digits */
    set_fc(42775); pop_getminleft(&g); CHECK_EQ(g.MinLeft, 0x02);
    CHECK_EQ(g.SecLeft, 0xFF);                     /* seconds only in the last minute */
    set_fc(42776); pop_getminleft(&g); CHECK_EQ(g.MinLeft, 0x01);
    CHECK_EQ(g.SecLeft, 0x60);
    set_fc(42788); pop_getminleft(&g); CHECK_EQ(g.SecLeft, 0x59);   /* a "second" is 12 frames */
    set_fc(43495); pop_getminleft(&g); CHECK_EQ(g.SecLeft, 0x01);
    set_fc(43496); pop_getminleft(&g); CHECK_EQ(g.SecLeft, 0x00);   /* 60 x 12 = 720 < 725: */
    CHECK_EQ(g.MinLeft, 0x01);                     /* 0 seconds, but the minute is not over */
    set_fc(43501); pop_getminleft(&g); CHECK_EQ(g.MinLeft, 0x00);
    set_fc(65535); pop_getminleft(&g); CHECK_EQ(g.MinLeft, 0x00);

    setup(3, POP_FACE_LEFT);                       /* keeptime */
    g.level = 1;
    g.kid.life = 0xFF;
    g.NextTimeMsg = 2;
    set_fc(3623);
    pop_keeptime(&g);
    CHECK_EQ(fc(), 3624);
    CHECK_EQ(g.timerequest, 0);
    pop_keeptime(&g);                              /* 3625: the 55-minute message falls due */
    CHECK_EQ(g.timerequest, 2);
    CHECK_EQ(g.NextTimeMsg, 4);
    g.NextTimeMsg = 2;
    g.timerequest = 0;
    g.msgtimer = 5;                                /* another message is up: wait */
    pop_keeptime(&g);
    CHECK_EQ(g.timerequest, 0);
    CHECK_EQ(g.NextTimeMsg, 2);
    g.msgtimer = 0;
    set_fc(0x0F00);                                /* later, with a low byte below $29: */
    pop_keeptime(&g);                              /* the original says "not yet" */
    CHECK_EQ(g.timerequest, 0);
    set_fc(0x0F28);
    pop_keeptime(&g);
    CHECK_EQ(g.timerequest, 2);
    g.kid.life = 0;                                /* the clock stops while he is dead */
    pop_keeptime(&g);
    CHECK_EQ(fc(), 0x0F29);
    g.kid.life = 0xFF;
    g.level = 0;                                   /* and in the demo */
    pop_keeptime(&g);
    CHECK_EQ(fc(), 0x0F29);
    g.level = 1;
    set_fc(0xFFFF);                                /* it does not wrap */
    pop_keeptime(&g);
    CHECK_EQ(fc(), 0xFFFF);

    g.NextTimeMsg = 4;                             /* a skipped level costs time */
    pop_shortentime(&g);
    CHECK_EQ(g.NextTimeMsg, 18);
    CHECK_EQ(fc(), 45 * 725);
    g.NextTimeMsg = 20;
    set_fc(7);
    pop_shortentime(&g);                           /* not when 10 minutes or less are left */
    CHECK_EQ(fc(), 7);
}

static void test_showtime(void)
{
    setup(3, POP_FACE_LEFT);
    g.level = 1;
    g.kid.life = 0xFF;
    set_fc(3626);
    pop_showtime(&g);                              /* not asked for */
    CHECK_EQ(g.message, 0);
    g.timerequest = 2;
    g.msgtimer = 3;                                /* another message is up: wait */
    pop_showtime(&g);
    CHECK_EQ(g.timerequest, 2);
    g.msgtimer = 0;
    pop_showtime(&g);
    CHECK_EQ(g.MinLeft, 0x55);
    CHECK_EQ(g.message, POP_MSG_TIME);
    CHECK_EQ(g.msgtimer, 20);
    CHECK_EQ(g.timerequest, 0);
    g.timerequest = 4;                             /* the vizier is dead: a little longer */
    g.msgtimer = 0;
    pop_showtime(&g);
    CHECK_EQ(g.msgtimer, 25);

    g.message = 0;                                 /* the last minute: seconds, every frame */
    g.msgtimer = 0;
    g.timerequest = 2;
    set_fc(42800);
    pop_showtime(&g);
    CHECK_EQ(g.SecLeft, 0x58);
    CHECK_EQ(g.message, POP_MSG_TIME);
    CHECK_EQ(g.msgtimer, 1);
    CHECK_EQ(g.timerequest, 1);
    set_fc(43495);                                 /* 1 second: the countdown ends */
    pop_showtime(&g);
    CHECK_EQ(g.timerequest, 0);
    CHECK_EQ(g.msgtimer, 0);

    set_fc(43501);                                 /* time is up: only when a key asks */
    g.timerequest = 2;
    g.message = 0;
    pop_showtime(&g);
    CHECK_EQ(g.message, 0);
    g.timerequest = 3;
    pop_showtime(&g);
    CHECK_EQ(g.message, POP_MSG_TIME);
    CHECK_EQ(g.msgtimer, 20);

    g.kid.life = 0;                                /* never while the kid is dead */
    g.timerequest = 3;
    g.msgtimer = 0;
    pop_showtime(&g);
    CHECK_EQ(g.timerequest, 3);
}

static void test_messages(void)
{
    setup(3, POP_FACE_LEFT);
    g.kid.life = 0xFF;
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_NOTHING);   /* no message */
    g.message = POP_MSG_LEVEL;
    g.msgtimer = 20;
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_NOTHING);   /* 19, 18: a short pause first */
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_NOTHING);
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_LEVEL);     /* 17 */
    CHECK_EQ(g.msgtimer, 17);
    CHECK(strcmp(hooklog, "l") == 0);
    g.message = POP_MSG_TIME;
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_TIME);

    g.kid.life = 3;                                /* dead: "Press button to continue" */
    g.message = POP_MSG_CONT;
    g.msgtimer = 255;
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_CONTINUE);  /* steady at first */
    g.msgtimer = 95;
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_NOTHING);   /* 94: blinking, off (94 & 7 = 6) */
    g.msgtimer = 91;
    g.soundon = 1;
    nhooks = 0;
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_CONTINUE);  /* 90: on, with its beep */
    CHECK_EQ(g.sq.count, 1);
    CHECK_EQ(g.sq.items[0], POP_SND_FLASHMSG);
    CHECK(strcmp(hooklog, "c") == 0);
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_CONTINUE);  /* 89: on */
    g.msgtimer = 89;
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_CONTINUE);  /* 88: on */
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_NOTHING);   /* 87: off */
    g.msgtimer = 91;
    g.soundon = 0;                                 /* sound off: a tone all the same */
    nhooks = 0;
    (void)pop_dispmsg(&g);
    CHECK(strcmp(hooklog, "gc") == 0);
    g.msgtimer = 15;
    CHECK_EQ(pop_dispmsg(&g), POP_SHOW_NOTHING);   /* the last 14 frames: off */
}

static void test_flashes_and_songs(void)
{
    setup(3, POP_FACE_LEFT);
    pop_flashon(&g);                               /* nothing to flash */
    pop_flashoff(&g);
    CHECK_EQ(nhooks, 0);
    g.ChgKidStr = 0xFF;                            /* the kid is hurt: one red frame */
    pop_flashon(&g);
    pop_flashoff(&g);
    CHECK(strcmp(hooklog, "F11f") == 0);
    g.ChgKidStr = 0;
    g.lightning = 2;                               /* two flashes asked for */
    g.lightcolor = 0xFF;
    nhooks = 0;
    pop_flashon(&g);
    pop_flashoff(&g);
    CHECK(strcmp(hooklog, "FFFf") == 0);
    CHECK_EQ(g.lightning, 1);
    pop_flashoff(&g);
    CHECK_EQ(g.lightning, 0);

    setup(3, POP_FACE_LEFT);                       /* a queued song waits for stillness */
    g.level = 1;
    g.NextLevel = 1;
    g.shad.face = POP_NO_CHAR;
    g.kid.posn = 9;                                /* running */
    g.SongCue = POP_SONG_ACCID;
    g.SongCount = 3;
    pop_songcues(&g);
    CHECK_EQ(g.SongCount, 2);
    CHECK_EQ(g.SongCue, POP_SONG_ACCID);
    CHECK_EQ(nhooks, 0);
    g.kid.posn = 15;                               /* standing: now */
    g.clrSEL[0] = 0xFF;
    pop_songcues(&g);
    CHECK(strcmp(hooklog, "M01") == 0);
    CHECK_EQ(g.SongCue, 0);
    CHECK_EQ(g.clrSEL[0], 0);                      /* and presses made meanwhile are dropped */
    g.SongCue = POP_SONG_ACCID;                    /* its time ran out unplayed */
    g.SongCount = 0;
    nhooks = 0;
    pop_songcues(&g);
    CHECK_EQ(g.SongCue, 0);
    CHECK_EQ(nhooks, 0);
    g.SongCue = POP_SONG_ACCID;                    /* a falling floor holds it up */
    g.SongCount = 9;
    g.nummob = 1;
    pop_songcues(&g);
    CHECK_EQ(nhooks, 0);
    g.nummob = 0;
    g.level = 0;                                   /* no music in the demo */
    pop_songcues(&g);
    CHECK_EQ(g.SongCount, 8);
    CHECK_EQ(nhooks, 0);
}

static void test_frameadv(void)
{
    unsigned i;
    setup(3, POP_FACE_LEFT);
    g.VisScrn = 1;
    g.kid.life = 0xFF;
    memset(g.rb, 7, sizeof g.rb);
    pop_frameadv(&g);                              /* nothing special: the changes only */
    CHECK(strcmp(hooklog, "QP") == 0);
    CHECK_EQ(g.rb[POP_RB_RED], 7);
    g.cutplan = 1;                                 /* a new screen */
    nhooks = 0;
    pop_frameadv(&g);
    CHECK(strcmp(hooklog, "SzQP") == 0);
    CHECK_EQ(g.cutplan, 0);
    CHECK_EQ(g.CUTTIMER, 2);
    CHECK_EQ(g.SCRNUM, 1);
    CHECK_EQ(g.rb[POP_RB_FLOOR + 29], 0);          /* the redraw marks start afresh */
    CHECK_EQ(g.rb[POP_RB_TOP + 9], 0);
    memset(g.rb, 7, sizeof g.rb);
    pop_zerored(&g);
    for (i = 0; i < 30; i++) CHECK_EQ(g.rb[POP_RB_WHITE + i], 7);    /* (all but this one) */
    CHECK_EQ(g.rb[POP_RB_HALF], 0);
    g.redrawflg = 2;                               /* a forced redraw, twice (both pages) */
    nhooks = 0;
    pop_frameadv(&g);
    CHECK(strcmp(hooklog, "SQP") == 0);
    CHECK_EQ(g.redrawflg, 1);
}

static void test_levels(void)
{
    setup(3, POP_FACE_LEFT);
    g.milestone = 1;
    g.MinLeft = 3;
    pop_startgame(&g, 1);
    CHECK_EQ(g.level, 1);
    CHECK_EQ(g.NextLevel, 1);
    CHECK_EQ(g.SongCue, POP_SONG_DANGER);
    CHECK_EQ(g.SongCount, 25);
    CHECK_EQ(g.origstrength, 3);
    CHECK_EQ(g.milestone, 0);
    CHECK_EQ(g.MinLeft, 0xFF);
    pop_startgame(&g, 0);                          /* the demo: no song */
    CHECK_EQ(g.level, 0);

    CHECK_EQ(pop_cut_before_level(2), 1);
    CHECK_EQ(pop_cut_before_level(8), 8);
    CHECK_EQ(pop_cut_before_level(9), 4);
    CHECK_EQ(pop_cut_before_level(3), 0);

    setup(3, POP_FACE_LEFT);
    g.level = 1;
    g.NextLevel = 2;
    g.MaxKidStr = 5;
    g.milestone = 1;
    pop_loadnextlevel(&g);
    CHECK_EQ(g.level, 2);
    CHECK_EQ(g.origstrength, 5);                   /* strength gained is kept */
    CHECK_EQ(g.milestone, 0);
    CHECK_EQ(g.timerequest, 1);                    /* the time is shown as a level begins */
    CHECK_EQ(g.leave, POP_LEAVE_RESTART);
    CHECK(strcmp(hooklog, "x1R") == 0);            /* the first princess scene, then the level */

    setup(3, POP_FACE_LEFT);
    g.level = 13;
    g.NextLevel = 14;
    pop_loadnextlevel(&g);
    CHECK_EQ(g.level, 14);
    CHECK_EQ(g.timerequest, 0);                    /* no clock on the last level */
    CHECK(strcmp(hooklog, "R") == 0);

    setup(3, POP_FACE_LEFT);
    g.level = 14;
    g.NextLevel = 15;                              /* no such level: this one again */
    pop_loadnextlevel(&g);
    CHECK_EQ(g.level, 14);
    CHECK_EQ(g.NextLevel, 14);
}

static void test_mainloop(void)
{
    int i;
    setup(3, POP_FACE_LEFT);                       /* quiet frames */
    g.level = 1;
    g.NextLevel = 1;
    g.VisScrn = g.cutscrn = 1;
    g.shad.face = POP_NO_CHAR;
    pop_g_jumpseq(&g, POP_SQ_STAND);
    g.kid = g.ch;
    g.MinLeft = g.SecLeft = 0xFF;
    g.ChgKidStr = 0xFF;                            /* (left over: cleared each frame) */
    for (i = 0; i < 3; i++) CHECK_EQ(pop_mainloop(&g), POP_LEAVE_NONE);
    /* input, draw, show, sounds; and in the third frame the clock's first
     * message, "60 minutes left", due at frame 0 and shown after its pause */
    CHECK(strcmp(hooklog, "sQPpsQPpsQmPp") == 0);
    CHECK_EQ(g.message, POP_MSG_TIME);
    CHECK_EQ(g.MinLeft, 0x60);
    CHECK_EQ(fc(), 3);
    CHECK_EQ(g.kid.x, 107);
    CHECK_EQ(g.unmodelled, 0);

    g.kid.life = POP_DEADENOUGH;                   /* dead; the button restarts the level */
    g.kid.posn = 185;
    g.message = POP_MSG_CONT;
    g.msgtimer = 100;
    g.BTN1 = 0xFF;
    nhooks = 0;
    CHECK_EQ(pop_mainloop(&g), POP_LEAVE_RESTART);
    CHECK(strcmp(hooklog, "sR") == 0);             /* and that frame is never drawn */

    setup(3, POP_FACE_LEFT);                       /* the level's end */
    g.level = 1;
    g.NextLevel = 2;
    g.VisScrn = g.cutscrn = 1;
    g.shad.face = POP_NO_CHAR;
    pop_g_jumpseq(&g, POP_SQ_STAND);
    g.kid = g.ch;
    g.MinLeft = g.SecLeft = 0xFF;
    CHECK_EQ(pop_mainloop(&g), POP_LEAVE_RESTART);
    CHECK(strcmp(hooklog, "sQPpyx1R") == 0);       /* drawn; copy check; scene 1; level 2 */
    CHECK_EQ(g.level, 2);

    setup(3, POP_FACE_LEFT);                       /* a key during the demo starts a game */
    g.level = 0;
    g.keypress = 0xA0;
    CHECK_EQ(pop_mainloop(&g), POP_LEAVE_RESTART);
    CHECK_EQ(g.level, 1);
    CHECK_EQ(g.SongCue, POP_SONG_DANGER);
    CHECK(strcmp(hooklog, "sR") == 0);
}

int main(void)
{
    RUN(test_keyboard);
    RUN(test_devices);
    RUN(test_press_flags);
    RUN(test_clock);
    RUN(test_showtime);
    RUN(test_messages);
    RUN(test_flashes_and_songs);
    RUN(test_frameadv);
    RUN(test_levels);
    RUN(test_mainloop);
    return pop_test_summary();
}
