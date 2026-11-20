/* pop_loop.c - see pop_loop.h.  New code; ports of original routines.
 *
 * The code follows the original branch by branch, with the original labels
 * in comments; docs/port/game-loop.md explains it.
 */

#include "pop_loop.h"

#include "pop_auto.h"
#include "pop_collide.h"
#include "pop_control.h"
#include "pop_mover.h"
#include "pop_top.h"
#include "pop_world.h"

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))
#define HOOK(name) do { if (g->hooks.name) g->hooks.name(g->hooks.ctx); } while (0)
#define HOOK1(name, arg) do { if (g->hooks.name) g->hooks.name(g->hooks.ctx, (arg)); } while (0)

/* --- input ------------------------------------------------------------------------------ */

/* The keys, SPECIALK.S 73-98, as the Apple II keyboard delivers them: ASCII
 * with the high bit set.  The player keys count in either case. */
enum {
    KEY_ESC = 0x9B, KEY_SHIFT = 0x20,
    KEY_LEFT = 0xEA /* j */, KEY_DOWN = 0xEB /* k */, KEY_RIGHT = 0xEC /* l */,
    KEY_UPLEFT = 0xF5 /* u */, KEY_UP = 0xE9 /* i */, KEY_UPRIGHT = 0xEF /* o */,
    KEY_SOUND = 0x93 /* ctrl-S */
};

void pop_kread(pop_game *g)
{
    uint8_t a = g->keypress;
    g->kbdX = g->kbdY = 0;
    if (!NEG(a)) {                             /* no new press: */
        if (!NEG(g->keydown)) return;          /* nothing held either */
        a |= 0x80;                             /* an old press, key still down */
    }
    if (a == KEY_LEFT || a == KEY_LEFT - KEY_SHIFT) {
        g->kbdX = 0xFF;
    } else if (a == KEY_RIGHT || a == KEY_RIGHT - KEY_SHIFT) {
        g->kbdX = 1;
    } else if (a == KEY_UP || a == KEY_UP - KEY_SHIFT) {
        g->kbdY = 0xFF;
    } else if (a == KEY_DOWN || a == KEY_DOWN - KEY_SHIFT) {
        g->kbdY = 1;
    } else if (a == KEY_UPLEFT || a == KEY_UPLEFT - KEY_SHIFT) {
        g->kbdX = 0xFF;
        g->kbdY = 0xFF;
    } else if (a == KEY_UPRIGHT || a == KEY_UPRIGHT - KEY_SHIFT) {
        g->kbdX = 1;
        g->kbdY = 0xFF;
    }
}

/* With the opponent played by hand (ManCtrl negative) button 0 belongs to
 * the keyboard and button 1 to the joystick; otherwise either button
 * serves either device. */
static void getjoy(pop_game *g)
{
    uint8_t a = g->BTN1;
    g->JSTKX = g->joyX;
    g->JSTKY = g->joyY;
    if (!NEG(g->ManCtrl)) a |= g->BTN0;
    g->btn = a;
}

static void getkbd(pop_game *g)
{
    uint8_t a = g->BTN0;
    g->JSTKX = g->kbdX;
    g->JSTKY = g->kbdY;
    if (!NEG(g->ManCtrl)) a |= g->BTN1;
    g->btn = a;
}

void pop_getselect(pop_game *g)
{
    if (g->joyon != 0) getjoy(g); else getkbd(g);
}

void pop_getdesel(pop_game *g)
{
    if (g->joyon != 0) getkbd(g); else getjoy(g);
}

/* One flag of CLRJSTK: `held` is whether its direction or button is down. */
static uint8_t clr1(uint8_t flag, int held)
{
    if (NEG(flag)) return flag;                /* an unused press stays */
    if (!held) return 0;
    return flag == 0 ? 0xFF : flag;            /* newly held: a press; used: stays used */
}

void pop_clrjstk(pop_game *g)
{
    g->clrF = clr1(g->clrF, NEG(g->JSTKX));
    g->clrB = clr1(g->clrB, g->JSTKX == 1);
    g->clrU = clr1(g->clrU, NEG(g->JSTKY));
    g->clrD = clr1(g->clrD, g->JSTKY == 1);
    g->clrbtn = clr1(g->clrbtn, NEG(g->btn));
}

static void flags_out(const pop_game *g, uint8_t *to)
{
    to[0] = g->clrF; to[1] = g->clrB; to[2] = g->clrU; to[3] = g->clrD; to[4] = g->clrbtn;
}

static void flags_in(pop_game *g, const uint8_t *from)
{
    g->clrF = from[0]; g->clrB = from[1]; g->clrU = from[2]; g->clrD = from[3];
    g->clrbtn = from[4];
}

