/* pop_gfx.h - what is drawn: the display lists, the routines that draw
 * them, and the full redraw of a screen from the level.
 *
 * New code for the pop-study reconstruction; no original code or data.
 * The piece tables and the images are original data and are NOT in this
 * repository: both are read from files generated from, or found in, the
 * user's own checkout (pop_bgdata, pop_imgtab).
 *
 * Ported routines (01 POP Source/Source, original commit 25243550); each
 * keeps its original name.  The finished page is compared, byte for byte,
 * with the page the original code produces in the two-bank simulator
 * (tests/compare_rooms.py).
 *
 *   GRAFIX.S    ADDBACK 203, ADDFORE 235, ADDMSG 258, ADDWIPE 285,
 *               ADDMID 341, ADDMIDEZ / ADDMIDEZO 386, ADDPEEL 431,
 *               DRAWALL 484, DOGEN 513, DRAWWIPE 533, DRAWBACK 566,
 *               DRAWFORE 600, SNGPEEL 636, DRAWMID 676, DRAWMSG 760,
 *               setbgimg 814, setcharimg 842, ZEROLSTS 962,
 *               ZEROPEELS 983, ZEROPEEL 999
 *   FRAMEADV.S  SURE 49, RedBlockSure 346, RedDSure 368, getprev 561,
 *               getbelow 616, drawfrnt 754, DrawGateBF? 829, drawmb 849,
 *               drawmc 880, drawc 906, drawb 997, drawd 1096, drawa 1134,
 *               add / setback 1161, drawma 1214, drawmd 1239, the loose
 *               floor, spike, torch, flask, sword, slicer, exit and gate
 *               pieces 1365-1927, getobjid 2041;
 *               FAST 189, RedBlockFast 376, RedDFast 441, drawfloor 1252,
 *               drawhalf 1268, wipesq 1326, wiped 1348, redrawd 1092,
 *               metbufs 2252; drawobjs 503, loadobj 716, drawobjx 1937,
 *               DrawFF 1969, sortlist 2134, compare 2182
 *   GAMEBG.S    setupflask 675, setupflame 735, UPDATEMETERS 479,
 *               DRAWKIDMETER 496, DRAWOPPMETER 579, DRAWKID 391,
 *               DRAWSWORD 408, DRAWSHAD 414, DRAWGUARD 420
 *
 * (Line each routine starts at.)  See docs/port/redraw.md.
 */
#ifndef POP_GFX_H
#define POP_GFX_H

#include "pop_game.h"
#include "pop_hires.h"

/* List sizes, EQ.S 294-300.  Entry 0 of each list's X array is its count. */
enum {
    POP_MAXBACK = 200, POP_MAXFORE = 100, POP_MAXWIPE = 20, POP_MAXPEEL = 46,
    POP_MAXMID = 46, POP_MAXMSG = 32
};

/* midTYP, GRAFIX.S 331-336. */
enum { POP_MID_FASTLAY = 0, POP_MID_LAY = 1, POP_MID_LAYSAVE = 2, POP_MID_CHARTABLE = 0x80 };

enum { POP_TORCHLAST = 17, POP_BUBBLAST = 8 };       /* MOVEDATA.S 42-43 */

/* An image table as it is in an IMG.* file: a count, count + 1 pointers
 * that assume the file is loaded at $6000, then the images. */
typedef struct pop_imgtab {
    const uint8_t *data;
    size_t size;
} pop_imgtab;

/* The piece tables of BGDATA.S (with the flame and bubble tables of
 * GAMEBG.S appended) as one block, and where each table is in it.
 * Constants are image numbers and limits the code compares with. */
typedef struct pop_bgdata {
    const uint8_t *image;
    size_t size;
    /* offsets of tables */
    uint16_t piecea, pieceay, maska, pieceb, pieceby, bstripe, piecec, pieced, fronti,
             fronty, frontx, maskb, blockb, blockc, blockd, blockfr, panelb, panelc,
             spaceb, spaceby, floorb, floorby, loosea, loosed, looseby, spikea, spikeb,
             slicerseq, slicertop, slicerbot, slicerbot2, slicergap, slicerfrnt, gate8b,
             gate8c, torchflame, bubble, bline;
    /* constants */
    uint8_t numblox, numpans, numbpans, panelb0, panelc0, archpanel, specialflask, looseb,
            Ffalling, spikeExt, slicerRet, stairs, door, doormask, toprepair, gatecmask,
            gatebotSTA, gatebotORA, gateB1, gmaxval, swordgleam0, swordgleam1,
            CUmask, CUpiece, CUpost, bullet, blank;
} pop_bgdata;

