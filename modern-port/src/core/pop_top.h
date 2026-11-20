/* pop_top.h - the top of the game: one whole frame, the kid's part of it
 * with his death, the start of a level, and getting ready for a new screen.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Ported routines (01 POP Source/Source, original commit 25243550); each
 * keeps its original name and is compared with the original code, run in a
 * 6502 simulator (tests/fuzz_control.py, tests/trace_kidstep.py,
 * tests/replay_demo.py).
 *
 *   TOPCTRL.S   RESTART (from line 274) 274-359
 *               NextFrame 503-597, FirstFrame 622-628, DoKid 637-690
 *               initCDbuf 1015-1025, PrepCut 1039-1064
 *               ctrlplayer 1095-1170, entrance 1361-1378, addsfx 1476-1487
 *               static?, cold? 1608-1631, misctimers 1652-1678,
 *               wtlessflash 1685-1700
 *   SUBS.S      DEMO 1233-1236, SETINITIALS 1441-1467,
 *               STARTKID, STARTKID1 1476-1604
 *   CTRL.S      DemoCtrl 587-606
 *   CTRLSUBS.S  GETSCRNS 284-318
 *   FRAMEADV.S  GETINITOBJ 2217-2249
 *
 * Where the original jumps out of the level (to the title sequence, a
 * restart, the win and lose scenes) the port sets pop_game.leave, calls a
 * hook, and every routine above returns at once.  The clock, the input
 * devices' state and the step to the next level are in pop_loop.c; drawing
 * is a hook.
 *
 * See docs/port/game-frame.md.
 */
#ifndef POP_TOP_H
#define POP_TOP_H

#include "pop_game.h"

enum { POP_SQ_RUNNING = 84 };                        /* SEQDATA.S */
enum { POP_SND_SWORDCLASH1 = 17, POP_SND_SWORDCLASH2 = 18 };
enum { POP_MSG_LEVEL = 1, POP_MSG_CONT = 2, POP_MSG_TIME = 3 };
enum { POP_DEADENOUGH = 4, POP_LEVELTIMER = 20, POP_MOUSETIMER = 150, POP_WTLFLASH = 15 };

/* Kid's start in the level's INFO area (EQ.S 275-277). */
enum { POP_KIDSTART_SCRN = 2112, POP_KIDSTART_BLOCK = 2113, POP_KIDSTART_FACE = 2114 };

/* --- a level begins ------------------------------------------------------------------- */

/* SETINITIALS: turn the editor's settings in the level file into the
 * states the game uses (gates up or down, loose floors idle, flask kinds). */
void pop_setinitials(pop_game *g);

/* RESTART from the point where the level is in memory: initial states,
 * guards, the reset of the per-level variables, the kid at his start, the
 * level message, the entrance slamming shut, the first screen. */
void pop_restart_level(pop_game *g);

void pop_startkid(pop_game *g);          /* STARTKID */
void pop_startkid1(pop_game *g);         /* STARTKID1: the part after the sequence is chosen */
void pop_entrance(pop_game *g);          /* entrance */
void pop_initcdbuf(pop_game *g);         /* initCDbuf */

/* --- a frame ---------------------------------------------------------------------------- */

/* NextFrame: everything that decides what the next frame looks like. */
void pop_nextframe(pop_game *g);

void pop_dokid(pop_game *g);             /* DoKid: the kid's whole frame */
void pop_ctrlplayer(pop_game *g);        /* ctrlplayer: control, and what follows his death */
void pop_misctimers(pop_game *g);        /* misctimers: once per frame, before NextFrame */
void pop_wtlessflash(pop_game *g);
void pop_addsfx(pop_game *g);            /* addsfx: the clash of a blocked strike */

/* static? / cold?: 0 if the frame is one a character rests in (cold: lies
 * dead in), 1 otherwise. */
uint8_t pop_static(uint8_t posn);
uint8_t pop_cold(uint8_t posn);

/* --- a new screen ------------------------------------------------------------------------ */

void pop_getscrns(pop_game *g);          /* GETSCRNS: the eight screens around VisScrn */
void pop_prepcut(pop_game *g);           /* PrepCut: if cutscrn differs from VisScrn, enter it */
void pop_firstframe(pop_game *g);        /* FirstFrame */

/* --- the self-running demo ---------------------------------------------------------------- */

void pop_democtrl(pop_game *g);          /* DemoCtrl: the kid's joystick on level 0 */

#endif /* POP_TOP_H */
