/* pop_app.c - the game as one object.  See pop_app.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pop_app.h"
#include "pop_top.h"

enum { TAB_BG1 = 0, TAB_BG2 = 1, TAB_CH1 = 2 };  /* slots of pop_app.tables */

/* --- files ---------------------------------------------------------------------------------------- */

static int path(char *out, size_t size, const char *dir, const char *sub, const char *name)
{
    int n = snprintf(out, size, "%s/%s%s", dir, sub, name);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

/* `opp`: for the opponents' table, which of the original's sets the file
 * stands for; its sizes are what a replacement is held to. */
static int load_table(pop_app *a, int slot, const char *name, pop_opponent_set opp)
{
    char file[1024];
    const char *err = NULL;
    const int replaced = a->config.images_dir != NULL;
    if (replaced) {
        if (path(file, sizeof file, a->config.images_dir, "", name) != 0) return -1;
    } else if (path(file, sizeof file, a->config.source_dir, "01 POP Source/Images/", name) != 0) {
        return -1;
    }
    pop_imgfile_free(&a->tables[slot]);
    if (pop_imgfile_load(file, &a->tables[slot], &err) != 0) {
        a->error = err ? err : "cannot load an image table";
        return -1;
    }
    if (slot < TAB_CH1) {
        a->v.bgtable[slot] = a->tables[slot].tab;
    } else {
        /* the game logic asks for image sizes: those of the table in memory */
        int t = slot - TAB_CH1;
        a->v.chtable[t] = a->tables[slot].tab;
        free(a->table_dims[t]);
        a->table_dims[t] = pop_imgtab_dims(&a->tables[slot].tab, &a->dims.dims.count[t]);
        a->dims.dims.wh[t] = a->table_dims[t];
        if (!a->table_dims[t]) {
            a->error = "not an image table";
            return -1;
        }
        if (replaced) {
            /* The rules read image sizes (GETEDGES): new pictures must not
             * change them. */
            uint16_t count = t == 3 ? a->dims.opp_count[opp] : a->expect.count[t];
            const uint8_t *wh = t == 3 ? a->dims.opp_wh[opp] : a->expect.wh[t];
            if (a->dims.dims.count[t] != count
                    || memcmp(a->table_dims[t], wh, 2u * count) != 0) {
                a->error = "a replacement character table does not have the original's "
                           "image sizes";
                return -1;
            }
        }
    }
    return 0;
}

/* Which file a background set is: set 1 is the palace; 0 and 2 are both
 * taken to be the dungeon (the repository has one dungeon set; the loader
 * reads them from different tracks, MASTER.S 524-525). */
static const char *bg_suffix(int set) { return set == 1 ? "PAL" : "DUN"; }

/* Which file an opponent set is.  The set numbers are the original's
 * (MISC.S 774); the files are inferred from who lives on the levels that
 * use each set: guards, the skeleton of level 3, guards again, the fat
 * guard of level 6, the shadow of level 12, the vizier. */
static const char *const CH_SUFFIX[6] = { "GD", "SKEL", "GD", "FAT", "SHAD", "VIZ" };
static const pop_opponent_set CH_OPP[6] = {
    POP_OPP_GUARD, POP_OPP_SKELETON, POP_OPP_GUARD, POP_OPP_FAT, POP_OPP_SHADOW, POP_OPP_VIZIER,
};

static int load_level(pop_app *a, uint8_t level)
{
    char name[64], file[1024];
    const pop_bgdata *bg = a->v.bg;
    int set1, set2, chset;
    FILE *f;
    size_t got;
    if (level > 14) { a->error = "there are levels 0 to 14"; return -1; }
    snprintf(name, sizeof name, "LEVEL%u", (unsigned)level);
    if (path(file, sizeof file, a->config.source_dir, "01 POP Source/Levels/", name) != 0) return -1;
    f = fopen(file, "rb");
    if (!f) { a->error = "cannot open the level file"; return -1; }
    got = fread(a->g.blueprint, 1, POP_LEVEL_SIZE, f);
    fclose(f);
    if (got != POP_LEVEL_SIZE) { a->error = "the level file is too short"; return -1; }

    set1 = bg->image[bg->level_bgset1 + level];
    set2 = bg->image[bg->level_bgset2 + level];
    chset = bg->image[bg->level_chset + level];
    if (chset > 5) { a->error = "unknown opponent set"; return -1; }
    if (set1 != a->bgset1) {
        snprintf(name, sizeof name, "IMG.BGTAB1.%s", bg_suffix(set1));
        if (load_table(a, TAB_BG1, name, POP_OPP_GUARD) != 0) return -1;
        a->bgset1 = set1;
    }
    if (set2 != a->bgset2) {
        snprintf(name, sizeof name, "IMG.BGTAB2.%s", bg_suffix(set2));
        if (load_table(a, TAB_BG2, name, POP_OPP_GUARD) != 0) return -1;
        a->bgset2 = set2;
    }
    if (chset != a->chset) {
        snprintf(name, sizeof name, "IMG.CHTAB4.%s", CH_SUFFIX[chset]);
        if (load_table(a, TAB_CH1 + 3, name, CH_OPP[chset]) != 0) return -1;
        a->chset = chset;
    }
    a->g.BGset1 = (uint8_t)set1;
    return 0;
}

/* --- hooks ---------------------------------------------------------------------------------------- */

/* The keyboard as the game's key handler sees it (docs/port/keys.md): a
 * latch holding the last key, with its high bit set until the game has
 * read it.  Keys typed faster than the game reads them wait in a queue; on
 * the Apple II a key typed before the last was read would replace it. */
static void next_key(pop_app *a)
{
    if ((a->latch & 0x80) == 0 && a->nkeys != 0) {
        a->latch = a->keys[0];
        memmove(a->keys, a->keys + 1, --a->nkeys);
    }
}

void pop_app_key(pop_app *a, uint8_t code)
{
    if ((code & 0x80) == 0 || a->nkeys >= sizeof a->keys) return;
    a->keys[a->nkeys++] = code;
    next_key(a);
}

static uint8_t h_keylatch(void *ctx, int waiting)
{
    pop_app *a = ctx;
    next_key(a);
    if (waiting && (a->latch & 0x80) == 0 && a->config.wait_key) {
        uint8_t code = a->config.wait_key(a->config.wait_ctx);
        if (code & 0x80) a->latch = code;
    }
    return a->latch;
}

static uint8_t h_keystrobe(void *ctx)
{
    pop_app *a = ctx;
    uint8_t v = (uint8_t)((a->input.key_down ? 0x80 : 0) | (a->latch & 0x7F));
    a->latch &= 0x7F;
    next_key(a);
    return v;
}

/* The directions as a joystick whose centre is PADDLE_CENTRE: a paddle
 * reads about 0 to 70 (GRAFIX.S 1190), and SETCENTER puts the dead zone 10
 * and 15 either side of where the stick stands when it is called. */
enum { PADDLE_CENTRE = 35, PADDLE_THROW = 35 };

static void h_paddles(void *ctx)
{
    pop_app *a = ctx;
    a->g.joyX = (uint8_t)(PADDLE_CENTRE + PADDLE_THROW * (a->input.x > 0) - PADDLE_THROW * (a->input.x < 0));
    a->g.joyY = (uint8_t)(PADDLE_CENTRE + PADDLE_THROW * (a->input.y > 0) - PADDLE_THROW * (a->input.y < 0));
}

static void h_buttons(void *ctx)
{
    pop_app *a = ctx;
    a->g.BTN0 = a->g.BTN1 = a->input.button ? 0x80 : 0;
}

static void h_strobe(void *ctx)
{
    pop_app *a = ctx;
    pop_strobe(&a->g);                       /* STROBE: KEYS, then CONTROLLER */
}

static void h_savegame(void *ctx)
{
    pop_app *a = ctx;
    FILE *f = a->config.save_path ? fopen(a->config.save_path, "wb") : NULL;
    a->saves++;
    a->save_failed = 1;
    if (!f) return;
    if (fwrite(a->g.savedgame, 1, sizeof a->g.savedgame, f) == sizeof a->g.savedgame) {
        a->save_failed = 0;
    }
    if (fclose(f) != 0) a->save_failed = 1;
}

static void h_loadgame(void *ctx)
{
    pop_app *a = ctx;
    FILE *f = a->config.save_path ? fopen(a->config.save_path, "rb") : NULL;
    uint8_t data[sizeof a->g.savedgame];
    memset(a->g.savedgame, 0xFF, sizeof a->g.savedgame);     /* no game saved */
    if (!f) return;
    if (fread(data, 1, sizeof data, f) == sizeof data) memcpy(a->g.savedgame, data, sizeof data);
    fclose(f);
}

static void append(pop_app *a, const pop_sfx *fx)
{
    pop_sfx_render_opts o;
    size_t n;
    memset(&o, 0, sizeof o);
    o.cpu_hz = a->sfx.cpu_hz;
    o.rate = a->config.audio_rate;
    o.amplitude = 0.25f;
    o.start_level = a->speaker;
    n = pop_sfx_render_length(fx, &o);
    if (a->audio_len + n > a->audio_cap) {
        size_t cap = (a->audio_len + n) * 2u;
        float *grown = realloc(a->audio, cap * sizeof *grown);
        if (!grown) return;
        a->audio = grown;
        a->audio_cap = cap;
    }
    a->audio_len += pop_sfx_render(fx, &o, a->audio + a->audio_len, n, &a->speaker);
}

/* gtone: the beep that confirms a special key.  GTONE calls `tone` with
 * pitch 15 and duration 50 (GRAFIX.S 1311-1317): 50 speaker accesses, the
 * first 21 cycles in, the routine over 158 cycles after the last: 7,872
 * cycles in all (counted in the simulator, tests/test_keys_original.py). */
static void h_tone(void *ctx)
{
    enum { GTONE_PITCH = 15, GTONE_DURATION = 50, GTONE_LEAD = 21, GTONE_TAIL = 158 };
    pop_app *a = ctx;
    uint32_t toggles[GTONE_DURATION], i;
    pop_sfx fx;
    for (i = 0; i < GTONE_DURATION; i++) {
        toggles[i] = GTONE_LEAD + i * pop_tone_half_period(GTONE_PITCH);
    }
    fx.toggles = toggles;
    fx.count = GTONE_DURATION;
    fx.total_cycles = toggles[GTONE_DURATION - 1] + GTONE_TAIL;
    a->beeps++;
    a->sound_cycles += fx.total_cycles;
    if (a->config.audio_rate != 0 && a->have_sfx) append(a, &fx);
}

/* playback: the frame's effects, one after the other.  The original plays
 * them here and now, and the game waits; this only renders them. */
static void h_playback(void *ctx)
{
    pop_app *a = ctx;
    uint8_t i;
    if (a->g.soundon == 0) return;           /* PLAYBACK: "sound switched off?", SOUND.S 100-102 */
    for (i = 0; i < a->g.sq.count; i++) {
        uint8_t n = a->g.sq.items[i];
        a->sound_count++;
        if (!a->have_sfx || n >= a->sfx.count) continue;
        a->sound_cycles += a->sfx.effects[n].total_cycles;
        if (a->config.audio_rate != 0) append(a, &a->sfx.effects[n]);
    }
}

static void h_zerolists(void *ctx) { pop_app *a = ctx; pop_zerolsts(&a->v); }
static void h_drawscreen(void *ctx) { pop_app *a = ctx; pop_sure(&a->v, &a->g); }
static void h_drawchanges(void *ctx) { pop_app *a = ctx; pop_fast(&a->v, &a->g); }
static void h_drawlists(void *ctx) { pop_app *a = ctx; pop_drawall(&a->v, &a->g); }
static void h_showframe(void *ctx) { pop_app *a = ctx; pop_pageflip(&a->v, &a->g); }
static void h_zeropeels(void *ctx) { pop_app *a = ctx; pop_gfx_zeropeels(&a->v); }
static void h_cutbegin(void *ctx) { pop_app *a = ctx; pop_cut_begin(&a->v); }
static void h_cutcopy(void *ctx) { pop_app *a = ctx; pop_cut_copy(&a->v, &a->g); }
static void h_message(void *ctx, uint8_t kind) { pop_app *a = ctx; pop_show_message(&a->v, &a->g, kind); }
static void h_flashon(void *ctx, uint8_t colour) { pop_app *a = ctx; pop_doflashon(&a->v, colour); }
static void h_flashoff(void *ctx) { pop_app *a = ctx; pop_doflashoff(&a->v); }
static void h_inverty(void *ctx) { pop_app *a = ctx; pop_hr_inverty(&a->v.hr); }

static void set_hooks(pop_app *a)
{
    pop_game_hooks *h = &a->g.hooks;
    memset(h, 0, sizeof *h);                 /* the rest: nothing happens (not ported) */
    h->ctx = a;
    h->strobe = h_strobe;
    h->key_latch = h_keylatch;
    h->key_strobe = h_keystrobe;
    h->read_paddles = h_paddles;
    h->read_buttons = h_buttons;
    h->save_game = h_savegame;
    h->load_game = h_loadgame;
    h->tone = h_tone;
    h->playback = h_playback;
    h->zero_lists = h_zerolists;
    h->draw_screen = h_drawscreen;
    h->draw_changes = h_drawchanges;
    h->draw_lists = h_drawlists;
    h->show_frame = h_showframe;
    h->zero_peels = h_zeropeels;
    h->cut_begin = h_cutbegin;
    h->cut_copy = h_cutcopy;
    h->show_message = h_message;
    h->flash_on = h_flashon;
    h->flash_off = h_flashoff;
    h->invert_y = h_inverty;
}

/* --- the game ------------------------------------------------------------------------------------- */

int pop_app_open(pop_app *a, const pop_app_config *config, const char **err)
{
    static const struct { const char *name; int slot; } fixed[] = {
        { "IMG.CHTAB1", TAB_CH1 }, { "IMG.CHTAB2", TAB_CH1 + 1 }, { "IMG.CHTAB3", TAB_CH1 + 2 },
        { "IMG.CHTAB5", TAB_CH1 + 4 },
    };
    char file[1024];
    const char *why = NULL;
    size_t i;
    memset(a, 0, sizeof *a);
    a->config = *config;
    a->bgset1 = a->bgset2 = a->chset = -1;
    a->speaker = -1;
#define LOAD(name, call) \
    do { \
        if (path(file, sizeof file, config->data_dir, "", name) != 0 || (call) != 0) { \
            *err = why ? why : "cannot load " name; \
            pop_app_close(a); \
            return -1; \
        } \
    } while (0)
    LOAD("framedef.bin", pop_framefile_load(file, &a->frames, &why));
    LOAD("seqtable.bin", pop_seqfile_load(file, &a->seq, &why));
    LOAD("imgdims.bin", pop_dimfile_load(file, &a->dims, &why));
    LOAD("autodata.bin", pop_autofile_load(file, &a->autodata, &why));
    LOAD("bgdata.bin", pop_bgfile_load(file, &a->bg, &why));
#undef LOAD
    /* sound effects are optional: without the file the game is silent */
    if (path(file, sizeof file, config->data_dir, "", "sfx.bin") == 0
            && pop_sfxfile_load(file, &a->sfx, &why) == 0) {
        a->have_sfx = 1;
    }
    pop_gfx_init(&a->v);
    a->v.bg = &a->bg.data;
    a->expect = a->dims.dims;
    if (config->images_dir) {
        /* new pictures are never used unlabelled */
        FILE *f = NULL;
        int labelled = 0;
        if (path(file, sizeof file, config->images_dir, "", "PROVENANCE.txt") == 0
                && (f = fopen(file, "rb")) != NULL) {
            labelled = fgetc(f) != EOF;
            fclose(f);
        }
        if (!labelled) {
            *err = "the image directory has no PROVENANCE.txt saying where its pictures "
                   "come from";
            pop_app_close(a);
            return -1;
        }
    }
    for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
        if (load_table(a, fixed[i].slot, fixed[i].name, POP_OPP_GUARD) != 0) {
            *err = a->error;
            pop_app_close(a);
            return -1;
        }
    }
    return 0;
}

