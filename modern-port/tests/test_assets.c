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

int main(void)
{
    RUN(test_load_valid);
    RUN(test_load_rejects_bad_files);
    return pop_test_summary();
}
