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
 *     --images dir    read the image tables from this directory of replacement
 *                     pictures, not from the checkout (docs/blender-to-sdl3.md);
 *                     the window title then says so
 *     --save-file f   where ctrl-G saves the game (default pop-saved-game.bin)
 *     --resume        start from the saved game, not on --level
 *     --frames n      stop after n frames (for tests)
 *     --no-wait       do not wait between frames (for tests)
 *     --screenshot f  write the last picture shown as a PNG file on leaving
 *     --type list     keys typed by nobody (for tests): "frame:code,..." with the
 *                     codes in hexadecimal, e.g. 20:9B,20:A0
 *     --press list    key events from nobody (for tests): "frame:name,..." with
 *                     SDL's key names, +c for ctrl and +s for shift, e.g.
 *                     20:Escape,25:Space,30:R+c
 *
 * Keys: arrows or J L I K, with U and O for the upper diagonals; Shift is
 * the button.  Every key typed also goes to the game's own key handler,
 * ported from the original: Escape freezes the game (Escape again: one
 * frame; any other key: go on), space shows the time left, ctrl-S turns
 * the sound off and on, ctrl-A starts the level again, ctrl-R ends the
 * game, ctrl-G saves it, and the rest of docs/port/keys.md.  The shell's
 * own: F1 switches colour and monochrome, ctrl-Q leaves.
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
    const char *source, *data, *screenshot, *images, *save_file, *type, *press;
    int level, scale, mono, stretch, mute, no_wait, resume;
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
    o->save_file = "pop-saved-game.bin";
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *next = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--mono")) o->mono = 1;
        else if (!strcmp(a, "--stretch")) o->stretch = 1;
        else if (!strcmp(a, "--mute")) o->mute = 1;
        else if (!strcmp(a, "--no-wait")) o->no_wait = 1;
        else if (!strcmp(a, "--resume")) o->resume = 1;
        else if (!next) return -1;
        else if (!strcmp(a, "--source")) o->source = argv[++i];
        else if (!strcmp(a, "--data")) o->data = argv[++i];
        else if (!strcmp(a, "--images")) o->images = argv[++i];
        else if (!strcmp(a, "--save-file")) o->save_file = argv[++i];
        else if (!strcmp(a, "--type")) o->type = argv[++i];
        else if (!strcmp(a, "--press")) o->press = argv[++i];
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

/* Everything the loop and the wait for a key share. */
typedef struct shell {
    options o;
    pop_app app;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint8_t rgb[POP_SCREEN_W * POP_SCREEN_H * 3];
    int running;
    int held;                    /* keys of the Apple's keyboard that are down: */
    bool down[SDL_SCANCODE_COUNT];
    const char *script;          /* what is left of --type */
    const char *presses;         /* what is left of --press */
    long shown;
} shell;

/* Show a picture: the 280 x 192 texture scaled up, nearest neighbour. */
static void show(shell *sh)
{
    SDL_UpdateTexture(sh->texture, NULL, sh->rgb, POP_SCREEN_W * 3);
    SDL_SetRenderDrawColor(sh->renderer, 0, 0, 0, 255);
    SDL_RenderClear(sh->renderer);
    SDL_RenderTexture(sh->renderer, sh->texture, NULL, NULL);
    SDL_RenderPresent(sh->renderer);
}

static void picture(shell *sh)
{
    pop_app_picture(&sh->app, sh->o.mono ? POP_PRESENT_MONO : POP_PRESENT_COLOUR, sh->rgb);
    show(sh);
}

/* The Apple II key code for a key of this keyboard, or 0: letters, digits
 * and punctuation as they are, with ctrl and shift; Return, Escape, space,
 * Delete.  The arrows are not keys here but the joystick (on a IIe up and
 * down would be ctrl-K and ctrl-J, which select keyboard and joystick). */
static uint8_t apple_code(SDL_Keycode key, SDL_Keymod mod)
{
    if (key == SDLK_RETURN) return 0x8D;
    if (key == SDLK_ESCAPE) return 0x9B;
    if (key == SDLK_BACKSPACE || key == SDLK_DELETE) return 0xFF;
    if (key >= 'a' && key <= 'z') {
        if (mod & SDL_KMOD_CTRL) return (uint8_t)(0x80 | (key & 0x1F));
        if (mod & SDL_KMOD_SHIFT) return (uint8_t)(0x80 | (key - 0x20));
        return (uint8_t)(0x80 | key);
    }
    if (key >= ' ' && key < 0x7F) return (uint8_t)(0x80 | key);
    return 0;
}

