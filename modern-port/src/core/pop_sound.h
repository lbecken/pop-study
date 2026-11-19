/* pop_sound.h - the per-frame sound effect queue.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Ported routines (01 POP Source/Source, original commit 25243550):
 *
 *   pop_zerosound   ZEROSOUND   SOUND.S 64-67
 *   pop_addsound    ADDSOUND    SOUND.S 79-91 (same code in SPECIALK.S 992-1004)
 *   play order      PLAYBACK    SOUND.S 100-118
 *
 * The game queues effect numbers while it simulates a frame, then plays the
 * whole queue after drawing and empties it (TOPCTRL.S 388-389).
 */
#ifndef POP_SOUND_H
#define POP_SOUND_H

#include <stdint.h>

#define POP_MAXSFX 32   /* maxsfx = $20, GAMEEQ.S 50 */
#define POP_SFX_COUNT 20 /* entries in the dispatch table, SOUND.S 32-51 */

/* soundtable, GAMEEQ.S 63: byte 0 is the count, entries follow from 1. */
typedef struct pop_soundq {
    uint8_t count;
    uint8_t items[POP_MAXSFX];
} pop_soundq;

void pop_zerosound(pop_soundq *q);

/* Append a sound number.  Silently ignored when the queue already holds
 * POP_MAXSFX entries, as in the original. */
void pop_addsound(pop_soundq *q, uint8_t sound);

/* The i-th sound PLAYBACK plays (0-based): it walks the table from the last
 * entry down to the first.  Returns -1 when i is out of range. */
int pop_soundq_play_order(const pop_soundq *q, unsigned i);

/* 1 if makesound would dispatch this number: `asl / cmp #maxaddr / bcs`
 * (SOUND.S 127-130) accepts 0..POP_SFX_COUNT-1 only. */
int pop_sound_is_valid(uint8_t sound);

#endif /* POP_SOUND_H */
