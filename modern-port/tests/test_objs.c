/* test_objs.c - the object list, on synthetic data.
 *
 * New code; uses the made-up state of pop_fixture.h.  Expected values were
 * worked out by hand from CTRLSUBS.S, GAMEBG.S, TOPCTRL.S and MOVER.S (see
 * pop_objs.h for where each routine starts).  The same routines are
 * compared with the original code on random states (tests/fuzz_control.py,
 * group @objs) when the original source is available.
 */

#include <string.h>

#include "pop_fixture.h"
#include "pop_mover.h"
#include "pop_objs.h"
#include "pop_test.h"

static uint8_t mark(unsigned buffer, unsigned block) { return g.rb[buffer + block]; }

static void test_addcharobj(void)
{
    setup(3, POP_FACE_LEFT);
    g.FCharX = 100; g.FCharY = 118; g.FCharImage = 9; g.FCharTable = 2; g.FCharFace = 0xFF;
    g.FCharCU = 3; g.FCharCD = 192; g.FCharCL = 4; g.FCharCR = 36; g.FCharIndex = 13;
    pop_addcharobj(&g, POP_OBJ_KID);
    CHECK_EQ(g.objX[0], 1);
    CHECK_EQ(g.objX[1], 14); CHECK_EQ(g.objOFF[1], 2);     /* dot 100 = 14 bytes of 7 dots + 2 */
    CHECK_EQ(g.objY[1], 118); CHECK_EQ(g.objIMG[1], 9); CHECK_EQ(g.objTAB[1], 2);
    CHECK_EQ(g.objFACE[1], 0xFF); CHECK_EQ(g.objTYP[1], POP_OBJ_KID);
    CHECK_EQ(g.objCU[1], 3); CHECK_EQ(g.objCD[1], 192); CHECK_EQ(g.objCL[1], 4); CHECK_EQ(g.objCR[1], 36);
    CHECK_EQ(g.objINDX[1], 13);
    CHECK_EQ(mark(POP_RB_OBJ, 13), 1);         /* its block is marked for the drawing code */

    g.FCharX = 0xFFFF;                         /* one dot off the left edge */
    g.FCharIndex = 30;                         /* off the screen: no block to mark */
    pop_addcharobj(&g, POP_OBJ_GD);
    CHECK_EQ(g.objX[2], 0xFF); CHECK_EQ(g.objOFF[2], 6);
    CHECK_EQ(g.objINDX[2], 30);
    g.FCharX = 279;
    pop_addcharobj(&g, POP_OBJ_GD);
    CHECK_EQ(g.objX[3], 39); CHECK_EQ(g.objOFF[3], 6);
    g.FCharX = 256;                            /* the high byte: 36 bytes per 252 dots */
    pop_addcharobj(&g, POP_OBJ_GD);
    CHECK_EQ(g.objX[4], 36); CHECK_EQ(g.objOFF[4], 4);
    CHECK_EQ(g.objX[0], 4);

    g.objX[0] = POP_MAXOBJ - 1;                /* full: 19 entries */
    pop_addcharobj(&g, POP_OBJ_KID);
    CHECK_EQ(g.objX[0], POP_MAXOBJ - 1);
    pop_zeroobjs(&g);
    CHECK_EQ(g.objX[0], 0);

    /* forward is left for a character facing left */
    g.FCharX = 100; g.FCharFace = 0xFF;
    pop_addfcharx(&g, 5);
    CHECK_EQ(g.FCharX, 95);
    pop_addfcharx(&g, (uint8_t)-3);
    CHECK_EQ(g.FCharX, 98);
    g.FCharFace = 0; g.FCharX = 254;
    pop_addfcharx(&g, 5);
    CHECK_EQ(g.FCharX, 259);
    pop_addfcharx(&g, (uint8_t)-5);
    CHECK_EQ(g.FCharX, 254);
    CHECK_EQ(g.unmodelled, 0);
}

static void edges(uint8_t left, uint8_t right, uint8_t top, uint8_t bottom)
{
    g.leftblock = left; g.rightblock = right; g.topblock = top; g.bottomblock = bottom;
}