void pop_app_close(pop_app *a)
{
    size_t i;
    for (i = 0; i < sizeof a->tables / sizeof a->tables[0]; i++) pop_imgfile_free(&a->tables[i]);
    for (i = 0; i < POP_CHTABLES; i++) {
        free(a->table_dims[i]);
        a->table_dims[i] = NULL;
    }
    free(a->audio);
    a->audio = NULL;
    a->audio_len = a->audio_cap = 0;
    if (a->have_sfx) pop_sfxfile_free(&a->sfx);
    a->have_sfx = 0;
    pop_bgfile_free(&a->bg);
    pop_autofile_free(&a->autodata);
    pop_dimfile_free(&a->dims);
    pop_seqfile_free(&a->seq);
    pop_framefile_free(&a->frames);
}

static int begin_level(pop_app *a)
{
    (void)h_keystrobe(a);                    /* RESTART: "clr kbd strobe", TOPCTRL.S 258 */
    if (load_level(a, a->g.level) != 0) return -1;
    a->g.leave = POP_LEAVE_NONE;             /* (what led here is done with: RESTART is a new start) */
    pop_restart_level(&a->g);
    return 0;
}

/* A cold start: every variable zero, no opponent, the copy-protection
 * check passed; then what the loader and INITSYSTEM leave behind: sound
 * and music on (MASTER.S 225, 1040), the joystick selected and centred
 * where it stands (TOPCTRL.S 121), development mode off (TOPCTRL.S
 * 125-126, which is assembled with FinalDisk = 1). */
