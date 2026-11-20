/* test_assets.c - seqtable.bin loader.  New code; synthetic files only. */

#include <stdio.h>
#include <string.h>

#include "pop_assets.h"
#include "pop_test.h"

static const char *write_file(const char *name, const uint8_t *bytes, size_t n)
{
    FILE *fp = fopen(name, "wb");
    if (!fp) return NULL;
    fwrite(bytes, 1, n, fp);
    fclose(fp);
    return name;
}

static void test_load_valid(void)
{
    static const uint8_t good[] = {
        'P', 'O', 'P', 'S', 'E', 'Q', 'T', '1',
        0x00, 0x30, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x30, 0x0F, 0xFF, 0x02,
    };
    pop_seqfile f;
    const char *err = NULL;
    CHECK(write_file("pop_test_good.bin", good, sizeof good) != NULL);
    CHECK_EQ(pop_seqfile_load("pop_test_good.bin", &f, &err), 0);
    CHECK_EQ(f.tab.origin, 0x3000);
    CHECK_EQ(f.tab.size, 5);
    CHECK_EQ(f.entries, 1);
    CHECK_EQ(f.tab.data[2], 0x0F);
    pop_seqfile_free(&f);
    CHECK(f.storage == NULL);
    remove("pop_test_good.bin");
}

static void test_load_rejects_bad_files(void)
{
    static const uint8_t bad_magic[] = {
        'N', 'O', 'P', 'E', 'N', 'O', 'P', 'E', 0, 0x30, 1, 0, 0, 0, 0, 0, 0x0F };
    static const uint8_t truncated[] = {
        'P', 'O', 'P', 'S', 'E', 'Q', 'T', '1', 0, 0x30, 9, 0, 0, 0, 0, 0, 0x0F };
    static const uint8_t too_many_entries[] = {
        'P', 'O', 'P', 'S', 'E', 'Q', 'T', '1', 0, 0x30, 1, 0, 5, 0, 0, 0, 0x0F };
    static const uint8_t past_64k[] = {
        'P', 'O', 'P', 'S', 'E', 'Q', 'T', '1', 0xFF, 0xFF, 2, 0, 0, 0, 0, 0, 1, 2 };
    pop_seqfile f;
    const char *err = NULL;

    CHECK(pop_seqfile_load("pop_test_missing.bin", &f, &err) != 0);
    CHECK(err != NULL && f.storage == NULL);
    write_file("pop_test_bad.bin", bad_magic, sizeof bad_magic);
    CHECK(pop_seqfile_load("pop_test_bad.bin", &f, &err) != 0);
    write_file("pop_test_bad.bin", truncated, sizeof truncated);
    CHECK(pop_seqfile_load("pop_test_bad.bin", &f, &err) != 0);
    write_file("pop_test_bad.bin", too_many_entries, sizeof too_many_entries);
    CHECK(pop_seqfile_load("pop_test_bad.bin", &f, &err) != 0);
    write_file("pop_test_bad.bin", past_64k, sizeof past_64k);
    CHECK(pop_seqfile_load("pop_test_bad.bin", &f, &err) != 0);
    write_file("pop_test_bad.bin", bad_magic, 4);
    CHECK(pop_seqfile_load("pop_test_bad.bin", &f, &err) != 0);
    remove("pop_test_bad.bin");
}

