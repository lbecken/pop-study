/* pop_control.h - player control, floor checks, falling and landing.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Ported routines (01 POP Source/Source, original commit 25243550); each
 * keeps its original name and is compared with the original code, run in a
 * 6502 simulator, on randomised states (tests/fuzz_control.py).
 *
 *   CTRL.S      falling, CHECKFLOOR, hitflr, fallon, onground, startfall,
 *               CDpatch, InsideBlock                              70-502
 *               PLAYERCTRL 554-582
 *               UserCtrl, clrall, GENCTRL, GuardCtrl, FightCtrl,
 *               DoTurnEng, DoBlock, DoStrike, DoRetreat, DoAdvance  610-1024
 *               standing, Stairs, crouching, starting, stjumpup,
 *               turning, arunning, hanging                          1031-1529
 *               DoStartrun ... DoJumphigh                           1537-1924
 *               CHECKIMPALE, DOIMPALE, TryPickup, PickItUp          2016-2162
 *   CTRLSUBS.S  INDEXBLOCK 615-631, SETUPCHAR 794-841, decodeim 1017-1039,
 *               GETEDGES 1071-1157, zerocrop 1485-1493,
 *               MARKRED/MARKWIPE 1728-1757, CHECKLEDGE 1814-1858,
 *               GETOPDIST 2052-2102
 *   COLL.S      GETFWDDIST 822-914, DBarr 925-983, gatebarr? 1374-1381
 *   SUBS.S      REMOVEOBJ 1415-1432
 *   MISC.S      DECSTR 691-735
 *   MOVER.S     GETSPIKES 365-382
 *   SPECIALK.S  FACEJSTK 788-797
 *
 * PLAYERCTRL (CTRL.S 554-582) is ported up to its calls into the input
 * code, which are hooks.  Not ported: the input devices themselves, DemoCtrl,
 * SHADCTRL (opponent AI), CHECKPRESS (pressure plates and loose floors).
 *
 * See docs/port/player-control.md.
 */
#ifndef POP_CONTROL_H
#define POP_CONTROL_H

#include "pop_game.h"

/* Sequence numbers used by the control code, SEQDATA.S 11-112. */
enum {
    POP_SQ_STARTRUN = 1, POP_SQ_STAND = 2, POP_SQ_STANDJUMP = 3, POP_SQ_RUNJUMP = 4,
    POP_SQ_TURN = 5, POP_SQ_RUNTURN = 6, POP_SQ_STEPFALL = 7, POP_SQ_JUMPHANGMED = 8,
    POP_SQ_CLIMBUP = 10, POP_SQ_HANGDROP = 11, POP_SQ_RUNSTOP = 13, POP_SQ_JUMPUP = 14,
    POP_SQ_FALLHANG = 15, POP_SQ_JUMPBACKHANG = 16, POP_SQ_SOFTLAND = 17,
    POP_SQ_JUMPFALL = 18, POP_SQ_STEPFALL2 = 19, POP_SQ_MEDLAND = 20,
    POP_SQ_RJUMPFALL = 21, POP_SQ_HARDLAND = 22, POP_SQ_HANGFALL = 23,
    POP_SQ_JUMPHANGLONG = 24, POP_SQ_HANGSTRAIGHT = 25, POP_SQ_RDIVEROLL = 26,
    POP_SQ_HIGHJUMP = 28, POP_SQ_STEPFWD1 = 29, POP_SQ_TURNRUN = 43,
    POP_SQ_TESTFOOT = 44, POP_SQ_STANDUP = 49, POP_SQ_STOOP = 50, POP_SQ_IMPALE = 51,
    POP_SQ_ENGARDE = 55, POP_SQ_ADVANCE = 56, POP_SQ_RETREAT = 57, POP_SQ_STRIKE = 58,
    POP_SQ_TURNENGARDE = 60, POP_SQ_STRIKEBLOCK = 61, POP_SQ_READYBLOCK = 62,
    POP_SQ_LANDENGARDE = 63, POP_SQ_BLOCKTOSTRIKE = 66, POP_SQ_CLIMBDOWN = 68,
    POP_SQ_CLIMBSTAIRS = 70, POP_SQ_DROPDEAD = 71, POP_SQ_CLIMBFAIL = 73,
    POP_SQ_FASTSTRIKE = 75, POP_SQ_DRINKPOTION = 78, POP_SQ_CRAWL = 79,
    POP_SQ_ALERTTURN = 80, POP_SQ_FIGHTFALL = 81, POP_SQ_EFIGHTFALL = 82,
    POP_SQ_EFIGHTFALLFWD = 83, POP_SQ_FASTADVANCE = 86, POP_SQ_GOALERTSTAND = 87,
    POP_SQ_TURNDRAW = 89, POP_SQ_GUARDENGARDE = 90, POP_SQ_PICKUPSWORD = 91,
    POP_SQ_RESHEATHE = 92, POP_SQ_FASTSHEATHE = 93, POP_SQ_PATCHFALL = 104
};