/* One event.  Returns the key typed for the game, or 0. */
static uint8_t handle(shell *sh, const SDL_Event *e)
{
    uint8_t code;
    if (e->type == SDL_EVENT_QUIT) sh->running = 0;
    if (e->type == SDL_EVENT_KEY_UP && e->key.scancode < SDL_SCANCODE_COUNT
            && sh->down[e->key.scancode]) {
        sh->down[e->key.scancode] = false;
        sh->held--;
    }
    if (e->type != SDL_EVENT_KEY_DOWN || e->key.repeat) return 0;
    if (e->key.key == SDLK_F1) {
        sh->o.mono = !sh->o.mono;
        return 0;
    }
    if (e->key.key == SDLK_Q && (e->key.mod & SDL_KMOD_CTRL)) {
        sh->running = 0;
        return 0;
    }
    code = apple_code(e->key.key, e->key.mod);
    if (code != 0 && e->key.scancode < SDL_SCANCODE_COUNT && !sh->down[e->key.scancode]) {
        sh->down[e->key.scancode] = true;
        sh->held++;
    }
    return code;
}

/* The next key of --type that is due at `frame` (any, if frame < 0). */
static uint8_t scripted(shell *sh, long frame)
{
    char *end;
    long at, code;
    if (!sh->script || !*sh->script) return 0;
    at = strtol(sh->script, &end, 10);
    if (*end != ':' || (frame >= 0 && at > frame)) return 0;
    code = strtol(end + 1, &end, 16);
    sh->script = *end == ',' ? end + 1 : end;
    return (uint8_t)code;
}

/* Put the key events of --press that are due at `frame` into SDL's queue,
 * a press and a release each, as if they came from a keyboard. */
static void press_due(shell *sh, long frame)
{
    while (sh->presses && *sh->presses) {
        char name[32], *end;
        size_t n;
        SDL_Event e;
        long at = strtol(sh->presses, &end, 10);
        if (*end != ':' || at > frame) return;
        end++;
        n = strcspn(end, "+,");
        if (n == 0 || n >= sizeof name) return;
        memcpy(name, end, n);
        name[n] = 0;
        end += n;
        memset(&e, 0, sizeof e);
        e.key.key = SDL_GetKeyFromName(name);
        e.key.scancode = SDL_GetScancodeFromKey(e.key.key, NULL);
        for (; *end == '+'; end += 2) {
            if (end[1] == 'c') e.key.mod |= SDL_KMOD_LCTRL;
            else if (end[1] == 's') e.key.mod |= SDL_KMOD_LSHIFT;
            else return;
        }
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.down = true;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_KEY_UP;
        e.key.down = false;
        SDL_PushEvent(&e);
        sh->presses = *end == ',' ? end + 1 : end;
    }
}

/* The game is frozen and waits for a key.  Nothing else happens until one
 * is typed, as in the original; the window is kept alive meanwhile. */
static uint8_t wait_key(void *ctx)
{
    shell *sh = ctx;
    uint8_t code = scripted(sh, -1);
    if (code != 0) return code;
    if (sh->o.frames >= 0) {                 /* a test run: nobody will type */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {          /* (but --press may have) */
            code = handle(sh, &e);
            if (code != 0) return code;
        }
        return 0;
    }
    while (sh->running) {
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 100)) continue;
        code = handle(sh, &e);
        if (code != 0) return code;
        picture(sh);                         /* (F1, or the window was uncovered) */
    }
    return 0;
}

