/* test_control_original.c - the control code with the original tables.
 *
 * New code.  Needs the data files and pop_seqnames.h generated from the
 * user's checkout, and an original level file.
 *
 *   test_control_original <data dir> <LEVEL1>
 */

#include <stdio.h>
#include <string.h>

#include "pop_assets.h"
#include "pop_control.h"
#include "pop_loop.h"
#include "pop_seqnames.h"
#include "pop_sfxnames.h"
#include "pop_test.h"
#include "pop_world.h"

/* The sequence and sound numbers written into pop_control.h must be the ones
 * the original assigns (SEQDATA.S, SOUNDNAMES.S). */
#define SAME(name) _Static_assert(POP_SQ_##name == POP_SEQNO_##name, #name)
SAME(STARTRUN); SAME(STAND); SAME(STANDJUMP); SAME(RUNJUMP); SAME(TURN); SAME(RUNTURN);
SAME(STEPFALL); SAME(JUMPHANGMED); SAME(CLIMBUP); SAME(HANGDROP); SAME(RUNSTOP);
SAME(JUMPUP); SAME(FALLHANG); SAME(JUMPBACKHANG); SAME(SOFTLAND); SAME(JUMPFALL);
SAME(STEPFALL2); SAME(MEDLAND); SAME(RJUMPFALL); SAME(HARDLAND); SAME(HANGFALL);
SAME(JUMPHANGLONG); SAME(HANGSTRAIGHT); SAME(RDIVEROLL); SAME(HIGHJUMP); SAME(STEPFWD1);
SAME(TURNRUN); SAME(TESTFOOT); SAME(STANDUP); SAME(STOOP); SAME(IMPALE); SAME(ENGARDE);
SAME(ADVANCE); SAME(RETREAT); SAME(STRIKE); SAME(TURNENGARDE); SAME(STRIKEBLOCK);
SAME(READYBLOCK); SAME(LANDENGARDE); SAME(BLOCKTOSTRIKE); SAME(CLIMBDOWN);
SAME(CLIMBSTAIRS); SAME(DROPDEAD); SAME(CLIMBFAIL); SAME(FASTSTRIKE); SAME(DRINKPOTION);
SAME(CRAWL); SAME(ALERTTURN); SAME(FIGHTFALL); SAME(EFIGHTFALL); SAME(EFIGHTFALLFWD);
SAME(FASTADVANCE); SAME(GOALERTSTAND); SAME(TURNDRAW); SAME(GUARDENGARDE);
SAME(PICKUPSWORD); SAME(RESHEATHE); SAME(FASTSHEATHE); SAME(PATCHFALL);
_Static_assert(POP_SND_SPLAT == POP_SFX_SPLAT, "Splat");
_Static_assert(POP_SND_IMPALED == POP_SFX_IMPALED, "Impaled");

static pop_game g;
static pop_seqfile seq;
static pop_framefile frames;
static pop_dimfile dims;

/* One frame of the kid, as far as it is ported, in the original's order
 * (TOPCTRL.S 635-690).  Collision, plates, slicers and drawing are missing. */
static void kid_frame(void)
{
    pop_loadkidwop(&g);
    pop_rereadblocks(&g);
    pop_playerctrl(&g);
    pop_g_animchar(&g);
    pop_gravity(&g);
    pop_addfall(&g);
    pop_setupchar(&g);
    pop_rereadblocks(&g);
    pop_getedges(&g);
    pop_checkfloor(&g);
    pop_checkimpale(&g);
    pop_savekid(&g);
}

/* What is held on the keyboard this frame; the control code reads it
 * through the selected device and keeps the press flags itself. */
static void press(uint8_t x, uint8_t y, uint8_t btn)
{
    g.kbdX = x;
    g.kbdY = y;
    g.BTN0 = btn;
}

/* Is this piece plain ground: something to stand on that does nothing? */
static int plain(uint8_t type)
{
    uint8_t id = type & POP_IDMASK;
    return pop_cmpspace(id) != 0 && pop_cmpbarr(id) == 0 && id != POP_SPIKES
        && id != POP_LOOSE && id != POP_PRESSPLATE && id != POP_UPRESSPLATE
        && id != POP_FLASK && id != POP_SWORD && id != POP_EXIT && id != POP_EXIT2;
}

/* Stand the kid, facing `face`, on the first piece in the level that has
 * five more pieces of plain ground ahead of it.  (Level 1's own starting
 * place is in mid-air: the game begins with a drop.) */
