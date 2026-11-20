/* pop_hiresrun - run the C blitter on commands from stdin, for comparison
 * with the original blitter (tests/fuzz_hires.py).
 *
 * New code for the pop-study reconstruction.
 *
 * Input, one command per line:
 *   B <seed>             both pages = a pseudo-random pattern (see fill());
 *                        the peel buffers and the line table are reset
 *   I <hex>              the image for the next calls: width, height, bytes
 *   S name=value ...     parameters: PAGE XCO YCO OFFSET OPACITY LEFTCUT
 *                        RIGHTCUT TOPCUT BOTCUT PEELBUF PEELIMG
 *   R <routine> [a] [b]  cls, lay, layrsave, peel, fastlay, fastmask,
 *                        fastblack <width> <height>, copyscrn <from> <to>,
 *                        inverty.  `peel` uses the PEELIMG of the last
 *                        layrsave.
 *   D                    print both pages in hex (for a failing case)
 * Output of R: unmodelled, a CRC-32 of the two pages and one of the peel
 * memory, and the peel variables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pop_hires.h"

static pop_hires h;
static uint8_t image[8192];
static size_t image_size;

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* xorshift32; the same in fuzz_hires.py */
static void fill(uint32_t seed)
{
    size_t i;
    uint32_t x = seed ? seed : 1u;
    for (i = 0; i < POP_VRAM_SIZE; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        h.vram[i] = (uint8_t)(x >> 24);
    }
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int main(void)
{
    static char line[20000];
    pop_hires_init(&h);
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'B') {
            pop_hires_init(&h);
            fill((uint32_t)strtoul(line + 1, NULL, 10));
        } else if (line[0] == 'I') {
            const char *p = line + 2;
            image_size = 0;
            while (hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && image_size < sizeof image) {
                image[image_size++] = (uint8_t)(hexval(p[0]) * 16 + hexval(p[1]));
                p += 2;
            }
        } else if (line[0] == 'S') {
            char *tok = strtok(line + 1, " \n");
            while (tok) {
                char *eq = strchr(tok, '=');
                long v;
                if (!eq) { fprintf(stderr, "bad S line\n"); return 2; }
                *eq = 0;
                v = strtol(eq + 1, NULL, 10);
                if (!strcmp(tok, "PAGE")) h.PAGE = (uint8_t)v;
                else if (!strcmp(tok, "XCO")) h.XCO = (uint8_t)v;
                else if (!strcmp(tok, "YCO")) h.YCO = (uint8_t)v;
                else if (!strcmp(tok, "OFFSET")) h.OFFSET = (uint8_t)v;
                else if (!strcmp(tok, "OPACITY")) h.OPACITY = (uint8_t)v;
                else if (!strcmp(tok, "LEFTCUT")) h.LEFTCUT = (uint8_t)v;
                else if (!strcmp(tok, "RIGHTCUT")) h.RIGHTCUT = (uint8_t)v;
                else if (!strcmp(tok, "TOPCUT")) h.TOPCUT = (uint8_t)v;
                else if (!strcmp(tok, "BOTCUT")) h.BOTCUT = (uint8_t)v;
                else if (!strcmp(tok, "PEELBUF")) h.PEELBUF = (uint16_t)v;
                else if (!strcmp(tok, "PEELIMG")) h.PEELIMG = (uint16_t)v;
                else { fprintf(stderr, "unknown parameter %s\n", tok); return 2; }
                tok = strtok(NULL, " \n");
            }
        } else if (line[0] == 'R') {
            char name[32];
            int a = 0, b = 0;
            pop_image im;
            im.bytes = image;
            im.size = image_size;
            h.unmodelled = 0;
            if (sscanf(line + 1, "%31s %d %d", name, &a, &b) < 1) return 2;
            if (!strcmp(name, "cls")) pop_hr_cls(&h);
            else if (!strcmp(name, "lay")) pop_hr_lay(&h, im);
            else if (!strcmp(name, "layrsave")) pop_hr_layrsave(&h, im);
            else if (!strcmp(name, "peel")) pop_hr_peel(&h, h.PEELIMG);
            else if (!strcmp(name, "fastlay")) pop_hr_fastlay(&h, im);
            else if (!strcmp(name, "fastmask")) pop_hr_fastmask(&h, im);
            else if (!strcmp(name, "fastblack")) pop_hr_fastblack(&h, (uint8_t)a, (uint8_t)b, h.OPACITY);
            else if (!strcmp(name, "copyscrn")) pop_hr_copyscrn(&h, (uint8_t)a, (uint8_t)b);
            else if (!strcmp(name, "inverty")) pop_hr_inverty(&h);
            else { fprintf(stderr, "unknown routine %s\n", name); return 2; }
            printf("unmodelled=%u pages=%08lx peel=%08lx PEELBUF=%u PEELIMG=%u PEELXCO=%u PEELYCO=%u\n",
                   (unsigned)h.unmodelled, (unsigned long)crc32(h.vram, sizeof h.vram),
                   (unsigned long)crc32(h.peelmem, sizeof h.peelmem), (unsigned)h.PEELBUF,
                   (unsigned)h.PEELIMG, (unsigned)h.PEELXCO, (unsigned)h.PEELYCO);
        } else if (line[0] == 'D') {
            size_t i;
            for (i = 0; i < sizeof h.vram; i++) printf("%02x", h.vram[i]);
            printf("\n");
        }
    }
    return 0;
}
