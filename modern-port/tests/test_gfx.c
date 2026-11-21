/* test_gfx.c - the display lists and the redraw of a screen, on made-up
 * piece tables, images and a made-up level.
 *
 * New code; no original data.  Expected values were worked out by hand from
 * GRAFIX.S and FRAMEADV.S (see pop_gfx.h for where each routine starts).
 * With the original source available, tests/compare_rooms.py compares the
 * same code with the original's drawing, screen by screen.
 */

#include <string.h>

#include "pop_gfx.h"
#include "pop_loop.h"
#include "pop_test.h"
#include "pop_top.h"
#include "pop_world.h"

static pop_game g;
static pop_gfx v;
static pop_bgdata bg;
static uint8_t tables[44 * 32];
/* A made-up image table in the file format: count, pointers from $6000,
 * images.  Image 1: 1 x 1, dot 0.  Image 2: 2 x 1, all dots.  Image 3: 1 x 2. */
static const uint8_t images[] = {
    3, 0x09, 0x60, 0x0C, 0x60, 0x10, 0x60, 0x14, 0x60,
    1, 1, 0x81,
    2, 1, 0xFF, 0xFF,
    1, 2, 0x82, 0x84,
};

/* Every table gets 32 bytes of zeros; the tests set the entries they use. */
static void make_tables(void)
{
    uint16_t *offsets[] = {
        &bg.piecea, &bg.pieceay, &bg.maska, &bg.pieceb, &bg.pieceby, &bg.bstripe, &bg.piecec,
        &bg.pieced, &bg.fronti, &bg.fronty, &bg.frontx, &bg.maskb, &bg.blockb, &bg.blockc,
        &bg.blockd, &bg.blockfr, &bg.panelb, &bg.panelc, &bg.spaceb, &bg.spaceby, &bg.floorb,
        &bg.floorby, &bg.loosea, &bg.loosed, &bg.looseby, &bg.spikea, &bg.spikeb,
        &bg.slicerseq, &bg.slicertop, &bg.slicerbot, &bg.slicerbot2, &bg.slicergap,
        &bg.slicerfrnt, &bg.gate8b, &bg.gate8c, &bg.torchflame, &bg.bubble, &bg.bline,
        &bg.level_bgset1, &bg.level_bgset2, &bg.level_chset,
    };
    unsigned i;
    memset(&bg, 0, sizeof bg);
    memset(tables, 0, sizeof tables);
    for (i = 0; i < sizeof offsets / sizeof offsets[0]; i++) *offsets[i] = (uint16_t)(32 * i);
    bg.image = tables;
    bg.size = sizeof tables;
    bg.numblox = 2; bg.numpans = 3; bg.numbpans = 3;
    bg.panelb0 = 0x70; bg.panelc0 = 0x71;      /* (numbers no test image uses) */
    bg.gmaxval = 188; bg.Ffalling = 10; bg.spikeExt = 5; bg.slicerRet = 6;
    bg.gatecmask = 0x21; bg.gatebotSTA = 0x22; bg.gatebotORA = 0x23; bg.gateB1 = 0x24;
    bg.stairs = 0x25; bg.door = 0x26; bg.doormask = 0x27; bg.toprepair = 0x28;
    bg.looseb = 0x29; bg.specialflask = 0x2A; bg.archpanel = 0x2B;
    bg.swordgleam0 = 0x2C; bg.swordgleam1 = 0x2D;
    bg.CUmask = 0x2E; bg.CUpiece = 0x2F; bg.CUpost = 0x38; bg.bullet = 0x88; bg.blank = 0x8C;
}

static void put(int scrn, int row, int col, uint8_t type, uint8_t spec)
{
    g.blueprint[POP_BLUETYPE + (scrn - 1) * 30 + row * 10 + col] = type;
    g.blueprint[POP_BLUESPEC + (scrn - 1) * 30 + row * 10 + col] = spec;
}

/* One screen with no neighbours, all empty space. */
static void setup(void)
{
    memset(&g, 0, sizeof g);
    make_tables();
    pop_gfx_init(&v);
    v.bg = &bg;
    v.bgtable[0].data = images;
    v.bgtable[0].size = sizeof images;
    v.chtable[0] = v.bgtable[0];
    g.level = 1;
    g.VisScrn = g.SCRNUM = 1;
    pop_getscrns(&g);
    tables[bg.blockb + 0] = 8;                 /* the wall that an absent left screen stands for */
    tables[bg.pieceby + POP_BLOCK] = 0xFD;     /* drawn 3 lines above the floor line */
}

static void test_images(void)
{
    pop_imgtab t = { images, sizeof images };
    pop_image im = pop_imgtab_image(&t, 2);
    CHECK(im.bytes == images + 12);
    CHECK_EQ(im.bytes[0], 2);
    CHECK(pop_imgtab_image(&t, 0).bytes == NULL);          /* numbers start at 1 */
    CHECK(pop_imgtab_image(&t, 200).bytes == NULL);
}

