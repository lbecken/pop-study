/* test_sfx_original.c - the generated effect data.
 *
 * New code.  Needs sfx.bin and pop_sfxnames.h generated from the user's
 * checkout.  Expected values are those measured by executing the original
 * SOUND.S (docs/audio-analysis.md).
 */

#include "pop_assets.h"
#include "pop_sfx.h"
#include "pop_sfxnames.h"
#include "pop_sound.h"
#include "pop_test.h"

static pop_sfxfile file;

static uint32_t gap(int effect, uint32_t i)
{
    return file.effects[effect].toggles[i + 1] - file.effects[effect].toggles[i];
}

static void test_header(void)
{
    CHECK_EQ(file.count, POP_SFX_ENTRY_COUNT);
    CHECK_EQ(file.count, POP_SFX_COUNT);
    CHECK_EQ(file.cpu_hz, POP_APPLE2_CPU_HZ);
    CHECK_EQ(POP_SFX_FOOTSTEP, 9);                   /* the numbers pop_seq.c queues */
    CHECK_EQ(POP_SFX_SMACKWALL, 13);
}

static void test_steady_tones_match_the_loop_formula(void)
{
    CHECK_EQ(file.effects[POP_SFX_FOOTSTEP].count, 3);
    CHECK_EQ(gap(POP_SFX_FOOTSTEP, 0), pop_tone_half_period(35));
    CHECK_EQ(gap(POP_SFX_FOOTSTEP, 1), pop_tone_half_period(35));
    CHECK_EQ(file.effects[POP_SFX_PLATEDOWN].count, 4);
    CHECK_EQ(gap(POP_SFX_PLATEDOWN, 0), pop_tone_half_period(70));
    CHECK_EQ(file.effects[POP_SFX_SPLAT].count, 3);
    CHECK_EQ(gap(POP_SFX_SPLAT, 0), pop_tone_half_period(1000));
    CHECK_EQ(file.effects[POP_SFX_JAWSCLASH].count, 50);
    CHECK_EQ(gap(POP_SFX_JAWSCLASH, 48), pop_tone_half_period(10));
    CHECK_EQ(file.effects[POP_SFX_FLASHMSG].count, 80);
    CHECK_EQ(gap(POP_SFX_FLASHMSG, 0), pop_tone_half_period(500));
    CHECK_EQ(gap(POP_SFX_FLASHMSG, 20), pop_tone_half_period(100));
}

static void test_longest_effect_outlasts_a_tick(void)
{
    /* 725 game ticks per minute (SPECIALK.S 68) */
    double tick = (double)file.cpu_hz * 60.0 / 725.0;
    int i;
    for (i = 0; i < file.count; i++) {
        int long_one = (i == POP_SFX_FLASHMSG || i == POP_SFX_GOTKEY);
        CHECK(((double)file.effects[i].total_cycles > tick) == long_one);
    }
}

static void test_render_every_effect(void)
{
    pop_sfx_render_opts o = { 0, 44100u, 0.005, 0.005, 0.5f, -1 };
    static float buf[16384];
    int i;
    o.cpu_hz = file.cpu_hz;
    for (i = 0; i < file.count; i++) {
        int end = 0;
        size_t j, n = pop_sfx_render(&file.effects[i], &o, buf, 16384, &end);
        CHECK(n == pop_sfx_render_length(&file.effects[i], &o));
        for (j = 0; j < n; j++) {
            if (buf[j] > 0.5001f || buf[j] < -0.5001f) break;
        }
        CHECK_EQ(j, n);                              /* never louder than the amplitude */
        CHECK_EQ(end, (file.effects[i].count & 1u) ? 1 : -1);
    }
}

int main(int argc, char **argv)
{
    const char *err = NULL;
    if (argc < 2) {
        printf("usage: %s <sfx.bin>\n", argv[0]);
        return POP_TEST_SKIP;
    }
    if (pop_sfxfile_load(argv[1], &file, &err) != 0) {
        printf("%s: %s\n", argv[1], err);
        return 1;
    }
    RUN(test_header);
    RUN(test_steady_tones_match_the_loop_formula);
    RUN(test_longest_effect_outlasts_a_tick);
    RUN(test_render_every_effect);
    pop_sfxfile_free(&file);
    return pop_test_summary();
}
