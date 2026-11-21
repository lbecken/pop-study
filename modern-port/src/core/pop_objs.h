/* pop_objs.h - what is to be drawn besides the level: the object list.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Each frame the game lists the characters, their swords, impact stars
 * and the falling floors that are on the visible screen (pop_game.obj*),
 * and marks in the redraw buffers what they may touch.  The drawing code
 * (pop_fast, src/video) turns the list into images.
 *
 * Ported routines (01 POP Source/Source, original commit 25243550), with
 * the line each starts at; each keeps its original name:
 *
 *   TOPCTRL.S   addchars 747, setupkid 803, setupshad 830
 *   CTRLSUBS.S  ADDFCHARX 374, INDEXCHAR 740, SETUPSWORD 852,
 *               getswordframe 944, QUICKFLOOR 1171, markul 1230,
 *               QUICKFG 1258, CROPCHAR 1313, ADDKIDOBJ ... ADDSWORDOBJ
 *               1592-1619, MARKFLOOR 1764, MARKHALF 1769, UNEVENFLOOR 2109
 *   GAMEBG.S    SETUPCOMIX 965, ADDCHAROBJ 1050, SETOBJINDX 1105
 *   GRAFIX.S    CVTX 903
 *   MOVER.S     ADDMOBS 1984, ATM 2004, addmobobj 2085 (in pop_mover.c)
 *
 * See docs/port/frame-drawing.md.
 */
#ifndef POP_OBJS_H
#define POP_OBJS_H

#include "pop_game.h"

void pop_zeroobjs(pop_game *g);                  /* the object list's share of ZEROLSTS */

/* ADDCHAROBJ: the character set up in the FChar variables, as an object
 * of kind `typ` (POP_OBJ_*).  The 280-dot x becomes a byte and a dot
 * offset (CVTX). */
void pop_addcharobj(pop_game *g, uint8_t typ);
void pop_setobjindx(pop_game *g, uint8_t slot);  /* SETOBJINDX */
void pop_addfcharx(pop_game *g, uint8_t dx);     /* FCharX forward by a signed amount */

void pop_markfloor(pop_game *g, uint8_t a, int *carry);
void pop_markhalf(pop_game *g, uint8_t a, int *carry);

void pop_indexchar(pop_game *g);                 /* which block the character is drawn with */
void pop_quickfloor(pop_game *g);                /* floor pieces to draw again in front of him */
void pop_quickfg(pop_game *g);                   /* front pieces to draw again */
void pop_cropchar(pop_game *g);                  /* where walls and ceilings cut him off */
void pop_unevenfloor(pop_game *g);               /* a pressed plate is one line lower */

void pop_setupsword(pop_game *g);                /* the sword in his hand, if it shows */
void pop_setupcomix(pop_game *g);                /* the star where he was hit */
void pop_setupkid(pop_game *g);
void pop_setupshad(pop_game *g);

/* addchars: reflection, opponent and kid of the visible screen onto the
 * object list, and the meters marked if a strength changed. */
void pop_addchars(pop_game *g);

#endif /* POP_OBJS_H */
