/* pop_char.h - the 16-byte character record.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Original: "character data", GAMEEQ.S lines 566-581 (dum Char), documented
 * by the author in 04 Support/MakeDisk/S/CHARCOMMENTS.S lines 7-106.
 * Zero-page addresses are for the working copy "Char" at $40; the kid's and
 * opponent's permanent copies are at $50 and $60.
 *
 * Every field is one byte in the original and stays uint8_t here, so that
 * arithmetic wraps exactly as on the 6502.  Fields with a signed meaning are
 * noted; read them through pop_s8().
 */
#ifndef POP_CHAR_H
#define POP_CHAR_H

#include <stdint.h>

#define POP_FACE_LEFT  0xFFu /* CharFace = -1: facing left (artwork as stored) */
#define POP_FACE_RIGHT 0x00u /* CharFace =  0: facing right (drawn mirrored)   */

typedef struct pop_char {
    uint8_t  posn;    /* $40 CharPosn    frame number being shown             */
    uint8_t  x;       /* $41 CharX       140-wide units, screen left = 58     */
    uint8_t  y;       /* $42 CharY       screen line of the floor under foot  */
    uint8_t  face;    /* $43 CharFace    POP_FACE_LEFT or POP_FACE_RIGHT      */
    uint8_t  block_x; /* $44 CharBlockX  0-9                                  */
    uint8_t  block_y; /* $45 CharBlockY  0-2 (wraps; -1 and 3 are off screen) */
    uint8_t  action;  /* $46 CharAction  see the `act` sequence instruction   */
    uint8_t  x_vel;   /* $47 CharXVel    signed                               */
    uint8_t  y_vel;   /* $48 CharYVel    signed                               */
    uint16_t seq;     /* $49 CharSeq     address in the sequence table        */
    uint8_t  scrn;    /* $4B CharScrn    screen number, 0 = none              */
    uint8_t  repeat;  /* $4C CharRepeat                                       */
    uint8_t  id;      /* $4D CharID      0 kid, 1 shadow, 2 guard, ...        */
    uint8_t  sword;   /* $4E CharSword   0 sheathed, 2 drawn                  */
    uint8_t  life;    /* $4F CharLife    $FF alive, 0-127 dead                */
} pop_char;

/* Interpret a byte as a signed 8-bit value. */
static inline int pop_s8(uint8_t b) { return b >= 0x80u ? (int)b - 256 : (int)b; }

#endif /* POP_CHAR_H */
