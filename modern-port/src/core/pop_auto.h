/* pop_auto.h - opponents: how guards and the shadow decide what to do, sword
 * hits, strength, guards entering and leaving the screen, and the kid's
 * passage from screen to screen.
 *
 * New code for the pop-study reconstruction; no original code or data.
 * The fight tables and scripted positions are original data and are NOT in
 * this repository: they are generated from the user's checkout (pop_autodata).
 *
 * Ported routines (01 POP Source/Source, original commit 25243550); each
 * keeps its original name and is compared with the original code, run in a
 * 6502 simulator, on randomised states (tests/fuzz_control.py).
 *
 *   AUTO.S      AUTOCTRL, MouseProg, ShadowProg and the level programs,
 *               SkelProg, VizierProg, KidProg, GuardProg          160-460
 *               Alert, EnGarde, FollowKid, InRange, GenFight,
 *               MaybeAdvance, MaybeBlock, MaybeStrike             467-819
 *               DoRelease ... DoRelBtn, rndp                       822-899
 *               CHECKSTRIKE, TestStrike, CHECKSTAB                 914-1040
 *               chgshadposn, csps                                  1047-1078
 *               AUTOPLAYBACK                                       1149-1218
 *               CUTCHECK, transferguard, updateguard, CUTGUARD,
 *               cutchar, milestone3, stealsword, jaffmusic,
 *               mirrmusic, CUT                                     1230-1738
 *               ADDGUARD, AddNormalGd, getgdstrength               1750-1949
 *   MISC.S      VANISHCHAR 72-81, FIRSTGUARD 191-218,
 *               MARKMETERS, MARKKIDMETER, MARKOPPMETER 225-246,
 *               POTIONEFFECT 257-345, MOUSERESCUE 352-373,
 *               STABCHAR 380-443, UNHOLY 450-472,
 *               REFLECTION, getreflect, CreateShad 486-582,
 *               BONESRISE 595-679, CHECKALERT 827-978
 *   CTRL.S      SHADCTRL 509-547
 *   CTRLSUBS.S  RECHARGEMETER 2020-2025, BOOSTMETER 2032-2041
 *   SUBS.S      INITIALGUARDS 1674-1690, DEADENEMY 1699-1738
 *   TOPCTRL.S   DoShad 701-740, deathsong 1200-1212, kill0 1220-1234,
 *               chgmeters 1313-1354
 *
 * See docs/port/opponents.md.
 */
#ifndef POP_AUTO_H
#define POP_AUTO_H

#include "pop_game.h"

/* Sequence numbers started here, SEQDATA.S. */
enum {
    POP_SQ_BLOCKEDSTRIKE = 69, POP_SQ_STABBED = 74, POP_SQ_ALERTSTAND = 77,
    POP_SQ_STABKILL = 85, POP_SQ_ARISE = 88, POP_SQ_MSCURRY = 105, POP_SQ_MLEAVE = 107
};

/* Song numbers, as passed to CUESONG. */
enum {
    POP_SONG_ACCID = 1, POP_SONG_HEROIC = 2, POP_SONG_DANGER = 3, POP_SONG_SWORD = 4,
    POP_SONG_REJOIN = 5, POP_SONG_VICT = 7, POP_SONG_JAFFAR = 10, POP_SONG_POTION = 11,
    POP_SONG_SHORTPOT = 12
};

enum { POP_SND_MIRRORCRACK = 6 };

/* Thresholds, AUTO.S 76-95. */
enum {
    POP_STRIKERANGE1 = 12, POP_STRIKERANGE2 = 29, POP_BLOCKRANGE2 = 29, POP_SWORDTHRES = 90,
    POP_BLOCKTHRES1 = 10, POP_TOOCLOSE = 12, POP_TOOFAR = 35, POP_OFFGUARDTHRES = 8,
    POP_JUMPTHRES = 50, POP_RUNTHRES = 40, POP_BLOCKTIME = 4, POP_SHADSTRENGTH = 4,
    POP_NUMPROGS = 12, POP_MAXMAXSTR = 10
};