void pop_saveselect(pop_game *g) { flags_out(g, g->clrSEL); }
void pop_loadselect(pop_game *g) { flags_in(g, g->clrSEL); }
void pop_savedesel(pop_game *g) { flags_out(g, g->clrDESEL); }
void pop_loaddesel(pop_game *g) { flags_in(g, g->clrDESEL); }

void pop_initinput(pop_game *g)
{
    unsigned i;
    for (i = 0; i < 5; i++) g->clrSEL[i] = g->clrDESEL[i] = 0;
}

void pop_clearjoy(pop_game *g)
{
    pop_loadselect(g);
    g->clrF = g->clrB = g->clrU = g->clrD = 0; /* (not the button's) */
    pop_saveselect(g);
}

uint8_t pop_demokeys(pop_game *g)
{
    if (g->level != 0) return 0;
    if (NEG(g->BTN0 | g->BTN1)) return 0xFF;   /* (the original reads the buttons themselves) */
    if (!NEG(g->keypress)) return 0;
    if (g->keypress == KEY_ESC || g->keypress == KEY_SOUND) return 0;
    return 0xFF;
}

/* --- the clock ---------------------------------------------------------------------------- */

uint16_t pop_timetable(unsigned i)
{
    /* minutes left when messages 0-14 fall due, SPECIALK.S 1234-1248 */
    static const uint8_t left[15] = { 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5, 4, 3, 2 };
    if (i < 15) return (uint16_t)((POP_TIME_LIMIT - left[i]) * POP_MIN_FRAMES);
    if (i == 15) return (uint16_t)((POP_TIME_LIMIT - 1) * POP_MIN_FRAMES + 1);
    if (i == 16) return (uint16_t)(POP_TIME_LIMIT * POP_MIN_FRAMES + 5);   /* game over */
    return 65535;
}

static uint16_t framecount(const pop_game *g)
{
    return (uint16_t)(g->FrameCount[0] | (g->FrameCount[1] << 8));
}

static void set_framecount(pop_game *g, uint16_t v)
{
    g->FrameCount[0] = u8(v);
    g->FrameCount[1] = u8(v >> 8);
}

/* GETMINLEFT's :sub.  Counts `step` frames at a time from `count` until
 * the frame counter is reached, taking one off a two-digit decimal (BCD)
 * number that starts at 61. */
static uint8_t countdown(const pop_game *g, uint16_t count, uint16_t step)
{
    uint8_t y = 0x61;
    while (count < framecount(g)) {
        count = (uint16_t)(count + step);
        y = (y & 0x0F) ? u8(y - 1) : u8(y - 7);              /* BCD: $60 -> $59, $00 -> $99 */
        if (NEG(y)) return 0;                  /* below zero */
    }
    return y;
}

void pop_getminleft(pop_game *g)
{
    g->MinLeft = countdown(g, 0, POP_MIN_FRAMES);
    if (g->MinLeft >= 2) return;
    /* the last minute: seconds */
    g->SecLeft = countdown(g, 59 * POP_MIN_FRAMES, POP_SEC_FRAMES);
}

void pop_keeptime(pop_game *g)
{
    uint8_t y;
    uint16_t due;
    if (g->level == 0) return;                 /* not in the demo */
    if (!NEG(g->kid.life)) return;             /* the clock stops while the kid is dead */
    set_framecount(g, (uint16_t)(framecount(g) + 1));
    if (framecount(g) == 0) set_framecount(g, 0xFFFF);       /* it does not wrap */

    y = g->NextTimeMsg;                        /* 0, 2, 4 ...: twice the message number */
    if (y >= 2 * POP_NUM_TIME_MSG) return;     /* no more messages */
    if ((y & 1) != 0) { g->unmodelled = 1; return; }
    due = pop_timetable(y / 2u);
    /* The original compares the high bytes, then the low bytes on their
     * own: "not yet" also when the high byte is past and the low one is not. */
    if (g->FrameCount[1] < (due >> 8)) return;
    if (g->FrameCount[0] < (due & 0xFF)) return;
    if (g->msgtimer != 0) return;              /* wait until other messages are gone */
    g->NextTimeMsg = u8(g->NextTimeMsg + 2);
    g->timerequest = 2;
}

void pop_shortentime(pop_game *g)
{
    if (g->NextTimeMsg >= 20) return;          /* already 10 minutes or less */
    g->NextTimeMsg = 18;
    set_framecount(g, pop_timetable(9));       /* 15 minutes left */
}

