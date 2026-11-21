/* pop_keys.h - the keyboard's special keys and the controller.
 *
 * New code for the pop-study reconstruction; no original code or data.
 *
 * Ported from the original, routine by routine (paths under
 * "01 POP Source/Source" of jmechner/Prince-of-Persia-Apple-II @ 25243550):
 *
 *   SPECIALK.S  KEYS, freeze 138-250; LegitKeys 265-352; DevelKeys 360-372;
 *               addkey 567-576; checkcodes 585-637; checkcode 647-671 and the
 *               code words 680-704; STROBE 1342-1345
 *   GRAFIX.S    CONTROLLER 1071-1074, JREAD 1086-1114, BREAD 1124-1136,
 *               cvtpdl 1176-1187, SETCENTER 1232-1269
 *   MISC.S      DOSAVEGAME 743-765
 *   TOPCTRL.S   STARTRESUME 110-114, ResumeGame 168-218
 *
 * as the repository's files are configured: SPECIALK.S has FinalDisk = 0,
 * DebugKeys = 0, EditorDisk = 0, so the code words exist and the "temporary
 * development keys" (TempDevel, 379-545) are not assembled and not ported.
 *
 * The hardware is behind hooks (pop_game_hooks): the keyboard latch and its
 * strobe, the two paddles, the two buttons, the disk.  docs/port/keys.md.
 */
#ifndef POP_KEYS_H
#define POP_KEYS_H

#include "pop_game.h"

/* Keys as the Apple II keyboard delivers them: ASCII with the high bit set;
 * a control key is its letter less $60 (SPECIALK.S 73-108). */
enum {
    POP_KEY_ESC = 0x9B,              /* freeze; again: one frame */
    POP_KEY_RESTART = 0x92,          /* ctrl-R: to the title sequence */
    POP_KEY_ABORT = 0x81,            /* ctrl-A: this level again */
    POP_KEY_SOUND = 0x93,            /* ctrl-S */
    POP_KEY_MUSIC = 0x8E,            /* ctrl-N */
    POP_KEY_SETKBD = 0x8B,           /* ctrl-K (and the IIe's up arrow): keyboard */
    POP_KEY_SETJSTK = 0x8A,          /* ctrl-J (and down arrow): joystick, centred where it is */
    POP_KEY_SAVEGAME = 0x87,         /* ctrl-G */
    POP_KEY_VERSION = 0x96,          /* ctrl-V */
    POP_KEY_SHOWTIME = 0xA0,         /* space */
    POP_KEY_FLIPX = 0x98,            /* ctrl-X */
    POP_KEY_FLIPY = 0x99,            /* ctrl-Y */
    POP_KEY_CLEAN = 0x8D             /* ctrl-M (Return): development mode off */
};

enum { POP_KEYBUFLEN = 10, POP_FIRST_SIDE_B = 3, POP_CWIDTHX = 10, POP_CWIDTHY = 15 };

/* KEYS: read the keyboard, steer by it (KREAD), and act on special keys
 * and code words.  May set pop_game.leave (ctrl-R, ctrl-A, or frozen with
 * no key to be had). */
void pop_keys(pop_game *g);

void pop_legitkeys(pop_game *g);         /* LegitKeys: acts on pop_game.keypress */
void pop_develkeys(pop_game *g);         /* DevelKeys: the code words, if develment */
void pop_checkcodes(pop_game *g);        /* checkcodes: BOOST, R, ZAP, Z, TINA */
void pop_addkey(pop_game *g, uint8_t key);               /* addkey */

/* checkcode: do the last keys typed spell `word` (capitals; small letters
 * typed count too)?  0 if so, else 0xFF. */
uint8_t pop_checkcode(pop_game *g, const char *word);

void pop_jread(pop_game *g);             /* JREAD: joyX, joyY as -1, 0, 1 */
void pop_bread(pop_game *g);             /* BREAD */
void pop_controller(pop_game *g);        /* CONTROLLER: both */
void pop_setcenter(pop_game *g);         /* SETCENTER: the joystick, centred where it stands */

/* STROBE: KEYS, then CONTROLLER.  What the main loop's strobe hook does in
 * a shell that has a keyboard. */
void pop_strobe(pop_game *g);

void pop_dosavegame(pop_game *g);        /* DOSAVEGAME: savedgame[POP_SAV_LEVEL] set by the caller */

/* STARTRESUME: start a game from what the load_game hook puts into
 * pop_game.savedgame, or on level 1 if no game was saved.  Leaves with
 * POP_LEAVE_RESTART: the caller loads the level, as after pop_startgame. */
void pop_startresume(pop_game *g);

#endif /* POP_KEYS_H */
