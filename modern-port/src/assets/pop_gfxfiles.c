/* pop_gfxfiles.c - see pop_gfxfiles.h.  New code. */

#include "pop_gfxfiles.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char **err, const char *msg, FILE *fp, void *mem)
{
    if (err) *err = msg;
    if (fp) fclose(fp);
    free(mem);
    return -1;
}

static uint8_t *slurp(const char *path, size_t *size, const char **err)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *mem;
    long n;
    if (!fp) { fail(err, "cannot open file", NULL, NULL); return NULL; }
    if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fail(err, "cannot read file", fp, NULL);
        return NULL;
    }
    mem = malloc((size_t)n + 1u);
    if (!mem) { fail(err, "out of memory", fp, NULL); return NULL; }
    if (fread(mem, 1, (size_t)n, fp) != (size_t)n) { fail(err, "cannot read file", fp, mem); return NULL; }
    fclose(fp);
    *size = (size_t)n;
    return mem;
}

#define TABLE(n) { #n, offsetof(pop_bgdata, n), 1 }
#define CONST(n) { #n, offsetof(pop_bgdata, n), 0 }
static const struct { const char *name; size_t at; int is_table; } FIELDS[] = {
    TABLE(piecea), TABLE(pieceay), TABLE(maska), TABLE(pieceb), TABLE(pieceby),
    TABLE(bstripe), TABLE(piecec), TABLE(pieced), TABLE(fronti), TABLE(fronty),
    TABLE(frontx), TABLE(maskb), TABLE(blockb), TABLE(blockc), TABLE(blockd),
    TABLE(blockfr), TABLE(panelb), TABLE(panelc), TABLE(spaceb), TABLE(spaceby),
    TABLE(floorb), TABLE(floorby), TABLE(loosea), TABLE(loosed), TABLE(looseby),
    TABLE(spikea), TABLE(spikeb), TABLE(slicerseq), TABLE(slicertop), TABLE(slicerbot),
    TABLE(slicerbot2), TABLE(slicergap), TABLE(slicerfrnt), TABLE(gate8b), TABLE(gate8c),
    TABLE(torchflame), TABLE(bubble), TABLE(bline),
    TABLE(level_bgset1), TABLE(level_bgset2), TABLE(level_chset),
    CONST(numblox), CONST(numpans), CONST(numbpans), CONST(panelb0), CONST(panelc0),
    CONST(archpanel), CONST(specialflask), CONST(looseb), CONST(Ffalling), CONST(spikeExt),
    CONST(slicerRet), CONST(stairs), CONST(door), CONST(doormask), CONST(toprepair),
    CONST(gatecmask), CONST(gatebotSTA), CONST(gatebotORA), CONST(gateB1), CONST(gmaxval),
    CONST(swordgleam0), CONST(swordgleam1), CONST(CUmask), CONST(CUpiece), CONST(CUpost),
    CONST(bullet), CONST(blank),
};
#define NFIELDS (sizeof FIELDS / sizeof FIELDS[0])

int pop_bgfile_load(const char *path, pop_bgfile *out, const char **err)
{
    size_t size, image_len, count, at, i, k;
    uint8_t found[NFIELDS];
    uint8_t *mem;

    memset(out, 0, sizeof *out);
    mem = slurp(path, &size, err);
    if (!mem) return -1;
    if (size < 12 || memcmp(mem, "POPBGDT1", 8) != 0) return fail(err, "not a POPBGDT1 file", NULL, mem);
    image_len = (size_t)mem[8] | ((size_t)mem[9] << 8);
    count = (size_t)mem[10] | ((size_t)mem[11] << 8);
    if (12 + image_len > size) return fail(err, "file shorter than its header says", NULL, mem);
    memset(found, 0, sizeof found);
    at = 12 + image_len;
    for (i = 0; i < count; i++) {
        size_t len;
        unsigned value;
        if (at >= size) return fail(err, "symbol directory cut short", NULL, mem);
        len = mem[at];
        if (at + 1 + len + 2 > size) return fail(err, "symbol directory cut short", NULL, mem);
        value = (unsigned)mem[at + 1 + len] | ((unsigned)mem[at + 2 + len] << 8);
        for (k = 0; k < NFIELDS; k++) {
            if (strlen(FIELDS[k].name) == len && memcmp(FIELDS[k].name, mem + at + 1, len) == 0) {
                if (FIELDS[k].is_table) {
                    if (value >= image_len) return fail(err, "a table lies outside the block", NULL, mem);
                    memcpy((uint8_t *)&out->data + FIELDS[k].at, &(uint16_t){ (uint16_t)value },
                           sizeof(uint16_t));
                } else {
                    *((uint8_t *)&out->data + FIELDS[k].at) = (uint8_t)value;
                }
                found[k] = 1;
            }
        }
        at += 1 + len + 2;
    }
    for (k = 0; k < NFIELDS; k++) {
        if (!found[k]) return fail(err, "a name the drawing code needs is missing", NULL, mem);
    }
    out->storage = mem;
    out->data.image = mem + 12;
    out->data.size = image_len;
    return 0;
}

void pop_bgfile_free(pop_bgfile *f)
{
    if (f) {
        free(f->storage);
        memset(f, 0, sizeof *f);
    }
}

int pop_imgfile_load(const char *path, pop_imgfile *out, const char **err)
{
    size_t size;
    uint8_t *mem;
    memset(out, 0, sizeof *out);
    mem = slurp(path, &size, err);
    if (!mem) return -1;
    if (size < 3 || mem[0] == 0 || 1u + 2u * ((size_t)mem[0] + 1u) > size)
        return fail(err, "not an image table", NULL, mem);
    out->storage = mem;
    out->tab.data = mem;
    out->tab.size = size;
    return 0;
}

void pop_imgfile_free(pop_imgfile *f)
{
    if (f) {
        free(f->storage);
        memset(f, 0, sizeof *f);
    }
}

uint8_t *pop_imgtab_dims(const pop_imgtab *t, uint16_t *count)
{
    uint16_t n, i;
    uint8_t *wh;
    *count = 0;
    if (!t->data || t->size < 1) return NULL;
    n = t->data[0];
    wh = malloc(2u * (size_t)n + 1u);
    if (!wh) return NULL;
    for (i = 0; i < n; i++) {
        pop_image im = pop_imgtab_image(t, (uint8_t)(i + 1));
        if (!im.bytes || im.size < 2) {
            free(wh);
            return NULL;
        }
        wh[2u * i] = im.bytes[0];
        wh[2u * i + 1u] = im.bytes[1];
    }
    *count = n;
    return wh;
}