static void test_adding(void)
{
    int i;
    setup();
    v.XCO = 8; v.YCO = 100; v.IMAGE = 0x85; v.OPACITY = POP_OP_ORA;
    pop_addback(&v);
    CHECK_EQ(v.bgX[0], 1);
    CHECK_EQ(v.bgX[1], 8);
    CHECK_EQ(v.bgY[1], 100);
    CHECK_EQ(v.bgIMG[1], 0x85);
    CHECK_EQ(v.bgOP[1], POP_OP_ORA);
    v.YCO = 192;                               /* off the bottom, or wrapped past the top: dropped */
    pop_addback(&v);
    pop_addfore(&v);
    CHECK_EQ(v.bgX[0], 1);
    CHECK_EQ(v.fgX[0], 0);
    v.YCO = 5;
    for (i = 0; i < 300; i++) pop_addback(&v); /* the list holds 199 */
    CHECK_EQ(v.bgX[0], POP_MAXBACK - 1);

    v.OFFSET = 3; v.TABLE = 2;
    pop_addmidezo(&v, POP_MID_LAY);            /* a piece: the whole screen is its area */
    CHECK_EQ(v.midX[0], 1);
    CHECK_EQ(v.midOFF[1], 3);
    CHECK_EQ(v.midCL[1], 0);
    CHECK_EQ(v.midCR[1], 40);
    CHECK_EQ(v.midCU[1], 0);
    CHECK_EQ(v.midCD[1], 192);
    g.FCharFace = 0x00;                        /* a character facing right is mirrored */
    g.FCharCL = 4; g.FCharCR = 36; g.FCharCU = 10; g.FCharCD = 180;
    v.OPACITY = POP_OP_MASK;
    pop_addmid(&v, &g, POP_MID_LAYSAVE | POP_MID_CHARTABLE);
    CHECK_EQ(v.midOP[2], POP_OP_MASK | POP_OP_MIRROR);
    CHECK_EQ(v.midCL[2], 4);
    CHECK_EQ(v.midCD[2], 180);
    CHECK_EQ(v.midTAB[2], 2);
    g.FCharFace = 0xFF;                        /* facing left: as stored */
    pop_addmid(&v, &g, POP_MID_LAYSAVE | POP_MID_CHARTABLE);
    CHECK_EQ(v.midOP[3], POP_OP_MASK);

    v.width = 4; v.IMAGE = 20;
    pop_addwipe(&v, 0x80);
    CHECK_EQ(v.wipeX[0], 1);
    CHECK_EQ(v.wipeW[1], 4);
    CHECK_EQ(v.wipeCOL[1], 0x80);

    pop_zerolsts(&v);                          /* a new frame: all lists but the peel lists */
    CHECK_EQ(v.bgX[0] | v.fgX[0] | v.midX[0] | v.wipeX[0] | v.msgX[0], 0);
}

static void test_peel_lists(void)
{
    setup();
    v.hr.PEELIMG = 0;                          /* nothing was saved: nothing listed */
    pop_addpeel(&v);
    CHECK_EQ(v.peelX[0], 0);
    v.hr.PEELIMG = 0xD012; v.hr.PEELXCO = 7; v.hr.PEELYCO = 99;
    pop_addpeel(&v);                           /* page 1: the first list */
    CHECK_EQ(v.peelX[0], 1);
    CHECK_EQ(v.peelX[1], 7);
    CHECK_EQ(v.peelY[1], 99);
    CHECK_EQ(v.peelIMGL[1], 0x12);
    CHECK_EQ(v.peelIMGH[1], 0xD0);
    v.hr.PAGE = 0x20;                          /* page 2: the second, 46 entries on */
    pop_addpeel(&v);
    CHECK_EQ(v.peelX[POP_MAXPEEL], 1);
    CHECK_EQ(v.peelX[POP_MAXPEEL + 1], 7);
    pop_zeropeel(&v);                          /* emptied per page, with its buffer */
    CHECK_EQ(v.peelX[POP_MAXPEEL], 0);
    CHECK_EQ(v.peelX[0], 1);
    CHECK_EQ(v.hr.PEELBUF, POP_PEELBUF2);

    /* The copy protection: on a palace level DRAWALL clears the first
     * entry of a two-entry table, and page 2's list is found through
     * table[purpleflag].  With the flag at 1 nothing changes ... */
    g.BGset1 = 1;
    pop_drawall(&v, &g);
    v.hr.PEELIMG = 0xD012;
    pop_addpeel(&v);
    CHECK_EQ(v.peelX[POP_MAXPEEL], 1);
    v.purpleflag = 0;                          /* ... with it at 0, page 2's saves land in page 1's list */
    pop_addpeel(&v);
    CHECK_EQ(v.peelX[POP_MAXPEEL], 1);
    CHECK_EQ(v.peelX[0], 2);
    CHECK_EQ(v.unmodelled, 0);
}

static void test_drawall(void)
{
    setup();
    v.genCLS = 1;
    v.XCO = 3; v.YCO = 10; v.IMAGE = 2; v.OPACITY = POP_OP_STA;      /* background: all dots */
    pop_addback(&v);
    v.IMAGE = 1; v.OPACITY = POP_OP_EOR;                             /* foreground, drawn after: */
    pop_addfore(&v);                                                 /* XOR takes dot 0 and bit 7 off */
    v.XCO = 20; v.YCO = 50; v.IMAGE = 3; v.OFFSET = 0; v.TABLE = 0; v.OPACITY = POP_OP_STA;
    g.FCharFace = 0xFF; g.FCharCR = 40; g.FCharCD = 192;
    pop_addmid(&v, &g, POP_MID_LAYSAVE | POP_MID_CHARTABLE);         /* a "character" */
    pop_drawall(&v, &g);
    CHECK_EQ(pop_hr_row(&v.hr, 1, 10)[3], 0x7E);
    CHECK_EQ(pop_hr_row(&v.hr, 1, 10)[4], 0xFF);
    CHECK_EQ(pop_hr_row(&v.hr, 1, 0)[0], 0x80);            /* the page was cleared first */
    CHECK_EQ(pop_hr_row(&v.hr, 1, 50)[20], 0x82);
    CHECK_EQ(pop_hr_row(&v.hr, 1, 49)[20], 0x84);
    CHECK_EQ(v.peelX[0], 1);                               /* what was under it is on the peel list */
    CHECK_EQ(v.unmodelled, 0);

    pop_zerolsts(&v);                          /* next frame on this page: nothing new to draw; */
    pop_drawall(&v, &g);                       /* the character is taken off again */
    CHECK_EQ(pop_hr_row(&v.hr, 1, 50)[20], 0x80);
    CHECK_EQ(pop_hr_row(&v.hr, 1, 10)[4], 0xFF);           /* the background stays */
    CHECK_EQ(v.peelX[0], 0);

    setup();                                   /* an image number the table does not have */
    v.XCO = 3; v.YCO = 10; v.IMAGE = 9;
    pop_addback(&v);
    pop_drawall(&v, &g);
    CHECK_EQ(v.unmodelled, 1);
}