int main(int argc, char **argv)
{
    static shell sh;
    pop_app *app = &sh.app;
    options *o = &sh.o;
    pop_app_config config;
    const char *err = NULL;
    SDL_Window *window = NULL;
    SDL_AudioStream *stream = NULL;
    Uint64 next;
    size_t samples = 0;
    unsigned saves = 0;
    int status = 1;

    if (parse(argc, argv, o) != 0) {
        fprintf(stderr, "usage: pop_sdl --source <original checkout> [--data <dir>] [--level n] "
                        "[--scale n] [--mono] [--rate n] [--stretch] [--mute] [--frames n] "
                        "[--no-wait] [--screenshot file] [--images dir] [--save-file file] "
                        "[--resume] [--type list] [--press list]\n");
        return 2;
    }
    sh.running = 1;
    sh.script = o->type;
    sh.presses = o->press;
    memset(&config, 0, sizeof config);
    config.data_dir = o->data;
    config.source_dir = o->source;
    config.images_dir = o->images;
    config.audio_rate = o->mute ? 0 : AUDIO_RATE;
    config.save_path = o->save_file;
    config.wait_key = wait_key;
    config.wait_ctx = &sh;
    if (pop_app_open(app, &config, &err) != 0
            || (o->resume ? pop_app_resume(app, &err)
                          : pop_app_start(app, (uint8_t)o->level, &err)) != 0) {
        fprintf(stderr, "pop_sdl: %s\n", err ? err : "cannot start");
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | (o->mute ? 0 : SDL_INIT_AUDIO))) {
        fprintf(stderr, "pop_sdl: SDL_Init: %s\n", SDL_GetError());
        goto done;
    }
    if (!SDL_CreateWindowAndRenderer(o->images
                                     ? "Prince of Persia (Apple II) - a study reconstruction "
                                       "[replacement pictures, not the original's]"
                                     : "Prince of Persia (Apple II) - a study reconstruction",
                                     POP_SCREEN_W * o->scale, POP_SCREEN_H * o->scale,
                                     SDL_WINDOW_RESIZABLE, &window, &sh.renderer)) {
        fprintf(stderr, "pop_sdl: window: %s\n", SDL_GetError());
        goto done;
    }
    SDL_SetRenderLogicalPresentation(sh.renderer, POP_SCREEN_W, POP_SCREEN_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    sh.texture = SDL_CreateTexture(sh.renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                   POP_SCREEN_W, POP_SCREEN_H);
    if (!sh.texture) {
        fprintf(stderr, "pop_sdl: texture: %s\n", SDL_GetError());
        goto done;
    }
    SDL_SetTextureScaleMode(sh.texture, SDL_SCALEMODE_NEAREST);
    if (!o->mute) {
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = AUDIO_RATE;
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (stream) SDL_ResumeAudioStreamDevice(stream);
        else fprintf(stderr, "pop_sdl: no sound: %s\n", SDL_GetError());
    }

    picture(&sh);
    next = SDL_GetTicksNS();
    while (sh.running && (o->frames < 0 || sh.shown < o->frames)) {
        pop_app_input in;
        SDL_Event e;
        Uint64 period = (Uint64)(60.0e9 / o->rate), now, before;
        uint8_t code;
        int leave;
        memset(&in, 0, sizeof in);
        press_due(&sh, sh.shown);
        while (SDL_PollEvent(&e)) {
            code = handle(&sh, &e);
            if (code != 0) pop_app_key(app, code);
        }
        while ((code = scripted(&sh, sh.shown)) != 0) pop_app_key(app, code);
        if (!sh.running) break;
        read_keys(&in);
        in.key_down = sh.held > 0;
        before = SDL_GetTicksNS();
        leave = pop_app_frame(app, &in);
        if (leave < 0) {
            fprintf(stderr, "pop_sdl: %s\n", app->error ? app->error : "cannot go on");
            break;
        }
        if (leave == POP_LEAVE_FROZEN) break;                  /* frozen, and told to leave */
        if (SDL_GetTicksNS() - before > 50000000u) next = SDL_GetTicksNS();   /* it was frozen */
        if (app->saves != saves) {
            saves = app->saves;
            fprintf(stderr, app->save_failed ? "pop_sdl: could not save the game to %s\n"
                                             : "pop_sdl: game saved to %s\n", o->save_file);
        }
        if (leave != POP_LEAVE_NONE && leave != POP_LEAVE_RESTART) {
            /* The title sequence and the endings are not ported: after the
             * demo, a death without continuing, ctrl-R, the hour running
             * out or the game won, the demo starts (again). */
            if (leave == POP_LEAVE_WIN) fprintf(stderr, "pop_sdl: the game is won\n");
            if (leave == POP_LEAVE_LOSE) fprintf(stderr, "pop_sdl: time is up\n");
            if (pop_app_start(app, 0, &err) != 0) break;
        }
        if (stream && app->audio_len != 0) {
            SDL_PutAudioStreamData(stream, app->audio, (int)(app->audio_len * sizeof(float)));
            samples += app->audio_len;
        }
        if (o->stretch) period += (Uint64)((double)app->sound_cycles * 1.0e9 / POP_APPLE2_CPU_HZ);

        /* What the frame showed before its picture: the flash (the lo-res
         * screen in one colour) or the blank screen of a new room.  The
         * original shows these for as long as it takes to draw the frame;
         * here, for the first half of the frame's time. */
        if (!o->no_wait && (strchr(app->v.swlog, 'l') || strchr(app->v.swlog, 't'))) {
            pop_gfx during = app->v;
            if (strchr(app->v.swlog, 'l')) during.sw_hires = 0;
            else during.sw_text = 1;
            (void)pop_present(&during, o->mono ? POP_PRESENT_MONO : POP_PRESENT_COLOUR, sh.rgb);
            show(&sh);
            SDL_DelayNS(period / 2);
        }
        picture(&sh);
        sh.shown++;

        next += period;
        now = SDL_GetTicksNS();
        if (o->no_wait || now > next + 250000000u) next = now;     /* far behind: start counting anew */
        else if (now < next) SDL_DelayNS(next - now);
    }
    status = 0;
    if (o->screenshot && pop_png_write(o->screenshot, sh.rgb, POP_SCREEN_W, POP_SCREEN_H, 1) != 0) {
        fprintf(stderr, "pop_sdl: cannot write %s\n", o->screenshot);
        status = 1;
    }
    if (o->frames >= 0) {
        printf("%ld frames shown, %lu sound samples queued, level %u%s%s\n", sh.shown,
               (unsigned long)samples, (unsigned)app->g.level,
               app->g.SINGSTEP ? ", frozen" : "",
               app->unmodelled ? ", the game left what the port models" : "");
    }
done:
    if (stream) SDL_DestroyAudioStream(stream);
    if (sh.texture) SDL_DestroyTexture(sh.texture);
    if (sh.renderer) SDL_DestroyRenderer(sh.renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    pop_app_close(app);
    return status;
}
