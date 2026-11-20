/* pop_world.h - reading the level around a character.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Ported routines (01 POP Source/Source, original commit 25243550).  Each C
 * function keeps the original name and is compared with the original code,
 * run in a 6502 simulator, on randomised inputs (tests/fuzz_world.py).
 *
 *   CTRLSUBS.S  RDBLOCK, RDBLOCK1 (+ handler)       137-204
 *               GETLEFT, GETRIGHT, GETUP, GETDOWN    244-275
 *               GETBASEX                             329-340
 *               GETUNDERFT ... GETABOVEBEH           400-468
 *               GETDIST, GETDIST1, GETBLOCKEJ        476-508
 *               GETBLOCKX, GETBLOCKXP                523-530
 *               GETBLOCKY, GETBLOCKYP                543-560
 *               GETBASEBLOCK, FACEDX                 622-640
 *               GETFRAME ..., GETFRAMEINFO, usealtsets  905-964, 1648-1715
 *               CMPSPACE, CMPBARR, CMPWALL           1508-1568
 *               LOADKID ... SAVESHADWOP              1940-2020
 *   CTRL.S      REREADBLOCKS                         1929-1931
 *   SUBS.S      GRAVITY, ADDFALL                     1613-1662
 *   GRAFIX.S    CALCBLUE                             1757-1778
 *
 * See docs/port/world-queries.md.
 */
#ifndef POP_WORLD_H
#define POP_WORLD_H

#include "pop_game.h"

/* Screen geometry, TABLES.S 31-36. */
enum {
    POP_SCRN_LEFT = 58, POP_SCRN_BOT = 191, POP_BLOCK_HEIGHT = 63, POP_VERT_DIST = 10,
    POP_BLOCK_WIDTH = 14, POP_ANGLE = 7 /* GAMEEQ.S 658 */
};

/* --- map and blueprint ------------------------------------------------------- */

uint8_t pop_getleft(pop_game *g, uint8_t scrn);
uint8_t pop_getright(pop_game *g, uint8_t scrn);
uint8_t pop_getup(pop_game *g, uint8_t scrn);
uint8_t pop_getdown(pop_game *g, uint8_t scrn);

/* CALCBLUE: select the blueprint of screen 1-32. */
void pop_calcblue(pop_game *g, uint8_t scrn);

/* (BlueType),y and (BlueSpec),y for the selected screen. */
uint8_t pop_bluetype(const pop_game *g, uint8_t y);
uint8_t pop_bluespec(const pop_game *g, uint8_t y);
void    pop_set_bluespec(pop_game *g, uint8_t y, uint8_t value);
void    pop_set_bluetype(pop_game *g, uint8_t y, uint8_t value);

/* RDBLOCK: piece id of block (x, y) of a screen; x and y may lie outside
 * 0-9 / 0-2 and are then followed onto neighbouring screens.  Screen 0 reads
 * as a solid block.  Leaves tempscrn/tempblockx/tempblocky = the block's home
 * coordinates, rx = the id, ry = the block index 0-29. */
uint8_t pop_rdblock(pop_game *g, uint8_t scrn, uint8_t x, uint8_t y);
uint8_t pop_rdblock1(pop_game *g);   /* same, from tempscrn/tempblockx/tempblocky */

/* --- around the current character (g->ch) ------------------------------------- */

uint8_t pop_getunderft(pop_game *g);
uint8_t pop_getinfront(pop_game *g);
uint8_t pop_get2infront(pop_game *g);
uint8_t pop_getbehind(pop_game *g);
uint8_t pop_getabove(pop_game *g);
uint8_t pop_getaboveinf(pop_game *g);
uint8_t pop_getabovebeh(pop_game *g);

uint8_t pop_getbasex(pop_game *g);          /* x of the supporting foot */
uint8_t pop_getdist(pop_game *g);           /* units from the foot to the block edge ahead */
uint8_t pop_getdist1(pop_game *g, uint8_t x);
void    pop_getbaseblock(pop_game *g);      /* ch.block_x from the foot position */
uint8_t pop_facedx(const pop_game *g, uint8_t dx);

/* --- coordinates ---------------------------------------------------------------- */

uint8_t pop_getblockej(pop_game *g, uint8_t block);  /* left edge x of block -5..14 */
uint8_t pop_getblockx(pop_game *g, uint8_t x);       /* block column; sets OFFSET */
uint8_t pop_getblockxp(pop_game *g, uint8_t x);      /* same, for the centre plane */
uint8_t pop_getblocky(uint8_t y);                    /* row 0-3, or $FF above the screen */
uint8_t pop_getblockyp(uint8_t y);                   /* same, by floor lines */
uint8_t pop_floory(pop_game *g, uint8_t index);      /* FloorY,x: floor line of row index-1 */
uint8_t pop_blockbot(pop_game *g, uint8_t index);    /* BlockBot,x: bottom line of row index-1 */
uint8_t pop_blockay(pop_game *g, uint8_t index);     /* BlockAy,x: bottom line - 3 */

/* --- piece classes ---------------------------------------------------------------- */

uint8_t pop_cmpspace(uint8_t objid);                 /* 0 = no floor, 1 = floor */
uint8_t pop_cmpbarr(uint8_t objid);                  /* 0 clear, 1 panel/gate, 3 mirror/slicer, 4 block */
uint8_t pop_cmpwall(const pop_game *g, uint8_t objid); /* 0 = wall, 1 = not */

/* --- frame information ------------------------------------------------------------ */

void pop_getframe(pop_game *g, uint8_t frame);       /* framepoint for a main-set frame */
void pop_getframeinfo(pop_game *g);                  /* Fimage..Fcheck for ch.posn / ch.id */
void pop_rereadblocks(pop_game *g);                  /* getframeinfo + getbaseblock */

/* --- falling ---------------------------------------------------------------------------- */

enum {                       /* SUBS.S 1613-1616 */
    POP_TERM_VELOCITY = 33, POP_ACCEL_GRAVITY = 3,
    POP_WTLESS_TERM_VEL = 4, POP_WTLESS_GRAVITY = 1
};

/* GRAVITY: while falling (CharAction = 4), add to the downward velocity up
 * to a terminal value.  The limit test is unsigned. */
void pop_gravity(pop_game *g);

/* ADDFALL: CharY += CharYVel always; while falling also move in x by
 * CharXVel and re-read the block under the character. */
void pop_addfall(pop_game *g);

/* --- character records -------------------------------------------------------------- */

void pop_loadkid(pop_game *g);
void pop_savekid(pop_game *g);
void pop_loadshad(pop_game *g);
void pop_saveshad(pop_game *g);
void pop_loadkidwop(pop_game *g);
void pop_savekidwop(pop_game *g);
void pop_loadshadwop(pop_game *g);
void pop_saveshadwop(pop_game *g);

#endif /* POP_WORLD_H */
