/* test_seq_original.c - the interpreter on the original sequence table.
 *
 * New code.  Needs seqtable.bin and pop_seqnames.h generated from the
 * user's checkout (CMake does this when POP_SOURCE_DIR is set).  Expected
 * values are those derived in docs/architecture.md section 3 and
 * docs/run-cycle-analysis.md from SEQTABLE.S 151-177 and 891-900.
 */

#include <string.h>

#include "pop_assets.h"
#include "pop_seq.h"
#include "pop_seqnames.h"
#include "pop_test.h"

static pop_seqfile file;
static int footsteps;

static void count_sound(void *ctx, uint8_t s)
{
    (void)ctx;
    if (s == POP_SND_FOOTSTEP) footsteps++;
}

static pop_char kid_at(uint8_t x, uint8_t face)
{
    pop_char c;
    memset(&c, 0, sizeof c);
    c.x = x;
    c.y = 181;
    c.face = face;
    c.block_y = 2;
    c.posn = 15;
    c.life = 0xFF;
    return c;
}

static void test_header(void)
{
    CHECK_EQ(file.tab.origin, 0x3000);                /* seqtab, GAMEEQ.S 22 */
    CHECK_EQ(file.entries, 114);
    CHECK_EQ(file.entries, POP_SEQ_ENTRY_COUNT);
    CHECK(file.tab.origin + file.tab.size <= 0x3A00); /* ctrl, GAMEEQ.S 23 */
}

static void test_startrun(void)
{
    static const uint8_t frames[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                      7, 8, 9, 10, 11, 12, 13, 14, 7 };
    static const uint8_t xs[] = { 168, 168, 168, 168, 160, 157, 154, 149, 148, 146,
                                  142, 137, 135, 132, 128, 123, 122, 120, 116, 111,
                                  109, 106, 102 };
    pop_anim_hooks hooks = { NULL, count_sound, NULL, NULL, NULL };
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(168, POP_FACE_LEFT);
    size_t i;

    CHECK_EQ(pop_jumpseq(&file.tab, &c, POP_SEQNO_STARTRUN), POP_SEQ_OK);
    CHECK_EQ(c.posn, 15);                             /* still standing until animchar */
    for (i = 0; i < sizeof frames; i++) {
        int before = footsteps;
        CHECK_EQ(pop_animchar(&file.tab, &c, &g, &hooks), POP_SEQ_OK);
        CHECK_EQ(c.posn, frames[i]);
        CHECK_EQ(c.x, xs[i]);
        CHECK_EQ(c.y, 181);
        CHECK_EQ(c.action, 1);
        CHECK_EQ(footsteps - before, (frames[i] == 9 || frames[i] == 13) ? 1 : 0);
    }
    CHECK_EQ(c.face, POP_FACE_LEFT);
    CHECK_EQ(c.block_y, 2);
}

static void test_run_right_is_mirrored(void)
{
    pop_anim_globals g = { 0, 0, 0 };
    pop_char l = kid_at(168, POP_FACE_LEFT), r = kid_at(255 - 168, POP_FACE_RIGHT);
    int i;
    CHECK_EQ(pop_jumpseq(&file.tab, &l, POP_SEQNO_STARTRUN), POP_SEQ_OK);
    CHECK_EQ(pop_jumpseq(&file.tab, &r, POP_SEQNO_STARTRUN), POP_SEQ_OK);
    for (i = 0; i < 40; i++) {
        CHECK_EQ(pop_animchar(&file.tab, &l, &g, NULL), POP_SEQ_OK);
        CHECK_EQ(pop_animchar(&file.tab, &r, &g, NULL), POP_SEQ_OK);
        CHECK_EQ(l.posn, r.posn);
        CHECK_EQ((uint8_t)(l.x + r.x), 255);
    }
}

static void test_running_joins_the_cycle(void)
{
    static const uint8_t frames[] = { 7, 8, 9, 10, 11, 12, 13, 14, 7 };
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(150, POP_FACE_LEFT);
    size_t i;
    CHECK_EQ(pop_jumpseq(&file.tab, &c, POP_SEQNO_RUNNING), POP_SEQ_OK);
    for (i = 0; i < sizeof frames; i++) {
        CHECK_EQ(pop_animchar(&file.tab, &c, &g, NULL), POP_SEQ_OK);
        CHECK_EQ(c.posn, frames[i]);
    }
    CHECK_EQ(c.x, 150 - 26);                          /* one full cycle */
}

static void test_runstop_ends_standing(void)
{
    static const uint8_t frames[] = { 53, 54, 55, 56, 49, 50, 51, 52, 15, 15 };
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(150, POP_FACE_LEFT);
    size_t i;
    CHECK_EQ(pop_jumpseq(&file.tab, &c, POP_SEQNO_RUNSTOP), POP_SEQ_OK);
    for (i = 0; i < sizeof frames; i++) {
        CHECK_EQ(pop_animchar(&file.tab, &c, &g, NULL), POP_SEQ_OK);
        CHECK_EQ(c.posn, frames[i]);
    }
    CHECK_EQ(c.x, 150 - 9);
    CHECK_EQ(c.action, 0);                            /* `stand` sets act 0 */
}

static void test_stand_holds_frame_15(void)
{
    pop_anim_globals g = { 0, 0, 0 };
    pop_char c = kid_at(100, POP_FACE_RIGHT);
    int i;
    CHECK_EQ(pop_jumpseq(&file.tab, &c, POP_SEQNO_STAND), POP_SEQ_OK);
    for (i = 0; i < 5; i++) {
        CHECK_EQ(pop_animchar(&file.tab, &c, &g, NULL), POP_SEQ_OK);
        CHECK_EQ(c.posn, 15);
        CHECK_EQ(c.x, 100);
    }
}

static void test_every_entry_runs(void)
{
    int entry, w;
    for (w = 0; w <= 1; w++) {
        for (entry = 1; entry <= file.entries; entry++) {
            pop_anim_globals g = { (uint8_t)w, 0, 0 };
            pop_char c = kid_at(128, POP_FACE_LEFT);
            int tick, ok = 1;
            CHECK_EQ(pop_jumpseq(&file.tab, &c, (uint8_t)entry), POP_SEQ_OK);
            for (tick = 0; tick < 300 && ok; tick++) {
                ok = pop_animchar(&file.tab, &c, &g, NULL) == POP_SEQ_OK;
            }
            CHECK(ok);
            CHECK(c.posn <= 240);                     /* frame table has 240 slots */
        }
    }
}

int main(int argc, char **argv)
{
    const char *err = NULL;
    if (argc < 2) {
        printf("usage: %s <seqtable.bin>\n", argv[0]);
        return POP_TEST_SKIP;
    }
    if (pop_seqfile_load(argv[1], &file, &err) != 0) {
        printf("%s: %s\n", argv[1], err);
        return 1;
    }
    RUN(test_header);
    RUN(test_startrun);
    RUN(test_run_right_is_mirrored);
    RUN(test_running_joins_the_cycle);
    RUN(test_runstop_ends_standing);
    RUN(test_stand_holds_frame_15);
    RUN(test_every_entry_runs);
    pop_seqfile_free(&file);
    return pop_test_summary();
}