static void test_index_and_marks(void)
{
    setup(3, POP_FACE_LEFT);
    edges(3, 4, 0, 1);
    g.ch.action = 1;                           /* on the ground: by the edges of his image */
    pop_indexchar(&g);
    CHECK_EQ(g.FCharIndex, 13);
    g.ch.action = 0;                           /* otherwise by the block he is in */
    g.ch.block_x = 5; g.ch.block_y = 2;
    pop_indexchar(&g);
    CHECK_EQ(g.FCharIndex, 25);
    g.ch.posn = 140;                           /* climbing up: one block to the left */
    pop_indexchar(&g);
    CHECK_EQ(g.FCharIndex, 24);
    g.ch.posn = 3;                             /* and in frames 2, 3, 4 and 6, which start a run */
    g.ch.action = 1;
    pop_indexchar(&g);
    CHECK_EQ(g.FCharIndex, 12);
    g.ch.posn = 5;
    pop_indexchar(&g);
    CHECK_EQ(g.FCharIndex, 13);
    g.ch.action = 0; g.ch.block_y = 3;         /* below the screen: "block 30" */
    pop_indexchar(&g);
    CHECK_EQ(g.FCharIndex, 30);

    /* QUICKFLOOR: nothing while he stands or runs */
    g.ch.posn = 15; g.ch.action = 1;
    pop_quickfloor(&g);
    CHECK_EQ(mark(POP_RB_FLOOR, 13) | mark(POP_RB_FLOOR, 14) | mark(POP_RB_HALF, 13), 0);
    g.ch.action = 3;                           /* falling: the floor pieces he may pass behind */
    pop_quickfloor(&g);
    CHECK_EQ(mark(POP_RB_FLOOR, 13), 2); CHECK_EQ(mark(POP_RB_FLOOR, 14), 2);
    CHECK_EQ(mark(POP_RB_FLOOR, 3), 2); CHECK_EQ(mark(POP_RB_FLOOR, 4), 2);
    CHECK_EQ(mark(POP_RB_FLOOR, 12) | mark(POP_RB_FLOOR, 15) | mark(POP_RB_HALF, 13), 0);
    memset(g.rb, 0, sizeof g.rb);
    g.ch.action = 1; g.ch.posn = 78;           /* frames 78 and 79 on the ground count as falling */
    pop_quickfloor(&g);
    CHECK_EQ(mark(POP_RB_FLOOR, 13), 2);
    memset(g.rb, 0, sizeof g.rb);
    g.ch.posn = 80;
    pop_quickfloor(&g);
    CHECK_EQ(mark(POP_RB_FLOOR, 13), 0);
    g.ch.posn = 140;                           /* climbing up: the other buffer */
    edges(3, 3, 0xFF, 0);                      /* his head above the screen: the top line's marks */
    pop_quickfloor(&g);
    CHECK_EQ(mark(POP_RB_HALF, 3), 2);
    CHECK_EQ(mark(POP_RB_FLOOR, 3), 0);
    CHECK_EQ(mark(POP_RB_TOP, 3), 2);

    /* QUICKFG: every front piece from his feet to his head ... */
    memset(g.rb, 0, sizeof g.rb);
    edges(3, 4, 0, 1);
    g.ch.sword = 0;
    pop_quickfg(&g);
    CHECK_EQ(mark(POP_RB_FRED, 13), 3); CHECK_EQ(mark(POP_RB_FRED, 14), 3);
    CHECK_EQ(mark(POP_RB_FRED, 3), 3); CHECK_EQ(mark(POP_RB_FRED, 4), 3);
    CHECK_EQ(mark(POP_RB_FRED, 12) | mark(POP_RB_FRED, 15) | mark(POP_RB_FRED, 23), 0);
    g.ch.sword = 2;                            /* ... and one block further with a sword drawn */
    pop_quickfg(&g);
    CHECK_EQ(g.leftblock, 2);                  /* (the widening stays) */
    CHECK_EQ(mark(POP_RB_FRED, 12), 3);
    g.ch.face = POP_FACE_RIGHT;
    pop_quickfg(&g);
    CHECK_EQ(g.rightblock, 5);
    CHECK_EQ(mark(POP_RB_FRED, 15), 3);

    {
        int carry = 0;
        g.ry = 7;
        pop_markfloor(&g, 2, &carry);
        CHECK_EQ(mark(POP_RB_FLOOR, 7), 2);
        carry = 1;                             /* not a block of this screen: the top line, if a column */
        g.ry = 9;
        pop_markhalf(&g, 2, &carry);
        CHECK_EQ(mark(POP_RB_TOP, 9), 2); CHECK_EQ(mark(POP_RB_HALF, 9), 0);
        g.ry = 10;
        pop_markhalf(&g, 2, &carry);
        CHECK_EQ(mark(POP_RB_HALF, 10), 0);
        CHECK_EQ(carry, 1);
    }
    CHECK_EQ(g.unmodelled, 0);
}

