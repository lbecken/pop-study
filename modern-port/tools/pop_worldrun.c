/* pop_worldrun - run ported routines on states given on standard input.
 *
 * New code.  Used by tests/fuzz_world.py, which feeds it the same inputs it
 * gives the original code in the 6502 simulator and compares the answers.
 *
 *   pop_worldrun <framedef.bin> [<seqtable.bin> <imgdims.bin>]
 *
 * The two optional files are needed by the control and floor routines.
 *
 * Input lines:
 *   L <4608 hex digits>           load a level blueprint
 *   S <name>=<n> ...              set state variables (decimal)
 *   P <index>=<n> ...             change blueprint bytes
 *   K <0|1>                       keep (1) or undo (0, default) level changes
 *   H <buffer> <hex digits>       set a buffer: redbuf, wipebuf, whitebuf, topbuf,
 *                                 CDthisframe ... SNabove
 *   R <routine> <a> <x> <y>       run a routine with those register values
 *
 * For each R line one line is printed: "a=<n> <name>=<n> ..." listing the
 * returned value and every state variable, then
 *   snd:<hex>     sounds queued by the routine, in order
 *   hooks:<chars> calls out of the ported code: s addslicers, j jamspikes,
 *                 p potioneffect, m smashmirror, k speaker click, i input
 *                 read, o input saved, d DemoCtrl
 *   lvl:<i>.<v>,… blueprint bytes the routine changed (then undone, unless
 *                 a "K 1" line asked for changes to persist)
 *   redbuf:<hex> wipebuf:<hex> whitebuf:<hex> topbuf:<hex>, and the eight
 *   collision buffers
 * ("-" for an empty list).  The sound queue and hook list start empty for
 * every R line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pop_assets.h"
#include "pop_collide.h"
#include "pop_control.h"
#include "pop_world.h"

static pop_game g;
static uint8_t pristine[POP_LEVEL_SIZE];
static char hooklog[64];
static size_t nhooks;

static void log_hook(char c) { if (nhooks < sizeof hooklog - 1) hooklog[nhooks++] = c; }
static void h_slicers(void *ctx) { (void)ctx; log_hook('s'); }
static void h_jam(void *ctx) { (void)ctx; log_hook('j'); }
static void h_potion(void *ctx) { (void)ctx; log_hook('p'); }
static void h_mirror(void *ctx) { (void)ctx; log_hook('m'); }
static void h_click(void *ctx) { (void)ctx; log_hook('k'); }
static void h_input(void *ctx) { (void)ctx; log_hook('i'); }
static void h_saveinput(void *ctx) { (void)ctx; log_hook('o'); }
static void h_demo(void *ctx) { (void)ctx; log_hook('d'); }

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
    { "weightless", &g.ag.weightless }, { "jarabove", &g.ag.jarabove },
    { "alertguard", &g.ag.alertguard },
    { "JSTKX", &g.JSTKX }, { "JSTKY", &g.JSTKY }, { "btn", &g.btn },
    { "clrF", &g.clrF }, { "clrB", &g.clrB }, { "clrU", &g.clrU }, { "clrD", &g.clrD },
    { "clrbtn", &g.clrbtn }, { "level", &g.level }, { "stunned", &g.stunned },
    { "gotsword", &g.gotsword }, { "offguard", &g.offguard },
    { "EnemyAlert", &g.EnemyAlert }, { "heroic", &g.heroic }, { "refract", &g.refract },
    { "gdtimer", &g.gdtimer }, { "KidStrength", &g.KidStrength },
    { "OppStrength", &g.OppStrength }, { "ChgKidStr", &g.ChgKidStr },
    { "ChgOppStr", &g.ChgOppStr }, { "mergetimer", &g.mergetimer },
    { "rjumpflag", &g.rjumpflag }, { "droppedout", &g.droppedout },
    { "savekidx", &g.savekidx }, { "lastpotion", &g.lastpotion },
    { "blockid", &g.blockid }, { "bufindex", &g.bufindex }, { "timebomb", &g.timebomb },
    { "FCharImage", &g.FCharImage }, { "FCharY", &g.FCharY }, { "FCharFace", &g.FCharFace },
    { "FCharCU", &g.FCharCU }, { "FCharCD", &g.FCharCD }, { "FCharCL", &g.FCharCL },
    { "FCharCR", &g.FCharCR }, { "FCharTable", &g.FCharTable },
    { "imwidth", &g.imwidth }, { "imheight", &g.imheight }, { "leftej", &g.leftej },
    { "rightej", &g.rightej }, { "topej", &g.topej }, { "leftblock", &g.leftblock },
    { "rightblock", &g.rightblock }, { "topblock", &g.topblock },
    { "bottomblock", &g.bottomblock }, { "CDLeftEj", &g.CDLeftEj },
    { "CDRightEj", &g.CDRightEj }, { "blockedge", &g.blockedge },
    { "tempobjid", &g.tempobjid }, { "height", &g.height },
    { "blocky", &g.blocky }, { "collideL", &g.collideL }, { "collideR", &g.collideR },
    { "collX", &g.collX }, { "begrange", &g.begrange }, { "endrange", &g.endrange },
    { "AMtimer", &g.AMtimer }, { "VisScrn", &g.VisScrn }, { "scrnLeft", &g.scrnLeft },
    { "scrnRight", &g.scrnRight }, { "scrnBelowL", &g.scrnBelowL },
    { "scrnBelowR", &g.scrnBelowR }, { "createshad", &g.createshad },
    { "NextLevel", &g.NextLevel }, { "BlockYthis", &g.BlockYthis },
    { "BlockYlast", &g.BlockYlast }, { "SongCue", &g.SongCue },
    { "SongCount", &g.SongCount },
    { "X", &g.rx }, { "Y", &g.ry }, { "blue", &g.blue },
    { "unmodelled", &g.unmodelled },
};
#define NVARS (sizeof vars / sizeof vars[0])

/* the 16-bit CharSeq fields are handled apart */
typedef struct { const char *name; uint16_t *p; } var16;
static var16 vars16[] = {
    { "CharSeq", &g.ch.seq }, { "KidSeq", &g.kid.seq }, { "ShadSeq", &g.shad.seq },
    { "OpSeq", &g.op.seq }, { "FCharX", &g.FCharX },
};

