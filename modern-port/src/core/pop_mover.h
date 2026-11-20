/* pop_mover.h - things in the level that move or change: gates, the exit,
 * pressure plates and what they are linked to, spikes, slicers, loose and
 * falling floors, torches, flasks, the sword.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Ported routines (01 POP Source/Source, original commit 25243550); each
 * keeps its original name and is compared with the original code, run in a
 * 6502 simulator, on randomised states (tests/fuzz_control.py).
 *
 *   MOVER.S     searchtrob, addtrob, addamob, savemob, loadmob     105-208
 *               TRIGSLICER, CLOSEEXIT, SMASHMIRROR, TRIGFLASK,
 *               TRIGSWORD, TRIGTORCH, TRIGSPIKES, JAMSPIKES        217-358
 *               BREAKLOOSE, BREAKLOOSE1, PUSHPP, JAMPP, trigger,
 *               trigobj, openexit, triggate                         389-619
 *               ANIMTRANS, animobj and the anim... routines         629-1248
 *               redtrobj ... redplate, check, checkright,
 *               checkabover                                          1253-1539
 *               gettimer ... getscrn                                 1549-1581
 *               ANIMMOBS, animmob, mobfloor, knockloose, makespace,
 *               passthru, makerubble, markmob, checkcrush,
 *               crushchar                                            1588-1977
 *               SHAKEM, SHAKEM1, shakeit                             2120-2168
 *   CTRL.S      CHECKPRESS 1939-2005
 *   CTRLSUBS.S  UNINDEX 648-656, CHECKSPIKES 1868-1909,
 *               MARKFRED 1739-1742, MARKMOVE 1759-1762
 *   TOPCTRL.S   shakeloose 1271-1291
 *   SUBS.S      CRUMBLE 108-140, ADDTORCHES 146-184, ADDSLICERS 250-299,
 *               ADDLOWERSOUND 1373-1406, MIRAPPEAR 1743-1758
 *   GRAFIX.S    RND 1386-1395
 *
 * (GETSPIKES is in pop_control.c.)  Not ported: ADDMOBS, ATM, addmobobj
 * (MOVER.S 1984-2112), which put falling floors on the list of things to
 * draw.
 *
 * Routines documented as taking "RDBLOCK results" use g->ry (the block
 * index, as the Y register) and the screen selected by the last calcblue.
 *
 * See docs/port/moving-objects.md.
 */
#ifndef POP_MOVER_H
#define POP_MOVER_H

#include "pop_game.h"

/* Sound numbers queued here, SOUNDNAMES.S. */
enum {
    POP_SND_PLATEDOWN = 0, POP_SND_PLATEUP = 1, POP_SND_GATEDOWN = 2,
    POP_SND_LOOSECRASH = 7, POP_SND_RAISINGEXIT = 10, POP_SND_RAISINGGATE = 11,
    POP_SND_LOWERINGGATE = 12, POP_SND_GATESLAM = 15, POP_SND_JAWSCLASH = 19
};

enum { POP_SONG_STAIRS = 8 };
enum { POP_SQ_CRUSH = 52 };                          /* SEQDATA.S */

/* State values, MOVEDATA.S 42-57 and MOVER.S 59-92. */
enum {
    POP_SPIKE_EXT = 5, POP_SPIKE_RET = 9, POP_SLICER_RET = 6, POP_TORCH_LAST = 17,
    POP_BUBB_LAST = 8, POP_FFALLING = 10,
    POP_GMAXVAL = 47 * 4, POP_GATETIMER = 47 * 4 + 50, POP_EMAXVAL = 43 * 4,
    POP_PPTIMER = 5, POP_SPIKETIMER = 15 + 128, POP_SLICETIMER = 15,
    POP_REQMASK = 0x20                               /* EQ.S: "required" floor piece */
};

/* Mirror position on level 4, SUBS.S 77-79. */
enum { POP_MIRSCRN = 4, POP_MIRX = 4, POP_MIRY = 0 };

/* RND: RNDseed = 5 * RNDseed + 23 (mod 256); returns the new seed. */
uint8_t pop_rnd(pop_game *g);

/* UNINDEX: block index -> column (returned) and row (*row). */
uint8_t pop_unindex(uint8_t index, uint8_t *row);

/* --- the list of things in transition ------------------------------------------------ */

/* addtrob: add (trloc[0], trscrn[0], trdirec[0]) to the list, or change the
 * direction of the entry already there. */
void pop_addtrob(pop_game *g);

/* ANIMTRANS: advance every listed object one frame and drop those that have
 * stopped.  ANIMMOBS: the same for falling floors, with crushing. */
void pop_animtrans(pop_game *g);
void pop_animmobs(pop_game *g);

/* --- triggers: In: the block last read (g->ry, calcblue) ------------------------------- */

void pop_trigslicer(pop_game *g, uint8_t state);
void pop_closeexit(pop_game *g, uint8_t scrn);
void pop_smashmirror(pop_game *g);
void pop_trigflask(pop_game *g, uint8_t scrn);
void pop_trigsword(pop_game *g, uint8_t scrn);
void pop_trigtorch(pop_game *g, uint8_t scrn);
void pop_trigspikes(pop_game *g);        /* also uses tempscrn */
void pop_jamspikes(pop_game *g);
void pop_breakloose(pop_game *g);
void pop_breakloose1(pop_game *g, uint8_t state);
void pop_pushpp(pop_game *g);
void pop_jampp(pop_game *g);

uint8_t pop_getflameframe(pop_game *g, uint8_t state);
uint8_t pop_getflaskframe(uint8_t frame);

/* SHAKEM: start every idle loose floor of a row of the visible screen
 * wiggling. */
void pop_shakem(pop_game *g, uint8_t blocky);

/* --- what a character sets off ------------------------------------------------------------ */

void pop_checkpress(pop_game *g);        /* CHECKPRESS: plates and loose floors underfoot */
void pop_checkspikes(pop_game *g);       /* CHECKSPIKES: spikes spring as he passes over */
void pop_shakeloose(pop_game *g);        /* shakeloose: a jump or landing jars the floors */
void pop_addslicers(pop_game *g);        /* ADDSLICERS: start the slicers of his row */

/* --- screen and level events ----------------------------------------------------------------- */

void pop_addtorches(pop_game *g);        /* ADDTORCHES: animate what is on the visible screen */
void pop_crumble(pop_game *g);           /* CRUMBLE: level 13's collapsing ceiling */
void pop_mirappear(pop_game *g);         /* MIRAPPEAR: level 4's mirror */
void pop_addlowersound(pop_game *g, uint8_t state);

/* --- redraw marks used here (CTRLSUBS.S 1739-1762) ------------------------------------------- */

void pop_markfred(pop_game *g, uint8_t a, int *carry);
void pop_markmove(pop_game *g, uint8_t a, int *carry);

#endif /* POP_MOVER_H */
