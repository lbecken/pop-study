/* pop_sfxdump - render every effect and print the samples.
 *
 * New code.  Used by tests/compare_sfx.py to compare the C renderer with
 * the Python one.
 *
 *   pop_sfxdump <sfx.bin> <rate>
 *
 * Output: one line "effect <n> <samples> <end_level>" followed by that many
 * lines with one sample each, rendered with 5 ms lead and tail, amplitude
 * 0.5 and start level -1.
 */

#include <stdio.h>
#include <stdlib.h>

#include "pop_assets.h"
#include "pop_sfx.h"

int main(int argc, char **argv)
{
    pop_sfxfile file;
    pop_sfx_render_opts o = { 0, 44100u, 0.005, 0.005, 0.5f, -1 };
    const char *err = NULL;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <sfx.bin> <rate>\n", argv[0]);
        return 2;
    }
    if (pop_sfxfile_load(argv[1], &file, &err) != 0) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 2;
    }
    o.cpu_hz = file.cpu_hz;
    o.rate = (uint32_t)atoi(argv[2]);
    for (i = 0; i < file.count; i++) {
        size_t n = pop_sfx_render_length(&file.effects[i], &o), j;
        float *buf = malloc(n * sizeof *buf);
        int end = 0;
        if (!buf) return 2;
        n = pop_sfx_render(&file.effects[i], &o, buf, n, &end);
        printf("effect %d %lu %d\n", i, (unsigned long)n, end);
        for (j = 0; j < n; j++) printf("%.7f\n", (double)buf[j]);
        free(buf);
    }
    pop_sfxfile_free(&file);
    return 0;
}
