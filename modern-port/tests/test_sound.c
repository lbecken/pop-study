/* test_sound.c - sound queue, tone arithmetic and effect rendering.
 * New code; synthetic data only. */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pop_assets.h"
#include "pop_sfx.h"
#include "pop_sound.h"
#include "pop_test.h"

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 2e-3)

static void test_queue(void)
{
    pop_soundq q;
    int i;
    memset(&q, 0xEE, sizeof q);
    pop_zerosound(&q);
    CHECK_EQ(q.count, 0);
    CHECK_EQ(pop_soundq_play_order(&q, 0), -1);
    pop_addsound(&q, 9);
    pop_addsound(&q, 0);
    pop_addsound(&q, 13);
    CHECK_EQ(q.count, 3);
    CHECK_EQ(pop_soundq_play_order(&q, 0), 13);      /* last added plays first */
    CHECK_EQ(pop_soundq_play_order(&q, 1), 0);
    CHECK_EQ(pop_soundq_play_order(&q, 2), 9);
    CHECK_EQ(pop_soundq_play_order(&q, 3), -1);
    for (i = 0; i < 40; i++) pop_addsound(&q, (uint8_t)(100 + i));
    CHECK_EQ(q.count, POP_MAXSFX);                   /* the 33rd and later are dropped */
    CHECK_EQ(q.items[31], 100 + 28);
    pop_zerosound(&q);
    CHECK_EQ(q.count, 0);
    CHECK_EQ(q.items[0], 9);                         /* entries are not cleared */
}

static void test_valid_numbers(void)
{
    CHECK(pop_sound_is_valid(0));
    CHECK(pop_sound_is_valid(19));
    CHECK(!pop_sound_is_valid(20));
    CHECK(!pop_sound_is_valid(127));
    CHECK(pop_sound_is_valid(128 + 5));              /* asl drops bit 7 */
}

static void test_tone_half_period(void)
{
    CHECK_EQ(pop_tone_half_period(35), 337);
    CHECK_EQ(pop_tone_half_period(70), 652);
    CHECK_EQ(pop_tone_half_period(1000), 6306);      /* $03E8: three passes of 232 */
    CHECK_EQ(pop_tone_half_period(500), 2218);       /* $01F4: one pass of 244 */
    CHECK_EQ(pop_tone_half_period(0x0100), pop_tone_half_period(1));
    CHECK_EQ(pop_tone_half_period(0xFFFF), 255u * (9u * 255u + 10u) + 12u);
}

static void test_render(void)
{
    static const uint32_t toggles[] = { 250, 600 };
    pop_sfx fx = { toggles, 2, 1000 };
    pop_sfx_render_opts o = { 1000000u, 10000u, 0.0, 0.0, 1.0f, -1 };  /* 100 cycles/sample */
    float out[32];
    int end = 0;
    size_t n = pop_sfx_render(&fx, &o, out, 32, &end);

    CHECK_EQ(n, 11);
    CHECK_EQ(pop_sfx_render_length(&fx, &o), 11);
    CHECK(NEAR(out[0], -1.0) && NEAR(out[1], -1.0));
    CHECK(NEAR(out[2], 0.0));                        /* edge half-way through the sample */
    CHECK(NEAR(out[3], 1.0) && NEAR(out[5], 1.0));
    CHECK(NEAR(out[6], -1.0) && NEAR(out[10], -1.0));
    CHECK_EQ(end, -1);                               /* two flips: back where it started */

    n = pop_sfx_render(&fx, &o, out, 4, &end);       /* output buffer too small */
    CHECK_EQ(n, 4);
    CHECK_EQ(end, -1);                               /* parity of all accesses still counted */

    o.lead_seconds = 0.0002;                         /* two samples of lead-in */
    o.amplitude = 0.5f;
    o.start_level = 1;
    n = pop_sfx_render(&fx, &o, out, 32, &end);
    CHECK_EQ(n, 13);
    CHECK(NEAR(out[0], 0.5) && NEAR(out[3], 0.5));
    CHECK(NEAR(out[4], 0.0) && NEAR(out[5], -0.5));
    CHECK_EQ(end, 1);
}

static void put32(FILE *fp, uint32_t v)
{
    fputc((int)(v & 0xFF), fp); fputc((int)((v >> 8) & 0xFF), fp);
    fputc((int)((v >> 16) & 0xFF), fp); fputc((int)((v >> 24) & 0xFF), fp);
}

static void write_sfx(const char *name, int ascending, int truncated)
{
    FILE *fp = fopen(name, "wb");
    fwrite("POPSFX01", 1, 8, fp);
    put32(fp, 1020484u);
    fputc(2, fp); fputc(0, fp); fputc(0, fp); fputc(0, fp);
    put32(fp, 1000); put32(fp, 2); put32(fp, 10); put32(fp, ascending ? 20u : 5u);
    put32(fp, 500); put32(fp, truncated ? 9u : 0u);
    fclose(fp);
}

static void test_sfx_loader(void)
{
    pop_sfxfile f;
    const char *err = NULL;
    write_sfx("pop_test_sfx.bin", 1, 0);
    CHECK_EQ(pop_sfxfile_load("pop_test_sfx.bin", &f, &err), 0);
    CHECK_EQ(f.count, 2);
    CHECK_EQ(f.cpu_hz, 1020484);
    CHECK_EQ(f.effects[0].count, 2);
    CHECK_EQ(f.effects[0].toggles[1], 20);
    CHECK_EQ(f.effects[1].count, 0);
    CHECK_EQ(f.effects[1].total_cycles, 500);
    pop_sfxfile_free(&f);
    CHECK(f.effects == NULL);

    write_sfx("pop_test_sfx.bin", 0, 0);             /* toggles out of order */
    CHECK(pop_sfxfile_load("pop_test_sfx.bin", &f, &err) != 0);
    write_sfx("pop_test_sfx.bin", 1, 1);             /* count runs past the file */
    CHECK(pop_sfxfile_load("pop_test_sfx.bin", &f, &err) != 0);
    CHECK(pop_sfxfile_load("pop_test_missing.bin", &f, &err) != 0);
    remove("pop_test_sfx.bin");
}

int main(void)
{
    RUN(test_queue);
    RUN(test_valid_numbers);
    RUN(test_tone_half_period);
    RUN(test_render);
    RUN(test_sfx_loader);
    return pop_test_summary();
}
