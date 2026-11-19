/* test_world.c - world-query routines on a synthetic level.
 *
 * New code; the level and frame table below were made up for these tests.
 * The same routines are compared with the original code on random states by
 * tests/fuzz_world.py when the original source is available.
 */

#include <string.h>

#include "pop_seq.h"
#include "pop_test.h"
#include "pop_world.h"

static pop_game g;
static uint8_t frame_bytes[1200 + 200 + 450];
static pop_frametab frames;

static void put(int scrn, int block, uint8_t type, uint8_t spec)
{
    g.blueprint[POP_BLUETYPE + (scrn - 1) * 30 + block] = type;
    g.blueprint[POP_BLUESPEC + (scrn - 1) * 30 + block] = spec;
}

static void map(int scrn, uint8_t l, uint8_t r, uint8_t u, uint8_t d)
{
    uint8_t *m = g.blueprint + POP_MAP + (scrn - 1) * 4;
    m[0] = l; m[1] = r; m[2] = u; m[3] = d;
}

static void setup(void)
{
    size_t i;
    memset(&g, 0, sizeof g);
    for (i = 0; i < sizeof frame_bytes; i++) frame_bytes[i] = (uint8_t)(i * 7u + 3u);
    frames.data = frame_bytes;
    frames.size = sizeof frame_bytes;
    frames.origin = 0x2800;
    frames.fdef = 0x2800;
    frames.altset1 = 0x2800 + 1200;
    frames.altset2 = 0x2800 + 1400;
    frames.swordtab = 0x2800 + 1850;
    g.frames = &frames;

    map(1, 0, 2, 3, 0);                  /* screen 1: right 2, above 3 */
    map(2, 1, 0, 0, 0);
    map(3, 0, 0, 0, 1);
    put(1, 14, POP_FLOOR | 0x20, 2);     /* row 1, column 4; flag bit must be masked */
    put(1, 15, POP_GATE, 188);
    put(1, 13, POP_PANELWIF, 1);
    put(1, 4, POP_TORCH, 0);             /* row 0, column 4 */
    put(2, 10, POP_SPIKES, 0);           /* screen 2, row 1, column 0 */
    put(3, 24, POP_LOOSE, 0);            /* screen 3, row 2, column 4 */

    g.ch.scrn = 1;
    g.ch.block_x = 4;
    g.ch.block_y = 1;
    g.ch.face = POP_FACE_LEFT;
}

static void test_map(void)
{
    setup();
    CHECK_EQ(pop_getright(&g, 1), 2);
    CHECK_EQ(pop_getleft(&g, 1), 0);
    CHECK_EQ(pop_getup(&g, 1), 3);
    CHECK_EQ(pop_getdown(&g, 3), 1);
    CHECK_EQ(pop_getleft(&g, 0), 0);                 /* screen 0 has no neighbours */
}

