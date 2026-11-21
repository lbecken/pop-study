/* pop_app.h - the game as one object: state, drawing, data files.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * This is the glue between the ported game (src/core, src/video) and
 * whatever shows it: it loads the generated tables and the user's own
 * level and image files, puts the drawing code behind the core's hooks,
 * turns the frame's sound effects into samples, and feeds the keys in.
 * It has no SDL and no clock; the SDL3 shell (src/sdl) and the headless
 * tools are both thin layers over it, so what the tests exercise is what
 * the window shows.
 *
 * What is NOT here because it is not ported: the title sequence and the
 * princess scenes, music, saved games, the special keys of SPECIALK.S
 * (docs/port/shell.md).
 */
#ifndef POP_APP_H
#define POP_APP_H

#include "pop_assets.h"
#include "pop_gfx.h"
#include "pop_gfxfiles.h"
#include "pop_loop.h"
#include "pop_present.h"

typedef struct pop_app_config {
    const char *data_dir;        /* generated at build time: framedef.bin, seqtable.bin, ... */
    const char *source_dir;      /* the user's checkout of the original repository */
    const char *images_dir;      /* NULL, or a directory of replacement image tables: every
                                  * IMG.* file is then read from it and none from the checkout.
                                  * It must hold a PROVENANCE.txt, and its character tables
                                  * must have the original's image sizes, which the rules use
                                  * (docs/blender-to-sdl3.md) */
    uint32_t    audio_rate;      /* samples per second; 0 = render no sound */
} pop_app_config;

/* The controls for one frame, as a joystick: the game reads them through
 * its joystick path (docs/port/shell.md, section 2). */
typedef struct pop_app_input {
    int x;                       /* -1 left, 0, +1 right */
    int y;                       /* -1 up, 0, +1 down */
    int button;                  /* held */
    int any_key;                 /* a key went down since the last frame (ends the demo) */
} pop_app_input;

typedef struct pop_app {
    pop_game g;
    pop_gfx v;
    pop_app_config config;

    pop_framefile frames;
    pop_seqfile seq;
    pop_dimfile dims;
    pop_autofile autodata;
    pop_bgfile bg;
    pop_sfxfile sfx;
    int have_sfx;

    pop_imgfile tables[9];       /* bgtable1-2, chtable1-7 */
    uint8_t *table_dims[POP_CHTABLES];
    pop_imgdims expect;          /* the sizes of the original's character tables (imgdims.bin) */
    int bgset1, bgset2, chset;   /* the sets in memory, -1 none */

    pop_app_input input;
    float *audio;                /* the last frame's sound effects, mono */
    size_t audio_len, audio_cap;
    unsigned sound_count;        /* how many effects that was */
    uint32_t sound_cycles;       /* how long the original spent playing them */
    int speaker;                 /* -1 or +1: where the last effect left the speaker */
    int unmodelled;              /* the game left what the port models, at least once */
    const char *error;           /* set when a level or table could not be loaded */
} pop_app;

/* Load the generated tables.  Returns 0, or -1 with *err set. */
int pop_app_open(pop_app *a, const pop_app_config *config, const char **err);
void pop_app_close(pop_app *a);

/* Start a game on `level` (0 = the self-running demo, 1 = the first
 * level): a cold start, as the original has after loading. */
int pop_app_start(pop_app *a, uint8_t level, const char **err);

/* One pass of the main loop: a frame of game logic and its drawing.
 * Returns POP_LEAVE_NONE, or why the level was left.  A new level
 * (POP_LEAVE_RESTART) has been loaded and started when this returns; the
 * other reasons (the demo ended or the game is over, time is up, the game
 * is won) lead to parts of the original that are not ported: the caller
 * decides what happens.  -1: a file could not be loaded (a->error). */
int pop_app_frame(pop_app *a, const pop_app_input *in);

/* What the monitor is shown now: 280 x 192 pixels, RGB. */
void pop_app_picture(const pop_app *a, pop_present_mode mode, uint8_t *rgb);

#endif /* POP_APP_H */