/* Places wired into the code, AUTO.S 59-69 and MISC.S 589-593. */
enum {
    POP_FLASKSCRN = 24, POP_FLASKX = 3, POP_FLASKY = 0,
    POP_SWORDSCRN = 15, POP_SWORDX = 1, POP_SWORDY = 0,
    POP_SKELSCRN = 1, POP_SKELX = 5, POP_SKELY = 1, POP_SKELTRIG = 2, POP_SKELPROG = 2
};

/* "No character": the value of a face field that marks an empty slot. */
enum { POP_NO_CHAR = 86 };

/* Commands of a scripted movement, AUTO.S 1094-1103. */
enum {
    POP_CMD_ENDDEMO = 0xFF, POP_CMD_ENDPROG = 0xFE, POP_CMD_CTR = 0, POP_CMD_FWD = 1,
    POP_CMD_BACK = 2, POP_CMD_UP = 3, POP_CMD_DOWN = 4, POP_CMD_UPFWD = 5,
    POP_CMD_PRESS = 6, POP_CMD_RELEASE = 7
};

/* --- control ---------------------------------------------------------------------------- */

/* AUTOCTRL: set the joystick variables for the character in g->ch as its
 * program decides.  In: g->op is the other character. */
void pop_autoctrl(pop_game *g);

/* SHADCTRL: the opponent's control step: death, then AUTOCTRL (or the hand-
 * controlled hook), then the same GenCtrl the kid uses. */
void pop_shadctrl(pop_game *g);

/* DoShad (TOPCTRL.S 701-740): the opponent's whole frame: control,
 * animation, falling, collision with walls, floor, plates, spikes, slicers. */
void pop_doshad(pop_game *g);

/* AUTOPLAYBACK: one frame of a scripted movement: (frame, command) pairs. */
void pop_autoplayback(pop_game *g, const uint8_t *prog, size_t len);

/* --- sword hits and strength -------------------------------------------------------------- */

void pop_checkstrike(pop_game *g);       /* CHECKSTRIKE: blocks and hits, for both */
void pop_checkstab(pop_game *g);         /* CHECKSTAB: act on the hits */
void pop_stabchar(pop_game *g);          /* STABCHAR: g->ch has been run through */
void pop_chgmeters(pop_game *g);         /* chgmeters: apply ChgKidStr / ChgOppStr */
void pop_rechargemeter(pop_game *g);
void pop_boostmeter(pop_game *g);
void pop_markkidmeter(pop_game *g);
void pop_markoppmeter(pop_game *g);
void pop_markmeters(pop_game *g);
void pop_unholy(pop_game *g);            /* UNHOLY: level 12, kid and shadow share a life */
void pop_kill0(pop_game *g);             /* kill0: a character on screen 0 dies */
void pop_deathsong(pop_game *g);
void pop_deadenemy(pop_game *g);

/* --- alertness, special appearances ----------------------------------------------------------- */

void pop_checkalert(pop_game *g);        /* CHECKALERT: can kid and opponent see / reach each other */
void pop_firstguard(pop_game *g);        /* FIRSTGUARD: no running past a guard en garde */
void pop_vanishchar(pop_game *g);
void pop_bonesrise(pop_game *g);         /* BONESRISE: level 3's skeleton */
void pop_mouserescue(pop_game *g);
void pop_potioneffect(pop_game *g);
/* REFLECTION: the kid before the mirror; on a smashed mirror the reflection
 * becomes the shadow.  The drawing itself is a hook. */
void pop_reflection(pop_game *g);

/* --- screens ------------------------------------------------------------------------------------ */

/* CUTCHECK: has the kid passed off the screen?  If so move him to the next
 * one (cutscrn = his new screen) and decide what becomes of the guard. */
void pop_cutcheck(pop_game *g);
void pop_cutguard(pop_game *g);          /* CUTGUARD: an opponent fallen off the bottom */
void pop_initialguards(pop_game *g);     /* INITIALGUARDS: once per level, before any ADDGUARD */
void pop_addguard(pop_game *g);          /* ADDGUARD: who is waiting on the new screen */
uint8_t pop_cutchar(pop_game *g);        /* cutchar: direction 0-3, or $FF for no cut */
void pop_cut(pop_game *g, uint8_t dir);  /* CUT: 0 left, 1 right, 2 up, 3 down */

#endif /* POP_AUTO_H */
