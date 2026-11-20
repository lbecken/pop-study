/* test_mover.c - moving level objects on synthetic data.
 *
 * New code; uses the made-up state of pop_fixture.h.  Expected values were
 * worked out by hand from MOVER.S (see pop_mover.h for the line ranges):
 * mostly whole timelines, frame by frame, since that is what these
 * routines are for.  The same routines are compared with the original code
 * on random states (tests/fuzz_control.py) and inside the multi-frame kid
 * trace (tests/trace_kidstep.py) when the original source is available.
 */

#include <string.h>

#include "pop_collide.h"
#include "pop_fixture.h"
#include "pop_mover.h"
#include "pop_test.h"

#define TYPE(row, col) (g.blueprint[POP_BLUETYPE + (row) * 10 + (col)])
#define SPEC(row, col) (g.blueprint[POP_BLUESPEC + (row) * 10 + (col)])

static int queued(uint8_t sound)
{
    unsigned i;
    for (i = 0; i < g.sq.count; i++) {
        if (g.sq.items[i] == sound) return 1;
    }
    return 0;
}

static void run_frames(int n)
{
    while (n-- > 0) {
        pop_zerosound(&g.sq);
        pop_animtrans(&g);
    }
}

/* Link entry `i`: block `loc` of screen `scrn`, last of its chain. */
static void link_to(uint8_t i, uint8_t scrn, uint8_t loc)
{
    g.blueprint[POP_LINKLOC + i] = (uint8_t)(loc | ((scrn & 3u) << 5) | 0x80u);
    g.blueprint[POP_LINKMAP + i] = (uint8_t)((scrn >> 2) << 5);
}

static void test_rnd_and_unindex(void)
{
    uint8_t row;
    setup(3, POP_FACE_LEFT);
    CHECK_EQ(pop_rnd(&g), 23);                     /* 5*0 + 23 */
    CHECK_EQ(pop_rnd(&g), 138);
    CHECK_EQ(pop_rnd(&g), 201);                    /* 713 mod 256 */
    CHECK_EQ(pop_unindex(27, &row), 7);
    CHECK_EQ(row, 2);
    CHECK_EQ(pop_unindex(9, &row), 9);
    CHECK_EQ(row, 0);
}