void pop_showtime(pop_game *g)
{
    if (g->timerequest == 0) return;
    if (!NEG(g->kid.life)) return;
    pop_getminleft(g);
    if (g->MinLeft < 2) {
        if (g->SecLeft != 0) {
            if (g->level < 14) {               /* :showsec the countdown of the last minute */
                if (g->SecLeft < 2) {          /* :nomsg */
                    g->timerequest = 0;
                    g->msgtimer = 0;
                    return;
                }
                if (g->message != POP_MSG_TIME) {
                    if (g->msgtimer != 0) return;
                    g->message = POP_MSG_TIME;
                }
                g->timerequest = 1;            /* again next frame */
                g->msgtimer = 1;
                return;
            }
        } else if (g->timerequest < 3) {       /* :timeup only when a key asks for it */
            return;
        }
    }
    /* :normal */
    if (g->msgtimer != 0) return;              /* wait until other messages are gone */
    g->message = POP_MSG_TIME;
    g->msgtimer = (g->timerequest >= 4) ? POP_TIMEMSGTIMER + 5 : POP_TIMEMSGTIMER;
    g->timerequest = 0;
}

/* --- messages, flashes, songs ------------------------------------------------------------------ */

uint8_t pop_dispmsg(pop_game *g)
{
    uint8_t show = POP_SHOW_NOTHING, a;
    if (g->msgtimer == 0) return show;
    g->msgtimer--;
    a = g->msgtimer;
    if (!NEG(g->kid.life)) {                   /* dead: "Press button to continue" */
        if (a < POP_CONTOFF) return show;      /* the last frames: off */
        if (a < POP_CONTFLASH) {               /* then blinking: on 3 frames in 8 */
            if ((a & 7) >= 3) return show;
            if ((a & 7) == 2) {                /* with a beep as it comes on */
                if (g->soundon == 0) HOOK(tone);
                pop_g_addsound(g, POP_SND_FLASHMSG);
            }
        }
        show = POP_SHOW_CONTINUE;              /* :steady */
    } else {                                   /* alive: the level or the time */
        if (a >= POP_LEVELTIMER - 2) return show;            /* a short pause first */
        if (g->message == POP_MSG_LEVEL) show = POP_SHOW_LEVEL;
        else if (g->message == POP_MSG_TIME) show = POP_SHOW_TIME;
        else return show;
    }
    HOOK1(show_message, show);
    return show;
}

void pop_flashon(pop_game *g)
{
    uint8_t colour;
    if (g->lightning != 0 && g->lightcolor != 0) {
        colour = g->lightcolor;
    } else {
        if (!NEG(g->ChgKidStr)) return;
        colour = 0x11;                         /* red: the kid has been hurt */
    }
    HOOK1(flash_on, colour);
}

void pop_flashoff(pop_game *g)
{
    if (g->lightning != 0) {
        g->lightning--;
        if (!NEG(g->lightning)) { HOOK(flash_off); return; }
    }
    if (!NEG(g->ChgKidStr)) return;
    HOOK(flash_off);
}

void pop_songcues(pop_game *g)
{
    if (g->SongCue == 0) return;
    if (g->level == 0) return;                 /* no music in the demo */
    if (g->SongCount == 0) {                   /* its time has run out: forget it */
        g->SongCue = 0;
        return;
    }
    g->SongCount--;
    /* gone up the stairs: only once the level is about to change */
    if (g->kid.posn == 0 && g->NextLevel == g->level) return;
    /* wait until nothing moves: the kid, a visible opponent, ... */
    if (pop_static(g->kid.posn) != 0) return;
    if (g->shad.face != POP_NO_CHAR && g->shad.scrn == g->VisScrn
        && pop_static(g->shad.posn) != 0) return;
    if (g->trobcount != 0) return;             /* ... slicers and the like, */
    if (g->nummob != 0) return;                /* falling floors, */
    if (g->lightning != 0) return;             /* flashes, */
    if (!NEG(g->mergetimer)) {
        if (g->mergetimer != 0) return;        /* the shadow merging, */
        if ((g->ChgKidStr | g->ChgOppStr) != 0) return;      /* and blows landing */
    }
    HOOK1(play_song, g->SongCue);              /* (the game stands still meanwhile) */
    g->SongCue = 0;
    pop_clearjoy(g);
}

/* --- drawing's share of the game state ----------------------------------------------------------- */