static int start_kid(uint8_t face)
{
    int scrn, row, col, k, dir = (face & 0x80u) ? -1 : 1;
    for (scrn = 1; scrn <= 24; scrn++) {
        for (row = 0; row < 3; row++) {
            for (col = 0; col < 10; col++) {
                const uint8_t *t = g.blueprint + POP_BLUETYPE + (scrn - 1) * 30 + row * 10;
                for (k = 0; k <= 5; k++) {
                    int c = col + dir * k;
                    if (c < 0 || c > 9 || !plain(t[c])) break;
                }
                if (k <= 5) continue;
                memset(&g.kid, 0, sizeof g.kid);
                memset(&g.shad, 0, sizeof g.shad);
                pop_zerosound(&g.sq);
                g.kbdX = g.kbdY = g.BTN0 = g.BTN1 = g.joyon = 0;
                pop_initinput(&g);
                g.kid.scrn = (uint8_t)scrn;
                g.kid.face = face;
                g.kid.block_x = (uint8_t)col;
                g.kid.block_y = (uint8_t)row;
                g.kid.x = (uint8_t)(pop_getblockej(&g, (uint8_t)col) + POP_ANGLE + 7);
                g.kid.y = pop_floory(&g, (uint8_t)(row + 1));
                g.kid.posn = 15;
                g.kid.life = 0xFF;
                g.ch = g.kid;
                pop_g_jumpseq(&g, POP_SQ_STAND);
                g.kid = g.ch;
                g.level = 1;
                g.KidStrength = 3;
                return 1;
            }
        }
    }
    return 0;
}

static void test_tables(void)
{
    CHECK_EQ(seq.tab.origin, 0x3000);
    CHECK(dims.dims.count[0] > 0);                 /* IMG.CHTAB1 */
    CHECK(dims.dims.count[6] > 0);                 /* IMG.CHTAB7 */
    CHECK_EQ(g.unmodelled, 0);
}

static void test_standing_still(void)
{
    int i;
    pop_char before;
    CHECK(start_kid(POP_FACE_LEFT));
    for (i = 0; i < 5; i++) kid_frame();           /* settle into the stand frame */
    before = g.kid;
    CHECK_EQ(before.posn, 15);
    for (i = 0; i < 20; i++) kid_frame();
    CHECK_EQ(g.kid.x, before.x);
    CHECK_EQ(g.kid.y, before.y);
    CHECK_EQ(g.kid.posn, 15);
    CHECK_EQ(g.kid.action, before.action);
    CHECK_EQ(g.unmodelled, 0);
}

static void run_and_stop(uint8_t face)
{
    /* Hold the stick towards where he faces: he must go through the
     * start-run frames 1-6 into the run cycle 7-14, moving forward, and
     * stand again once the stick is released. */
    int i, in_cycle = 0;
    uint8_t x0, forward = (face & 0x80u) ? 0xFF : 0x01;   /* raw stick: left = $FF */
    CHECK(start_kid(face));
    for (i = 0; i < 5; i++) kid_frame();
    x0 = g.kid.x;
    for (i = 0; i < 12; i++) {
        press(forward, 0, 0);
        kid_frame();
        if (i < 6) CHECK_EQ(g.kid.posn, i + 1);    /* startrun: frames 1..6 */
        if (g.kid.posn >= 7 && g.kid.posn <= 14) in_cycle++;
    }
    CHECK_EQ(in_cycle, 6);
    if (face & 0x80u) CHECK(g.kid.x < x0);
    else CHECK(g.kid.x > x0);
    CHECK(g.sq.count > 0);                         /* footsteps were queued */
    for (i = 0; i < 30 && g.kid.posn != 15; i++) {
        press(0, 0, 0);
        kid_frame();
    }
    CHECK_EQ(g.kid.posn, 15);                      /* standing again */
    CHECK_EQ(g.kid.face, face);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_run_and_stop(void)
{
    run_and_stop(POP_FACE_LEFT);
    run_and_stop(POP_FACE_RIGHT);
}

int main(int argc, char **argv)
{
    char path[1024];
    const char *err = NULL;
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> <LEVEL1>\n", argv[0]);
        return 2;
    }
    memset(&g, 0, sizeof g);
    snprintf(path, sizeof path, "%s/seqtable.bin", argv[1]);
    if (pop_seqfile_load(path, &seq, &err) != 0) { fprintf(stderr, "%s: %s\n", path, err); return 2; }
    snprintf(path, sizeof path, "%s/framedef.bin", argv[1]);
    if (pop_framefile_load(path, &frames, &err) != 0) { fprintf(stderr, "%s: %s\n", path, err); return 2; }
    snprintf(path, sizeof path, "%s/imgdims.bin", argv[1]);
    if (pop_dimfile_load(path, &dims, &err) != 0) { fprintf(stderr, "%s: %s\n", path, err); return 2; }
    if (pop_level_load(argv[2], &g, &err) != 0) { fprintf(stderr, "%s: %s\n", argv[2], err); return 2; }
    g.seq = &seq.tab;
    g.frames = &frames.tab;
    g.dims = &dims.dims;

    RUN(test_tables);
    RUN(test_standing_still);
    RUN(test_run_and_stop);

    pop_seqfile_free(&seq);
    pop_framefile_free(&frames);
    pop_dimfile_free(&dims);
    return pop_test_summary();
}