static void test_dimfile(void)
{
    /* 11 tables: table t has t+1 images of size (t+1) x (10+t) */
    uint8_t bytes[12 + 11 * 2 + 2 * 66];
    size_t at = 12, t, i;
    pop_dimfile f;
    const char *err = NULL;
    memcpy(bytes, "POPIDIM1", 8);
    bytes[8] = 11; bytes[9] = 0; bytes[10] = 0; bytes[11] = 0;
    for (t = 0; t < 11; t++) {
        bytes[at++] = (uint8_t)(t + 1);
        bytes[at++] = 0;
        for (i = 0; i <= t; i++) {
            bytes[at++] = (uint8_t)(t + 1);
            bytes[at++] = (uint8_t)(10 + t);
        }
    }
    CHECK_EQ(at, sizeof bytes);
    CHECK(write_file("pop_test_dims.bin", bytes, sizeof bytes) != NULL);
    CHECK_EQ(pop_dimfile_load("pop_test_dims.bin", &f, &err), 0);
    CHECK_EQ(f.dims.count[0], 1);
    CHECK_EQ(f.dims.count[6], 7);
    CHECK_EQ(f.dims.wh[6][0], 7);
    CHECK_EQ(f.dims.wh[6][1], 16);
    CHECK_EQ(f.dims.count[3], 4);                  /* the guard set by default */
    pop_dimfile_use_opponent(&f, POP_OPP_SKELETON);/* file table 9 */
    CHECK_EQ(f.dims.count[3], 10);
    CHECK_EQ(f.dims.wh[3][1], 19);
    pop_dimfile_use_opponent(&f, POP_OPP_GUARD);
    CHECK_EQ(f.dims.count[3], 4);
    pop_dimfile_free(&f);
    CHECK(f.storage == NULL);

    /* one byte short, one byte long, wrong table count */
    CHECK(write_file("pop_test_dims.bin", bytes, sizeof bytes - 1) != NULL);
    CHECK(pop_dimfile_load("pop_test_dims.bin", &f, &err) != 0);
    CHECK(f.storage == NULL && f.dims.wh[0] == NULL);
    bytes[8] = 7;
    CHECK(write_file("pop_test_dims.bin", bytes, sizeof bytes) != NULL);
    CHECK(pop_dimfile_load("pop_test_dims.bin", &f, &err) != 0);
    remove("pop_test_dims.bin");
}

static void test_autofile(void)
{
    uint8_t bytes[16 + POP_FT_SIZE + POP_SHADPOS_SIZE + 4];
    pop_autofile f;
    const char *err = NULL;
    size_t i;
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, "POPAUTO1", 8);
    bytes[8] = POP_FT_SIZE;
    bytes[10] = POP_SHADPOS_SIZE;
    bytes[12] = 4;
    for (i = 16; i < sizeof bytes; i++) bytes[i] = (uint8_t)i;
    CHECK(write_file("pop_test_auto.bin", bytes, sizeof bytes) != NULL);
    CHECK_EQ(pop_autofile_load("pop_test_auto.bin", &f, &err), 0);
    CHECK_EQ(f.data.fight[0], 16);
    CHECK_EQ(f.data.fight[POP_FT_BASICCOLOR], 16 + 110);
    CHECK_EQ(f.data.shadpos[POP_SHADPOS_12], 16 + POP_FT_SIZE + 16);
    CHECK_EQ(f.data.shadprog5_len, 4);
    CHECK_EQ(f.data.shadprog5[3], (uint8_t)(sizeof bytes - 1));
    pop_autofile_free(&f);
    CHECK(f.storage == NULL);

    /* truncated, too long, wrong table size */
    CHECK(write_file("pop_test_auto.bin", bytes, sizeof bytes - 1) != NULL);
    CHECK(pop_autofile_load("pop_test_auto.bin", &f, &err) != 0);
    bytes[12] = 2;                                 /* now the file is 2 bytes longer than it says */
    CHECK(write_file("pop_test_auto.bin", bytes, sizeof bytes) != NULL);
    CHECK(pop_autofile_load("pop_test_auto.bin", &f, &err) != 0);
    bytes[12] = 4;
    bytes[8] = 100;
    CHECK(write_file("pop_test_auto.bin", bytes, sizeof bytes) != NULL);
    CHECK(pop_autofile_load("pop_test_auto.bin", &f, &err) != 0);
    CHECK(f.storage == NULL);
    remove("pop_test_auto.bin");
}

int main(void)
{
    RUN(test_load_valid);
    RUN(test_load_rejects_bad_files);
    RUN(test_dimfile);
    RUN(test_autofile);
    return pop_test_summary();
}
