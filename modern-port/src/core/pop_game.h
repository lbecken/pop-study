/* pop_game.h - game state shared by the ported routines.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * The original keeps its state in zero page and a few fixed buffers; every
 * routine reads and writes those locations directly.  This struct gathers
 * the locations ported so far, under their original names.  It will grow as
 * more of the game is ported.
 *
 * Addresses are those of the original (EQ.S, GAMEEQ.S at commit 25243550).
 */
#ifndef POP_GAME_H
#define POP_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "pop_char.h"
#include "pop_seq.h"

/* Level blueprint: the 2304 bytes at $B700 (EQ.S 258-265). */
enum {
    POP_LEVEL_SIZE   = 2304,
    POP_BLUETYPE     = 0,      /* 24 x 30 */
    POP_BLUESPEC     = 720,
    POP_LINKLOC      = 1440,
    POP_LINKMAP      = 1696,
    POP_MAP          = 1952,   /* 24 x (left, right, up, down) */
    POP_INFO         = 2048,
    POP_IDMASK       = 0x1F    /* EQ.S 486 */
};

/* Piece ids, MOVEDATA.S 8-37. */
enum {
    POP_SPACE = 0, POP_FLOOR, POP_SPIKES, POP_POSTS, POP_GATE, POP_DPRESSPLATE,
    POP_PRESSPLATE, POP_PANELWIF, POP_PILLARBOTTOM, POP_PILLARTOP, POP_FLASK,
    POP_LOOSE, POP_PANELWOF, POP_MIRROR, POP_RUBBLE, POP_UPRESSPLATE, POP_EXIT,
    POP_EXIT2, POP_SLICER, POP_TORCH, POP_BLOCK, POP_BONES, POP_SWORD, POP_WINDOW,
    POP_WINDOW2, POP_ARCHBOT, POP_ARCHTOP1, POP_ARCHTOP2, POP_ARCHTOP3, POP_ARCHTOP4
};

/* Frame definition tables (FRAMEDEF.S), as assembled.  `data[0]` is the byte
 * at address `origin`; the other fields are original addresses. */
typedef struct pop_frametab {
    const uint8_t *data;
    size_t         size;
    uint16_t       origin;    /* $2800 */
    uint16_t       fdef;      /* Fdef     */
    uint16_t       altset1;   /* altset1  */
    uint16_t       altset2;   /* altset2  */
    uint16_t       swordtab;  /* swordtab */
} pop_frametab;

typedef struct pop_game {
    uint8_t blueprint[POP_LEVEL_SIZE];   /* $B700 */

    pop_char ch;     /* $40  Char: the character being worked on */
    pop_char kid;    /* $50  Kid  */
    pop_char shad;   /* $60  Shad */
    pop_char op;     /* $39C Op: the other character */

    /* frame information for ch.posn, set by pop_getframeinfo */
    uint16_t framepoint;                 /* $74 */
    uint8_t  Fimage, Fdx, Fdy, Fcheck;   /* $76-$79 */
    uint8_t  Fsword;                     /* $D9 */

    /* block-reading variables */
    uint8_t  blockx, blocky;             /* (not yet used) */
    uint8_t  infrontx, behindx, abovey;  /* $81-$83 */
    uint8_t  tempblockx, tempblocky, tempscrn;  /* $84-$86 */
    uint8_t  OFFSET;                     /* $03: pixel within block from GETBLOCKX */

    /* weightless ($C5), jarabove ($CD), alertguard ($E4) */
    pop_anim_globals ag;

    /* Which screen's blueprint BlueType/BlueSpec ($25-$28) point at, as set
     * by the last calcblue; 0 = not a level screen. */
    uint8_t  blue;

    /* The X and Y registers as the last block-reading routine left them.
     * Original callers use them directly, e.g. `jsr getunderft` followed by
     * `lda (BlueSpec),y`.  Only the routines documented as setting them do. */
    uint8_t  rx, ry;

    /* Set when a routine was asked for something the port does not model
     * (see docs/port/world-queries.md).  Never set by valid game states. */
    uint8_t  unmodelled;

    const pop_frametab *frames;
} pop_game;

#endif /* POP_GAME_H */