static void test_addtrob(void)
{
    int i;
    setup(3, POP_FACE_LEFT);
    g.trloc[0] = 12; g.trscrn[0] = 1; g.trdirec[0] = 1;
    pop_addtrob(&g);
    CHECK_EQ(g.numtrans, 1);
    g.trdirec[0] = 3;                              /* same object again: only its direction */
    pop_addtrob(&g);
    CHECK_EQ(g.numtrans, 1);
    CHECK_EQ(g.trdirec[1], 3);
    g.trscrn[0] = 2;                               /* same block of another screen: new */
    pop_addtrob(&g);
    CHECK_EQ(g.numtrans, 2);
    for (i = 0; i < 40; i++) {                     /* the list holds 31 */
        g.trloc[0] = (uint8_t)(13 + i);
        pop_addtrob(&g);
    }
    CHECK_EQ(g.numtrans, 31);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_plate_opens_gate(void)
{
    /* The kid steps on an "up" plate in column 3 linked to the gate in
     * column 5.  The plate stays down 5 frames; the gate rises 4 a frame to
     * 188, waits while its state counts 238 down to 188, then comes down 1
     * a frame: 47 + 238 = 285 frames from the push until it is shut. */
    setup(3, POP_FACE_LEFT);
    put(1, 1, 3, POP_UPRESSPLATE, 7);              /* its link index */
    put(1, 1, 5, POP_GATE, 0);
    link_to(7, 1, 15);
    pop_checkpress(&g);
    CHECK_EQ(g.numtrans, 2);
    CHECK_EQ(g.trloc[1], 13);                      /* the plate, then the gate */
    CHECK_EQ(g.trloc[2], 15);
    CHECK_EQ(g.trdirec[2], 1);
    CHECK_EQ(g.blueprint[POP_LINKMAP + 7] & 0x1F, 5);        /* the plate's timer */
    CHECK(queued(POP_SND_PLATEDOWN));
    CHECK_EQ(g.ag.alertguard, 1);
    CHECK_EQ(g.rb[POP_RB_RED + 13], 2);                     /* plate marked for redraw */

    run_frames(1);
    CHECK_EQ(SPEC(1, 5), 4);
    CHECK(queued(POP_SND_RAISINGGATE));
    CHECK_EQ(g.rb[POP_RB_MOVE + 16], 2);                    /* redrawn through the piece to its right */
    run_frames(3);
    CHECK_EQ(g.numtrans, 1);                       /* plate back up after 4 frames */
    CHECK(queued(POP_SND_PLATEUP));
    CHECK_EQ(g.blueprint[POP_LINKMAP + 7] & 0x1F, 1);
    run_frames(43);
    CHECK_EQ(SPEC(1, 5), 238);                     /* frame 47: at the top, timer started */
    CHECK_EQ(g.trdirec[1], 0);
    run_frames(50);
    CHECK_EQ(SPEC(1, 5), 188);                     /* still fully open */
    run_frames(187);
    CHECK_EQ(SPEC(1, 5), 1);
    CHECK_EQ(g.numtrans, 1);
    run_frames(1);                                     /* frame 285 */
    CHECK_EQ(SPEC(1, 5), 0);
    CHECK_EQ(g.numtrans, 0);
    CHECK(queued(POP_SND_GATEDOWN));

    /* stepping on it again while the gate is open just restarts the wait */
    setup(3, POP_FACE_LEFT);
    put(1, 1, 3, POP_UPRESSPLATE, 7);
    put(1, 1, 5, POP_GATE, 200);
    link_to(7, 1, 15);
    pop_checkpress(&g);
    CHECK_EQ(SPEC(1, 5), 238);
    CHECK_EQ(g.numtrans, 1);                       /* only the plate */
}

static void test_plate_closes_gate_and_jams(void)
{
    setup(3, POP_FACE_LEFT);                       /* a "down" plate drops the gate fast */
    put(1, 1, 3, POP_PRESSPLATE, 7);
    put(1, 1, 5, POP_GATE, 188);
    link_to(7, 1, 15);
    pop_checkpress(&g);
    CHECK_EQ(g.trdirec[2], 3);
    /* It drops 40, 60, 80, 100 lines' worth a frame.  (The speed table
     * starts 20, 40 ...; the index is stepped before its first use, so the
     * 20 is never applied: MOVER.S 900-909.) */
    run_frames(1);
    CHECK_EQ(SPEC(1, 5), 148);
    run_frames(2);
    CHECK_EQ(SPEC(1, 5), 8);
    run_frames(1);
    CHECK_EQ(SPEC(1, 5), 0);
    CHECK(queued(POP_SND_GATESLAM));
    CHECK_EQ(g.numtrans, 0);                       /* gate shut; the plate is up again too */

    setup(3, POP_FACE_LEFT);                       /* a dead body is a permanent weight */
    put(1, 1, 3, POP_PRESSPLATE, 7);
    put(1, 1, 5, POP_GATE, 188);
    link_to(7, 1, 15);
    g.ch.life = 0;
    pop_checkpress(&g);
    CHECK_EQ(TYPE(1, 3), POP_DPRESSPLATE);
    CHECK_EQ(g.trdirec[2], 3);
}

static void test_spikes(void)
{
    static const uint8_t state[] = { 1, 2, 3, 4, 5, 143, 142 };
    static const uint8_t danger[] = { 2, 2, 2, 2, 0, 1, 1 };
    unsigned i;
    setup(3, POP_FACE_LEFT);
    put(1, 1, 3, POP_SPIKES, 0);
    pop_setupchar(&g);
    pop_getedges(&g);
    pop_checkspikes(&g);                           /* he is over them: they spring */
    CHECK_EQ(g.numtrans, 1);
    CHECK_EQ(SPEC(1, 3), 0);
    CHECK(queued(POP_SND_GATEDOWN));
    for (i = 0; i < sizeof state; i++) {
        run_frames(1);
        CHECK_EQ(SPEC(1, 3), state[i]);
        (void)pop_rdblock(&g, 1, 3, 1);
        /* 2 = springing, 1 = out, 0 = harmless.  The one frame in which
         * the state is exactly 5 (fully out, timer not yet started) reads
         * as harmless: GETSPIKES tests "< 5" (MOVER.S 370-373). */
        CHECK_EQ(pop_getspikes(&g), danger[i]);
    }
    run_frames(14);
    CHECK_EQ(SPEC(1, 3), 6);                       /* timer ran out: retracting */
    run_frames(3);
    CHECK_EQ(SPEC(1, 3), 9);
    CHECK_EQ(g.numtrans, 1);
    run_frames(1);                                     /* frame 25 */
    CHECK_EQ(SPEC(1, 3), 0);
    CHECK_EQ(g.numtrans, 0);

    setup(3, POP_FACE_LEFT);                       /* passing over extended spikes resets the timer */
    put(1, 1, 3, POP_SPIKES, 0x85);
    pop_setupchar(&g);
    pop_getedges(&g);
    pop_checkspikes(&g);
    CHECK_EQ(SPEC(1, 3), 143);
    CHECK_EQ(g.numtrans, 0);
}

static void test_loose_floor(void)
{
    int i;
    setup(3, POP_FACE_LEFT);
    put(1, 1, 3, POP_LOOSE, 0);
    put(1, 2, 3, POP_FLOOR, 0);                    /* something to land on */
    pop_checkpress(&g);                            /* standing on it starts the count */
    CHECK_EQ(SPEC(1, 3), 1);
    CHECK_EQ(g.trdirec[1], 0);
    run_frames(8);
    CHECK_EQ(SPEC(1, 3), 9);
    CHECK_EQ(TYPE(1, 3), POP_LOOSE);
    run_frames(1);                                     /* the count reaches 10: it lets go */
    CHECK_EQ(TYPE(1, 3), POP_SPACE);
    CHECK_EQ(g.numtrans, 0);
    CHECK_EQ(g.nummob, 1);
    CHECK_EQ(g.mobx[1], 12);                       /* column x 4 */
    CHECK_EQ(g.moby[1], 128);                      /* bottom line of row 1 */
    CHECK_EQ(g.moblevel[1], 1);

    /* it falls 3, 6, 9 ... faster each frame, and lands on the floor of
     * row 2 (plane at 188) in the sixth */
    g.kid = g.ch;
    g.kid.scrn = 9;                                /* keep the kid out of the way */
    for (i = 0; i < 5; i++) pop_animmobs(&g);
    CHECK_EQ(g.moby[1], 128 + 3 + 6 + 9 + 12 + 15);
    CHECK_EQ(g.moblevel[1], 2);                    /* through the gap it left */
    pop_zerosound(&g.sq);
    pop_animmobs(&g);
    CHECK(queued(POP_SND_LOOSECRASH));
    CHECK_EQ(TYPE(2, 3), POP_RUBBLE);
    CHECK_EQ(g.nummob, 0);
    CHECK_EQ(g.unmodelled, 0);
}

static void test_wiggle(void)
{
    setup(3, POP_FACE_LEFT);
    put(1, 1, 2, POP_LOOSE, 0);
    put(1, 1, 5, POP_LOOSE, 1);                    /* already counting: left alone */
    g.ag.jarabove = 0xFF;                          /* he landed on this row */
    pop_shakeloose(&g);
    CHECK_EQ(g.ag.jarabove, 0);
    CHECK_EQ(SPEC(1, 2), 0x80);
    CHECK_EQ(SPEC(1, 5), 1);
    CHECK_EQ(g.numtrans, 1);
    run_frames(3);
    CHECK_EQ(SPEC(1, 2), 0x83);
    run_frames(1);                                     /* four frames of wiggling, then still */
    CHECK_EQ(SPEC(1, 2), 0);
    CHECK_EQ(g.numtrans, 0);
    CHECK_EQ(TYPE(1, 2), POP_LOOSE);
}

static void test_crush(void)
{
    setup(3, POP_FACE_LEFT);                       /* a floor 20 lines above his feet, falling */
    g.kid = g.ch;
    g.nummob = 1;
    g.mobx[1] = 12; g.moby[1] = 95; g.mobscrn[1] = 1; g.mobvel[1] = 0; g.moblevel[1] = 1;
    pop_animmobs(&g);                              /* 98: within 30 lines above 118 */
    CHECK_EQ(g.ChgKidStr, 0xFF);                   /* one unit */
    CHECK_EQ(g.kid.seq, seq_addr(POP_SQ_CRUSH));

    setup(3, POP_FACE_LEFT);                       /* running: he gets away with it */
    g.ch.posn = 9;
    g.kid = g.ch;
    g.nummob = 1;
    g.mobx[1] = 12; g.moby[1] = 95; g.mobscrn[1] = 1; g.moblevel[1] = 1;
    pop_animmobs(&g);
    CHECK_EQ(g.ChgKidStr, 0);
    CHECK_EQ(g.kid.seq, 0);
}

static void test_slicers(void)
{
    setup(3, POP_FACE_LEFT);
    put(1, 1, 1, POP_SLICER, 0);
    put(1, 1, 4, POP_SLICER, 0);
    put(1, 1, 6, POP_SLICER, 3);                   /* in mid-slice: not restarted */
    g.kid = g.ch;
    pop_addslicers(&g);                            /* the slicers of his row, out of step */
    CHECK_EQ(g.numtrans, 2);
    CHECK_EQ(SPEC(1, 1), 15);
    CHECK_EQ(SPEC(1, 4), 12);
    CHECK_EQ(SPEC(1, 6), 3);
    run_frames(1);
    CHECK_EQ(SPEC(1, 1), 1);                       /* 15 wraps to 1 */
    CHECK_EQ(SPEC(1, 4), 13);
    run_frames(1);
    CHECK_EQ(SPEC(1, 1), POP_SLICER_EXT);          /* shut */
    CHECK(queued(POP_SND_JAWSCLASH));
    CHECK_EQ(g.rb[POP_RB_WIPE + 11], 2);
    CHECK_EQ(g.rb[POP_RB_WHITE + 11], 63);

    g.kid.block_y = 2;                             /* he leaves the row: each stops once open */
    run_frames(1);
    CHECK_EQ(SPEC(1, 4), 15);                      /* this one was open already: dropped at once */
    CHECK_EQ(g.numtrans, 1);
    run_frames(3);
    CHECK_EQ(SPEC(1, 1), POP_SLICER_RET);          /* the other finishes its bite first */
    CHECK_EQ(g.numtrans, 0);
}

static void test_screen_objects(void)
{
    setup(3, POP_FACE_LEFT);
    put(1, 0, 2, POP_TORCH, 0);
    put(1, 1, 6, POP_FLASK, 0x40);                 /* potion 2 */
    pop_addtorches(&g);                            /* entering the screen */
    CHECK_EQ(g.numtrans, 2);
    CHECK_EQ(SPEC(1, 6) & 0xE0, 0x40);             /* potion kept, bubble frame random */
    run_frames(1);
    CHECK_EQ(SPEC(1, 6) & 0xE0, 0x40);
    CHECK(SPEC(0, 2) <= POP_TORCH_LAST);
    CHECK_EQ(g.rb[POP_RB_MOVE + 3], 2);                     /* torch: redrawn via the piece to its right */
    CHECK_EQ(g.rb[POP_RB_MOVE + 16], 2);                    /* flask: its own block */
    g.VisScrn = 2;                                 /* leaving it: both are dropped */
    run_frames(1);
    CHECK_EQ(g.numtrans, 0);
}

static void test_mirror_and_exit(void)
{
    setup(3, POP_FACE_LEFT);
    put(1, 1, 5, POP_EXIT, 0);
    put(1, 1, 3, POP_UPRESSPLATE, 7);
    link_to(7, 1, 15);
    pop_checkpress(&g);                            /* the plate that opens the way out */
    CHECK_EQ(g.trloc[2], 15);
    run_frames(42);
    CHECK_EQ(SPEC(1, 5), 168);
    CHECK_EQ(g.exitopen, 0);
    run_frames(1);                                     /* 43 x 4 = 172: open */
    CHECK_EQ(SPEC(1, 5), POP_EMAXVAL);
    CHECK_EQ(g.exitopen, 1);
    CHECK_EQ(g.SongCue, POP_SONG_STAIRS);
    CHECK_EQ(g.SongCount, 15);
    CHECK_EQ(g.numtrans, 0);

    pop_checkpress(&g);                            /* an open exit is not triggered again */
    CHECK_EQ(g.numtrans, 1);                       /* (only the plate itself goes down) */
    CHECK_EQ(g.trloc[1], 13);
    CHECK_EQ(SPEC(1, 5), POP_EMAXVAL);
}

int main(void)
{
    RUN(test_rnd_and_unindex);
    RUN(test_addtrob);
    RUN(test_plate_opens_gate);
    RUN(test_plate_closes_gate_and_jams);
    RUN(test_spikes);
    RUN(test_loose_floor);
    RUN(test_wiggle);
    RUN(test_crush);
    RUN(test_slicers);
    RUN(test_screen_objects);
    RUN(test_mirror_and_exit);
    return pop_test_summary();
}