/* DRAWALL passes its parameters to the blitter through the variables the
 * adding routines use, and leaves them there (the height and width of a
 * wipe are IMAGE and the byte after it). */
static void test_drawall_leaves(void)
{
    setup();
    v.XCO = 8; v.YCO = 100; v.IMAGE = 20; v.width = 4;
    pop_addwipe(&v, 0x80);
    v.IMAGE = 33;                              /* (as if a second wipe had not fitted in the list) */
    pop_drawall(&v, &g);
    CHECK_EQ(v.IMAGE, 20);
    CHECK_EQ(v.width, 4);
    CHECK_EQ(v.OPACITY, 0x80);
    CHECK_EQ(v.XCO, 8); CHECK_EQ(v.YCO, 100);
    pop_zerolsts(&v);
    v.XCO = 3; v.YCO = 10; v.IMAGE = 0x81; v.OPACITY = POP_OP_ORA;   /* image 1 of the second table */
    v.bgtable[1] = v.bgtable[0];
    pop_addback(&v);
    v.IMAGE = 2; v.OFFSET = 5; v.YCO = 30; v.OPACITY = POP_OP_STA;
    pop_addmsg(&v);
    v.OFFSET = 0;
    pop_drawall(&v, &g);
    CHECK_EQ(v.IMAGE, 2);
    CHECK_EQ(v.OFFSET, 5);
    CHECK_EQ(v.YCO, 30);
    CHECK_EQ(v.OPACITY, POP_OP_STA);
    pop_zerolsts(&v);
    v.IMAGE = 0x81; v.YCO = 10; v.OPACITY = POP_OP_ORA;
    pop_addback(&v);
    v.IMAGE = 0;
    pop_drawall(&v, &g);
    CHECK_EQ(v.IMAGE, 1);                      /* without the table bit */
    CHECK_EQ(v.TABLE, 0);
    CHECK_EQ(v.unmodelled, 0);
}

static void test_empty_screen(void)
{
    int i;
    setup();
    tables[bg.piecea + POP_FLOOR] = 5;
    tables[bg.pieced + POP_FLOOR] = 6;
    put(1, 1, 2, POP_FLOOR, 0);                /* one floor piece: row 1, column 2 */
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    CHECK_EQ(v.genCLS, 1);
    CHECK_EQ(v.bgX[0], 15);
    CHECK_EQ(v.fgX[0], 0);
    /* rows from the bottom up; in each, the wall to the left shows its B-section */
    CHECK_EQ(v.bgIMG[1], 8); CHECK_EQ(v.bgX[1], 0); CHECK_EQ(v.bgY[1], 188 - 3);
    CHECK_EQ(v.bgIMG[2], 8); CHECK_EQ(v.bgY[2], 125 - 3);
    /* the floor piece: its front edge (D), then its top (A) */
    CHECK_EQ(v.bgIMG[3], 6); CHECK_EQ(v.bgX[3], 8); CHECK_EQ(v.bgY[3], 128);
    CHECK_EQ(v.bgOP[3], POP_OP_STA);
    CHECK_EQ(v.bgIMG[4], 5); CHECK_EQ(v.bgX[4], 8); CHECK_EQ(v.bgY[4], 125);
    CHECK_EQ(v.bgOP[4], POP_OP_ORA);
    CHECK_EQ(v.bgIMG[5], 8); CHECK_EQ(v.bgY[5], 62 - 3);
    /* no screen above: its bottom row is taken to be floor, whose front
     * edges show in the top three lines */
    for (i = 0; i < 10; i++) {
        CHECK_EQ(v.bgIMG[6 + i], 6);
        CHECK_EQ(v.bgX[6 + i], 4 * i);
        CHECK_EQ(v.bgY[6 + i], 2);
    }
    CHECK_EQ(v.unmodelled | g.unmodelled, 0);
}

