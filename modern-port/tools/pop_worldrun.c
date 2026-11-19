/* pop_worldrun - run ported routines on states given on standard input.
 *
 * New code.  Used by tests/fuzz_world.py, which feeds it the same inputs it
 * gives the original code in the 6502 simulator and compares the answers.
 *
 *   pop_worldrun <framedef.bin>
 *
 * Input lines:
 *   L <4608 hex digits>           load a level blueprint
 *   S <name>=<n> ...              set state variables (decimal)
 *   R <routine> <a> <x> <y>       run a routine with those register values
 *
 * For each R line one line is printed: "a=<n> <name>=<n> ..." listing the
 * returned value and every state variable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pop_assets.h"
#include "pop_world.h"

static pop_game g;

typedef struct { const char *name; uint8_t *p; } var;

#define CHARVARS(prefix, c)                                                  \
    { prefix "Posn", &c.posn }, { prefix "X", &c.x }, { prefix "Y", &c.y },  \
    { prefix "Face", &c.face }, { prefix "BlockX", &c.block_x },             \
    { prefix "BlockY", &c.block_y }, { prefix "Action", &c.action },         \
    { prefix "XVel", &c.x_vel }, { prefix "YVel", &c.y_vel },                \
    { prefix "Scrn", &c.scrn }, { prefix "Repeat", &c.repeat },              \
    { prefix "ID", &c.id }, { prefix "Sword", &c.sword }, { prefix "Life", &c.life }

static var vars[] = {
    CHARVARS("Char", g.ch), CHARVARS("Kid", g.kid), CHARVARS("Shad", g.shad),
    CHARVARS("Op", g.op),
    { "Fimage", &g.Fimage }, { "Fsword", &g.Fsword }, { "Fdx", &g.Fdx },
    { "Fdy", &g.Fdy }, { "Fcheck", &g.Fcheck },
    { "infrontx", &g.infrontx }, { "behindx", &g.behindx }, { "abovey", &g.abovey },
    { "tempblockx", &g.tempblockx }, { "tempblocky", &g.tempblocky },
    { "tempscrn", &g.tempscrn }, { "OFFSET", &g.OFFSET },
    { "weightless", &g.ag.weightless },
    { "X", &g.rx }, { "Y", &g.ry }, { "blue", &g.blue },
    { "unmodelled", &g.unmodelled },
};
#define NVARS (sizeof vars / sizeof vars[0])

/* the 16-bit CharSeq fields are handled apart */
typedef struct { const char *name; uint16_t *p; } var16;
static var16 vars16[] = {
    { "CharSeq", &g.ch.seq }, { "KidSeq", &g.kid.seq }, { "ShadSeq", &g.shad.seq },
    { "OpSeq", &g.op.seq },
};
#define NVARS16 (sizeof vars16 / sizeof vars16[0])

static int set_var(const char *name, long v)
{
    size_t i;
    for (i = 0; i < NVARS; i++) {
        if (strcmp(vars[i].name, name) == 0) { *vars[i].p = (uint8_t)v; return 1; }
    }
    for (i = 0; i < NVARS16; i++) {
        if (strcmp(vars16[i].name, name) == 0) { *vars16[i].p = (uint16_t)v; return 1; }
    }
    return 0;
}

