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

int pop_framefile_load(const char *path, pop_framefile *out, const char **err)
{
    uint8_t header[POP_FRAMEFILE_HEADER];
    uint8_t *mem;
    uint16_t size;
    FILE *fp;

    memset(out, 0, sizeof *out);
    fp = fopen(path, "rb");
    if (!fp) return fail(err, "cannot open file", NULL, NULL);
    if (fread(header, 1, sizeof header, fp) != sizeof header)
        return fail(err, "file shorter than its header", fp, NULL);
    if (memcmp(header, POP_FRAMEFILE_MAGIC, 8) != 0)
        return fail(err, "not a POPFRAM1 file", fp, NULL);
    size = u16le(header + 10);
    if (size == 0) return fail(err, "inconsistent header", fp, NULL);
    mem = malloc(size);
    if (!mem) return fail(err, "out of memory", fp, NULL);
    if (fread(mem, 1, size, fp) != size)
        return fail(err, "file shorter than its declared size", fp, mem);
    fclose(fp);

    out->storage = mem;
    out->tab.data = mem;
    out->tab.size = size;
    out->tab.origin = u16le(header + 8);
    out->tab.fdef = u16le(header + 12);
    out->tab.altset1 = u16le(header + 14);
    out->tab.altset2 = u16le(header + 16);
    out->tab.swordtab = u16le(header + 18);
    if (out->tab.fdef < out->tab.origin || out->tab.altset1 < out->tab.fdef
        || out->tab.altset2 < out->tab.altset1 || out->tab.swordtab < out->tab.altset2
        || (uint32_t)out->tab.swordtab > (uint32_t)out->tab.origin + size) {
        pop_framefile_free(out);
        return fail(err, "table addresses out of order", NULL, NULL);
    }
    return 0;
}

void pop_framefile_free(pop_framefile *f)
{
    if (f) {
        free(f->storage);
        memset(f, 0, sizeof *f);
    }
}

int pop_dimfile_load(const char *path, pop_dimfile *out, const char **err)
{
    uint8_t header[12];
    uint8_t *mem;
    long size;
    size_t at = 0, t;
    FILE *fp;

    memset(out, 0, sizeof *out);
    fp = fopen(path, "rb");
    if (!fp) return fail(err, "cannot open file", NULL, NULL);
    if (fread(header, 1, sizeof header, fp) != sizeof header)
        return fail(err, "file shorter than its header", fp, NULL);
    if (memcmp(header, POP_DIMFILE_MAGIC, 8) != 0)
        return fail(err, "not a POPIDIM1 file", fp, NULL);
    if (u16le(header + 8) != POP_DIMFILE_TABLES)
        return fail(err, "unexpected number of image tables", fp, NULL);
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < (long)sizeof header
        || size > 65536L || fseek(fp, (long)sizeof header, SEEK_SET) != 0)
        return fail(err, "cannot size file", fp, NULL);
    size -= (long)sizeof header;
    mem = malloc((size_t)size + 1u);
    if (!mem) return fail(err, "out of memory", fp, NULL);
    if (fread(mem, 1, (size_t)size, fp) != (size_t)size)
        return fail(err, "read error", fp, mem);
    fclose(fp);

    for (t = 0; t < POP_DIMFILE_TABLES; t++) {
        uint16_t n;
        if (at + 2 > (size_t)size) break;
        n = u16le(mem + at);
        at += 2;
        if (at + 2u * n > (size_t)size) break;
        if (t < POP_CHTABLES) {
            out->dims.count[t] = n;
            out->dims.wh[t] = mem + at;
        } else {
            out->opp_count[t - POP_CHTABLES + 1] = n;
            out->opp_wh[t - POP_CHTABLES + 1] = mem + at;
        }
        at += 2u * n;
    }
    if (t != POP_DIMFILE_TABLES || at != (size_t)size) {
        memset(out, 0, sizeof *out);
        return fail(err, "image tables do not fill the file", NULL, mem);
    }
    out->opp_count[POP_OPP_GUARD] = out->dims.count[3];
    out->opp_wh[POP_OPP_GUARD] = out->dims.wh[3];
    out->storage = mem;
    return 0;
}

void pop_dimfile_free(pop_dimfile *f)
{
    if (f) {
        free(f->storage);
        memset(f, 0, sizeof *f);
    }
}

void pop_dimfile_use_opponent(pop_dimfile *f, pop_opponent_set set)
{
    if (f && f->storage && (int)set >= 0 && set < POP_OPP_SETS) {
        f->dims.count[3] = f->opp_count[set];
        f->dims.wh[3] = f->opp_wh[set];
    }
}

int pop_autofile_load(const char *path, pop_autofile *out, const char **err)
{
    uint8_t header[16];
    uint8_t *mem;
    size_t fight, shadpos, prog, demo, total;
    FILE *fp;

    memset(out, 0, sizeof *out);
    fp = fopen(path, "rb");
    if (!fp) return fail(err, "cannot open file", NULL, NULL);
    if (fread(header, 1, sizeof header, fp) != sizeof header)
        return fail(err, "file shorter than its header", fp, NULL);
    if (memcmp(header, POP_AUTOFILE_MAGIC, 8) != 0)
        return fail(err, "not a POPAUTO1 file", fp, NULL);
    fight = u16le(header + 8);
    shadpos = u16le(header + 10);
    prog = u16le(header + 12);
    demo = u16le(header + 14);
    if (fight != POP_FT_SIZE || shadpos != POP_SHADPOS_SIZE || prog < 2 || (prog & 1u)
        || demo < 2 || (demo & 1u))
        return fail(err, "unexpected table sizes", fp, NULL);
    total = fight + shadpos + prog + demo;
    mem = malloc(total + 1u);
    if (!mem) return fail(err, "out of memory", fp, NULL);
    if (fread(mem, 1, total + 1u, fp) != total)
        return fail(err, "file length does not match its header", fp, mem);
    fclose(fp);
    out->storage = mem;
    out->data.fight = mem;
    out->data.shadpos = mem + fight;
    out->data.shadprog5 = mem + fight + shadpos;
    out->data.shadprog5_len = prog;
    out->data.demoprog = mem + fight + shadpos + prog;
    out->data.demoprog_len = demo;
    return 0;
}

void pop_autofile_free(pop_autofile *f)
{
    if (f) {
        free(f->storage);
        memset(f, 0, sizeof *f);
    }
}

int pop_level_load(const char *path, pop_game *g, const char **err)
{
    uint8_t extra;
    FILE *fp = fopen(path, "rb");
    if (!fp) return fail(err, "cannot open file", NULL, NULL);
    if (fread(g->blueprint, 1, POP_LEVEL_SIZE, fp) != POP_LEVEL_SIZE)
        return fail(err, "level file shorter than 2304 bytes", fp, NULL);
    if (fread(&extra, 1, 1, fp) != 0)
        return fail(err, "level file longer than 2304 bytes", fp, NULL);
    fclose(fp);
    return 0;
}