static void test_pieces_with_state(void)
{
    setup();
    put(1, 1, 3, POP_GATE, 0);                 /* a shut gate: seen from the block to its right */
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    /* its bottom piece crosses the floor line, so what it hides is redrawn
     * first and the piece ORed in two lines higher: 125 - 1 - 2 */
    {
        int i, found = 0, bars = 0;
        for (i = 1; i <= v.bgX[0]; i++) {
            if (v.bgIMG[i] == bg.gatebotORA) { found++; CHECK_EQ(v.bgY[i], 122); CHECK_EQ(v.bgX[i], 16); }
            if (v.bgIMG[i] == bg.gateB1) bars++;
        }
        CHECK_EQ(found, 1);
        CHECK_EQ(bars, 5);                     /* 8-line bars at 112 ... 80; the next would */
                                               /* pass the block's top at line 66 */
    }

    setup();
    put(1, 1, 3, POP_GATE, 188);               /* fully open: bottom at 125 - 48 */
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    {
        int i, found = 0;
        for (i = 1; i <= v.bgX[0]; i++) {
            if (v.bgIMG[i] == bg.gatebotSTA) { found++; CHECK_EQ(v.bgY[i], 125 - 48); }
        }
        CHECK_EQ(found, 1);
    }

    setup();
    for (int k = 0; k < 8; k++) tables[bg.gate8b + k] = (uint8_t)(0x30 + k);
    put(1, 1, 3, POP_GATE, 0);                 /* shut: above the 5 bars a top piece of 7 lines */
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    {
        int i, top = 0;
        for (i = 1; i <= v.bgX[0]; i++) if (v.bgIMG[i] == 0x36) { top++; CHECK_EQ(v.bgY[i], 72); }
        CHECK_EQ(top, 1);
    }
    put(1, 1, 3, POP_GATE, 28);                /* 28 units open: the bars end exactly at the */
    pop_zerolsts(&v);                          /* block's top, and there is no top piece */
    pop_sure(&v, &g);
    {
        int i, bars = 0, top = 0;
        for (i = 1; i <= v.bgX[0]; i++) {
            if (v.bgIMG[i] == bg.gateB1) bars++;
            if (v.bgIMG[i] >= 0x30 && v.bgIMG[i] <= 0x37) top++;
        }
        CHECK_EQ(bars, 5);                     /* at 105, 97, 89, 81, 73 */
        CHECK_EQ(top, 0);
    }
    CHECK_EQ(v.unmodelled | g.unmodelled, 0);

    setup();                                   /* screen 0, beyond the map: black, with the wall */
    g.VisScrn = g.SCRNUM = 0;                  /* an absent left screen stands for and the floor */
    pop_getscrns(&g);                          /* an absent upper one stands for */
    tables[bg.pieced + POP_FLOOR] = 6;
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    CHECK_EQ(v.bgX[0], 3 + 10);
    CHECK_EQ(v.unmodelled | g.unmodelled, 0);

    setup();                                   /* the bars are drawn again in front of the */
    put(1, 1, 3, POP_GATE, 0);                 /* kid when he stands in the gate's block */
    g.kid.scrn = 1; g.kid.block_x = 3; g.kid.block_y = 1;
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    CHECK_EQ(v.fgX[0], 6);                     /* the bottom piece and the 5 bars */
    CHECK_EQ(v.fgIMG[1], bg.gatebotORA);
    CHECK_EQ(v.fgOP[1], POP_OP_ORA);
    g.kid.block_x = 4;                         /* one block on: behind them no longer */
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    CHECK_EQ(v.fgX[0], 0);
    /* The test does not ask which screen the kid is on, only that he is
     * not on the screen to the right (the same block there is another). */
    g.kid.block_x = 3; g.kid.scrn = 5; g.scrnRight = 5;
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    CHECK_EQ(v.fgX[0], 0);
    g.scrnRight = 0;

    setup();
    put(1, 1, 3, POP_UPRESSPLATE, 2);          /* a plate that opens: down, it looks like floor */
    tables[bg.pieced + POP_FLOOR] = 6;
    tables[bg.pieced + POP_UPRESSPLATE] = 7;
    g.blueprint[POP_LINKMAP + 2] = 5;          /* its timer is running: pressed */
    pop_zerolsts(&v);
    pop_sure(&v, &g);
    {
        int i, floor_d = 0;
        for (i = 1; i <= v.bgX[0]; i++) if (v.bgIMG[i] == 6 && v.bgY[i] == 128) floor_d++;
        CHECK_EQ(floor_d, 1);
    }
    CHECK_EQ(v.unmodelled | g.unmodelled, 0);
}

static void test_meters(void)
{
    setup();
    tables[bg.bline + 0] = 0x89; tables[bg.bline + 1] = 0x8A; tables[bg.bline + 2] = 0x8B;
    /* 3 of 5: two units as one image, the third on its own, two empty places */
    g.KidStrength = 3; g.MaxKidStr = 5;
    pop_zerolsts(&v);
    pop_drawkidmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 4);
    CHECK_EQ(v.msgIMG[1], 0x8A); CHECK_EQ(v.msgX[1], 0); CHECK_EQ(v.msgOFF[1], 0);
    CHECK_EQ(v.msgY[1], 191); CHECK_EQ(v.msgOP[1], POP_OP_STA);
    CHECK_EQ(v.msgIMG[2], 0x88); CHECK_EQ(v.msgX[2], 2); CHECK_EQ(v.msgOFF[2], 2);
    CHECK_EQ(v.msgIMG[3], 0x8C); CHECK_EQ(v.msgX[3], 3); CHECK_EQ(v.msgOFF[3], 3);
    CHECK_EQ(v.msgOP[3], POP_OP_AND);
    CHECK_EQ(v.msgIMG[4], 0x8C); CHECK_EQ(v.msgX[4], 4);
    /* 7 of 8: 3 + 3 + 1; the eighth place is in the next group of seven dots */
    g.KidStrength = 7; g.MaxKidStr = 8;
    pop_zerolsts(&v);
    pop_drawkidmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 4);
    CHECK_EQ(v.msgIMG[1], 0x8B); CHECK_EQ(v.msgX[1], 0);
    CHECK_EQ(v.msgIMG[2], 0x8B); CHECK_EQ(v.msgX[2], 3); CHECK_EQ(v.msgOFF[2], 3);
    CHECK_EQ(v.msgIMG[3], 0x88); CHECK_EQ(v.msgX[3], 6); CHECK_EQ(v.msgOFF[3], 6);
    CHECK_EQ(v.msgIMG[4], 0x8C); CHECK_EQ(v.msgX[4], 8); CHECK_EQ(v.msgOFF[4], 0);
    /* the last unit is drawn on one of the two pages only: it blinks */
    g.KidStrength = 1; g.MaxKidStr = 3;
    pop_zerolsts(&v);
    pop_drawkidmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 3);
    CHECK_EQ(v.msgIMG[1], 0x8C); CHECK_EQ(v.msgX[1], 0);
    v.hr.PAGE = 0x20;
    pop_zerolsts(&v);
    pop_drawkidmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 3);
    CHECK_EQ(v.msgIMG[1], 0x88); CHECK_EQ(v.msgIMG[2], 0x8C); CHECK_EQ(v.msgX[2], 1);
    v.hr.PAGE = 0;

    /* the opponent's: from the right edge, mirrored, and without empty places */
    g.OppStrength = 4; g.shad.id = 2;
    pop_zerolsts(&v);
    pop_drawoppmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 2);
    CHECK_EQ(v.msgIMG[1], 0x8B); CHECK_EQ(v.msgX[1], 39); CHECK_EQ(v.msgOFF[1], 5);
    CHECK_EQ(v.msgOP[1], POP_OP_STA | 0x80);
    CHECK_EQ(v.msgIMG[2], 0x88); CHECK_EQ(v.msgX[2], 36); CHECK_EQ(v.msgOFF[2], 2);
    g.OppStrength = 1;                         /* blinking: an empty place on the other page */
    pop_zerolsts(&v);
    pop_drawoppmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 1);
    CHECK_EQ(v.msgIMG[1], 0x8C); CHECK_EQ(v.msgX[1], 39); CHECK_EQ(v.msgOP[1], POP_OP_AND | 0x80);
    /* no meter for the skeleton and the mouse; the shadow's on level 12 only */
    g.OppStrength = 3;
    pop_zerolsts(&v);
    g.shad.id = 4;  pop_drawoppmeter(&v, &g);
    g.shad.id = 24; pop_drawoppmeter(&v, &g);
    g.shad.id = 1;  pop_drawoppmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 0);
    g.level = 12;
    pop_drawoppmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 2);
    g.OppStrength = 0;
    pop_drawoppmeter(&v, &g);
    CHECK_EQ(v.msgX[0], 2);
    CHECK_EQ(v.unmodelled, 0);
}

