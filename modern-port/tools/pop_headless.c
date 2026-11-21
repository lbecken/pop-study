/* pop_headless - play the game without a window, and say what it showed.
 *
 * New code for the pop-study reconstruction.  This is the game as the SDL3
 * shell runs it (pop_app), minus the window, the clock and the sound
 * device: for tests, and for looking at frames where there is no display.
 *
 * Usage:
 *   pop_headless <data dir> <original checkout> <level> <frames> [options]
 *     --input <file>     one line per frame: "<x> <y> <button> <key> [<held>]"; x, y:
 *                        -1, 0, 1 (the joystick); key: 0, or a key typed, as an Apple II
 *                        key code (0x9B Escape, 0xA0 space, 0x93 ctrl-S ...; 1 stands
 *                        for Return); held: a key is down (default: when one is typed).
 *                        Frames beyond the file get no input.  While the game is frozen
 *                        (Escape) lines are read without a frame passing, until one
 *                        has a key
 *     --save <file>      where ctrl-G saves the game (16 bytes, the original's layout)
 *     --resume           start from that file instead of on <level>
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
 * CRC-32 of the two hi-res pages, the display switches touched, the
 * number of sound effects with the time the original would have spent
 * playing them, in 6502 cycles, and the number of key-confirmation beeps.
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

static int parse_input(const char *line, pop_app_input *in, uint8_t *key)
{
    long x = 0, y = 0, button = 0, k = 0, held = -1;
    char *end;
    x = strtol(line, &end, 0);
    y = strtol(end, &end, 0);
    button = strtol(end, &end, 0);
    k = strtol(end, &end, 0);
    while (*end == ' ') end++;
    if (*end && *end != '\n') held = strtol(end, NULL, 0);
    in->x = (int)x; in->y = (int)y; in->button = (int)button;
    *key = k == 1 ? 0x8D : (uint8_t)k;
    in->key_down = held >= 0 ? held != 0 : k != 0;
    return 0;
}

/* The game is frozen and waits for a key: read on in the input. */
static uint8_t wait_key(void *ctx)
{
    FILE *input = ctx;
    char line[64];
    while (input && fgets(line, sizeof line, input)) {
        pop_app_input in;
        uint8_t key = 0;
        (void)parse_input(line, &in, &key);
        if (key & 0x80) return key;
    }
    return 0;
}

int main(int argc, char **argv)
{
    static pop_app app;
    static uint8_t rgb[POP_SCREEN_W * POP_SCREEN_H * 3];
    pop_app_config config;
    const char *err = NULL, *png_dir = NULL, *wav = NULL, *images = NULL, *save = NULL;
    FILE *input = NULL;
    pop_present_mode mode = POP_PRESENT_COLOUR;
    long frames, n;
    int every = 1, i, status = 0, resume = 0;
    float *mix = NULL;
    size_t mix_len = 0, mix_cap = 0;

    if (argc < 5) {
        fprintf(stderr, "usage: pop_headless <data dir> <original checkout> <level> <frames> "
                        "[--input file] [--png dir every] [--mono] [--wav file] [--images dir] "
                        "[--save file] [--resume]\n");
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
        } else if (!strcmp(argv[i], "--save") && i + 1 < argc) {
            save = argv[++i];
        } else if (!strcmp(argv[i], "--resume")) {
            resume = 1;
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
    config.save_path = save;
    config.wait_key = wait_key;
    config.wait_ctx = input;
    if (pop_app_open(&app, &config, &err) != 0
            || (resume ? pop_app_resume(&app, &err)
                       : pop_app_start(&app, (uint8_t)strtol(argv[3], NULL, 10), &err)) != 0) {
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
                uint8_t key = 0;
                if (fgets(line, sizeof line, input)) {
                    (void)parse_input(line, &in, &key);
                    pop_app_key(&app, key);
                }
            }
            leave = pop_app_frame(&app, &in);
            if (leave < 0) {
                fprintf(stderr, "pop_headless: %s\n", app.error ? app.error : "cannot go on");
                status = 1;
                break;
            }
            if (leave == POP_LEAVE_FROZEN) {
                fprintf(stderr, "pop_headless: frozen at frame %ld, and no key in the input\n", n);
                break;
            }
        }
        printf("frame %ld leave=%d level=%u pg1=%08lx pg2=%08lx sw=%s sounds=%u cycles=%lu "
               "beeps=%u\n", n, leave,
               (unsigned)app.g.level,
               (unsigned long)pop_png_crc(0, app.v.hr.vram, POP_HR_PAGE),
               (unsigned long)pop_png_crc(0, app.v.hr.vram + POP_HR_PAGE, POP_HR_PAGE),
               app.v.nswlog ? app.v.swlog : "-", app.sound_count,
               (unsigned long)app.sound_cycles, app.beeps);
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
