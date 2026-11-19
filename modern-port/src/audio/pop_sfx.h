/* pop_sfx.h - sound effects as speaker toggle streams, and their rendering.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * An Apple II sound effect is a list of moments at which the program
 * touched the speaker switch.  The lists are produced by executing the
 * original SOUND.S in a 6502 simulator (tools/extract_audio) and loaded from
 * a generated file; this module turns such a list into PCM samples.  It has
 * no I/O and no SDL.
 *
 * See docs/audio-analysis.md.
 */
#ifndef POP_SFX_H
#define POP_SFX_H

#include <stddef.h>
#include <stdint.h>

/* Average CPU clock of an NTSC Apple II, in Hz (hardware fact, not from the
 * repository).  The generated data carries its own copy. */
#define POP_APPLE2_CPU_HZ 1020484u

typedef struct pop_sfx {
    const uint32_t *toggles;   /* CPU cycle of each speaker access, ascending */
    uint32_t        count;     /* number of accesses */
    uint32_t        total_cycles; /* length of the PLAYBACK call that produced it */
} pop_sfx;

typedef struct pop_sfx_render_opts {
    uint32_t cpu_hz;       /* cycles per second */
    uint32_t rate;         /* output samples per second */
    double   lead_seconds; /* silence before cycle 0 */
    double   tail_seconds; /* silence after total_cycles */
    float    amplitude;    /* output is +/- amplitude */
    int      start_level;  /* -1 or +1: speaker state before the first access */
} pop_sfx_render_opts;

/* Cycles between speaker accesses in the original `tone` loop for a 16-bit
 * pitch value: hi * (9 * lo + 10) + 12, with a zero byte counting as 1
 * (derivation in docs/audio-analysis.md). */
uint32_t pop_tone_half_period(uint16_t pitch);

/* Number of samples pop_sfx_render produces for this effect. */
size_t pop_sfx_render_length(const pop_sfx *fx, const pop_sfx_render_opts *o);

/* Render the 1-bit speaker signal.  Each sample is the average of the
 * signal over that sample's time span.  Writes at most `capacity` samples
 * and returns the number written.  If `end_level` is not NULL it receives
 * the speaker state after the last access. */
size_t pop_sfx_render(const pop_sfx *fx, const pop_sfx_render_opts *o,
                      float *out, size_t capacity, int *end_level);

#endif /* POP_SFX_H */