static void crop_reset(void)
{
    g.FCharCU = 0; g.FCharCD = 192; g.FCharCL = 0; g.FCharCR = 40;
}

static void test_cropchar(void)
{
    setup(3, POP_FACE_LEFT);
    edges(3, 3, 0, 1);
    g.FCharY = 118; g.topej = 60;
    g.CDLeftEj = (uint8_t)(POP_SCRN_LEFT + 14 * 3 + 2);
    g.CDRightEj = (uint8_t)(POP_SCRN_LEFT + 14 * 3 + 10);
    crop_reset();
    pop_cropchar(&g);                          /* open space above: nothing is cut */
    CHECK_EQ(g.FCharCU, 0); CHECK_EQ(g.FCharCR, 40);

    put(1, 0, 3, POP_FLOOR, 0);                /* a floor over his head: cut at its underside */
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCU, 66); CHECK_EQ(g.topej, 66);
    crop_reset();
    g.topej = 40;                              /* his head is above the floor's thickness: not cut */
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCU, 0);
    g.topej = 60;
    edges(3, 4, 0, 1);                         /* the block over his right edge is open */
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCU, 0);
    g.ch.action = 0; g.ch.posn = 81;           /* unless he is jumping up to touch the ceiling */
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCU, 66);
    g.ch.action = 1; g.ch.posn = 15;
    put(1, 0, 3, POP_SPACE, 0);
    crop_reset();

    g.ch.posn = 226;                           /* on the stairs: behind the door, cut at its top */
    g.doortop = 50;
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCU, 52);
    CHECK_EQ(g.unmodelled, 0);
    g.doortop = 115;                           /* one line above his feet: still cut */
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCU, 117);
    CHECK_EQ(g.unmodelled, 0);
    g.doortop = 116;                           /* at his feet: the original calls a debugging routine */
    pop_cropchar(&g);
    CHECK_EQ(g.unmodelled, 1);
    g.unmodelled = 0;
    g.ch.posn = 15;
    crop_reset();

    edges(3, 3, 1, 1);
    put(1, 1, 3, POP_PANELWIF, 0);             /* his leading edge in a panel's block: cut on the right */
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCR, 16);
    put(1, 1, 3, POP_FLOOR, 0);
    crop_reset();
    put(1, 1, 3, POP_BLOCK, 0);                /* inside a wall, feet and head */
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCR, 12);
    crop_reset();
    edges(3, 3, 0, 1);                         /* head in the open */
    pop_cropchar(&g);
    CHECK_EQ(g.FCharCR, 40);
    put(1, 1, 3, POP_FLOOR, 0);

    put(1, 1, 3, POP_DPRESSPLATE, 0);          /* a pressed plate is a line lower */
    pop_unevenfloor(&g);
    CHECK_EQ(g.ch.y, 119);
    put(1, 1, 3, POP_FLOOR, 0);
    pop_unevenfloor(&g);
    CHECK_EQ(g.ch.y, 119);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_sword_and_star(void)
{
    setup(3, POP_FACE_LEFT);
    frame_bytes[1850 + 3 * 4] = 7;             /* sword frame 5: image 7, 2 forward, 3 up */
    frame_bytes[1850 + 3 * 4 + 1] = 2;
    frame_bytes[1850 + 3 * 4 + 2] = 0xFD;
    g.FCharX = 100; g.FCharY = 118; g.FCharFace = 0xFF; g.FCharIndex = 13;
    g.FCharCU = 5; g.FCharCD = 192; g.FCharCR = 40;
    g.Fsword = 0xC5;                           /* (the top two bits belong to the image table) */
    pop_setupsword(&g);
    CHECK_EQ(g.objX[0], 0);                    /* sword not drawn: none */
    g.ch.sword = 2;
    pop_setupsword(&g);
    CHECK_EQ(g.objX[0], 1);
    CHECK_EQ(g.objTYP[1], POP_OBJ_SWORD); CHECK_EQ(g.objIMG[1], 7); CHECK_EQ(g.objTAB[1], 2);
    CHECK_EQ(g.objX[1], 14); CHECK_EQ(g.objOFF[1], 0);       /* 98 dots */
    CHECK_EQ(g.objY[1], 115);
    CHECK_EQ(g.objCU[1], 5);                   /* cut as its bearer is */
    CHECK_EQ(g.objINDX[1], 13);
    CHECK_EQ(g.FCharY, 115);                   /* (the FChar variables stay moved) */
    g.Fsword = 0;                              /* a frame without a sword */
    pop_setupsword(&g);
    CHECK_EQ(g.objX[0], 1);
    g.Fsword = 6;                              /* an empty entry of the sword table */
    pop_setupsword(&g);
    CHECK_EQ(g.objX[0], 1);
    g.Fsword = 5; g.ch.sword = 0;
    g.ch.posn = 230;                           /* sheathing: still shown */
    pop_setupsword(&g);
    CHECK_EQ(g.objX[0], 2);
    g.ch.posn = 15; g.ch.id = 2;               /* a live guard's sword always shows */
    pop_setupsword(&g);
    CHECK_EQ(g.objX[0], 3);
    g.ch.life = 0;
    pop_setupsword(&g);
    CHECK_EQ(g.objX[0], 3);

    /* the star: 15 lines above his feet (11 for an opponent), 5 dots ahead, on top of everything */
    pop_zeroobjs(&g);
    g.ch.id = 0; g.ch.life = 0xFF;
    g.FCharX = 100; g.FCharY = 118; g.FCharImage = 9; g.FCharTable = 1; g.FCharCU = 5;
    pop_setupcomix(&g);
    CHECK_EQ(g.objX[0], 1);
    CHECK_EQ(g.objTYP[1], POP_OBJ_COMIX); CHECK_EQ(g.objINDX[1], 0xFF);
    CHECK_EQ(g.objIMG[1], 0x41); CHECK_EQ(g.objTAB[1], 0);
    CHECK_EQ(g.objY[1], 103);
    CHECK_EQ(g.objX[1], 13); CHECK_EQ(g.objOFF[1], 5);       /* dot 96: 95, made even ... */
    CHECK_EQ(g.objCU[1], 0); CHECK_EQ(g.objCR[1], 40);       /* never cut */
    CHECK_EQ(g.FCharX, 100); CHECK_EQ(g.FCharY, 118); CHECK_EQ(g.FCharImage, 9);
    CHECK_EQ(g.FCharCU, 5); CHECK_EQ(g.FCharIndex, 13);      /* the character's own set-up is kept */
    g.ch.id = 2;                               /* ... and odd for an opponent: the other colour */
    pop_setupcomix(&g);
    CHECK_EQ(g.objY[2], 107);
    CHECK_EQ(g.objX[2], 13); CHECK_EQ(g.objOFF[2], 4);
    g.ch.posn = 185;                           /* lying dead: lower */
    pop_setupcomix(&g);
    CHECK_EQ(g.objY[3], 122);
    g.ch.posn = 177;                           /* impaled: at his feet, behind */
    pop_setupcomix(&g);
    CHECK_EQ(g.objY[4], 118);
    CHECK_EQ(g.objX[4] * 7 + g.objOFF[4], 105);
    g.ch.posn = 178;                           /* cut in half: none */
    pop_setupcomix(&g);
    CHECK_EQ(g.objX[0], 4);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_addmobs(void)
{
    setup(3, POP_FACE_LEFT);
    g.VisScrn = 1; g.scrnBelow = 2;
    g.nummob = 2;
    g.mobx[1] = 12; g.moby[1] = 100; g.mobscrn[1] = 1; g.mobtype[1] = 0;
    g.mobx[2] = 20; g.moby[2] = 70; g.mobscrn[2] = 1; g.mobtype[2] = 0;
    g.objCD[1] = 77;                           /* what the slot's last user left */
    pop_addmobs(&g);
    CHECK_EQ(g.objX[0], 2);                    /* the list is walked from its end */
    CHECK_EQ(g.objTYP[1], POP_OBJ_FF); CHECK_EQ(g.objX[1], 20); CHECK_EQ(g.objY[1], 70);
    CHECK_EQ(g.objIMG[1], 10); CHECK_EQ(g.objOFF[1], 0);
    CHECK_EQ(g.objCU[1], 0); CHECK_EQ(g.objCL[1], 0); CHECK_EQ(g.objCR[1], 40);
    CHECK_EQ(g.objCD[1], 77);                  /* the bottom cut is not set */
    CHECK_EQ(g.objINDX[1], 15); CHECK_EQ(mark(POP_RB_OBJ, 15), 1);
    /* the pieces of the block to the right, and of the one above when it reaches up there */
    CHECK_EQ(mark(POP_RB_FLOOR, 16), 2); CHECK_EQ(mark(POP_RB_FRED, 16), 2);
    CHECK_EQ(mark(POP_RB_FLOOR, 6), 2); CHECK_EQ(mark(POP_RB_FRED, 6), 2);
    CHECK_EQ(g.objINDX[2], 13);
    CHECK_EQ(mark(POP_RB_FLOOR, 14), 2); CHECK_EQ(mark(POP_RB_FLOOR, 4), 0);

    pop_zeroobjs(&g);
    g.nummob = 1;
    g.moby[1] = 209;                           /* below the bottom edge by more than its height */
    pop_addmobs(&g);
    CHECK_EQ(g.objX[0], 0);
    g.moby[1] = 208;
    pop_addmobs(&g);
    CHECK_EQ(g.objX[0], 1);
    pop_zeroobjs(&g);
    g.mobscrn[1] = 2;                          /* on the screen below, still reaching up into this one */
    g.moby[1] = 5;
    pop_addmobs(&g);
    CHECK_EQ(g.objX[0], 1);
    CHECK_EQ(g.objY[1], 197);
    CHECK_EQ(g.moby[1], 5);                    /* (the list keeps its own value) */
    pop_zeroobjs(&g);
    g.moby[1] = 17;
    pop_addmobs(&g);
    CHECK_EQ(g.objX[0], 0);
    g.mobscrn[1] = 3;                          /* elsewhere */
    g.moby[1] = 5;
    pop_addmobs(&g);
    CHECK_EQ(g.objX[0], 0);
    g.mobscrn[1] = 1; g.moby[1] = 100; g.mobtype[1] = 1;     /* not a floor */
    pop_addmobs(&g);
    CHECK_EQ(g.objX[0], 0);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_addchars(void)
{
    setup(3, POP_FACE_LEFT);
    g.VisScrn = 1;
    g.kid = g.ch;
    g.shad.face = 86;                          /* no opponent */
    pop_addchars(&g);
    CHECK_EQ(g.objX[0], 1);
    CHECK_EQ(g.objTYP[1], POP_OBJ_KID);
    CHECK_EQ(g.objY[1], 118); CHECK_EQ(g.objIMG[1], 1); CHECK_EQ(g.objFACE[1], POP_FACE_LEFT);
    CHECK_EQ(g.objINDX[1], 13);
    CHECK_EQ(mark(POP_RB_FRED, 13), 3);        /* front pieces from his feet to his head */
    CHECK_EQ(mark(POP_RB_RED, 20), 0);         /* no strength changed: the meters stay */

    pop_zeroobjs(&g);
    g.ChgKidStr = 0xFF;                        /* hurt: a star, and his meter redrawn */
    pop_addchars(&g);
    CHECK_EQ(g.objX[0], 2);
    CHECK_EQ(g.objTYP[2], POP_OBJ_COMIX);
    CHECK_EQ(mark(POP_RB_RED, 20), 2); CHECK_EQ(mark(POP_RB_RED, 22), 2);
    CHECK_EQ(mark(POP_RB_RED, 28), 0);
    g.ChgKidStr = 0;

    pop_zeroobjs(&g);
    g.shad = g.ch;                             /* a guard on the same screen: listed first */
    g.shad.id = 2;
    g.shad.x = (uint8_t)(g.ch.x - 28);
    g.shad.block_x = 1;
    pop_addchars(&g);
    CHECK_EQ(g.objX[0], 2);
    CHECK_EQ(g.objTYP[1], POP_OBJ_GD); CHECK_EQ(g.objTYP[2], POP_OBJ_KID);
    pop_zeroobjs(&g);
    g.shad.id = 1;                             /* the shadow */
    pop_addchars(&g);
    CHECK_EQ(g.objTYP[1], POP_OBJ_SHAD);
    CHECK_EQ(g.objCL[1], 0);
    pop_zeroobjs(&g);
    g.level = 4; g.VisScrn = 4;                /* by the mirror of level 4 it is cut at the mirror */
    g.shad.scrn = 4; g.kid.scrn = 4;
    pop_addchars(&g);
    CHECK_EQ(g.objTYP[1], POP_OBJ_SHAD);
    CHECK_EQ(g.objCL[1], 17);

    pop_zeroobjs(&g);
    g.level = 1; g.VisScrn = 1; g.kid.scrn = 1;
    g.shad.scrn = 2;                           /* not in sight */
    g.kid.posn = 0;                            /* "frame 0": he is not drawn */
    pop_addchars(&g);
    CHECK_EQ(g.objX[0], 0);
    g.kid.posn = 15; g.kid.scrn = 2;
    pop_addchars(&g);
    CHECK_EQ(g.objX[0], 0);
}

int main(void)
{
    RUN(test_addcharobj);
    RUN(test_index_and_marks);
    RUN(test_cropchar);
    RUN(test_sword_and_star);
    RUN(test_addmobs);
    RUN(test_addchars);
    return pop_test_summary();
}
