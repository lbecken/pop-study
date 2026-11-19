/* pop_seq.h - animation sequence table and its interpreter.
 *
 * New code for the pop-study reconstruction; no original code or data.
 * The sequence table itself is original data and is NOT part of this
 * repository: it is generated from the user's checkout (see README.md).
 *
 * Ported routines (01 POP Source/Source of the original repository,
 * commit 25243550):
 *
 *   pop_add_char_x   ADDCHARX   CTRLSUBS.S 353-363
 *   pop_jumpseq      JUMPSEQ    CTRLSUBS.S 700-711
 *   pop_getseq       GETSEQ     CTRLSUBS.S 1629-1639
 *   pop_animchar     ANIMCHAR   COLL.S 994-1156
 *
 * See docs/port/sequence-interpreter.md for pseudocode, inputs/outputs and
 * the list of semantic pitfalls.
 */
#ifndef POP_SEQ_H
#define POP_SEQ_H

#include <stddef.h>
#include <stdint.h>

#include "pop_char.h"

/* Sequence instruction codes, SEQTABLE.S 10-24.  Any byte that is not one of
 * these is a frame number and ends the scan. */
enum {
    POP_SEQ_GOTO      = 0xFF, /* -1  addr lo, addr hi */
    POP_SEQ_ABOUTFACE = 0xFE, /* -2                   */
    POP_SEQ_UP        = 0xFD, /* -3                   */
    POP_SEQ_DOWN      = 0xFC, /* -4                   */
    POP_SEQ_CHX       = 0xFB, /* -5  dx               */
    POP_SEQ_CHY       = 0xFA, /* -6  dy               */
    POP_SEQ_ACT       = 0xF9, /* -7  action           */
    POP_SEQ_SETFALL   = 0xF8, /* -8  xvel, yvel       */
    POP_SEQ_IFWTLESS  = 0xF7, /* -9  addr lo, addr hi */
    POP_SEQ_DIE       = 0xF6, /* -10                  */
    POP_SEQ_JARU      = 0xF5, /* -11                  */
    POP_SEQ_JARD      = 0xF4, /* -12                  */
    POP_SEQ_EFFECT    = 0xF3, /* -13 effect number    */
    POP_SEQ_TAP       = 0xF2, /* -14 sound selector   */
    POP_SEQ_NEXTLEVEL = 0xF1  /* -15                  */
};

/* Sound numbers queued by `tap`, SOUNDNAMES.S 15 and 19. */
enum { POP_SND_FOOTSTEP = 9, POP_SND_SMACKWALL = 13 };

/* A read-only view of the assembled sequence table.  `data[0]` is the byte
 * the game had at address `origin` ($3000 in the shipped game). */
typedef struct pop_seqtab {
    const uint8_t *data;
    size_t         size;
    uint16_t       origin;
} pop_seqtab;

/* The game globals ANIMCHAR reads or writes besides the character record. */
typedef struct pop_anim_globals {
    uint8_t weightless;  /* $C5 in:  non-zero makes `ifwtless` branch        */
    uint8_t jarabove;    /* $CD out: 1 after `jaru`, $FF after `jard`        */
    uint8_t alertguard;  /* $E4 out: 1 after any `tap` 0, 1 or 2             */
} pop_anim_globals;

/* Calls ANIMCHAR makes into subsystems that are not ported yet.  They are
 * invoked at the same point in the scan as the original `jsr`.  Any pointer
 * may be NULL, in which case the call is skipped. */
typedef struct pop_anim_hooks {
    void *ctx;
    void (*add_sound)(void *ctx, uint8_t sound);   /* addsound      */
    void (*add_slicers)(void *ctx);                /* addslicers    */
    void (*gone_upstairs)(void *ctx);              /* GoneUpstairs  */
    void (*potion_effect)(void *ctx);              /* potioneffect  */
} pop_anim_hooks;

typedef enum pop_seq_status {
    POP_SEQ_OK = 0,
    /* The scan read outside the table.  The original would have read
     * whatever else was in memory; the port stops instead. */
    POP_SEQ_ERR_BOUNDS,
    /* No frame number within POP_SEQ_MAX_SCAN instructions.  The original
     * would loop forever; the port stops instead. */
    POP_SEQ_ERR_NO_FRAME
} pop_seq_status;

#define POP_SEQ_MAX_SCAN 256

/* CharX + a when facing right, CharX - a when facing left; 8-bit result. */
uint8_t pop_add_char_x(const pop_char *c, uint8_t a);

/* Point c->seq at sequence number `seq_no` (1-127). */
pop_seq_status pop_jumpseq(const pop_seqtab *t, pop_char *c, uint8_t seq_no);

/* Read the byte at c->seq and advance c->seq by one. */
pop_seq_status pop_getseq(const pop_seqtab *t, pop_char *c, uint8_t *out);

/* Execute sequence instructions until a frame number is read; store it in
 * c->posn.  On an error return, c holds the state reached so far. */
pop_seq_status pop_animchar(const pop_seqtab *t, pop_char *c,
                            pop_anim_globals *g, const pop_anim_hooks *hooks);

#endif /* POP_SEQ_H */