void pop_zerored(pop_game *g)
{
    static const uint16_t bufs[7] = { POP_RB_RED, POP_RB_FRED, POP_RB_FLOOR, POP_RB_HALF,
                                      POP_RB_WIPE, POP_RB_MOVE, POP_RB_OBJ };
    unsigned i, y;
    for (i = 0; i < 7; i++) {
        for (y = 0; y < 30; y++) g->rb[bufs[i] + y] = 0;
    }
    for (y = 0; y < 10; y++) g->rb[POP_RB_TOP + y] = 0;      /* (whitebuf is not cleared) */
}

static void dofast(pop_game *g)                /* DoFast, TOPCTRL.S 961-982 */
{
    g->SCRNUM = g->VisScrn;
    if (g->redrawflg != 0) {                   /* develpatch: a forced full redraw */
        g->redrawflg--;
        pop_markmeters(g);
        HOOK(draw_screen);
    }
    pop_reflection(g);                         /* addchars: the mirror, and the shadow's birth */
    if (g->ChgKidStr != 0) pop_markkidmeter(g);              /* checkmeters */
    if (g->ChgOppStr != 0) pop_markoppmeter(g);
    HOOK(draw_changes);                        /* moving pieces, characters, changed blocks */
    (void)pop_dispmsg(g);
}

void pop_cleancut(pop_game *g)                 /* DoCleanCut, drawbg, DoSure */
{
    g->cutplan = 0;
    g->CUTTIMER = 2;                           /* no other cut for 2 frames */
    g->SCRNUM = g->VisScrn;
    HOOK(draw_screen);
    HOOK(zero_peels);
    pop_zerored(g);
    pop_markmeters(g);
    dofast(g);
    HOOK(show_frame);
}

void pop_frameadv(pop_game *g)
{
    if (g->cutplan != 0) {
        pop_cleancut(g);
        return;
    }
    dofast(g);
    HOOK(show_frame);
}

/* --- the loop -------------------------------------------------------------------------------------- */

void pop_initgame(pop_game *g)
{
    g->redrawflg = 0;
    g->ManCtrl = 0;
    g->vibes = 0;
    g->invert = 0;
    g->milestone = 0;
    g->timerequest = 0;
    g->FrameCount[0] = g->FrameCount[1] = 0;
    g->NextTimeMsg = 0;
    g->MinLeft = g->SecLeft = 0xFF;
}

void pop_startgame(pop_game *g, uint8_t level)
{
    g->level = g->NextLevel = level;
    if (level == 1) pop_cuesong(g, POP_SONG_DANGER, 25);
    g->origstrength = POP_INITMAXSTR;
    pop_initgame(g);
}

uint8_t pop_cut_before_level(uint8_t level)
{
    switch (level) {                           /* TOPCTRL.S 455-490 */
    case 2:  return 1;
    case 4:  return 2;
    case 6:  return 3;
    case 8:  return 8;
    case 9:  return 4;
    case 12: return 5;
    default: return 0;
    }
}

void pop_loadnext1(pop_game *g)
{
    uint8_t cut;
    g->origstrength = g->MaxKidStr;            /* strength gained is kept */
    g->milestone = 0;
    if (g->NextLevel < 1 || g->NextLevel > POP_LAST_LEVEL) { /* :illegal: this level again */
        g->NextLevel = g->level;
    } else {
        g->level = g->NextLevel;
        cut = pop_cut_before_level(g->level);
        if (cut != 0) HOOK1(princess_cut, cut);
    }
    g->leave = POP_LEAVE_RESTART;
    HOOK(restart);
}

void pop_loadnextlevel(pop_game *g)
{
    if (g->NextLevel != 14) g->timerequest = 1;              /* show the time left */
    pop_loadnext1(g);
}

uint8_t pop_mainloop(pop_game *g)
{
    g->leave = POP_LEAVE_NONE;
    (void)pop_rnd(g);
    g->ChgKidStr = g->ChgOppStr = 0;
    HOOK(strobe);                              /* keys, joystick, buttons */
    if (g->leave != 0) return g->leave;        /* (a key may restart or quit) */
    if (NEG(pop_demokeys(g))) {                /* a key during the demo starts a game */
        pop_startgame(g, 1);
        g->leave = POP_LEAVE_RESTART;
        HOOK(restart);
        return g->leave;
    }
    pop_misctimers(g);
    pop_nextframe(g);
    if (g->leave != 0) return g->leave;
    pop_flashon(g);
    pop_frameadv(g);
    HOOK(playback);                            /* this frame's sounds */
    pop_zerosound(&g->sq);
    pop_flashoff(g);
    pop_songcues(g);
    if (g->NextLevel == g->level) return 0;
    if (g->NextLevel == 2) HOOK(copy_check);   /* yellowcheck, TOPCTRL.S 1707-1713 */
    pop_loadnextlevel(g);
    return g->leave;
}