static unsigned listed(void)
{
    return (unsigned)(v.bgX[0] + v.fgX[0] + v.midX[0] + v.wipeX[0] + v.msgX[0]);
}

static void test_fast(void)
{
    setup();
    tables[bg.piecea + POP_FLOOR] = 5;
    tables[bg.pieced + POP_FLOOR] = 6;
    tables[bg.bline + 1] = 0x8A;
    put(1, 1, 2, POP_FLOOR, 0);                /* block 12 */
    pop_zerolsts(&v);
    pop_fast(&v, &g);                          /* nothing marked, nothing to draw */
    CHECK_EQ(listed(), 0);
    CHECK_EQ(v.genCLS, 0);

    g.rb[POP_RB_RED + 12] = 2;                 /* the block, on both pages */
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(g.rb[POP_RB_RED + 12], 1);
    CHECK_EQ(v.bgX[0], 2);                     /* as in the full redraw: D, then A */
    CHECK_EQ(v.bgIMG[1], 6); CHECK_EQ(v.bgX[1], 8); CHECK_EQ(v.bgY[1], 128);
    CHECK_EQ(v.bgIMG[2], 5); CHECK_EQ(v.bgY[2], 125);
    CHECK_EQ(listed(), 2);
    g.rb[POP_RB_MOVE + 12] = 1;                /* a block redrawn whole keeps its "moving parts" mark */
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(g.rb[POP_RB_RED + 12], 0);
    CHECK_EQ(g.rb[POP_RB_MOVE + 12], 1);
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(g.rb[POP_RB_MOVE + 12], 0);
    CHECK_EQ(listed(), 0);                     /* a floor piece has no moving parts */

    g.rb[POP_RB_WIPE + 12] = 1;                /* a rectangle cleared: up from the block's bottom line */
    g.rb[POP_RB_WHITE + 12] = 20;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.wipeX[0], 1);
    CHECK_EQ(v.wipeX[1], 8); CHECK_EQ(v.wipeY[1], 128); CHECK_EQ(v.wipeH[1], 20);
    CHECK_EQ(v.wipeW[1], 4); CHECK_EQ(v.wipeCOL[1], 0x80);
    CHECK_EQ(g.rb[POP_RB_WIPE + 12], 0);
    CHECK_EQ(g.rb[POP_RB_WHITE + 12], 20);
    CHECK_EQ(listed(), 1);

    /* the floor piece again over a character behind it: on the mid list */
    g.rb[POP_RB_FLOOR + 12] = 1;
    g.rb[POP_RB_HALF + 12] = 1;                /* (waits: one or the other) */
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[0], 2);
    CHECK_EQ(v.midIMG[1], 5); CHECK_EQ(v.midX[1], 8); CHECK_EQ(v.midY[1], 125);
    CHECK_EQ(v.midTYP[1], POP_MID_FASTLAY); CHECK_EQ(v.midOP[1], POP_OP_ORA);
    CHECK_EQ(v.midCR[1], 40); CHECK_EQ(v.midCD[1], 192);
    CHECK_EQ(v.midIMG[2], 6); CHECK_EQ(v.midY[2], 128);
    CHECK_EQ(listed(), 2);
    CHECK_EQ(g.rb[POP_RB_HALF + 12], 1);
    pop_zerolsts(&v);                          /* the climbing-up version: a mask and a shorter piece */
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[0], 3);
    CHECK_EQ(v.midIMG[1], 0x2E); CHECK_EQ(v.midOP[1], POP_OP_AND); CHECK_EQ(v.midY[1], 125);
    CHECK_EQ(v.midIMG[2], 0x2F); CHECK_EQ(v.midOP[2], POP_OP_ORA);
    CHECK_EQ(v.midIMG[3], 6);
    CHECK_EQ(g.rb[POP_RB_HALF + 12], 0);
    put(1, 1, 1, POP_FLOOR, 0);                /* not with a piece to the left */
    g.rb[POP_RB_HALF + 12] = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(listed(), 0);
    CHECK_EQ(g.rb[POP_RB_HALF + 12], 0);

    /* the top three lines show the screen above; without one, the mark stays */
    g.rb[POP_RB_TOP + 3] = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(listed(), 0);
    CHECK_EQ(g.rb[POP_RB_TOP + 3], 1);
    g.scrnAbove = 2;
    put(2, 2, 3, POP_FLOOR, 0);
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(g.rb[POP_RB_TOP + 3], 0);
    CHECK_EQ(v.wipeX[0], 1);
    CHECK_EQ(v.wipeX[1], 12); CHECK_EQ(v.wipeY[1], 2); CHECK_EQ(v.wipeH[1], 3);
    CHECK_EQ(v.bgX[0], 1);                     /* its front edge, on both planes */
    CHECK_EQ(v.bgIMG[1], 6); CHECK_EQ(v.bgX[1], 12); CHECK_EQ(v.bgY[1], 2);
    CHECK_EQ(v.fgX[0], 1);
    CHECK_EQ(v.fgIMG[1], 6); CHECK_EQ(v.fgX[1], 12); CHECK_EQ(v.fgY[1], 2);
    g.scrnAbove = 0;

    /* a meter is drawn again when a block under it is: blocks 20-22 and 28-29 */
    g.KidStrength = 3; g.MaxKidStr = 3; g.OppStrength = 3; g.shad.id = 2;
    g.rb[POP_RB_FRED + 21] = 1;
    g.rb[POP_RB_MOVE + 28] = 1;                /* (moving parts do not count) */
    g.rb[POP_RB_RED + 27] = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK(v.redkidmeter != 0);
    CHECK_EQ(v.redoppmeter, 0);
    CHECK_EQ(v.msgX[0], 2);
    CHECK_EQ(v.msgIMG[1], 0x8A); CHECK_EQ(v.msgX[1], 0);
    g.rb[POP_RB_WIPE + 29] = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.redkidmeter, 0);
    CHECK(v.redoppmeter != 0);
    CHECK_EQ(v.msgX[0], 2);
    CHECK_EQ(v.msgX[1], 39);
    CHECK_EQ(v.unmodelled | g.unmodelled, 0);
}