typedef struct { const char *name; uint8_t *p; size_t n; } buf;
static buf bufs[] = {
    { "redbuf", g.redbuf, sizeof g.redbuf }, { "wipebuf", g.wipebuf, sizeof g.wipebuf },
    { "whitebuf", g.whitebuf, sizeof g.whitebuf }, { "topbuf", g.topbuf, sizeof g.topbuf },
    { "CDthisframe", g.CD[POP_CD_THIS], 16 }, { "CDlastframe", g.CD[POP_CD_LAST], 16 },
    { "CDbelow", g.CD[POP_CD_BELOW], 16 }, { "CDabove", g.CD[POP_CD_ABOVE], 16 },
    { "SNthisframe", g.SN[POP_CD_THIS], 16 }, { "SNlastframe", g.SN[POP_CD_LAST], 16 },
    { "SNbelow", g.SN[POP_CD_BELOW], 16 }, { "SNabove", g.SN[POP_CD_ABOVE], 10 },
};
#define NBUFS (sizeof bufs / sizeof bufs[0])
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

    /* control and floor: need the sequence table and image dimensions */
    if (!g.seq || !g.dims) return 0;
    if (!strcmp(r, "jumpseq"))      DO(pop_g_jumpseq(&g, a));
    if (!strcmp(r, "animchar"))     DO(pop_g_animchar(&g));
    if (!strcmp(r, "clrall"))       RET(pop_clrall(&g));
    if (!strcmp(r, "facejstk"))     DO(pop_facejstk(&g));
    if (!strcmp(r, "decstr"))       RET(pop_decstr(&g, a));
    if (!strcmp(r, "getspikes"))    { g.ry = y; RET(pop_getspikes(&g)); }
    if (!strcmp(r, "getopdist"))    RET(pop_getopdist(&g));
    if (!strcmp(r, "checkledge"))   { g.ry = y; RET(pop_checkledge(&g, a)); }
    if (!strcmp(r, "indexblock"))   RET(pop_indexblock(&g));
    if (!strcmp(r, "markwipe"))     { int c = x & 1; g.ry = y; pop_markwipe(&g, a, &c); RET(c); }
    if (!strcmp(r, "markred"))      { int c = x & 1; g.ry = y; pop_markred(&g, a, &c); RET(c); }
    if (!strcmp(r, "removeobj"))    { g.ry = y; DO(pop_removeobj(&g, a)); }
    if (!strcmp(r, "setupchar"))    DO(pop_setupchar(&g));
    if (!strcmp(r, "getedges"))     DO(pop_getedges(&g));
    if (!strcmp(r, "getfwddist"))   RET(pop_getfwddist(&g));
    if (!strcmp(r, "playerctrl"))   DO(pop_playerctrl(&g));
    if (!strcmp(r, "userctrl"))     DO(pop_userctrl(&g));
    if (!strcmp(r, "genctrl"))      DO(pop_genctrl(&g));
    if (!strcmp(r, "guardctrl"))    DO(pop_guardctrl(&g));
    if (!strcmp(r, "fightctrl"))    DO(pop_fightctrl(&g));
    if (!strcmp(r, "standing"))     DO(pop_standing(&g));
    if (!strcmp(r, "crouching"))    DO(pop_crouching(&g));
    if (!strcmp(r, "starting"))     DO(pop_starting(&g));
    if (!strcmp(r, "stjumpup"))     DO(pop_stjumpup(&g));
    if (!strcmp(r, "turning"))      DO(pop_turning(&g));
    if (!strcmp(r, "arunning"))     DO(pop_arunning(&g));
    if (!strcmp(r, "hanging"))      DO(pop_hanging(&g));
    if (!strcmp(r, "doblock"))      DO(pop_doblock(&g));
    if (!strcmp(r, "dostrike"))     DO(pop_dostrike(&g, x));
    if (!strcmp(r, "doretreat"))    DO(pop_doretreat(&g));
    if (!strcmp(r, "doadvance"))    DO(pop_doadvance(&g));
    if (!strcmp(r, "dostartrun"))   DO(pop_dostartrun(&g));
    if (!strcmp(r, "doturn"))       DO(pop_doturn(&g));
    if (!strcmp(r, "dostandjump"))  DO(pop_dostandjump(&g));
    if (!strcmp(r, "docrouch"))     DO(pop_docrouch(&g));
    if (!strcmp(r, "doengarde"))    DO(pop_doengarde(&g));
    if (!strcmp(r, "dojumpup"))     DO(pop_dojumpup(&g));
    if (!strcmp(r, "dojumpedge"))   DO(pop_dojumpedge(&g));
    if (!strcmp(r, "dojumphang"))   DO(pop_dojumphang(&g));
    if (!strcmp(r, "dorunjump"))    DO(pop_dorunjump(&g));
    if (!strcmp(r, "dostepfwd"))    DO(pop_dostepfwd(&g));
    if (!strcmp(r, "dojumphigh"))   DO(pop_dojumphigh(&g));
    if (!strcmp(r, "trypickup"))    RET(pop_trypickup(&g));
    if (!strcmp(r, "pickitup"))     { g.ry = y; DO(pop_pickitup(&g, a)); }
    if (!strcmp(r, "checkfloor"))   DO(pop_checkfloor(&g));
    if (!strcmp(r, "falling"))      DO(pop_falling(&g));
    if (!strcmp(r, "hitflr"))       DO(pop_hitflr(&g));
    if (!strcmp(r, "fallon"))       DO(pop_fallon(&g));
    if (!strcmp(r, "onground"))     DO(pop_onground(&g));
    if (!strcmp(r, "startfall"))    DO(pop_startfall(&g));
    if (!strcmp(r, "cdpatch"))      DO(pop_cdpatch(&g));
    if (!strcmp(r, "insideblock"))  RET(pop_insideblock(&g));
    if (!strcmp(r, "checkimpale"))  DO(pop_checkimpale(&g));
    if (!strcmp(r, "doimpale"))     { g.ry = y; DO(pop_doimpale(&g)); }

    /* collision */
    if (!strcmp(r, "checkbarr"))    DO(pop_checkbarr(&g));
    if (!strcmp(r, "collisions"))   DO(pop_collisions(&g));
    if (!strcmp(r, "checkcoll"))    { g.ry = y; RET(pop_checkcoll(&g, a)); }
    if (!strcmp(r, "checkgate"))    DO(pop_checkgate(&g));
    if (!strcmp(r, "checkslice"))   DO(pop_checkslice(&g));
    if (!strcmp(r, "checkslice2"))  RET(pop_checkslice2(&g));
    if (!strcmp(r, "enemycoll"))    DO(pop_enemycoll(&g));
    if (!strcmp(r, "goneupstairs")) DO(pop_goneupstairs(&g));
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
    pop_seqfile seq;
    pop_dimfile dims;
    const char *err = NULL;
    size_t i;
    int keep = 0;

    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: %s <framedef.bin> [<seqtable.bin> <imgdims.bin>]\n"
                        "       (commands on standard input)\n", argv[0]);
        return 2;
    }
    if (pop_framefile_load(argv[1], &frames, &err) != 0) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 2;
    }
    memset(&g, 0, sizeof g);
    memset(&seq, 0, sizeof seq);
    memset(&dims, 0, sizeof dims);
    g.frames = &frames.tab;
    if (argc == 4) {
        if (pop_seqfile_load(argv[2], &seq, &err) != 0) {
            fprintf(stderr, "%s: %s\n", argv[2], err);
            return 2;
        }
        if (pop_dimfile_load(argv[3], &dims, &err) != 0) {
            fprintf(stderr, "%s: %s\n", argv[3], err);
            return 2;
        }
        g.seq = &seq.tab;
        g.dims = &dims.dims;
    }
    g.hooks.add_slicers = h_slicers;
    g.hooks.jam_spikes = h_jam;
    g.hooks.potion_effect = h_potion;
    g.hooks.smash_mirror = h_mirror;
    g.hooks.speaker_click = h_click;
    g.hooks.get_input = h_input;
    g.hooks.save_input = h_saveinput;
    g.hooks.demo_ctrl = h_demo;

    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'L') {
            const char *h = line + 2;
            for (i = 0; i < POP_LEVEL_SIZE; i++) {
                int hi = hexval(h[2 * i]), lo = hi < 0 ? -1 : hexval(h[2 * i + 1]);
                if (lo < 0) { fprintf(stderr, "bad level line\n"); return 2; }
                g.blueprint[i] = (uint8_t)(hi * 16 + lo);
            }
            memcpy(pristine, g.blueprint, sizeof pristine);
        } else if (line[0] == 'K') {           /* K 1: level changes persist; K 0: undone */
            keep = line[1] && line[2] == '1';
        } else if (line[0] == 'P') {           /* poke blueprint bytes */
            char *tok = strtok(line + 1, " \n");
            while (tok) {
                char *eq = strchr(tok, '=');
                long at = strtol(tok, NULL, 10);
                if (!eq || at < 0 || at >= POP_LEVEL_SIZE) { fprintf(stderr, "bad P line\n"); return 2; }
                g.blueprint[at] = pristine[at] = (uint8_t)strtol(eq + 1, NULL, 10);
                tok = strtok(NULL, " \n");
            }
        } else if (line[0] == 'H') {
            char name[16];
            int at = 0;
            size_t b;
            const char *h;
            if (sscanf(line + 1, "%15s %n", name, &at) != 1) { fprintf(stderr, "bad H line\n"); return 2; }
            h = line + 1 + at;
            for (b = 0; b < NBUFS && strcmp(bufs[b].name, name) != 0; b++) { }
            if (b == NBUFS) { fprintf(stderr, "unknown buffer %s\n", name); return 2; }
            for (i = 0; i < bufs[b].n; i++) {
                int hi = hexval(h[2 * i]), lo = hi < 0 ? -1 : hexval(h[2 * i + 1]);
                if (lo < 0) { fprintf(stderr, "bad H line\n"); return 2; }
                bufs[b].p[i] = (uint8_t)(hi * 16 + lo);
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
            int a, x, y, result, any = 0;
            size_t b;
            g.unmodelled = 0;
            nhooks = 0;
            pop_zerosound(&g.sq);
            if (sscanf(line + 1, "%31s %d %d %d", name, &a, &x, &y) != 4
                || !run(name, (uint8_t)a, (uint8_t)x, (uint8_t)y, &result)) {
                fprintf(stderr, "bad R line: %s", line);
                return 2;
            }
            printf("a=%d", result);
            for (i = 0; i < NVARS; i++) printf(" %s=%u", vars[i].name, (unsigned)*vars[i].p);
            for (i = 0; i < NVARS16; i++) printf(" %s=%u", vars16[i].name, (unsigned)*vars16[i].p);
            printf(" snd:");
            if (g.sq.count == 0) printf("-");
            for (i = 0; i < g.sq.count; i++) printf("%02x", (unsigned)g.sq.items[i]);
            hooklog[nhooks] = '\0';
            printf(" hooks:%s lvl:", nhooks ? hooklog : "-");
            for (i = 0; i < POP_LEVEL_SIZE; i++) {
                if (g.blueprint[i] != pristine[i]) {
                    printf("%s%u.%u", any ? "," : "", (unsigned)i, (unsigned)g.blueprint[i]);
                    if (keep) pristine[i] = g.blueprint[i];
                    else g.blueprint[i] = pristine[i];
                    any = 1;
                }
            }
            if (!any) printf("-");
            for (b = 0; b < NBUFS; b++) {
                printf(" %s:", bufs[b].name);
                for (i = 0; i < bufs[b].n; i++) printf("%02x", (unsigned)bufs[b].p[i]);
            }
            printf("\n");
        }
    }
    pop_framefile_free(&frames);
    pop_seqfile_free(&seq);
    pop_dimfile_free(&dims);
    return 0;
}