static void cold_start(pop_app *a)
{
    pop_game *g = &a->g;
    pop_app_input none;
    memset(g, 0, sizeof *g);
    g->frames = &a->frames.tab;
    g->seq = &a->seq.tab;
    g->dims = &a->dims.dims;
    g->autodata = &a->autodata.data;
    set_hooks(a);
    g->yellowflag = 0x80;
    g->shad.face = 86;
    g->blue = 1;
    g->soundon = g->musicon = 1;
    memset(&none, 0, sizeof none);
    a->input = none;
    pop_setcenter(g);
    g->joyX = g->joyY = 0;
    g->develment = 0;
    a->audio_len = 0;
    a->sound_cycles = 0;
    a->beeps = 0;
}

int pop_app_start(pop_app *a, uint8_t level, const char **err)
{
    cold_start(a);
    pop_startgame(&a->g, level);
    if (begin_level(a) != 0) {
        *err = a->error;
        return -1;
    }
    return 0;
}

int pop_app_resume(pop_app *a, const char **err)
{
    cold_start(a);
    pop_startresume(&a->g);
    if (begin_level(a) != 0) {
        *err = a->error;
        return -1;
    }
    return 0;
}

int pop_app_frame(pop_app *a, const pop_app_input *in)
{
    uint8_t leave;
    a->input = *in;
    a->audio_len = 0;
    a->sound_cycles = 0;
    a->sound_count = 0;
    a->beeps = 0;
    a->v.nswlog = 0;
    a->v.swlog[0] = 0;
    a->g.unmodelled = a->v.unmodelled = a->v.hr.unmodelled = 0;
    leave = pop_mainloop(&a->g);
    if (a->g.unmodelled || a->v.unmodelled || a->v.hr.unmodelled) a->unmodelled = 1;
    if (leave == POP_LEAVE_RESTART && begin_level(a) != 0) return -1;
    return leave;
}

void pop_app_picture(const pop_app *a, pop_present_mode mode, uint8_t *rgb)
{
    (void)pop_present(&a->v, mode, rgb);
}
