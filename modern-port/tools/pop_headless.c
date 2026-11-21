/* pop_headless - play the game without a window, and say what it showed.
 *
 * New code for the pop-study reconstruction.  This is the game as the SDL3
 * shell runs it (pop_app), minus the window, the clock and the sound
 * device: for tests, and for looking at frames where there is no display.
 *
 * Usage:
 *   pop_headless <data dir> <original checkout> <level> <frames> [options]
 *     --input <file>     one line per frame: "<x> <y> <button> <key>" (x, y: -1, 0, 1);
 *                        frames beyond the file get no input
 *     --png <dir> <every>   write every n-th frame as <dir>/frame_NNNN.png
 *     --mono             ... as a monochrome monitor shows it
 *     --images <dir>     read the image tables from this directory of replacement
 *                        pictures instead of the checkout (docs/blender-to-sdl3.md)
 *     --wav <file>       the sound effects of the whole run, each frame's at the
 *                        time its frame starts when frames follow each other at
 *                        the fixed rate (725 a minute)
 *
 * Prints a line per frame: its number, why the level was left if it was
 * (1 title sequence, 2 time up, 3 won, 4 a level started), the level, the
 * CRC-32 of the two hi-res pages, the display switches touched, and the
 * number of sound effects with the time the original would have spent
 * playing them, in 6502 cycles.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pop_app.h"
#include "pop_png.h"

enum { RATE = 22050 };

static void le(FILE *f, uint32_t v, int bytes)
{
    int i;
    for (i = 0; i < bytes; i++) fputc((int)((v >> (8 * i)) & 0xFF), f);
}

static int write_wav(const char *path, const float *samples, size_t n)
{
    FILE *f = fopen(path, "wb");
    size_t i;
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f); le(f, (uint32_t)(36 + 2 * n), 4); fwrite("WAVEfmt ", 1, 8, f);
    le(f, 16, 4); le(f, 1, 2); le(f, 1, 2); le(f, RATE, 4); le(f, 2 * RATE, 4); le(f, 2, 2); le(f, 16, 2);
    fwrite("data", 1, 4, f); le(f, (uint32_t)(2 * n), 4);
    for (i = 0; i < n; i++) {
        float s = samples[i] > 1.0f ? 1.0f : samples[i] < -1.0f ? -1.0f : samples[i];
        le(f, (uint32_t)(int32_t)(s * 32767.0f) & 0xFFFFu, 2);
    }
    return fclose(f);
}

int main(int argc, char **argv)
{
    static pop_app app;
    static uint8_t rgb[POP_SCREEN_W * POP_SCREEN_H * 3];
    pop_app_config config;
    const char *err = NULL, *png_dir = NULL, *wav = NULL, *images = NULL;
    FILE *input = NULL;
    pop_present_mode mode = POP_PRESENT_COLOUR;
    long frames, n;
    int every = 1, i, status = 0;
    float *mix = NULL;
    size_t mix_len = 0, mix_cap = 0;

    if (argc < 5) {
        fprintf(stderr, "usage: pop_headless <data dir> <original checkout> <level> <frames> "
                        "[--input file] [--png dir every] [--mono] [--wav file] [--images dir]\n");
        return 2;
    }
    frames = strtol(argv[4], NULL, 10);
    for (i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--input") && i + 1 < argc) {
            input = fopen(argv[++i], "r");
            if (!input) { fprintf(stderr, "%s: cannot open\n", argv[i]); return 2; }
        } else if (!strcmp(argv[i], "--png") && i + 2 < argc) {
            png_dir = argv[++i];
            every = atoi(argv[++i]);
            if (every < 1) every = 1;
        } else if (!strcmp(argv[i], "--mono")) {
            mode = POP_PRESENT_MONO;
        } else if (!strcmp(argv[i], "--wav") && i + 1 < argc) {
            wav = argv[++i];
        } else if (!strcmp(argv[i], "--images") && i + 1 < argc) {
            images = argv[++i];
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    memset(&config, 0, sizeof config);
    config.data_dir = argv[1];
    config.source_dir = argv[2];
    config.images_dir = images;
    config.audio_rate = wav ? RATE : 0;
    if (pop_app_open(&app, &config, &err) != 0
            || pop_app_start(&app, (uint8_t)strtol(argv[3], NULL, 10), &err) != 0) {
        fprintf(stderr, "pop_headless: %s\n", err ? err : "cannot start");
        return 1;
    }
    if (images) fprintf(stderr, "pop_headless: replacement pictures from %s\n", images);
    for (n = 0; n <= frames; n++) {
        pop_app_input in;
        int leave = 0;
        memset(&in, 0, sizeof in);
        if (n > 0) {                             /* frame 0 is the one the start drew */
            if (input) {
                char line[64];
                if (fgets(line, sizeof line, input)) {
                    sscanf(line, "%d %d %d %d", &in.x, &in.y, &in.button, &in.any_key);
                }
            }
            leave = pop_app_frame(&app, &in);
            if (leave < 0) {
                fprintf(stderr, "pop_headless: %s\n", app.error ? app.error : "cannot go on");
                status = 1;
                break;
            }
        }
        printf("frame %ld leave=%d level=%u pg1=%08lx pg2=%08lx sw=%s sounds=%u cycles=%lu\n", n, leave,
               (unsigned)app.g.level,
               (unsigned long)pop_png_crc(0, app.v.hr.vram, POP_HR_PAGE),
               (unsigned long)pop_png_crc(0, app.v.hr.vram + POP_HR_PAGE, POP_HR_PAGE),
               app.v.nswlog ? app.v.swlog : "-", app.sound_count,
               (unsigned long)app.sound_cycles);
        if (png_dir && n % every == 0) {
            char file[1024];
            snprintf(file, sizeof file, "%s/frame_%04ld.png", png_dir, n);
            pop_app_picture(&app, mode, rgb);
            if (pop_png_write(file, rgb, POP_SCREEN_W, POP_SCREEN_H, 2) != 0) {
                fprintf(stderr, "%s: cannot write\n", file);
                status = 1;
                break;
            }
        }
        if (wav) {
            /* frame n starts at n * 60 / 725 seconds */
            size_t at = (size_t)((double)n * 60.0 / 725.0 * RATE), k;
            if (at + app.audio_len > mix_cap) {
                size_t cap = (at + app.audio_len) * 2u + RATE;
                float *grown = realloc(mix, cap * sizeof *grown);
                if (!grown) { status = 1; break; }
                memset(grown + mix_cap, 0, (cap - mix_cap) * sizeof *grown);
                mix = grown;
                mix_cap = cap;
            }
            for (k = 0; k < app.audio_len; k++) mix[at + k] += app.audio[k];
            if (at + app.audio_len > mix_len) mix_len = at + app.audio_len;
        }
        if (leave != 0 && leave != POP_LEAVE_RESTART) break;      /* the unported parts begin */
    }
    if (app.unmodelled) fprintf(stderr, "pop_headless: the game left what the port models\n");
    if (wav && status == 0 && write_wav(wav, mix, mix_len) != 0) {
        fprintf(stderr, "%s: cannot write\n", wav);
        status = 1;
    }
    free(mix);
    if (input) fclose(input);
    pop_app_close(&app);
    return status;
}