static int run(const char *r, uint8_t a, uint8_t x, uint8_t y, int *out)
{
#define RET(expr) do { *out = (int)(expr); return 1; } while (0)
#define DO(expr)  do { expr; *out = -1; return 1; } while (0)
    if (!strcmp(r, "rdblock"))      RET(pop_rdblock(&g, a, x, y));
    if (!strcmp(r, "rdblock1"))     { g.ry = y; RET(pop_rdblock1(&g)); }
    if (!strcmp(r, "getleft"))      RET(pop_getleft(&g, a));
    if (!strcmp(r, "getright"))     RET(pop_getright(&g, a));
    if (!strcmp(r, "getup"))        RET(pop_getup(&g, a));
    if (!strcmp(r, "getdown"))      RET(pop_getdown(&g, a));
    if (!strcmp(r, "getunderft"))   RET(pop_getunderft(&g));
    if (!strcmp(r, "getinfront"))   RET(pop_getinfront(&g));
    if (!strcmp(r, "get2infront"))  RET(pop_get2infront(&g));
    if (!strcmp(r, "getbehind"))    RET(pop_getbehind(&g));
    if (!strcmp(r, "getabove"))     RET(pop_getabove(&g));
    if (!strcmp(r, "getaboveinf"))  RET(pop_getaboveinf(&g));
    if (!strcmp(r, "getabovebeh"))  RET(pop_getabovebeh(&g));
    if (!strcmp(r, "getbasex"))     RET(pop_getbasex(&g));
    if (!strcmp(r, "getdist"))      RET(pop_getdist(&g));
    if (!strcmp(r, "getdist1"))     RET(pop_getdist1(&g, a));
    if (!strcmp(r, "getblockej"))   RET(pop_getblockej(&g, a));
    if (!strcmp(r, "getblockx"))    RET(pop_getblockx(&g, a));
    if (!strcmp(r, "getblockxp"))   RET(pop_getblockxp(&g, a));
    if (!strcmp(r, "getblocky"))    RET(pop_getblocky(a));
    if (!strcmp(r, "getblockyp"))   RET(pop_getblockyp(a));
    if (!strcmp(r, "facedx"))       RET(pop_facedx(&g, a));
    if (!strcmp(r, "cmpspace"))     RET(pop_cmpspace(a));
    if (!strcmp(r, "cmpbarr"))      RET(pop_cmpbarr(a));
    if (!strcmp(r, "cmpwall"))      RET(pop_cmpwall(&g, a));
    if (!strcmp(r, "addcharx"))     RET(pop_add_char_x(&g.ch, a));
    if (!strcmp(r, "getbaseblock")) DO(pop_getbaseblock(&g));
    if (!strcmp(r, "getframeinfo")) DO(pop_getframeinfo(&g));
    if (!strcmp(r, "rereadblocks")) DO(pop_rereadblocks(&g));
    if (!strcmp(r, "gravity"))      DO(pop_gravity(&g));
    if (!strcmp(r, "addfall"))      DO(pop_addfall(&g));
    if (!strcmp(r, "loadkid"))      DO(pop_loadkid(&g));
    if (!strcmp(r, "savekid"))      DO(pop_savekid(&g));
    if (!strcmp(r, "loadshad"))     DO(pop_loadshad(&g));
    if (!strcmp(r, "saveshad"))     DO(pop_saveshad(&g));
    if (!strcmp(r, "loadkidwop"))   DO(pop_loadkidwop(&g));
    if (!strcmp(r, "savekidwop"))   DO(pop_savekidwop(&g));
    if (!strcmp(r, "loadshadwop"))  DO(pop_loadshadwop(&g));
    if (!strcmp(r, "saveshadwop"))  DO(pop_saveshadwop(&g));
    return 0;
#undef RET
#undef DO
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(int argc, char **argv)
{
    static char line[2 * POP_LEVEL_SIZE + 64];
    pop_framefile frames;
    const char *err = NULL;
    size_t i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <framedef.bin>  (commands on standard input)\n", argv[0]);
        return 2;
    }
    if (pop_framefile_load(argv[1], &frames, &err) != 0) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 2;
    }
    memset(&g, 0, sizeof g);
    g.frames = &frames.tab;

    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'L') {
            const char *h = line + 2;
            for (i = 0; i < POP_LEVEL_SIZE; i++) {
                int hi = hexval(h[2 * i]), lo = hi < 0 ? -1 : hexval(h[2 * i + 1]);
                if (lo < 0) { fprintf(stderr, "bad level line\n"); return 2; }
                g.blueprint[i] = (uint8_t)(hi * 16 + lo);
            }
        } else if (line[0] == 'S') {
            char *tok = strtok(line + 1, " \n");
            while (tok) {
                char *eq = strchr(tok, '=');
                if (!eq) { fprintf(stderr, "bad S token %s\n", tok); return 2; }
                *eq = '\0';
                if (!set_var(tok, strtol(eq + 1, NULL, 10))) {
                    fprintf(stderr, "unknown variable %s\n", tok);
                    return 2;
                }
                tok = strtok(NULL, " \n");
            }
        } else if (line[0] == 'R') {
            char name[32];
            int a, x, y, result;
            g.unmodelled = 0;
            if (sscanf(line + 1, "%31s %d %d %d", name, &a, &x, &y) != 4
                || !run(name, (uint8_t)a, (uint8_t)x, (uint8_t)y, &result)) {
                fprintf(stderr, "bad R line: %s", line);
                return 2;
            }
            printf("a=%d", result);
            for (i = 0; i < NVARS; i++) printf(" %s=%u", vars[i].name, (unsigned)*vars[i].p);
            for (i = 0; i < NVARS16; i++) printf(" %s=%u", vars16[i].name, (unsigned)*vars16[i].p);
            printf("\n");
        }
    }
    pop_framefile_free(&frames);
    return 0;
}
