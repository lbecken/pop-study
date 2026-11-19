/* pop_assets.c - see pop_assets.h.  New code. */

#include "pop_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char **err, const char *msg, FILE *fp, uint8_t *mem)
{
    if (err) *err = msg;
    if (fp) fclose(fp);
    free(mem);
    return 1;
}

static uint16_t u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

int pop_seqfile_load(const char *path, pop_seqfile *out, const char **err)
{
    uint8_t header[POP_SEQFILE_HEADER];
    uint8_t *mem;
    uint16_t origin, size, entries;
    FILE *fp;

    memset(out, 0, sizeof *out);
    fp = fopen(path, "rb");
    if (!fp) return fail(err, "cannot open file", NULL, NULL);
    if (fread(header, 1, sizeof header, fp) != sizeof header)
        return fail(err, "file shorter than its header", fp, NULL);
    if (memcmp(header, POP_SEQFILE_MAGIC, 8) != 0)
        return fail(err, "not a POPSEQT1 file", fp, NULL);

    origin = u16le(header + 8);
    size = u16le(header + 10);
    entries = u16le(header + 12);
    if (size == 0 || (size_t)entries * 2u > size)
        return fail(err, "inconsistent header", fp, NULL);
    if ((uint32_t)origin + size > 0x10000u)
        return fail(err, "table does not fit the 16-bit address space", fp, NULL);

    mem = malloc(size);
    if (!mem) return fail(err, "out of memory", fp, NULL);
    if (fread(mem, 1, size, fp) != size)
        return fail(err, "file shorter than its declared size", fp, mem);
    fclose(fp);

    out->storage = mem;
    out->entries = entries;
    out->tab.data = mem;
    out->tab.size = size;
    out->tab.origin = origin;
    return 0;
}

void pop_seqfile_free(pop_seqfile *f)
{
    if (f) {
        free(f->storage);
        memset(f, 0, sizeof *f);
    }
}

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

int pop_sfxfile_load(const char *path, pop_sfxfile *out, const char **err)
{
    uint8_t *raw = NULL;
    long size;
    size_t pos, total = 0, used = 0;
    uint16_t i;
    FILE *fp;

    memset(out, 0, sizeof *out);
    fp = fopen(path, "rb");
    if (!fp) return fail(err, "cannot open file", NULL, NULL);
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 16 || size > 16 * 1024 * 1024
        || fseek(fp, 0, SEEK_SET) != 0)
        return fail(err, "bad file size", fp, NULL);
    raw = malloc((size_t)size);
    if (!raw) return fail(err, "out of memory", fp, NULL);
    if (fread(raw, 1, (size_t)size, fp) != (size_t)size)
        return fail(err, "read error", fp, raw);
    fclose(fp);
    if (memcmp(raw, POP_SFXFILE_MAGIC, 8) != 0)
        return fail(err, "not a POPSFX01 file", NULL, raw);

    out->cpu_hz = u32le(raw + 8);
    out->count = u16le(raw + 12);
    if (out->cpu_hz == 0 || out->count == 0)
        return fail(err, "inconsistent header", NULL, raw);

    /* first pass: validate and size */
    pos = 16;
    for (i = 0; i < out->count; i++) {
        uint32_t n;
        if (pos + 8 > (size_t)size) return fail(err, "truncated effect header", NULL, raw);
        n = u32le(raw + pos + 4);
        if (n > ((size_t)size - pos - 8) / 4) return fail(err, "truncated toggle list", NULL, raw);
        total += n;
        pos += 8 + (size_t)n * 4;
    }
    out->effects = calloc(out->count, sizeof *out->effects);
    out->storage = calloc(total ? total : 1, sizeof *out->storage);
    if (!out->effects || !out->storage) {
        pop_sfxfile_free(out);
        return fail(err, "out of memory", NULL, raw);
    }
    pos = 16;
    for (i = 0; i < out->count; i++) {
        uint32_t n = u32le(raw + pos + 4), j, last = 0;
        out->effects[i].total_cycles = u32le(raw + pos);
        out->effects[i].count = n;
        out->effects[i].toggles = out->storage + used;
        for (j = 0; j < n; j++) {
            uint32_t c = u32le(raw + pos + 8 + (size_t)j * 4);
            if (c < last || c > out->effects[i].total_cycles) {
                pop_sfxfile_free(out);
                return fail(err, "toggle list not ascending within the effect", NULL, raw);
            }
            out->storage[used + j] = c;
            last = c;
        }
        used += n;
        pos += 8 + (size_t)n * 4;
    }
    free(raw);
    return 0;
}

void pop_sfxfile_free(pop_sfxfile *f)
{
    if (f) {
        free(f->effects);
        free(f->storage);
        memset(f, 0, sizeof *f);
    }
}
