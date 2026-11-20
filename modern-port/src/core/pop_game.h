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
#include "pop_sound.h"

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

/* Width (bytes) and height (lines) of every character image, by table.
 * Stands in for DIMCHAR / GETWIDTH (GRAFIX.S 879-883, HIRES.S 287-301),
 * which read them from the image tables in memory. */
#define POP_CHTABLES 7
typedef struct pop_imgdims {
    uint16_t       count[POP_CHTABLES];
    const uint8_t *wh[POP_CHTABLES];     /* count[t] pairs: width, height */
} pop_imgdims;

/* Calls into subsystems that are not ported yet (moving level objects,
 * potion effects, level change).  Each is made at the point where the
 * original has its `jsr`.  Any pointer may be NULL. */
typedef struct pop_game_hooks {
    void *ctx;
    void (*add_slicers)(void *ctx);      /* addslicers,   SUBS.S 250  */
    void (*jam_spikes)(void *ctx);       /* jamspikes,    MOVER.S 354 (after the spec write) */
    void (*potion_effect)(void *ctx);    /* potioneffect, MISC.S      */
    void (*smash_mirror)(void *ctx);     /* smashmirror,  MOVER.S 259 */
    void (*speaker_click)(void *ctx);    /* a read of $C030, COLL.S 403 */
    /* PLAYERCTRL's input step: LoadSelect, getselect, clrjstk (CTRL.S
     * 573-577).  Expected to set JSTKX, JSTKY, btn and the clr* flags. */
    void (*get_input)(void *ctx);
    void (*save_input)(void *ctx);       /* SaveSelect,   CTRL.S 582  */
    void (*demo_ctrl)(void *ctx);        /* DemoCtrl,     CTRL.S 587: the demo level's kid */
} pop_game_hooks;

/* Order of the collision buffers in memory. */
enum { POP_CD_THIS = 0, POP_CD_LAST = 1, POP_CD_BELOW = 2, POP_CD_ABOVE = 3 };

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
    uint8_t  blockx, blocky;             /* $8C, $8D */
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

    /* --- player input, relative to the facing direction (CTRL.S 636-646) ---
     * JSTKX: - forward, + back, 0 centre.  JSTKY: - up, + down.
     * btn: - pressed.  clr*: - means a fresh press not yet used; set to 1
     * once it has been acted on. */
    uint8_t  JSTKX, JSTKY;               /* $18, $19 */
    uint8_t  btn;                        /* $3D */
    uint8_t  clrF, clrB, clrU, clrD, clrbtn;   /* $D4-$D8 */

    /* --- game variables used by control and falling --- */
    uint8_t  level;                      /* $3F4 */
    uint8_t  stunned, gotsword, offguard, EnemyAlert, heroic, refract, gdtimer;
    uint8_t  KidStrength, OppStrength, ChgKidStr, ChgOppStr;
    uint8_t  mergetimer, rjumpflag, droppedout, savekidx, lastpotion;
    uint8_t  blockid, bufindex, timebomb;

    /* --- character as set up for drawing and collision (SETUPCHAR, GETEDGES) --- */
    uint8_t  FCharImage, FCharY, FCharFace, FCharCU, FCharCD, FCharCL, FCharCR, FCharTable;
    uint16_t FCharX;
    uint8_t  imwidth, imheight;
    uint8_t  leftej, rightej, topej;
    uint8_t  leftblock, rightblock, topblock, bottomblock;
    uint8_t  CDLeftEj, CDRightEj;
    uint8_t  blockedge, tempobjid;

    /* --- collision detection (COLL.S; GAMEEQ.S 405-469, 545-554) ---
     * CD: per column 0-9, low nybble $F = the character's right edge is past
     * the barrier's left edge, high nybble $F = his left edge is short of
     * its right edge; $FF = overlapping it.  SN: the screen the column's
     * data came from, $FF = none.  Kept for this frame, the last frame, and
     * the rows below and above. */
    uint8_t  CD[4][16], SN[4][16];       /* $320-$399; see POP_CD_* */
    uint8_t  BlockYthis, BlockYlast;     /* $39A, $39B */
    uint8_t  collideL, collideR;         /* $C3, $C4: column, or $FF for none */
    uint8_t  collX;                      /* $87 */
    uint8_t  begrange, endrange;         /* $C8, $C0 */
    uint8_t  AMtimer;                    /* $C7: frames of walking through walls */
    uint8_t  VisScrn;                    /* $CB: the screen being shown */
    uint8_t  scrnLeft, scrnRight, scrnBelowL, scrnBelowR;   /* $31, $32, $35, $38 */
    uint8_t  createshad;                 /* $E5: set when the mirror is smashed */
    uint8_t  NextLevel;                  /* $31C */
    /* music request: song number, and the number of frames within which
     * it must start (CUESONG, SPECIALK.S 1332-1335) */
    uint8_t  SongCue, SongCount;

    /* --- redraw marks (CTRLSUBS.S 1734-1757) --- */
    uint8_t  redbuf[30], wipebuf[30], whitebuf[30], topbuf[10];
    uint8_t  height;                     /* $04: wipe height for MARKWIPE */

    pop_soundq sq;                       /* soundtable, $B6C0 */

    const pop_seqtab  *seq;
    const pop_imgdims *dims;
    pop_game_hooks     hooks;

    /* Set when a routine was asked for something the port does not model
     * (see docs/port/world-queries.md).  Never set by valid game states. */
    uint8_t  unmodelled;

    const pop_frametab *frames;
} pop_game;

#endif /* POP_GAME_H */
