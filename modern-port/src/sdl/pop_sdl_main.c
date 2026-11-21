/* pop_sdl_main.c - the SDL3 shell: a window, the keyboard, a sound device
 * and a clock around the game (pop_app).
 *
 * New code for the pop-study reconstruction; no original code or data.
 * Everything the game does is in src/core and src/video and is the same
 * code the tests compare with the original; this file only shows it,
 * times it and feeds the keys in.  See docs/port/shell.md.
 *
 *   pop_sdl --source <original checkout> [--data <dir>] [--level n] [options]
 *
 *     --level n       start on level n; 0 (the default) is the self-running
 *                     demo, which a key press turns into a game on level 1
 *     --scale n       window size: n screen pixels per dot (default 3)
 *     --mono          a monochrome monitor
 *     --rate n        frames per minute (default 725: the game's own count)
 *     --stretch       make a frame as much longer as its sounds took the original
 *     --mute          no sound
 *     --frames n      stop after n frames (for tests)
 *     --no-wait       do not wait between frames (for tests)
 *     --screenshot f  write the last picture shown as a PNG file on leaving
 *
 * Keys: arrows or J L I K, with U and O for the upper diagonals; Shift is
 * the button.  P pauses, F1 switches colour and monochrome, Ctrl-A starts
 * the level again, Ctrl-R the game, Escape leaves.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "pop_app.h"
#include "pop_png.h"

enum { AUDIO_RATE = 44100 };

typedef struct options {
    const char *source, *data, *screenshot;
    int level, scale, mono, stretch, mute, no_wait;
    long frames;
    double rate;                 /* frames per minute */
} options;

static int parse(int argc, char **argv, options *o)
{
    int i;
    memset(o, 0, sizeof *o);
    o->scale = 3;
    o->rate = 725.0;
    o->frames = -1;
    o->data = "data";
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *next = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--mono")) o->mono = 1;
        else if (!strcmp(a, "--stretch")) o->stretch = 1;
        else if (!strcmp(a, "--mute")) o->mute = 1;
        else if (!strcmp(a, "--no-wait")) o->no_wait = 1;
        else if (!next) return -1;
        else if (!strcmp(a, "--source")) o->source = argv[++i];
        else if (!strcmp(a, "--data")) o->data = argv[++i];
        else if (!strcmp(a, "--screenshot")) o->screenshot = argv[++i];
        else if (!strcmp(a, "--level")) o->level = atoi(argv[++i]);
        else if (!strcmp(a, "--scale")) o->scale = atoi(argv[++i]);
        else if (!strcmp(a, "--rate")) o->rate = atof(argv[++i]);
        else if (!strcmp(a, "--frames")) o->frames = atol(argv[++i]);
        else return -1;
    }
    if (!o->source || o->scale < 1 || o->scale > 16 || o->rate < 60.0 || o->level < 0 || o->level > 14) {
        return -1;
    }
    return 0;
}

static void read_keys(pop_app_input *in)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    int left = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_J] || k[SDL_SCANCODE_U];
    int right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_L] || k[SDL_SCANCODE_O];
    int up = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_I] || k[SDL_SCANCODE_U] || k[SDL_SCANCODE_O];
    int down = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_K];
    in->x = right - left;
    in->y = down - up;
    in->button = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
}