static void test_rdblock(void)
{
    setup();
    CHECK_EQ(pop_rdblock(&g, 1, 4, 1), POP_FLOOR);
    CHECK_EQ(g.rx, POP_FLOOR);
    CHECK_EQ(g.ry, 14);
    CHECK_EQ(g.blue, 1);
    CHECK_EQ(pop_bluespec(&g, g.ry), 2);
    CHECK_EQ(pop_bluetype(&g, g.ry), POP_FLOOR | 0x20);

    CHECK_EQ(pop_rdblock(&g, 1, 10, 1), POP_SPIKES); /* one column past the right edge */
    CHECK_EQ(g.tempscrn, 2);
    CHECK_EQ(g.tempblockx, 0);
    CHECK_EQ(g.ry, 10);

    CHECK_EQ(pop_rdblock(&g, 1, 4, 0xFF), POP_LOOSE); /* row -1: bottom row of screen above */
    CHECK_EQ(g.tempscrn, 3);
    CHECK_EQ(g.tempblocky, 2);
    CHECK_EQ(g.ry, 24);

    CHECK_EQ(pop_rdblock(&g, 1, 0xFF, 1), POP_BLOCK); /* no screen to the left: solid */
    CHECK_EQ(g.tempscrn, 0);
    CHECK_EQ(g.tempblockx, 9);
    CHECK_EQ(g.rx, POP_BLOCK);
    CHECK_EQ(g.ry, 1);                               /* Y is left as the caller passed it */

    CHECK_EQ(pop_rdblock(&g, 1, 4, 3), POP_BLOCK);   /* below, no screen */
    CHECK_EQ(pop_rdblock(&g, 0, 4, 1), POP_BLOCK);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_around_character(void)
{
    setup();
    CHECK_EQ(pop_getunderft(&g), POP_FLOOR);
    CHECK_EQ(pop_getinfront(&g), POP_PANELWIF);      /* facing left: column 3 */
    CHECK_EQ(g.infrontx, 3);
    CHECK_EQ(pop_getbehind(&g), POP_GATE);
    CHECK_EQ(g.behindx, 5);
    CHECK_EQ(pop_bluespec(&g, g.ry), 188);
    CHECK_EQ(pop_getabove(&g), POP_TORCH);
    CHECK_EQ(g.abovey, 0);
    CHECK_EQ(pop_get2infront(&g), POP_SPACE);

    g.ch.face = POP_FACE_RIGHT;
    CHECK_EQ(pop_getinfront(&g), POP_GATE);
    CHECK_EQ(g.infrontx, 5);
    CHECK_EQ(pop_getbehind(&g), POP_PANELWIF);
    CHECK_EQ(pop_getaboveinf(&g), POP_SPACE);
    CHECK_EQ(pop_getabovebeh(&g), POP_SPACE);
    CHECK_EQ(g.unmodelled, 0);

    g.ch.face = 0x40;                                /* not a facing the tables cover */
    (void)pop_getbehind(&g);
    CHECK_EQ(g.unmodelled, 1);
}

static void test_coordinates(void)
{
    setup();
    CHECK_EQ(pop_getblockx(&g, 58), 0);              /* screen's left edge starts column 0 */
    CHECK_EQ(g.OFFSET, 0);
    CHECK_EQ(pop_getblockx(&g, 57), 0xFF);           /* column -1 */
    CHECK_EQ(g.OFFSET, 13);
    CHECK_EQ(pop_getblockx(&g, 58 + 14 * 9 + 13), 9);
    CHECK_EQ(pop_getblockx(&g, 0), 0xFB);            /* -5 */
    CHECK_EQ(pop_getblockx(&g, 255), 14);
    CHECK_EQ(pop_getblockxp(&g, 65), 0);             /* centre plane: 7 units further right */
    CHECK_EQ(pop_getblockxp(&g, 64), 0xFF);

    CHECK_EQ(pop_getblockej(&g, 0), 58);
    CHECK_EQ(pop_getblockej(&g, 9), 58 + 9 * 14);
    CHECK_EQ(pop_getblockej(&g, 0xFB), 0xF4);        /* block -5: -12 */
    CHECK_EQ(pop_getblockej(&g, 14), (uint8_t)(58 + 14 * 14));

    CHECK_EQ(pop_getblocky(2), 0xFF);
    CHECK_EQ(pop_getblocky(3), 0);
    CHECK_EQ(pop_getblocky(65), 0);
    CHECK_EQ(pop_getblocky(66), 1);
    CHECK_EQ(pop_getblocky(191), 2);
    CHECK_EQ(pop_getblocky(192), 3);
    CHECK_EQ(pop_getblockyp(54), 0xFF);
    CHECK_EQ(pop_getblockyp(55), 0);
    CHECK_EQ(pop_getblockyp(118), 1);
    CHECK_EQ(pop_getblockyp(181), 2);
    CHECK_EQ(pop_getblockyp(244), 3);
}

static void test_foot_and_distance(void)
{
    setup();
    g.ch.x = 120;
    g.Fdx = 2;
    g.Fcheck = 0x40 | 5;                             /* foot 5 units in */
    CHECK_EQ(pop_getbasex(&g), 120 - 2 + 5);         /* facing left */
    /* foot at 123: centre plane 116 -> column 4 (114..127), pixel 2 */
    CHECK_EQ(pop_getdist(&g), 2);
    pop_getbaseblock(&g);
    CHECK_EQ(g.ch.block_x, 4);
    g.ch.face = POP_FACE_RIGHT;
    CHECK_EQ(pop_getbasex(&g), 120 + 2 - 5);
    CHECK_EQ(pop_getdist(&g), 13 - 10);              /* 117 -> 110: column 3, pixel 10 */
    CHECK_EQ(pop_getdist1(&g, 65), 13);
    g.ch.face = 0x01;                                /* only exactly 0 counts as right */
    CHECK_EQ(pop_getdist1(&g, 65), 0);
}

static void test_facedx(void)
{
    setup();
    CHECK_EQ(pop_facedx(&g, 5), 5);                  /* facing left: unchanged */
    g.ch.face = POP_FACE_RIGHT;
    CHECK_EQ(pop_facedx(&g, 5), 0xFB);               /* facing right: negated */
    CHECK_EQ(pop_facedx(&g, 0), 0);
}

static void test_piece_classes(void)
{
    setup();
    CHECK_EQ(pop_cmpspace(POP_SPACE), 0);
    CHECK_EQ(pop_cmpspace(POP_BLOCK), 0);            /* a solid block has no floor */
    CHECK_EQ(pop_cmpspace(POP_PILLARTOP), 0);
    CHECK_EQ(pop_cmpspace(POP_PANELWOF), 0);
    CHECK_EQ(pop_cmpspace(POP_ARCHTOP1), 0);
    CHECK_EQ(pop_cmpspace(POP_ARCHBOT), 1);
    CHECK_EQ(pop_cmpspace(POP_FLOOR), 1);
    CHECK_EQ(pop_cmpspace(POP_GATE), 1);

    CHECK_EQ(pop_cmpbarr(POP_GATE), 1);
    CHECK_EQ(pop_cmpbarr(POP_PANELWIF), 1);
    CHECK_EQ(pop_cmpbarr(POP_MIRROR), 3);
    CHECK_EQ(pop_cmpbarr(POP_SLICER), 3);
    CHECK_EQ(pop_cmpbarr(POP_BLOCK), 4);
    CHECK_EQ(pop_cmpbarr(POP_FLOOR), 0);

    CHECK_EQ(pop_cmpwall(&g, POP_BLOCK), 0);
    CHECK_EQ(pop_cmpwall(&g, POP_PANELWIF), 0);      /* facing left: panels are walls */
    g.ch.face = POP_FACE_RIGHT;
    CHECK_EQ(pop_cmpwall(&g, POP_PANELWIF), 1);
    CHECK_EQ(pop_cmpwall(&g, POP_BLOCK), 0);
    CHECK_EQ(pop_cmpwall(&g, POP_GATE), 1);
}

static uint8_t pattern(unsigned offset) { return (uint8_t)(offset * 7u + 3u); }

static void test_frame_info(void)
{
    setup();
    g.ch.posn = 15;
    g.ch.id = 0;
    pop_getframeinfo(&g);
    CHECK_EQ(g.framepoint, 0x2800 + 70);
    CHECK_EQ(g.Fimage, pattern(70));
    CHECK_EQ(g.Fsword, pattern(71));
    CHECK_EQ(g.Fdx, pattern(72));
    CHECK_EQ(g.Fdy, pattern(73));
    CHECK_EQ(g.Fcheck, pattern(74));

    g.ch.posn = 160;                                 /* 150-189: opponents use altset1 */
    g.ch.id = 1;
    pop_getframeinfo(&g);
    CHECK_EQ(g.framepoint, 0x2800 + 1200 + 10 * 5);
    g.ch.id = 0;
    pop_getframeinfo(&g);
    CHECK_EQ(g.framepoint, 0x2800 + 159 * 5);

    g.ch.posn = 104;                                 /* guards: 102-106 become 172-176 */
    g.ch.id = 2;
    pop_getframeinfo(&g);
    CHECK_EQ(g.framepoint, 0x2800 + 1200 + (174 - 150) * 5);
    g.ch.id = 1;                                     /* the shadow does not get that */
    pop_getframeinfo(&g);
    CHECK_EQ(g.framepoint, 0x2800 + 103 * 5);
    g.ch.posn = 101;
    g.ch.id = 2;
    pop_getframeinfo(&g);
    CHECK_EQ(g.framepoint, 0x2800 + 100 * 5);

    g.ch.posn = 3;                                   /* princess and vizier: altset2 */
    g.ch.id = 5;
    pop_getframeinfo(&g);
    CHECK_EQ(g.framepoint, 0x2800 + 1400 + 2 * 5);
    g.ch.id = 24;                                    /* the mouse uses the main set */
    pop_getframeinfo(&g);
    CHECK_EQ(g.framepoint, 0x2800 + 2 * 5);
    CHECK_EQ(g.unmodelled, 0);

    g.ch.posn = 200;                                 /* altset2 has 90 slots */
    g.ch.id = 5;
    pop_getframeinfo(&g);
    CHECK_EQ(g.unmodelled, 1);
    CHECK_EQ(g.Fimage, 0);
}

static void test_rereadblocks(void)
{
    setup();
    g.ch.posn = 15;
    g.ch.x = 130;
    g.ch.block_x = 99;
    pop_rereadblocks(&g);
    /* frame 15 of the synthetic table: Fdx = pattern(72), Fcheck = pattern(74) */
    {
        uint8_t fdx = pattern(72), foot = pattern(74) & 0x1F;
        uint8_t base = (uint8_t)(130u - (uint8_t)(fdx - foot));
        CHECK_EQ(g.ch.block_x, (uint8_t)(((uint8_t)(base - 7u) + 12u) / 14u - 5u));
    }
}

static void test_gravity_and_fall(void)
{
    setup();
    g.ch.action = 1;                                 /* not falling: velocity untouched */
    g.ch.y_vel = 5;
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 5);

    g.ch.action = 4;
    g.ch.y_vel = 0;
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 3);
    g.ch.y_vel = 31;
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 33);                        /* terminal velocity */
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 33);
    g.ch.y_vel = 0xFE;                               /* -2: wraps to +1 */
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 1);
    g.ch.y_vel = 0xF0;                               /* -16: the unsigned test clamps it */
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 33);

    g.ag.weightless = 1;
    g.ch.y_vel = 0;
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 1);
    g.ch.y_vel = 3;
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 4);
    pop_gravity(&g);
    CHECK_EQ(g.ch.y_vel, 4);

    setup();
    g.ch.posn = 15;
    g.ch.x = 130;
    g.ch.y = 100;
    g.ch.y_vel = 7;
    g.ch.x_vel = 2;
    g.ch.action = 1;                                 /* y moves even when not falling */
    pop_addfall(&g);
    CHECK_EQ(g.ch.y, 107);
    CHECK_EQ(g.ch.x, 130);
    g.ch.action = 4;
    g.ch.y_vel = 0xFD;
    pop_addfall(&g);
    CHECK_EQ(g.ch.y, 104);
    CHECK_EQ(g.ch.x, 128);                           /* facing left: forward is -x */
    CHECK_EQ(g.Fimage, pattern(70));                 /* block info was re-read */
}

