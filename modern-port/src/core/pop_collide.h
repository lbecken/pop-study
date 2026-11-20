/* pop_collide.h - collisions with walls, gates, slicers and the mirror.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Ported routines (01 POP Source/Source/COLL.S, original commit 25243550);
 * each keeps its original name and is compared with the original code, run
 * in a 6502 simulator, on randomised states (tests/fuzz_collide.py).
 *
 *   CHECKBARR 72-184, getCData 194-255, initCDbufs 267-328
 *   getleftbar 344-359, getrightbar 368-385
 *   COLLISIONS 400-433, rightcoll 440-460, leftcoll 467-486
 *   checkcoll1 495-515, CHECKCOLL 527-589, AdjustScrn 602-620
 *   collide 631-680, AirBump 685-708, BumpSound 704-708, GroundBump 713-808
 *   GoneUpstairs 1162-1176
 *   CHECKSLICE 1186-1255, CHECKSLICE2 1266-1309
 *   CHECKGATE 1319-1367, gatebarr? 1374-1381
 *   ENEMYCOLL 1389-1453, DBarr2 1464-1509
 *
 * (GETFWDDIST and DBarr, also in COLL.S, are in pop_control.c; ANIMCHAR is
 * in pop_seq.c.)
 *
 * See docs/port/collision.md.
 */
#ifndef POP_COLLIDE_H
#define POP_COLLIDE_H

#include "pop_game.h"

/* Sequence numbers started here, SEQDATA.S. */
enum {
    POP_SQ_BUMPFALL = 45, POP_SQ_HARDBUMP = 46, POP_SQ_BUMP = 47, POP_SQ_HALVE = 54,
    POP_SQ_BUMPENGFWD = 64, POP_SQ_BUMPENGBACK = 65
};

/* Song numbers passed to CUESONG by GoneUpstairs. */
enum { POP_SONG_SHADOW = 6, POP_SONG_UPSTAIRS = 9 };

enum { POP_SLICER_EXT = 2 };     /* slicer state: blades closed, MOVEDATA.S 48 */

/* CHECKBARR: compare the character's edges (GETEDGES) with every barrier in
 * the blocks around him, on his row and the rows above and below, and with
 * the same comparison made last frame.  A barrier edge he was not past last
 * frame and is past now is a collision.
 * Out: collideL / collideR = column 0-9 of the barrier whose left / right
 * edge was crossed, or $FF. */
void pop_checkbarr(pop_game *g);

/* COLLISIONS: act on what CHECKBARR found: push the character back out of
 * the barrier and start a bump. */
void pop_collisions(pop_game *g);

/* CHECKCOLL: is the piece just read with rdblock (id `objid`, block ry) a
 * barrier for this character right now?  Returns the carry: 1 = yes, and
 * blockedge is set.  A running jump through the mirror smashes it. */
int pop_checkcoll(pop_game *g, uint8_t objid);

/* gatebarr?: for the gate just read; returns the carry: 0 = it bars the
 * character, 1 = open enough to pass under. */
int pop_gatebarr(pop_game *g);

/* CHECKGATE: a gate closing on a standing or crouching character knocks him
 * aside. */
void pop_checkgate(pop_game *g);

/* CHECKSLICE: is the character inside a closed slicer (from the collision
 * data)?  CHECKSLICE2: the same for a character without collision data, from
 * his edges; returns the carry, 1 = sliced. */
void pop_checkslice(pop_game *g);
int  pop_checkslice2(pop_game *g);

/* ENEMYCOLL: the limited check for opponents: backing into a wall or gate
 * while fighting. */
void pop_enemycoll(pop_game *g);

/* CUESONG (SPECIALK.S 1332-1335): ask for a song to be played within a
 * number of frames.  Nothing plays music yet; the request is recorded in
 * SongCue / SongCount as in the original. */
void pop_cuesong(pop_game *g, uint8_t song, uint8_t within);

/* GoneUpstairs: cue the end-of-level music and ask for the next level. */
void pop_goneupstairs(pop_game *g);

#endif /* POP_COLLIDE_H */