/* Show a picture: the 280 x 192 texture scaled up, nearest neighbour. */
static void show(SDL_Renderer *renderer, SDL_Texture *texture, const uint8_t *rgb)
{
    SDL_UpdateTexture(texture, NULL, rgb, POP_SCREEN_W * 3);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

int main(int argc, char **argv)
{
    static pop_app app;
    static uint8_t rgb[POP_SCREEN_W * POP_SCREEN_H * 3], before[POP_SCREEN_W * POP_SCREEN_H * 3];
    options o;
    pop_app_config config;
    const char *err = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_AudioStream *stream = NULL;
    Uint64 next;
    long shown = 0;
    size_t samples = 0;
    int running = 1, paused = 0, status = 1, level;

    if (parse(argc, argv, &o) != 0) {
        fprintf(stderr, "usage: pop_sdl --source <original checkout> [--data <dir>] [--level n] "
                        "[--scale n] [--mono] [--rate n] [--stretch] [--mute] [--frames n] "
                        "[--no-wait] [--screenshot file]\n");
        return 2;
    }
    memset(&config, 0, sizeof config);
    config.data_dir = o.data;
    config.source_dir = o.source;
    config.audio_rate = o.mute ? 0 : AUDIO_RATE;
    level = o.level;
    if (pop_app_open(&app, &config, &err) != 0 || pop_app_start(&app, (uint8_t)level, &err) != 0) {
        fprintf(stderr, "pop_sdl: %s\n", err ? err : "cannot start");
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | (o.mute ? 0 : SDL_INIT_AUDIO))) {
        fprintf(stderr, "pop_sdl: SDL_Init: %s\n", SDL_GetError());
        goto done;
    }
    if (!SDL_CreateWindowAndRenderer("Prince of Persia (Apple II) - a study reconstruction",
                                     POP_SCREEN_W * o.scale, POP_SCREEN_H * o.scale,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        fprintf(stderr, "pop_sdl: window: %s\n", SDL_GetError());
        goto done;
    }
    SDL_SetRenderLogicalPresentation(renderer, POP_SCREEN_W, POP_SCREEN_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                POP_SCREEN_W, POP_SCREEN_H);
    if (!texture) {
        fprintf(stderr, "pop_sdl: texture: %s\n", SDL_GetError());
        goto done;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    if (!o.mute) {
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = AUDIO_RATE;
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (stream) SDL_ResumeAudioStreamDevice(stream);
        else fprintf(stderr, "pop_sdl: no sound: %s\n", SDL_GetError());
    }

    pop_app_picture(&app, o.mono ? POP_PRESENT_MONO : POP_PRESENT_COLOUR, rgb);
    show(renderer, texture, rgb);
    next = SDL_GetTicksNS();
    while (running && (o.frames < 0 || shown < o.frames)) {
        pop_app_input in;
        SDL_Event e;
        Uint64 period = (Uint64)(60.0e9 / o.rate), now;
        int leave, restart = -1;
        memset(&in, 0, sizeof in);
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) continue;
            if (e.key.key == SDLK_ESCAPE) running = 0;
            else if (e.key.key == SDLK_P) paused = !paused;
            else if (e.key.key == SDLK_F1) o.mono = !o.mono;
            else if (e.key.key == SDLK_A && (e.key.mod & SDL_KMOD_CTRL)) restart = app.g.level;
            else if (e.key.key == SDLK_R && (e.key.mod & SDL_KMOD_CTRL)) restart = 1;
            else in.any_key = 1;
        }
        if (restart >= 0) {
            level = restart;
            if (pop_app_start(&app, (uint8_t)level, &err) != 0) break;
        }
        if (paused) {
            SDL_Delay(20);
            next = SDL_GetTicksNS();
            continue;
        }
        read_keys(&in);
        memcpy(before, rgb, sizeof before);
        leave = pop_app_frame(&app, &in);
        if (leave < 0) {
            fprintf(stderr, "pop_sdl: %s\n", app.error ? app.error : "cannot go on");
            break;
        }
        if (leave != POP_LEAVE_NONE && leave != POP_LEAVE_RESTART) {
            /* The title sequence and the endings are not ported: after the
             * demo, a death without continuing, the hour running out or
             * the game won, the demo starts (again). */
            if (leave == POP_LEAVE_WIN) fprintf(stderr, "pop_sdl: the game is won\n");
            if (leave == POP_LEAVE_LOSE) fprintf(stderr, "pop_sdl: time is up\n");
            if (pop_app_start(&app, 0, &err) != 0) break;
        }
        if (stream && app.audio_len != 0) {
            SDL_PutAudioStreamData(stream, app.audio, (int)(app.audio_len * sizeof(float)));
            samples += app.audio_len;
        }
        if (o.stretch) period += (Uint64)((double)app.sound_cycles * 1.0e9 / POP_APPLE2_CPU_HZ);

        /* What the frame showed before its picture: the flash (the lo-res
         * screen in one colour) or the blank screen of a new room.  The
         * original shows these for as long as it takes to draw the frame;
         * here, for the first half of the frame's time. */
        if (!o.no_wait && (strchr(app.v.swlog, 'l') || strchr(app.v.swlog, 't'))) {
            pop_gfx during = app.v;
            if (strchr(app.v.swlog, 'l')) during.sw_hires = 0;
            else during.sw_text = 1;
            (void)pop_present(&during, o.mono ? POP_PRESENT_MONO : POP_PRESENT_COLOUR, rgb);
            show(renderer, texture, rgb);
            SDL_DelayNS(period / 2);
        }
        pop_app_picture(&app, o.mono ? POP_PRESENT_MONO : POP_PRESENT_COLOUR, rgb);
        show(renderer, texture, rgb);
        shown++;

        next += period;
        now = SDL_GetTicksNS();
        if (o.no_wait || now > next + 250000000u) next = now;      /* far behind: start counting anew */
        else if (now < next) SDL_DelayNS(next - now);
    }
    status = 0;
    if (o.screenshot && pop_png_write(o.screenshot, rgb, POP_SCREEN_W, POP_SCREEN_H, 1) != 0) {
        fprintf(stderr, "pop_sdl: cannot write %s\n", o.screenshot);
        status = 1;
    }
    if (o.frames >= 0) {
        printf("%ld frames shown, %lu sound samples queued, level %u%s\n", shown,
               (unsigned long)samples, (unsigned)app.g.level,
               app.unmodelled ? ", the game left what the port models" : "");
    }
done:
    if (stream) SDL_DestroyAudioStream(stream);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    pop_app_close(&app);
    return status;
}
