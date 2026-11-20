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

/* Tables from AUTO.S that describe content rather than rules: how each kind
 * of opponent fights, where the shadow appears, a scripted movement.  They
 * are generated from the user's checkout like the other game data.
 *   fight: 124 bytes, the eight per-program tables (12 entries each) and the
 *          two per-level tables (14 each), contiguous as in memory;
 *   shadpos: 3 x 8 bytes (Posn, X, Y, Face, BlockX, BlockY, Action, sequence)
 *          for levels 6, 5 and 12;
 *   shadprog5: (frame, command) pairs for the shadow's theft on level 5. */
typedef struct pop_autodata {
    const uint8_t *fight;
    const uint8_t *shadpos;
    const uint8_t *shadprog5;
    size_t         shadprog5_len;
} pop_autodata;

enum {
    POP_FT_STRIKE = 0, POP_FT_RESTRIKE = 12, POP_FT_BLOCK = 24, POP_FT_IMPBLOCK = 36,
    POP_FT_ADV = 48, POP_FT_REFRACT = 60, POP_FT_SPECIALCOLOR = 72, POP_FT_EXTRASTR = 84,
    POP_FT_BASICSTR = 96, POP_FT_BASICCOLOR = 110, POP_FT_SIZE = 124,
    POP_SHADPOS_6A = 0, POP_SHADPOS_5 = 8, POP_SHADPOS_12 = 16, POP_SHADPOS_SIZE = 24
};

/* Guard start data per screen, inside the level's INFO area (EQ.S 282-287). */
enum {
    POP_GD_BLOCK = 2119, POP_GD_FACE = 2143, POP_GD_X = 2167, POP_GD_SEQL = 2191,
    POP_GD_PROG = 2215, POP_GD_SEQH = 2239
};

/* Calls into subsystems that are not ported yet (moving level objects,
 * potion effects, level change).  Each is made at the point where the
 * original has its `jsr`.  Any pointer may be NULL. */
typedef struct pop_game_hooks {
    void *ctx;
    void (*invert_y)(void *ctx);         /* inverty: the upside-down potion's redraw */
    void (*add_refl_obj)(void *ctx);     /* addreflobj: draw the kid's reflection */
    void (*attract_mode)(void *ctx);     /* attractmode: a scripted demo has ended */
    void (*desel_input)(void *ctx);      /* LoadDesel, getdesel, clrjstk: opponent played by hand */
    void (*save_desel)(void *ctx);       /* SaveDesel */
    void (*speaker_click)(void *ctx);    /* a read of $C030, COLL.S 403 */
    /* PLAYERCTRL's input step: LoadSelect, getselect, clrjstk (CTRL.S
     * 573-577).  Expected to set JSTKX, JSTKY, btn and the clr* flags. */
    void (*get_input)(void *ctx);
    void (*save_input)(void *ctx);       /* SaveSelect,   CTRL.S 582  */
    void (*demo_ctrl)(void *ctx);        /* DemoCtrl,     CTRL.S 587: the demo level's kid */
} pop_game_hooks;

/* The redraw buffers are one block of memory (EQ.S 218-229): 30 bytes each,
 * one per block of the screen, and 10 for the top line.  They are kept as
 * one array because the original indexes them with unchecked 8-bit values:
 * index 30 of one buffer is index 0 of the next. */
enum {
    POP_RB_HALF = 60, POP_RB_RED = 90, POP_RB_FRED = 120, POP_RB_FLOOR = 150,
    POP_RB_WIPE = 180, POP_RB_MOVE = 210, POP_RB_OBJ = 240, POP_RB_WHITE = 270,
    POP_RB_TOP = 300, POP_RB_SIZE = 310
};

#define POP_TROBSPACE 32                 /* GAMEEQ.S 48 */
#define POP_MOBSPACE  16                 /* GAMEEQ.S 49 */

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

    /* --- moving level objects (MOVER.S; GAMEEQ.S 46-65) ---
     * TROBs ("transitional objects": gates, plates, spikes, slicers, loose
     * floors before they drop, torches, flasks, the sword) are a list of
     * (block, screen, direction); MOBs are falling floors.  In both, entry 0
     * is the working copy the routines operate on and the list is 1..count. */
    uint8_t  trloc[POP_TROBSPACE], trscrn[POP_TROBSPACE], trdirec[POP_TROBSPACE];   /* $B600 */
    uint8_t  mobx[POP_MOBSPACE], moby[POP_MOBSPACE], mobscrn[POP_MOBSPACE];        /* $B660 */
    uint8_t  mobvel[POP_MOBSPACE], mobtype[POP_MOBSPACE], moblevel[POP_MOBSPACE];
    uint8_t  numtrans, nummob;           /* $95, $30D */
    uint8_t  trobcount, tempnt;          /* $B6E0, $96 */
    uint8_t  exitopen;                   /* $86 */
    uint8_t  RNDseed;                    /* $9E */
    uint8_t  scrnAbove, scrnBelow;       /* $33, $34 */
    uint8_t  BGset1;                     /* $303: background set, 1 = palace */
    /* scratch that carries values between MOVER routines (its `locals`) */
    uint8_t  mv_state, linkindex, pptype, underFF;

    /* --- opponents, fighting, screen changes (AUTO.S, MISC.S) --- */
    uint8_t  guardprog;                  /* $DD: the opponent's "program": index into the fight tables */
    uint8_t  justblocked;                /* $7E: frames of impaired blocking after a blocked strike */
    uint8_t  shadowaction;               /* $217 */
    uint8_t  PlayCount, PreRecPtr;       /* $A0, $309: scripted-movement playback */
    uint8_t  CUTTIMER, cutscrn;          /* $28, $A6 */
    uint8_t  milestone;                  /* $212 */
    uint8_t  MaxKidStr, MaxOppStr;       /* $D0, $DC */
    uint8_t  origstrength;               /* $E1 */
    uint8_t  GuardColor;                 /* $216 */
    uint8_t  lightcolor, lightning;      /* $89, $88: colour and number of screen flashes asked for */
    uint8_t  ManCtrl;                    /* $DE: non-zero = the opponent is played by hand */
    uint8_t  vibes, invert, redrawflg;   /* $318, $9F, $97 */
    uint8_t  msgtimer;                   /* $DB */
    uint8_t  timerequest;                /* ask for the time-left message */
    uint8_t  mirrx, dmirr;               /* $B0, $B1: mirror x, distance from it (REFLECTION) */
    const struct pop_autodata *autodata; /* fight tables and scripted positions (generated) */

    /* --- redraw marks (CTRLSUBS.S 1734-1757) --- */
    uint8_t  rb[POP_RB_SIZE];            /* $5E00: the redraw buffers, see POP_RB_* */
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