static void test_records(void)
{
    setup();
    g.kid.x = 11; g.kid.seq = 0x3123;
    g.shad.x = 22; g.shad.seq = 0x3456;
    pop_loadkidwop(&g);
    CHECK_EQ(g.ch.x, 11);
    CHECK_EQ(g.ch.seq, 0x3123);
    CHECK_EQ(g.op.x, 22);
    g.ch.x = 12;
    g.op.x = 23;
    pop_savekidwop(&g);
    CHECK_EQ(g.kid.x, 12);
    CHECK_EQ(g.shad.x, 23);
    pop_loadshadwop(&g);
    CHECK_EQ(g.ch.x, 23);
    CHECK_EQ(g.op.x, 12);
    g.ch.x = 24;
    pop_saveshad(&g);
    CHECK_EQ(g.shad.x, 24);
    pop_loadkid(&g);
    CHECK_EQ(g.ch.x, 12);
    g.ch.x = 13;
    pop_savekid(&g);
    pop_loadshad(&g);
    CHECK_EQ(g.ch.x, 24);
    pop_saveshadwop(&g);
    CHECK_EQ(g.kid.x, 12);                           /* Op still held the kid from before */
}

static void test_unmodelled_requests(void)
{
    setup();
    pop_calcblue(&g, 40);                            /* beyond the 32-entry table */
    CHECK_EQ(g.unmodelled, 1);
    CHECK_EQ(pop_bluetype(&g, 0), 0);
    g.unmodelled = 0;
    (void)pop_getblockej(&g, 40);                    /* past the tables that follow */
    CHECK_EQ(g.unmodelled, 1);
}

int main(void)
{
    RUN(test_map);
    RUN(test_rdblock);
    RUN(test_around_character);
    RUN(test_coordinates);
    RUN(test_foot_and_distance);
    RUN(test_facedx);
    RUN(test_piece_classes);
    RUN(test_frame_info);
    RUN(test_rereadblocks);
    RUN(test_gravity_and_fall);
    RUN(test_records);
    RUN(test_unmodelled_requests);
    return pop_test_summary();
}