static void obj(uint8_t slot, uint8_t typ, uint8_t indx, uint8_t x, uint8_t y, uint8_t img)
{
    g.objTYP[slot] = typ; g.objINDX[slot] = indx; g.objX[slot] = x; g.objY[slot] = y;
    g.objIMG[slot] = img; g.objOFF[slot] = 0; g.objFACE[slot] = 0xFF; g.objTAB[slot] = 0;
    g.objCU[slot] = 0; g.objCD[slot] = 192; g.objCL[slot] = 0; g.objCR[slot] = 40;
    if (slot > g.objX[0]) g.objX[0] = slot;
}

static void test_objects(void)
{
    setup();
    /* a character: saved under and drawn masked from a character table,
     * when the block it is indexed to is marked */
    obj(1, POP_OBJ_KID, 12, 9, 100, 3);
    g.objFACE[1] = 0;                          /* facing right: drawn mirrored */
    g.objCU[1] = 65; g.objCL[1] = 4; g.objOFF[1] = 5; g.objTAB[1] = 2;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(listed(), 0);                     /* no mark, no character */
    g.rb[POP_RB_OBJ + 12] = 1;
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[0], 1);
    CHECK_EQ(v.midX[1], 9); CHECK_EQ(v.midOFF[1], 5); CHECK_EQ(v.midY[1], 100);
    CHECK_EQ(v.midIMG[1], 3); CHECK_EQ(v.midTAB[1], 2);
    CHECK_EQ(v.midTYP[1], POP_MID_LAYSAVE | POP_MID_CHARTABLE);
    CHECK_EQ(v.midOP[1], POP_OP_MASK | 0x80);
    CHECK_EQ(v.midCU[1], 65); CHECK_EQ(v.midCD[1], 192); CHECK_EQ(v.midCL[1], 4); CHECK_EQ(v.midCR[1], 40);
    CHECK_EQ(g.rb[POP_RB_OBJ + 12], 0);        /* cleared, not counted down */

    /* merging with the shadow: every other frame the kid is drawn as the shadow is */
    g.mergetimer = 3;
    g.rb[POP_RB_OBJ + 12] = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midOP[1], POP_OP_EOR | 0x80);
    g.mergetimer = 0x83;                       /* (negative: not merging) */
    g.rb[POP_RB_OBJ + 12] = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midOP[1], POP_OP_MASK | 0x80);
    g.mergetimer = 0;
    g.backtolife = 1;                          /* blinking: on page 2 only */
    g.rb[POP_RB_OBJ + 12] = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[0], 0);
    v.hr.PAGE = 0x20;
    g.rb[POP_RB_OBJ + 12] = 1;
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[0], 1);
    v.hr.PAGE = 0;
    g.backtolife = 0;

    /* the kinds: shadow XORed; sword and star as the kid; a guard of the
     * other colour one dot to the right; an unknown kind not at all */
    g.objX[0] = 0;
    obj(1, POP_OBJ_SHAD, 30, 5, 90, 1);        /* index 30: off the screen, drawn first, unmarked */
    obj(2, POP_OBJ_SWORD, 30, 6, 80, 2);
    obj(3, POP_OBJ_COMIX, 0xFF, 7, 70, 3);     /* index $FF: on top of everything, drawn last */
    obj(4, POP_OBJ_GD, 30, 8, 60, 1);
    g.objOFF[4] = 6;
    obj(5, 6, 30, 9, 50, 1);
    obj(6, POP_OBJ_REFLECT, 30, 10, 40, 1);
    g.GuardColor = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[0], 5);
    /* (already in order: the lowest on the screen first) */
    CHECK_EQ(v.midY[1], 90); CHECK_EQ(v.midOP[1], POP_OP_EOR);
    CHECK_EQ(v.midY[2], 80); CHECK_EQ(v.midOP[2], POP_OP_MASK);
    CHECK_EQ(v.midY[3], 60); CHECK_EQ(v.midOP[3], POP_OP_MASK);
    CHECK_EQ(v.midX[3], 9); CHECK_EQ(v.midOFF[3], 0);        /* 8 + 6 dots, one more */
    CHECK_EQ(v.midY[4], 40);
    CHECK_EQ(v.midY[5], 70); CHECK_EQ(v.midIMG[5], 3);
    g.GuardColor = 0;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[3], 8); CHECK_EQ(v.midOFF[3], 6);

    /* The sort makes one pass.  Three characters at 100, 120, 140 come
     * out as 140, 100, 120; in order would be 140, 120, 100. */
    g.objX[0] = 0;
    obj(1, POP_OBJ_KID, 30, 1, 100, 1);
    obj(2, POP_OBJ_GD, 30, 2, 120, 1);
    obj(3, POP_OBJ_GD, 30, 3, 140, 1);
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[0], 3);
    CHECK_EQ(v.midY[1], 140); CHECK_EQ(v.midY[2], 100); CHECK_EQ(v.midY[3], 120);
    /* an opponent never moves towards the back */
    g.objX[0] = 0;
    obj(1, POP_OBJ_KID, 30, 1, 100, 1);
    obj(2, POP_OBJ_SHAD, 30, 2, 150, 1);
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midY[1], 100); CHECK_EQ(v.midY[2], 150);

    /* a falling floor: the loose floor's sections, at the mover's place */
    tables[bg.maska + POP_FLOOR] = 0x11;
    tables[bg.loosea + 10] = 0x12;
    tables[bg.loosed + 10] = 0x13;
    g.objX[0] = 0;
    obj(1, POP_OBJ_FF, 14, 16, 90, 10);
    g.objFACE[1] = 0;                          /* (whatever the slot held: never mirrored) */
    g.objCU[1] = 20;
    g.rb[POP_RB_OBJ + 14] = 1;
    pop_zerolsts(&v);
    pop_fast(&v, &g);
    CHECK_EQ(v.midX[0], 4);
    CHECK_EQ(v.midIMG[1], 0x11); CHECK_EQ(v.midOP[1], POP_OP_AND); CHECK_EQ(v.midY[1], 87);
    CHECK_EQ(v.midTYP[1], POP_MID_LAYSAVE); CHECK_EQ(v.midCU[1], 20);
    CHECK_EQ(v.midIMG[2], 0x12); CHECK_EQ(v.midOP[2], POP_OP_ORA); CHECK_EQ(v.midTYP[2], POP_MID_LAY);
    CHECK_EQ(v.midIMG[3], 0x13); CHECK_EQ(v.midOP[3], POP_OP_STA); CHECK_EQ(v.midY[3], 90);
    CHECK_EQ(v.midIMG[4], 0x29); CHECK_EQ(v.midX[4], 20); CHECK_EQ(v.midY[4], 86);
    CHECK_EQ(v.midTYP[4], POP_MID_LAYSAVE);
    CHECK_EQ(v.unmodelled | g.unmodelled, 0);

    g.objX[0] = 25;                            /* more entries than the list has places for */
    g.rb[POP_RB_OBJ + 14] = 1;
    pop_fast(&v, &g);
    CHECK_EQ(v.unmodelled, 1);
}

