/* pop_sfx.c - see pop_sfx.h.  New code. */

#include "pop_sfx.h"

uint32_t pop_tone_half_period(uint16_t pitch)
{
    uint32_t lo = pitch & 0xFFu, hi = (uint32_t)(pitch >> 8);
    if (lo == 0) lo = 1;
    if (hi == 0) hi = 1;
    return hi * (9u * lo + 10u) + 12u;
}

size_t pop_sfx_render_length(const pop_sfx *fx, const pop_sfx_render_opts *o)
{
    double length = o->lead_seconds + (double)fx->total_cycles / (double)o->cpu_hz
                    + o->tail_seconds;
    return (size_t)(length * (double)o->rate) + 1u;
}

size_t pop_sfx_render(const pop_sfx *fx, const pop_sfx_render_opts *o,
                      float *out, size_t capacity, int *end_level)
{
    size_t n = pop_sfx_render_length(fx, o);
    size_t i, written = 0;
    uint32_t k = 0;
    int level = o->start_level < 0 ? -1 : 1;
    double rate = (double)o->rate, hz = (double)o->cpu_hz;

    for (i = 0; i < n && written < capacity; i++) {
        double t0 = (double)i / rate, t1 = (double)(i + 1u) / rate;
        double acc = 0.0, t = t0;
        while (k < fx->count) {
            double edge = o->lead_seconds + (double)fx->toggles[k] / hz;
            if (edge >= t1) break;
            acc += (double)level * (edge - t);
            t = edge;
            level = -level;
            k++;
        }
        acc += (double)level * (t1 - t);
        out[written++] = (float)((double)o->amplitude * acc * rate);
    }
    if (end_level) {
        /* accesses that fall after the rendered span still flip the speaker */
        if ((fx->count - k) & 1u) level = -level;
        *end_level = level;
    }
    return written;
}