typedef struct pop_gfx {
    pop_hires hr;
    const pop_bgdata *bg;
    pop_imgtab bgtable[2];               /* bgtable1, bgtable2 */
    pop_imgtab chtable[7];               /* chtable1 ... chtable7 */

    /* --- the lists (EQ.S 304-357) --- */
    uint8_t genCLS;                      /* clear the page before drawing */
    uint8_t bgX[POP_MAXBACK], bgY[POP_MAXBACK], bgIMG[POP_MAXBACK], bgOP[POP_MAXBACK];
    uint8_t fgX[POP_MAXFORE], fgY[POP_MAXFORE], fgIMG[POP_MAXFORE], fgOP[POP_MAXFORE];
    uint8_t wipeX[POP_MAXWIPE], wipeY[POP_MAXWIPE], wipeH[POP_MAXWIPE], wipeW[POP_MAXWIPE],
            wipeCOL[POP_MAXWIPE];
    uint8_t peelX[2 * POP_MAXPEEL], peelY[2 * POP_MAXPEEL], peelIMGL[2 * POP_MAXPEEL],
            peelIMGH[2 * POP_MAXPEEL];   /* one list per display page */
    uint8_t midX[POP_MAXMID], midOFF[POP_MAXMID], midY[POP_MAXMID], midIMG[POP_MAXMID],
            midOP[POP_MAXMID], midTYP[POP_MAXMID], midCU[POP_MAXMID], midCD[POP_MAXMID],
            midCL[POP_MAXMID], midCR[POP_MAXMID], midTAB[POP_MAXMID];
    uint8_t msgX[POP_MAXMSG], msgOFF[POP_MAXMSG], msgY[POP_MAXMSG], msgIMG[POP_MAXMSG],
            msgOP[POP_MAXMSG];

    /* --- what the adding routines read (the original's zero page).  The
     *     height of a wipe is passed in IMAGE and its width in the byte
     *     after it (EQ.S 384-385: height = IMAGE, width = IMAGE+1), and
     *     DRAWALL leaves in all of them what it drew last; one path (a
     *     torch whose state is beyond the flame table) uses what it finds
     *     there --- */
    uint8_t XCO, YCO, OFFSET, IMAGE, OPACITY, TABLE;
    uint8_t width;                       /* IMAGE + 1 */
    /* The game logic uses two of these cells too: IMAGE as `height`
     * (pop_game.height: the wipe height for MARKWIPE, and the image number
     * DIMCHAR asks about) and OFFSET (pop_game.OFFSET: GETBLOCKX, CVTX).
     * The routines below that take the game state start from its values
     * and hand theirs back (pop_gfx_cells_in / _out), so that each pair of
     * fields acts as the one cell. */

    /* --- FRAMEADV's variables; they persist from block to block and some
     *     paths use a value an earlier block left --- */
    uint8_t PRECED, spreced, objid, state;
    uint8_t PREV[3], sprev[3], BELOW[10], SBELOW[10];
    uint8_t rowno, colno, yindex, Ay, Dy, blockxco, blockthr, gateposn, gatebot, xsave;
    uint8_t sortX[16];                   /* the objects of one block, back to front ($3C0) */
    uint8_t addmid_sel;                  /* `add`: 0 the background list (setback), 1 the mid list (setmid) */
    uint8_t redkidmeter, redoppmeter;    /* a strength meter is to be redrawn (GAMEEQ.S 537-540) */

    /* Copy protection, GRAFIX.S 189, 441-446, 520-524: on a palace level
     * dummy[0] is cleared, and the second page's save-under list is then
     * found through dummy[purpleflag]: right only if purpleflag is 1. */
    uint8_t purpleflag, dummy[2];

    /* --- what the monitor is shown: the soft switches the game touches
     *     (PageFlip, drawbg, the flash).  Which hi-res page, hi-res or
     *     lo-res, graphics or text; and the byte the text / lo-res page is
     *     filled with (LRCLS), which is all that page ever holds in play:
     *     spaces under a redraw, a colour for a flash --- */
    uint8_t sw_page2, sw_hires, sw_text, sw_an3;
    uint8_t scrncolor;                   /* (GAMEEQ.S 539) */
    /* The switches touched since the log was last emptied, in order: '1',
     * '2' the page, 'l', 'h' lo-res / hi-res, 'g', 't' graphics / text.
     * Within one frame the monitor can be shown several things (a flash
     * lasts from DOFLASHON to the next PageFlip). */
    char    swlog[24];
    uint8_t nswlog;

    uint8_t unmodelled;                  /* as in pop_hires; also set from hr.unmodelled */
} pop_gfx;