static void test_messages(void)
{
    uint8_t temp;
    setup();
    g.level = 3;
    pop_zerolsts(&v);
    pop_printlevel(&v, &g);
    CHECK_EQ(v.msgX[0], 3);
    /* an empty box, saved under (bit 6); "LEVEL"; the number */
    CHECK_EQ(v.msgIMG[1], 0x7B); CHECK_EQ(v.msgX[1], 15); CHECK_EQ(v.msgY[1], 90);
    CHECK_EQ(v.msgOFF[1], 0); CHECK_EQ(v.msgOP[1], POP_OP_STA | 0x40);
    CHECK_EQ(v.msgIMG[2], 0x7A); CHECK_EQ(v.msgX[2], 16); CHECK_EQ(v.msgY[2], 85);
    CHECK_EQ(v.msgOFF[2], 3); CHECK_EQ(v.msgOP[2], POP_OP_ORA);
    CHECK_EQ(v.msgIMG[3], 0x73); CHECK_EQ(v.msgX[3], 22); CHECK_EQ(v.msgOFF[3], 3);
    g.level = 12;                              /* two digits: the word 3 dots further left */
    pop_zerolsts(&v);
    pop_printlevel(&v, &g);
    CHECK_EQ(v.msgX[0], 4);
    CHECK_EQ(v.msgOFF[2], 0);
    CHECK_EQ(v.msgIMG[3], 0x71); CHECK_EQ(v.msgX[3], 22);
    CHECK_EQ(v.msgIMG[4], 0x72); CHECK_EQ(v.msgX[4], 23);
    g.level = 14;                              /* the last levels are announced as 12 */
    pop_zerolsts(&v);
    pop_printlevel(&v, &g);
    CHECK_EQ(v.msgIMG[4], 0x72);
    g.level = 13;
    pop_zerolsts(&v);
    pop_printlevel(&v, &g);
    CHECK_EQ(v.msgX[0], 4);
    CHECK_EQ(v.msgIMG[3], 0x71); CHECK_EQ(v.msgIMG[4], 0x72);

    /* the time: the box, then one or two digits over it */
    g.level = 3;
    g.FrameCount[0] = g.FrameCount[1] = 0;
    pop_zerolsts(&v);
    pop_timeleftmsg(&v, &g);
    temp = g.MinLeft;                          /* (worked out by the routine: BCD) */
    CHECK(temp >= 0x10);
    CHECK_EQ(v.msgX[0], 3);
    CHECK_EQ(v.msgIMG[1], 0x7D); CHECK_EQ(v.msgX[1], 11); CHECK_EQ(v.msgY[1], 90);
    CHECK_EQ(v.msgOP[1], POP_OP_STA | 0x40);
    CHECK_EQ(v.msgIMG[2], 0x70 + (temp >> 4)); CHECK_EQ(v.msgX[2], 12); CHECK_EQ(v.msgY[2], 85);
    CHECK_EQ(v.msgOP[2], POP_OP_ORA);
    CHECK_EQ(v.msgIMG[3], 0x70 + (temp & 15)); CHECK_EQ(v.msgX[3], 13);
    /* In the last minute it counts seconds, and the box is lower when the
     * kid is on the middle row and not falling; that is decided by the
     * time worked out before this call. */
    g.FrameCount[0] = 0xF0; g.FrameCount[1] = 0xFF;          /* (as late as the count goes) */
    pop_getminleft(&g);
    CHECK(g.MinLeft < 2);
    if (g.MinLeft < 2) {
        g.kid.block_y = 1; g.kid.action = 0;
        pop_zerolsts(&v);
        pop_timeleftmsg(&v, &g);
        CHECK_EQ(v.msgY[1], 153);
        CHECK_EQ(v.msgIMG[v.msgX[0]], 0x7F);   /* "SECONDS" over "MINUTES" */
        CHECK_EQ(v.msgX[v.msgX[0]], 14); CHECK_EQ(v.msgY[v.msgX[0]], 148);
        CHECK_EQ(v.msgOP[v.msgX[0]], POP_OP_STA);
        g.kid.action = 4;                      /* falling: where it always is */
        pop_zerolsts(&v);
        pop_timeleftmsg(&v, &g);
        CHECK_EQ(v.msgY[1], 90);
    }

    g.kid.block_x = 4;                         /* low when he lies in an even column, else high */
    pop_zerolsts(&v);
    pop_continuemsg(&v, &g);
    CHECK_EQ(v.msgX[0], 1);
    CHECK_EQ(v.msgIMG[1], 0x7C); CHECK_EQ(v.msgX[1], 13); CHECK_EQ(v.msgY[1], 168);
    CHECK_EQ(v.msgOP[1], POP_OP_STA | 0x40);
    g.kid.block_x = 5;
    pop_zerolsts(&v);
    pop_show_message(&v, &g, POP_SHOW_CONTINUE);
    CHECK_EQ(v.msgY[1], 73);
    pop_zerolsts(&v);
    pop_show_message(&v, &g, POP_SHOW_LEVEL);
    CHECK_EQ(v.msgIMG[1], 0x7B);
    pop_zerolsts(&v);
    pop_show_message(&v, &g, POP_SHOW_NOTHING);
    CHECK_EQ(v.msgX[0], 0);
    CHECK_EQ(v.unmodelled | g.unmodelled, 0);
}

