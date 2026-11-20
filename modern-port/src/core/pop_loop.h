/* pop_loop.h - the game loop around a frame: the input devices' state, the
 * 60-minute clock, the messages, the cue for a song, the flashes, the pass
 * from one level to the next, and MainLoop itself.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Ported routines (01 POP Source/Source, original commit 25243550); each
 * keeps its original name and is compared with the original code, run in a
 * 6502 simulator (tests/fuzz_control.py).
 *
 *   GRAFIX.S    GETSELECT, GETDESEL, getjoy, getkbd 1030-1062
 *   SPECIALK.S  KREAD 718-786, SAVESELECT ... INITINPUT 807-843,
 *               CLRJSTK 883-962, DEMOKEYS 1015-1031,
 *               GETMINLEFT 1173-1232, timetable 1234-1253,
 *               KEEPTIME 1261-1297, SHORTENTIME 1305-1316
 *   SUBS.S      SHOWTIME 1304-1366
 *   TOPCTRL.S   StartGame 142-161, initgame 219-241, MainLoop 366-399,
 *               LoadNextLevel, LoadNext1 414-490, FrameAdv 606-613,
 *               DoCleanCut 891-906, drawbg 918-931, DoFast 961-982,
 *               flashon, flashoff 990-1008, checkmeters 1299-1306,
 *               songcues 1388-1467, dispmsg 1495-1537, clearjoy 1638-1645,
 *               develpatch 1721-1734
 *   GRAFIX.S    ZERORED 1805-1823
 *
 * What is not here: reading the keyboard, joystick and buttons (the caller
 * sets keypress, keydown, joyX, joyY, BTN0, BTN1), drawing, playing sounds
 * and songs, loading a level, the princess scenes.  Those are hooks.
 *
 * See docs/port/game-loop.md.
 */
#ifndef POP_LOOP_H
#define POP_LOOP_H

#include "pop_game.h"

/* The clock, SPECIALK.S 68-71: the game's "minute" is 725 frames and its
 * "second" 725/60 = 12 frames, so 60 "seconds" are 5 frames short of a
 * "minute". */
enum { POP_MIN_FRAMES = 725, POP_SEC_FRAMES = POP_MIN_FRAMES / 60, POP_TIME_LIMIT = 60 };
enum { POP_NUM_TIME_MSG = 18 };                      /* entries of timetable */
enum { POP_TIMEMSGTIMER = 20, POP_CONTFLASH = 95, POP_CONTOFF = 15 };
enum { POP_SND_FLASHMSG = 16 };
enum { POP_INITMAXSTR = 3, POP_LAST_LEVEL = 14 };

/* What dispmsg asks to be drawn over the frame. */
enum { POP_SHOW_NOTHING = 0, POP_SHOW_CONTINUE, POP_SHOW_LEVEL, POP_SHOW_TIME };

/* --- input ------------------------------------------------------------------------------ */

/* KREAD: the keyboard as a joystick.  In: keypress (high bit: a new press),
 * keydown (high bit: a key is held).  Out: kbdX, kbdY. */
void pop_kread(pop_game *g);

/* GETSELECT / GETDESEL: JSTKX, JSTKY and btn from the selected device (the
 * joystick if joyon, else the keyboard) or from the other one. */
void pop_getselect(pop_game *g);
void pop_getdesel(pop_game *g);

/* CLRJSTK: keep the five "press" flags: a direction or button newly held
 * becomes an unused press (-1); a used press (1) is cleared on release. */
void pop_clrjstk(pop_game *g);

void pop_saveselect(pop_game *g);        /* the flags, kept per device */
void pop_loadselect(pop_game *g);
void pop_savedesel(pop_game *g);
void pop_loaddesel(pop_game *g);
void pop_initinput(pop_game *g);
void pop_clearjoy(pop_game *g);          /* clearjoy: forget the direction presses */

/* DEMOKEYS: 0xFF if a key or button interrupts the demo, else 0. */
uint8_t pop_demokeys(pop_game *g);

/* --- the clock ---------------------------------------------------------------------------- */

void pop_keeptime(pop_game *g);          /* KEEPTIME: count the frame; time for a message? */
void pop_getminleft(pop_game *g);        /* GETMINLEFT: FrameCount to MinLeft, SecLeft (BCD) */
void pop_shortentime(pop_game *g);       /* SHORTENTIME: 15 minutes left, for a skipped level */
void pop_showtime(pop_game *g);          /* SHOWTIME: put up the time message if asked */

/* Frame count at which time message i (0-17) falls due. */
uint16_t pop_timetable(unsigned i);

/* --- messages, flashes, songs ------------------------------------------------------------------ */

/* dispmsg: count the message timer down; returns what to draw (POP_SHOW_*)
 * and passes it to the show_message hook. */
uint8_t pop_dispmsg(pop_game *g);

void pop_flashon(pop_game *g);           /* before the frame is drawn */
void pop_flashoff(pop_game *g);          /* after it is shown */

/* songcues: play the queued song if nothing on screen is moving. */
void pop_songcues(pop_game *g);

/* --- drawing's share of the game state ----------------------------------------------------------- */

/* FrameAdv: a whole new screen if PrepCut asked for one, else the changes.
 * The drawing itself is the draw_screen / draw_changes hooks; this does
 * what the drawing code does to the game state. */
void pop_frameadv(pop_game *g);
void pop_cleancut(pop_game *g);          /* DoCleanCut: always the whole screen */
void pop_zerored(pop_game *g);           /* ZERORED: clear the redraw buffers */

/* --- the loop -------------------------------------------------------------------------------------- */

void pop_initgame(pop_game *g);          /* initgame: before a game or the demo */

/* StartGame: level 0 for the demo, 1 for a new game. */
void pop_startgame(pop_game *g, uint8_t level);

/* One pass of MainLoop: one frame.  Returns g->leave: 0 while the level
 * goes on, else why it stopped (POP_LEAVE_*).  After POP_LEAVE_RESTART and
 * POP_LEAVE_NEXT (level already set) the caller loads the level into
 * g->blueprint and calls pop_restart_level. */
uint8_t pop_mainloop(pop_game *g);

/* LoadNextLevel: called by pop_mainloop when NextLevel differs from level. */
void pop_loadnextlevel(pop_game *g);

/* LoadNext1: the part without the time message. */
void pop_loadnext1(pop_game *g);

/* The princess scene played before `level` begins (1-8), or 0. */
uint8_t pop_cut_before_level(uint8_t level);

#endif /* POP_LOOP_H */
