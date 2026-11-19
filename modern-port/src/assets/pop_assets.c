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
