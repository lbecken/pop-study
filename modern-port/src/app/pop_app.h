/* pop_app.h - the game as one object: state, drawing, data files.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * This is the glue between the ported game (src/core, src/video) and
 * whatever shows it: it loads the generated tables and the user's own
 * level and image files, puts the drawing code behind the core's hooks,
 * turns the frame's sound effects into samples, and stands in for the
 * keyboard, the joystick and the disk the game's key handler asks for.
 * It has no SDL and no clock; the SDL3 shell (src/sdl) and the headless
 * tools are both thin layers over it, so what the tests exercise is what
 * the window shows.
 *
 * What is NOT here because it is not ported: the title sequence and the
 * princess scenes, music (docs/port/shell.md).
 */
#ifndef POP_APP_H
#define POP_APP_H

#include "pop_assets.h"
#include "pop_gfx.h"
#include "pop_gfxfiles.h"
#include "pop_keys.h"
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
    const char *save_path;       /* NULL, or the file a saved game is written to (ctrl-G) and
                                  * read from (pop_app_resume): 16 bytes, the original's */
    /* While the game is frozen (ESC) its key handler waits for a key and
     * does nothing else.  This is called to do the waiting: it returns the
     * next key typed (as pop_app_key takes it), or 0 to stop waiting, which
     * is only for ending the session.  NULL: there is nothing to wait for. */
    uint8_t   (*wait_key)(void *ctx);
    void       *wait_ctx;
} pop_app_config;

/* The controls for one frame.  Directions and the button are a joystick
 * and its buttons, which the game reads through its controller code
 * (docs/port/shell.md, section 2); keys typed go in through pop_app_key. */
typedef struct pop_app_input {
    int x;                       /* -1 left, 0, +1 right */
    int y;                       /* -1 up, 0, +1 down */
    int button;                  /* held */
    int key_down;                /* some key of the keyboard is held */
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
    uint8_t latch;               /* the keyboard: the last key, high bit until it is read */
    uint8_t keys[32];            /* keys typed and not yet arrived */
    size_t nkeys;
    unsigned saves;              /* saved games written; save_failed: the last one was not */
    int save_failed;
    unsigned beeps;              /* key-confirmation tones this frame */
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

/* Start from the saved game in config.save_path, as the original does
 * when ctrl-L is pressed at its title: on the saved level with the saved
 * strength and time, or a new game if there is no saved game. */
int pop_app_resume(pop_app *a, const char **err);

/* A key was typed: an Apple II key code, ASCII with the high bit set
 * (control keys: the letter's code less $60; POP_KEY_* in pop_keys.h).
 * Keys arrive at the game one per frame, in order. */
void pop_app_key(pop_app *a, uint8_t code);

/* One pass of the main loop: a frame of game logic and its drawing.
 * Returns POP_LEAVE_NONE, or why the level was left.  A new level
 * (POP_LEAVE_RESTART) has been loaded and started when this returns; the
 * other reasons (the demo ended or the game is over, ctrl-R, time is up,
 * the game is won) lead to parts of the original that are not ported: the
 * caller decides what happens.  POP_LEAVE_FROZEN: the game is frozen and
 * wait_key gave up; nothing was drawn.  -1: a file could not be loaded
 * (a->error). */
int pop_app_frame(pop_app *a, const pop_app_input *in);

/* What the monitor is shown now: 280 x 192 pixels, RGB. */
void pop_app_picture(const pop_app *a, pop_present_mode mode, uint8_t *rgb);

#endif /* POP_APP_H */
