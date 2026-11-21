/* pop_gfxrun - run the C redraw on commands from stdin, for comparison with
 * the original drawing code (tests/compare_rooms.py).
 *
 * New code for the pop-study reconstruction.
 *
 * Usage: pop_gfxrun <bgdata.bin>
 * Input, one command per line:
 *   L <hex>              a level (2304 bytes); game and drawing state are reset
 *   T <n> <path>         image table: 1, 2 = bgtable1, bgtable2; 11-17 = chtable1-7
 *   S name=value ...     level BGset1 KidScrn KidBlockX KidBlockY PAGE
 *                        KidStrength MaxKidStr OppStrength ShadID
 *   P index=value ...    poke level bytes
 *   H <hex>              the redraw buffers (310 bytes)
 *   R setinitials        SETINITIALS
 *   R room <screen>      GETSCRNS, then DoSure: SURE, the lists drawn
 *   R fast               ZEROLSTS, FAST, DRAWALL on the screen of the last room
 *   R lists              print the lists of the last room
 * Output of `R room`: unmodelled, CRC-32 of the page drawn and of the
 * three lists, and the lists' lengths.  `R fast` adds the message and wipe
 * lists, the redraw buffers afterwards and the two meter flags.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pop_gfx.h"
#include "pop_gfxfiles.h"
#include "pop_top.h"

static pop_game g;
static pop_gfx v;
static pop_bgfile bg;
static pop_imgfile tables[9];

static uint32_t crc32(uint32_t c, const uint8_t *p, size_t n)
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

static uint32_t list_crc(const uint8_t *x, const uint8_t *y, const uint8_t *img,
                         const uint8_t *op, const uint8_t *off)
{
    size_t n = x[0];
    uint32_t c = crc32(0, x + 1, n);
    c = crc32(c, y + 1, n);
    c = crc32(c, img + 1, n);
    c = crc32(c, op + 1, n);
    if (off) c = crc32(c, off + 1, n);
    return c;
}

static uint32_t mid_crc(void)
{
    size_t n = v.midX[0];
    uint32_t c = list_crc(v.midX, v.midY, v.midIMG, v.midOP, v.midOFF);
    c = crc32(c, v.midTYP + 1, n);
    c = crc32(c, v.midTAB + 1, n);
    c = crc32(c, v.midCU + 1, n);
    c = crc32(c, v.midCD + 1, n);
    c = crc32(c, v.midCL + 1, n);
    return crc32(c, v.midCR + 1, n);
}

static uint32_t wipe_crc(void)
{
    size_t n = v.wipeX[0];
    uint32_t c = crc32(0, v.wipeX + 1, n);
    c = crc32(c, v.wipeY + 1, n);
    c = crc32(c, v.wipeH + 1, n);
    c = crc32(c, v.wipeW + 1, n);
    return crc32(c, v.wipeCOL + 1, n);
}

static void print_lists(void)
{
    printf("unmodelled=%u page=%08lx back=%08lx fore=%08lx mid=%08lx nback=%u nfore=%u nmid=%u",
           (unsigned)(g.unmodelled | v.unmodelled),
           (unsigned long)crc32(0, v.hr.vram + (v.hr.PAGE ? POP_HR_PAGE : 0), POP_HR_PAGE),
           (unsigned long)list_crc(v.bgX, v.bgY, v.bgIMG, v.bgOP, NULL),
           (unsigned long)list_crc(v.fgX, v.fgY, v.fgIMG, v.fgOP, NULL),
           (unsigned long)mid_crc(),
           (unsigned)v.bgX[0], (unsigned)v.fgX[0], (unsigned)v.midX[0]);
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int main(int argc, char **argv)
{
    static char line[2 * POP_LEVEL_SIZE + 64];
    const char *err = NULL;
    size_t i;
    if (argc < 2) { fprintf(stderr, "usage: pop_gfxrun <bgdata.bin>\n"); return 2; }
    if (pop_bgfile_load(argv[1], &bg, &err) != 0) { fprintf(stderr, "%s: %s\n", argv[1], err); return 2; }
    pop_gfx_init(&v);
    v.bg = &bg.data;
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'L') {                  /* a level: everything starts afresh */
            const char *h = line + 2;
            pop_imgtab bgt[2] = { v.bgtable[0], v.bgtable[1] }, cht[7];
            memcpy(cht, v.chtable, sizeof cht);
            memset(&g, 0, sizeof g);
            pop_gfx_init(&v);
            v.bg = &bg.data;
            v.bgtable[0] = bgt[0];
            v.bgtable[1] = bgt[1];
            memcpy(v.chtable, cht, sizeof cht);
            for (i = 0; i < POP_LEVEL_SIZE; i++) {
                int hi = hexval(h[2 * i]), lo = hi < 0 ? -1 : hexval(h[2 * i + 1]);
                if (lo < 0) { fprintf(stderr, "bad level line\n"); return 2; }
                g.blueprint[i] = (uint8_t)(hi * 16 + lo);
            }
        } else if (line[0] == 'T') {
            int n = atoi(line + 2), slot;
            char *path = strchr(line + 2, ' ');
            if (!path) return 2;
            path++;
            path[strcspn(path, "\n")] = 0;
            slot = (n == 1 || n == 2) ? n - 1 : (n >= 11 && n <= 17) ? n - 9 : -1;
            if (slot < 0) { fprintf(stderr, "bad table number\n"); return 2; }
            pop_imgfile_free(&tables[slot]);
            if (pop_imgfile_load(path, &tables[slot], &err) != 0) {
                fprintf(stderr, "%s: %s\n", path, err);
                return 2;
            }
            if (slot < 2) v.bgtable[slot] = tables[slot].tab;
            else v.chtable[slot - 2] = tables[slot].tab;
        } else if (line[0] == 'S' || line[0] == 'P') {
            char *tok = strtok(line + 1, " \n");
            while (tok) {
                char *eq = strchr(tok, '=');
                long val;
                if (!eq) { fprintf(stderr, "bad %c line\n", line[0]); return 2; }
                *eq = 0;
                val = strtol(eq + 1, NULL, 10);
                if (line[0] == 'P') {
                    long at = strtol(tok, NULL, 10);
                    if (at < 0 || at >= POP_LEVEL_SIZE) return 2;
                    g.blueprint[at] = (uint8_t)val;
                } else if (!strcmp(tok, "level")) g.level = (uint8_t)val;
                else if (!strcmp(tok, "BGset1")) g.BGset1 = (uint8_t)val;
                else if (!strcmp(tok, "KidScrn")) g.kid.scrn = (uint8_t)val;
                else if (!strcmp(tok, "KidBlockX")) g.kid.block_x = (uint8_t)val;
                else if (!strcmp(tok, "KidBlockY")) g.kid.block_y = (uint8_t)val;
                else if (!strcmp(tok, "PAGE")) v.hr.PAGE = (uint8_t)val;
                else if (!strcmp(tok, "KidStrength")) g.KidStrength = (uint8_t)val;
                else if (!strcmp(tok, "MaxKidStr")) g.MaxKidStr = (uint8_t)val;
                else if (!strcmp(tok, "OppStrength")) g.OppStrength = (uint8_t)val;
                else if (!strcmp(tok, "ShadID")) g.shad.id = (uint8_t)val;
                else { fprintf(stderr, "unknown variable %s\n", tok); return 2; }
                tok = strtok(NULL, " \n");
            }
        } else if (line[0] == 'H') {
            const char *h = line + 2;
            for (i = 0; i < POP_RB_SIZE; i++) {
                int hi = hexval(h[2 * i]), lo = hi < 0 ? -1 : hexval(h[2 * i + 1]);
                if (lo < 0) { fprintf(stderr, "bad H line\n"); return 2; }
                g.rb[i] = (uint8_t)(hi * 16 + lo);
            }
        } else if (line[0] == 'R') {
            char name[32];
            int a = 0;
            if (sscanf(line + 1, "%31s %d", name, &a) < 1) return 2;
            g.unmodelled = v.unmodelled = v.hr.unmodelled = 0;
            if (!strcmp(name, "setinitials")) {
                pop_setinitials(&g);
                printf("unmodelled=%u\n", (unsigned)g.unmodelled);
            } else if (!strcmp(name, "room")) {
                g.VisScrn = (uint8_t)a;
                pop_getscrns(&g);
                g.SCRNUM = g.VisScrn;                    /* DoSure */
                pop_zerolsts(&v);
                pop_sure(&v, &g);
                pop_gfx_zeropeels(&v);
                pop_drawall(&v, &g);
                print_lists();
                printf("\n");
            } else if (!strcmp(name, "fast")) {
                g.SCRNUM = g.VisScrn;                    /* DoFast, without objects and messages */
                pop_zerolsts(&v);
                pop_fast(&v, &g);
                pop_drawall(&v, &g);
                print_lists();
                printf(" msg=%08lx wipe=%08lx nmsg=%u nwipe=%u rb=%08lx redkid=%u redopp=%u\n",
                       (unsigned long)list_crc(v.msgX, v.msgY, v.msgIMG, v.msgOP, v.msgOFF),
                       (unsigned long)wipe_crc(), (unsigned)v.msgX[0], (unsigned)v.wipeX[0],
                       (unsigned long)crc32(0, g.rb, POP_RB_SIZE), (unsigned)v.redkidmeter,
                       (unsigned)v.redoppmeter);
            } else if (!strcmp(name, "lists")) {         /* for a failing case: the lists in full */
                unsigned k;
                for (k = 1; k <= v.bgX[0]; k++)
                    printf("back %u,%u img %u op %u; ", v.bgX[k], v.bgY[k], v.bgIMG[k], v.bgOP[k]);
                for (k = 1; k <= v.fgX[0]; k++)
                    printf("fore %u,%u img %u op %u; ", v.fgX[k], v.fgY[k], v.fgIMG[k], v.fgOP[k]);
                for (k = 1; k <= v.midX[0]; k++)
                    printf("mid %u+%u,%u img %u.%u op %u typ %u cut %u-%u,%u-%u; ", v.midX[k],
                           v.midOFF[k], v.midY[k], v.midTAB[k], v.midIMG[k], v.midOP[k], v.midTYP[k],
                           v.midCL[k], v.midCR[k], v.midCU[k], v.midCD[k]);
                for (k = 1; k <= v.msgX[0]; k++)
                    printf("msg %u+%u,%u img %u op %u; ", v.msgX[k], v.msgOFF[k], v.msgY[k],
                           v.msgIMG[k], v.msgOP[k]);
                for (k = 1; k <= v.wipeX[0]; k++)
                    printf("wipe %u,%u %ux%u col %u; ", v.wipeX[k], v.wipeY[k], v.wipeW[k],
                           v.wipeH[k], v.wipeCOL[k]);
                printf("\n");
            } else {
                fprintf(stderr, "unknown routine %s\n", name);
                return 2;
            }
        }
    }
    return 0;
}
