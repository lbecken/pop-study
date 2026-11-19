/* pop_seq.c - see pop_seq.h.  New code; ports of four original routines. */

#include "pop_seq.h"

/* ADDCHARX, CTRLSUBS.S 353-363.
 *
 *     bit CharFace        ; -1 = left (normal)
 *     bpl :right          ;  0 = right (mirrored)
 *     eor #$ff / clc / adc #1      ; A := -A
 *   :right clc / adc CharX
 *
 * Only bit 7 of CharFace is tested.  The carry out of the addition is
 * discarded, so the result wraps modulo 256.
 */
uint8_t pop_add_char_x(const pop_char *c, uint8_t a)
{
    if (c->face & 0x80u) {
        a = (uint8_t)(0u - a);
    }
    return (uint8_t)(a + c->x);
}

static pop_seq_status read_at(const pop_seqtab *t, uint16_t address, uint8_t *out)
{
    /* Unsigned subtraction: an address below the origin becomes a large
     * offset and fails the size test. */
    size_t off = (uint16_t)(address - t->origin);
    if (address < t->origin || off >= t->size) {
        return POP_SEQ_ERR_BOUNDS;
    }
    *out = t->data[off];
    return POP_SEQ_OK;
}

/* JUMPSEQ, CTRLSUBS.S 700-711.
 *
 *     sec / sbc #1 / asl / tax     ; x = 2(a-1)
 *     lda seqtab,x   : sta CharSeq
 *     lda seqtab+1,x : sta CharSeq+1
 *
 * The index is computed in 8 bits: (a-1)*2 with the top bit shifted out.
 * Sequence numbers 1-127 are the documented range; 0 and 129-255 alias
 * other entries exactly as they would on the 6502.
 */
pop_seq_status pop_jumpseq(const pop_seqtab *t, pop_char *c, uint8_t seq_no)
{
    uint8_t index = (uint8_t)((uint8_t)(seq_no - 1u) << 1);
    uint8_t lo, hi;
    pop_seq_status st;

    st = read_at(t, (uint16_t)(t->origin + index), &lo);
    if (st != POP_SEQ_OK) return st;
    st = read_at(t, (uint16_t)(t->origin + index + 1u), &hi);
    if (st != POP_SEQ_OK) return st;
    c->seq = (uint16_t)(lo | ((uint16_t)hi << 8));
    return POP_SEQ_OK;
}

/* GETSEQ, CTRLSUBS.S 1629-1639: lda (CharSeq),y ; inc CharSeq (16-bit). */
pop_seq_status pop_getseq(const pop_seqtab *t, pop_char *c, uint8_t *out)
{
    pop_seq_status st = read_at(t, c->seq, out);
    if (st != POP_SEQ_OK) return st;
    c->seq = (uint16_t)(c->seq + 1u);
    return POP_SEQ_OK;
}

#define GET(var)                                   \
    do {                                           \
        st = pop_getseq(t, c, &(var));             \
        if (st != POP_SEQ_OK) return st;           \
    } while (0)

/* ANIMCHAR, COLL.S 994-1156. */
pop_seq_status pop_animchar(const pop_seqtab *t, pop_char *c,
                            pop_anim_globals *g, const pop_anim_hooks *hooks)
{
    pop_seq_status st;
    int scanned;

    for (scanned = 0; scanned < POP_SEQ_MAX_SCAN; scanned++) {
        uint8_t op, a, b;

        GET(op);                                   /* :next jsr getseq */
        switch (op) {
        case POP_SEQ_CHX:                          /* COLL.S 999-1007 */
            GET(a);
            c->x = pop_add_char_x(c, a);
            break;

        case POP_SEQ_CHY:                          /* 1010-1019 */
            GET(a);
            c->y = (uint8_t)(a + c->y);
            break;

        case POP_SEQ_ABOUTFACE:                    /* 1022-1029 */
            c->face = (uint8_t)(c->face ^ 0xFFu);
            break;

        case POP_SEQ_GOTO:                         /* 1032-1044 */
            GET(a);                                /* low byte  */
            GET(b);                                /* high byte */
            c->seq = (uint16_t)(a | ((uint16_t)b << 8));
            break;

        case POP_SEQ_UP:                           /* 1047-1054 */
            c->block_y = (uint8_t)(c->block_y - 1u);
            if (hooks && hooks->add_slicers) hooks->add_slicers(hooks->ctx);
            break;

        case POP_SEQ_DOWN:                         /* 1057-1064 */
            c->block_y = (uint8_t)(c->block_y + 1u);
            if (hooks && hooks->add_slicers) hooks->add_slicers(hooks->ctx);
            break;

        case POP_SEQ_ACT:                          /* 1067-1073 */
            GET(a);
            c->action = a;
            break;

        case POP_SEQ_SETFALL:                      /* 1075-1084 */
            GET(a);
            c->x_vel = a;
            GET(b);
            c->y_vel = b;
            break;

        case POP_SEQ_IFWTLESS:                     /* 1086-1094 */
            GET(a);
            GET(b);
            if (g->weightless != 0) {              /* yes--branch (as goto) */
                c->seq = (uint16_t)(a | ((uint16_t)b << 8));
            }                                      /* no--the 2 bytes are skipped */
            break;

        case POP_SEQ_DIE:                          /* 1096-1098: no effect */
            break;

        case POP_SEQ_JARU:                         /* 1100-1105 */
            g->jarabove = 1;
            break;

        case POP_SEQ_JARD:                         /* 1107-1112 */
            g->jarabove = 0xFFu;
            break;

        case POP_SEQ_TAP:                          /* 1115-1134 */
            GET(a);
            if (a == 1 || a == 2) {
                if (hooks && hooks->add_sound) {
                    hooks->add_sound(hooks->ctx,
                                     a == 1 ? POP_SND_FOOTSTEP : POP_SND_SMACKWALL);
                }
            }
            if (a <= 2) {
                g->alertguard = 1;                 /* selectors above 2 do nothing */
            }
            break;

        case POP_SEQ_NEXTLEVEL:                    /* 1136-1140 */
            if (hooks && hooks->gone_upstairs) hooks->gone_upstairs(hooks->ctx);
            break;

        case POP_SEQ_EFFECT:                       /* 1142-1150 */
            GET(a);
            if (a == 1) {
                if (hooks && hooks->potion_effect) hooks->potion_effect(hooks->ctx);
            }
            break;

        default:                                   /* :no15 sta CharPosn */
            c->posn = op;
            return POP_SEQ_OK;
        }
    }
    return POP_SEQ_ERR_NO_FRAME;
}
