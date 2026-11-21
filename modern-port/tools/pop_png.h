/* pop_png.h - write an RGB image as a PNG file, without a library.
 *
 * New code for the pop-study reconstruction.  The image data is stored,
 * not compressed ("stored" deflate blocks), which every PNG reader
 * accepts; a 280 x 192 frame is about 160 KB.
 */
#ifndef POP_PNG_H
#define POP_PNG_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t pop_png_crc(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;
    int k;
    c = ~c;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void pop_png_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int pop_png_chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    uint8_t head[8], tail[4];
    uint32_t crc;
    pop_png_be32(head, (uint32_t)n);
    memcpy(head + 4, type, 4);
    crc = pop_png_crc(0, head + 4, 4);
    crc = pop_png_crc(crc, data, n);
    pop_png_be32(tail, crc);
    return fwrite(head, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n)
        && fwrite(tail, 1, 4, f) == 4;
}

/* rgb: width * height * 3 bytes, top line first.  Each pixel is written
 * `scale` times in both directions.  Returns 0, or -1. */
static int pop_png_write(const char *path, const uint8_t *rgb, unsigned width, unsigned height,
                         unsigned scale)
{
    static const uint8_t magic[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    unsigned w = width * scale, h = height * scale, x, y, k;
    size_t line = 1u + 3u * w, raw = line * h, blocks = (raw + 65534u) / 65535u;
    size_t zsize = 2u + raw + 5u * blocks + 4u, at = 0, done = 0;
    uint8_t ihdr[13], *z, *row;
    uint32_t a = 1, b = 0;
    FILE *f;
    int ok;
    if (scale == 0 || w == 0 || h == 0) return -1;
    z = malloc(zsize);
    row = malloc(line);
    if (!z || !row) { free(z); free(row); return -1; }
    z[at++] = 0x78;
    z[at++] = 0x01;
    for (y = 0; y < h; y++) {
        const uint8_t *src = rgb + (size_t)(y / scale) * width * 3u;
        size_t i;
        row[0] = 0;                              /* filter: none */
        for (x = 0; x < w; x++) {
            for (k = 0; k < 3; k++) row[1u + 3u * x + k] = src[3u * (x / scale) + k];
        }
        for (i = 0; i < line; i++) {
            if (done % 65535u == 0) {            /* a new stored block */
                size_t len = raw - done > 65535u ? 65535u : raw - done;
                z[at++] = (uint8_t)(raw - done <= 65535u);
                z[at++] = (uint8_t)len; z[at++] = (uint8_t)(len >> 8);
                z[at++] = (uint8_t)~len; z[at++] = (uint8_t)(~len >> 8);
            }
            z[at++] = row[i];
            a = (a + row[i]) % 65521u;           /* Adler-32 */
            b = (b + a) % 65521u;
            done++;
        }
    }
    pop_png_be32(z + at, (b << 16) | a);
    at += 4;
    pop_png_be32(ihdr, w);
    pop_png_be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;      /* 8-bit RGB */
    f = fopen(path, "wb");
    ok = f && fwrite(magic, 1, 8, f) == 8 && pop_png_chunk(f, "IHDR", ihdr, 13)
        && pop_png_chunk(f, "IDAT", z, at) && pop_png_chunk(f, "IEND", NULL, 0);
    if (f && fclose(f) != 0) ok = 0;
    free(z);
    free(row);
    return ok ? 0 : -1;
}

#endif /* POP_PNG_H */