/* Sound numbers queued here, SOUNDNAMES.S 11 and 20. */
enum { POP_SND_SPLAT = 5, POP_SND_IMPALED = 14 };

/* --- helpers ---------------------------------------------------------------------- */

void    pop_g_jumpseq(pop_game *g, uint8_t seq_no);
void    pop_g_animchar(pop_game *g);
void    pop_g_addsound(pop_game *g, uint8_t sound);

uint8_t pop_clrall(pop_game *g);                   /* clrF/B/U/D = 0; returns 1 */
void    pop_facejstk(pop_game *g);
uint8_t pop_decstr(pop_game *g, uint8_t amount);   /* 0 if the character dies */
uint8_t pop_getspikes(pop_game *g);                /* 0 safe, 1 sprung, 2 springing */
uint8_t pop_getopdist(pop_game *g);                /* signed distance to the opponent */
uint8_t pop_checkledge(pop_game *g, uint8_t ledge_id);   /* 1 if it can be grabbed */
int     pop_indexblock(pop_game *g);               /* sets ry; returns the carry flag */
void    pop_markwipe(pop_game *g, uint8_t a, int *carry);
void    pop_markred(pop_game *g, uint8_t a, int *carry);
void    pop_removeobj(pop_game *g, uint8_t lastpotion);

void    pop_setupchar(pop_game *g);
void    pop_getedges(pop_game *g);
/* GETFWDDIST: returns the careful-step distance; rx = 0 edge, 1 barrier,
 * 2 clear; ry = id of the piece stepped up to. */
uint8_t pop_getfwddist(pop_game *g);

/* --- control (8b) --------------------------------------------------------------------- */

void pop_playerctrl(pop_game *g);     /* PLAYERCTRL: the kid's control step */
void pop_userctrl(pop_game *g);
void pop_genctrl(pop_game *g);
void pop_guardctrl(pop_game *g);
void pop_fightctrl(pop_game *g);
void pop_standing(pop_game *g);
void pop_crouching(pop_game *g);
void pop_starting(pop_game *g);
void pop_stjumpup(pop_game *g);
void pop_turning(pop_game *g);
void pop_arunning(pop_game *g);
void pop_hanging(pop_game *g);

void pop_doblock(pop_game *g);
void pop_dostrike(pop_game *g, uint8_t posn);
void pop_doretreat(pop_game *g);
void pop_doadvance(pop_game *g);
void pop_dostartrun(pop_game *g);
void pop_doturn(pop_game *g);
void pop_dostandjump(pop_game *g);
void pop_docrouch(pop_game *g);
void pop_doengarde(pop_game *g);
void pop_dojumpup(pop_game *g);
void pop_dojumpedge(pop_game *g);
void pop_dojumphang(pop_game *g);
void pop_dorunjump(pop_game *g);
void pop_dostepfwd(pop_game *g);
void pop_dojumphigh(pop_game *g);
uint8_t pop_trypickup(pop_game *g);
void pop_pickitup(pop_game *g, uint8_t objid);

/* --- floor, falling, landing (8c) -------------------------------------------------------- */

void    pop_checkfloor(pop_game *g);
void    pop_falling(pop_game *g);
void    pop_hitflr(pop_game *g);
void    pop_fallon(pop_game *g);
void    pop_onground(pop_game *g);
void    pop_startfall(pop_game *g);
void    pop_cdpatch(pop_game *g);
uint8_t pop_insideblock(pop_game *g);
void    pop_checkimpale(pop_game *g);
void    pop_doimpale(pop_game *g);

#endif /* POP_CONTROL_H */