/* Lists empty, the page table upright, the copy-protection flag "passed". */
void pop_gfx_init(pop_gfx *v);

void pop_gfx_cells_in(pop_gfx *v, const pop_game *g);    /* IMAGE and OFFSET as the game left them */
void pop_gfx_cells_out(const pop_gfx *v, pop_game *g);   /* ... and as the drawing code leaves them */

/* Image `number` (1-based) of a table; an empty image (size 0) if there is
 * no such entry. */
pop_image pop_imgtab_image(const pop_imgtab *t, uint8_t number);

/* --- lists -------------------------------------------------------------------------------- */

void pop_zerolsts(pop_gfx *v);           /* ZEROLSTS: all but the peel lists */
void pop_gfx_zeropeels(pop_gfx *v);      /* ZEROPEELS */
void pop_zeropeel(pop_gfx *v);           /* ZEROPEEL: the list of the page being drawn */
void pop_addback(pop_gfx *v);            /* In: XCO, YCO, IMAGE (bit 7: bgtable2), OPACITY */
void pop_addfore(pop_gfx *v);
void pop_addmsg(pop_gfx *v);
void pop_addwipe(pop_gfx *v, uint8_t colour);
void pop_addmidezo(pop_gfx *v, uint8_t typ);         /* no mirroring, no cropping */
void pop_addmid(pop_gfx *v, const pop_game *g, uint8_t typ);     /* with the character's cuts */
void pop_addpeel(pop_gfx *v);            /* after layrsave */

/* DRAWALL: put back what the last frame's characters covered, then draw
 * the wipes, the background list, the mid list (saving under characters),
 * the foreground list and the messages.  In: g for BGset1 only. */
void pop_drawall(pop_gfx *v, pop_game *g);

/* --- the whole screen ---------------------------------------------------------------------- */

/* SURE: fill the lists for all of screen g->SCRNUM (no characters).
 * Uses g's level and neighbour screens; leaves g->blue on SCRNUM. */
void pop_sure(pop_gfx *v, pop_game *g);

/* FAST: fill the lists with what the redraw buffers (g->rb) ask for, and
 * take the marks off: wipes, whole blocks, moving parts, floor pieces in
 * front of a climbing character, the objects of g's object list (characters,
 * swords, impact stars, falling floors), front pieces, the top line of
 * D-sections, and the strength meters if a redrawn block is under them. */
void pop_fast(pop_gfx *v, pop_game *g);

void pop_drawkidmeter(pop_gfx *v, const pop_game *g);     /* DRAWKIDMETER, GAMEBG.S 496 */
void pop_drawoppmeter(pop_gfx *v, const pop_game *g);     /* DRAWOPPMETER, GAMEBG.S 579 */

/* --- messages (pop_screen.c; GAMEBG.S 162-345) -------------------------------------------------- */

void pop_printlevel(pop_gfx *v, const pop_game *g);       /* PRINTLEVEL: "LEVEL n" in a box */
void pop_timeleftmsg(pop_gfx *v, pop_game *g);            /* TIMELEFTMSG: "n MINUTES LEFT"; runs GETMINLEFT */
void pop_continuemsg(pop_gfx *v, const pop_game *g);      /* CONTINUEMSG: "Press button to continue" */
void pop_show_message(pop_gfx *v, pop_game *g, uint8_t kind);    /* by POP_SHOW_* (pop_loop.h) */

/* --- the screen (pop_screen.c) ------------------------------------------------------------------ */

void pop_lrclse(pop_gfx *v, uint8_t value);  /* LRCLSE, LRCLS: fill the text / lo-res page */
/* DoCleanCut's first lines and drawbg's: draw on page 2, and show a blank
 * text screen while the new background is built. */
void pop_cut_begin(pop_gfx *v);
/* DoCleanCut after the background is drawn: copy it to page 1 and draw there. */
void pop_cut_copy(pop_gfx *v, pop_game *g);
void pop_pageflip(pop_gfx *v, const pop_game *g);    /* PAGEFLIP, SUBS.S 397: show the page drawn, draw on the other */
void pop_doflashon(pop_gfx *v, uint8_t colour);      /* DOFLASHON, SUBS.S 210: a screen of one colour */
void pop_doflashoff(pop_gfx *v);                     /* DOFLASHOFF, SUBS.S 220 */

#endif /* POP_GFX_H */