static void test_screen(void)
{
    setup();
    /* a new screen: drawn on page 2 behind a blank text screen ... */
    pop_cut_begin(&v);
    CHECK_EQ(v.hr.PAGE, 0x20);
    CHECK_EQ(v.sw_text, 1); CHECK_EQ(v.sw_page2, 0); CHECK_EQ(v.scrncolor, 0xA0);
    v.hr.vram[POP_HR_PAGE + 100] = 0x55;       /* (something drawn on page 2) */
    v.hr.vram[100] = 0x11;
    /* ... copied to page 1, where the characters are added ... */
    pop_cut_copy(&v, &g);
    CHECK_EQ(v.hr.PAGE, 0);
    CHECK_EQ(v.hr.vram[100], 0x55);
    CHECK_EQ(v.IMAGE, 0x40); CHECK_EQ(v.width, 0x20);        /* (the two page addresses stay behind) */
    CHECK_EQ(g.height, 0x40);
    /* ... and shown: page 1 on the screen, page 2 the one drawn on next */
    pop_pageflip(&v, &g);
    CHECK_EQ(v.hr.PAGE, 0x20);
    CHECK_EQ(v.sw_page2, 0); CHECK_EQ(v.sw_hires, 1); CHECK_EQ(v.sw_text, 0);
    pop_pageflip(&v, &g);
    CHECK_EQ(v.hr.PAGE, 0);
    CHECK_EQ(v.sw_page2, 1);
    /* a flash: the lo-res screen in one colour, then back to the page shown */
    pop_doflashon(&v, 0x11);
    CHECK_EQ(v.scrncolor, 0x11); CHECK_EQ(v.sw_hires, 0); CHECK_EQ(v.sw_page2, 0);
    pop_doflashoff(&v);
    CHECK_EQ(v.sw_hires, 1); CHECK_EQ(v.sw_page2, 1);
    pop_pageflip(&v, &g);                      /* now page 1 is shown */
    pop_doflashon(&v, 0xFF);
    pop_doflashoff(&v);
    CHECK_EQ(v.sw_page2, 0);

    /* the two cells the game logic shares with the drawing code */
    g.height = 9; g.OFFSET = 4;
    pop_zerolsts(&v);
    pop_show_message(&v, &g, POP_SHOW_NOTHING);
    CHECK_EQ(v.IMAGE, 9); CHECK_EQ(v.OFFSET, 4);
    v.IMAGE = 33; v.OFFSET = 2;
    pop_gfx_cells_out(&v, &g);
    CHECK_EQ(g.height, 33); CHECK_EQ(g.OFFSET, 2);
}

int main(void)
{
    RUN(test_images);
    RUN(test_adding);
    RUN(test_peel_lists);
    RUN(test_drawall);
    RUN(test_drawall_leaves);
    RUN(test_empty_screen);
    RUN(test_pieces_with_state);
    RUN(test_meters);
    RUN(test_fast);
    RUN(test_objects);
    RUN(test_messages);
    RUN(test_screen);
    return pop_test_summary();
}
