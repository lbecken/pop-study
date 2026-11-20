/* pop_fixture.h - a made-up game state shared by the C unit tests.
 *
 * New code; nothing here comes from the original: the level, the frame
 * table, the sequence table and the image sizes are invented so that the
 * tests can be built and run without the original source.
 *
 *   - every frame uses image 1 (2 bytes x 40 lines), has no offsets, and
 *     needs floor;
 *   - sequence n consists of the single frame number n, repeated, so after
 *     jumpseq(n) + animchar the character is in "frame n";
 *   - screen 1 has no neighbours; its row 1 is floor in columns 0-6 and
 *     empty in 7-9, rows 0 and 2 are empty.
 */
#ifndef POP_FIXTURE_H
#define POP_FIXTURE_H

#include <string.h>

#include "pop_auto.h"
#include "pop_control.h"
#include "pop_world.h"

enum { SEQ_ORIGIN = 0x3000, SEQ_COUNT = 127, SEQ_BODY = SEQ_ORIGIN + 2 * SEQ_COUNT };

static pop_game g;
static uint8_t frame_bytes[1200 + 200 + 450 + 192];
static pop_frametab frames;
static uint8_t seq_bytes[2 * SEQ_COUNT + 4 * SEQ_COUNT];
static pop_seqtab seqtab;
static const uint8_t dims0[] = { 2, 40 };          /* one image: 2 bytes x 40 lines */
static pop_imgdims dims;
/* Made-up fight tables: opponent type 0 never does anything by choice;
 * type 1 always blocks and advances, strikes 100 times in 256, never
 * restrikes, is dazed 20 frames by a hit, wears the other colour and has
 * one unit more.  Guards have 3 units on even levels and 4 on odd ones. */
static uint8_t fx_fight[POP_FT_SIZE];
/* (Posn, X, Y, Face, BlockX, BlockY, Action, sequence) x 3 */
static const uint8_t fx_shadpos[POP_SHADPOS_SIZE] = {
    15, 100, 118, 0x00, 3, 1, 0, 2,
    15, 60, 55, 0xFF, 0, 0, 0, 2,
    15, 81, 55, 0x00, 1, 0, 0, 7,
};
/* forward at frame 1, button at 5, everything released at 9 */
static const uint8_t fx_script[] = { 0, 0, 1, 1, 5, 6, 9, 0, 255, 0xFE };
static pop_autodata fx_auto;
static char hooklog[32];
static size_t nhooks;

static void hook(char c) { if (nhooks + 1 < sizeof hooklog) { hooklog[nhooks++] = c; hooklog[nhooks] = 0; } }
static void h_input(void *c) { (void)c; hook('i'); }
static void h_save(void *c) { (void)c; hook('o'); }
static void h_demo(void *c) { (void)c; hook('d'); }
static void h_click(void *c) { (void)c; hook('k'); }
static void h_attract(void *c) { (void)c; hook('a'); }
static void h_refl(void *c) { (void)c; hook('r'); }
static void h_invert(void *c) { (void)c; hook('v'); }

/* Address of made-up sequence n: its only frame is number n, repeated. */
static uint16_t seq_addr(unsigned n) { return (uint16_t)(SEQ_BODY + 4u * (n - 1u)); }

static void put(int scrn, int row, int col, uint8_t type, uint8_t spec)
{
    g.blueprint[POP_BLUETYPE + (scrn - 1) * 30 + row * 10 + col] = type;
    g.blueprint[POP_BLUESPEC + (scrn - 1) * 30 + row * 10 + col] = spec;
}

/* Screen 1, no neighbours.  Row 1 is floor in columns 0-6 and empty in 7-9;
 * rows 0 and 2 are empty.  The kid stands in column `col` of row 1, alive. */
static void setup(int col, uint8_t face)
{
    unsigned i;
    int c;
    memset(&g, 0, sizeof g);
    nhooks = 0;
    hooklog[0] = 0;

    for (i = 0; i < 1200; i += 5) {                /* every frame: image 1, needs floor */
        frame_bytes[i] = 1;                        /* Fimage */
        frame_bytes[i + 4] = 0x40;                 /* Fcheck: fcheckmark, foot offset 0 */
    }
    frames.data = frame_bytes;
    frames.size = sizeof frame_bytes;
    frames.origin = 0x2800;
    frames.fdef = 0x2800;
    frames.altset1 = 0x2800 + 1200;
    frames.altset2 = 0x2800 + 1400;
    frames.swordtab = 0x2800 + 1850;
    g.frames = &frames;

    for (i = 1; i <= SEQ_COUNT; i++) {
        uint16_t a = seq_addr(i);
        uint8_t *body = seq_bytes + (a - SEQ_ORIGIN);
        seq_bytes[2 * (i - 1)] = (uint8_t)a;
        seq_bytes[2 * (i - 1) + 1] = (uint8_t)(a >> 8);
        body[0] = (uint8_t)i;                      /* frame number = sequence number */
        body[1] = POP_SEQ_GOTO;
        body[2] = (uint8_t)a;
        body[3] = (uint8_t)(a >> 8);
    }
    seqtab.data = seq_bytes;
    seqtab.size = sizeof seq_bytes;
    seqtab.origin = SEQ_ORIGIN;
    g.seq = &seqtab;

    memset(fx_fight, 0, sizeof fx_fight);
    fx_fight[POP_FT_STRIKE + 1] = 100;
    fx_fight[POP_FT_BLOCK + 1] = 255;
    fx_fight[POP_FT_ADV + 1] = 255;
    fx_fight[POP_FT_REFRACT + 1] = 20;
    fx_fight[POP_FT_SPECIALCOLOR + 1] = 1;
    fx_fight[POP_FT_EXTRASTR + 1] = 1;
    for (i = 0; i < 14; i++) {
        fx_fight[POP_FT_BASICSTR + i] = (uint8_t)(3 + (i & 1u));
        fx_fight[POP_FT_BASICCOLOR + i] = (uint8_t)(i & 1u);
    }
    fx_auto.fight = fx_fight;
    fx_auto.shadpos = fx_shadpos;
    fx_auto.shadprog5 = fx_script;
    fx_auto.shadprog5_len = sizeof fx_script;
    g.autodata = &fx_auto;

    dims.count[0] = 1;
    dims.wh[0] = dims0;
    g.dims = &dims;

    g.hooks.get_input = h_input;
    g.hooks.save_input = h_save;
    g.hooks.demo_ctrl = h_demo;
    g.hooks.speaker_click = h_click;
    g.hooks.attract_mode = h_attract;
    g.hooks.add_refl_obj = h_refl;
    g.hooks.invert_y = h_invert;

    for (c = 0; c <= 6; c++) put(1, 1, c, POP_FLOOR, 0);

    g.ch.scrn = 1;
    g.ch.posn = 15;
    g.ch.face = face;
    g.ch.x = (uint8_t)(POP_SCRN_LEFT + POP_BLOCK_WIDTH * col + 7);
    g.ch.y = 118;                                  /* floor line of row 1 */
    g.ch.block_x = (uint8_t)col;
    g.ch.block_y = 1;
    g.ch.life = 0xFF;
    g.level = 1;
    g.KidStrength = 3;
    g.OppStrength = 3;
    g.MaxKidStr = 3;
    g.MaxOppStr = 3;
    g.shad.face = POP_NO_CHAR;
    g.VisScrn = 1;
    g.collideL = g.collideR = 0xFF;
    pop_calcblue(&g, 1);
    pop_rereadblocks(&g);
}

#endif /* POP_FIXTURE_H */
