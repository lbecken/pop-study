/* pop_sound.c - see pop_sound.h.  New code. */

#include "pop_sound.h"

void pop_zerosound(pop_soundq *q)
{
    q->count = 0;                 /* the entries themselves are left as they were */
}

void pop_addsound(pop_soundq *q, uint8_t sound)
{
    /* ldx soundtable / cpx #maxsfx / bcs :rts / inx / sta soundtable,x */
    if (q->count >= POP_MAXSFX) {
        return;
    }
    q->items[q->count] = sound;
    q->count++;
}

int pop_soundq_play_order(const pop_soundq *q, unsigned i)
{
    if (i >= q->count) {
        return -1;
    }
    return q->items[q->count - 1u - i];
}

int pop_sound_is_valid(uint8_t sound)
{
    /* asl discards bit 7 before the compare, so 128+n behaves like n. */
    return (uint8_t)(sound << 1) < 2u * POP_SFX_COUNT;
}
