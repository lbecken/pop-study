/* test_keys.c - the special keys, the code words, the controller and saved
 * games, on synthetic data.
 *
 * New code; uses the made-up state of pop_fixture.h and a keyboard made up
 * here.  Expected values were worked out by hand from SPECIALK.S, GRAFIX.S,
 * MISC.S and TOPCTRL.S (see pop_keys.h for the line ranges).  The same
 * routines are compared with the original code on random states
 * (tests/fuzz_control.py @keys) when the original source is available.
 */

#include <string.h>

#include "pop_fixture.h"
#include "pop_keys.h"
#include "pop_loop.h"
#include "pop_test.h"

/* The keyboard: a latch with its strobe in the high bit; the next key
 * typed arrives when the strobe is cleared. */
static struct {
    uint8_t latch, any, padx, pady, b0, b1, saved[16];
    uint8_t queue[16];
    size_t n, at;
    int waits;                                     /* how often the game waited for a key */
} hw;

static uint8_t h_latch(void *c, int waiting) { (void)c; hw.waits += waiting; return hw.latch; }
static uint8_t h_keystrobe(void *c)
{
    uint8_t v = (uint8_t)((hw.any ? 0x80 : 0) | (hw.latch & 0x7F));
    (void)c;
    hw.latch &= 0x7F;
    if (hw.at < hw.n) hw.latch = (uint8_t)(hw.queue[hw.at++] | 0x80);
    return v;
}
static void h_paddles(void *c) { (void)c; g.joyX = hw.padx; g.joyY = hw.pady; }
static void h_buttons(void *c) { (void)c; g.BTN0 = hw.b0; g.BTN1 = hw.b1; }
static void h_save(void *c) { (void)c; hook('G'); }
static void h_load(void *c) { (void)c; memcpy(g.savedgame, hw.saved, sizeof g.savedgame); }
static void h_version(void *c) { (void)c; hook('V'); }

/* A fresh game and keyboard; `keys`: what is typed, the first key already
 * in the latch. */
static void start(const char *keys)
{
    size_t i;
    setup(3, POP_FACE_LEFT);
    memset(&hw, 0, sizeof hw);
    hw.padx = hw.pady = 35;
    for (i = 1; keys[0] && keys[i]; i++) hw.queue[hw.n++] = (uint8_t)keys[i];
    hw.latch = (uint8_t)keys[0];
    g.hooks.key_latch = h_latch;
    g.hooks.key_strobe = h_keystrobe;
    g.hooks.read_paddles = h_paddles;
    g.hooks.read_buttons = h_buttons;
    g.hooks.save_game = h_save;
    g.hooks.load_game = h_load;
    g.hooks.show_version = h_version;
    g.level = g.NextLevel = 1;
    g.soundon = g.musicon = 1;
}

/* Type a word: one pass of KEYS per key. */
static void type(const char *word)
{
    for (; *word; word++) {
        hw.latch = (uint8_t)(*word | 0x80);
        pop_keys(&g);
    }
}

/* What the main loop's strobe does with the keyboard, and what it returns. */
static uint8_t pop_mainloop_keys_only(void)
{
    g.leave = POP_LEAVE_NONE;
    pop_strobe(&g);
    return g.leave;
}

static unsigned fc(void) { return (unsigned)(g.FrameCount[0] | (g.FrameCount[1] << 8)); }

static void test_plain_keys(void)
{
    start("\xEA");                                 /* "j" */
    pop_keys(&g);
    CHECK_EQ(g.keypress, 0xEA);
    CHECK_EQ(g.keydown, 0x6A);                     /* nothing held; the low bits are the key */
    CHECK_EQ(g.kbdX, 0xFF);                        /* KREAD ran: left */
    CHECK_EQ(hw.latch, 0x6A);                      /* read: the strobe is clear */
    CHECK_EQ(g.keybufptr, 1);
    CHECK_EQ(g.keybuf[1], 0xEA);
    CHECK_EQ(nhooks, 0);

    hw.any = 1;                                    /* the next pass: no new key, but one held */
    pop_keys(&g);
    CHECK_EQ(g.keypress, 0x6A);
    CHECK_EQ(g.keydown, 0xEA);
    CHECK_EQ(g.kbdX, 0xFF);                        /* still left */
    CHECK_EQ(g.keybufptr, 1);                      /* an old key is not typed again */
    hw.any = 0;
    pop_keys(&g);
    CHECK_EQ(g.kbdX, 0);
    CHECK_EQ(hw.waits, 0);
    CHECK_EQ(g.leave, POP_LEAVE_NONE);

    start("");                                     /* no keyboard hooks: nothing happens */
    g.hooks.key_latch = NULL;
    g.hooks.key_strobe = NULL;
    g.keypress = 0xEA;
    pop_keys(&g);
    CHECK_EQ(g.keypress, 0);
    CHECK_EQ(g.keydown, 0);
}

static void test_legit_keys(void)
{
    start("\xA0");                                 /* space: how much time is left? */
    pop_keys(&g);
    CHECK_EQ(g.timerequest, 3);
    CHECK_EQ(nhooks, 0);

    start("\x93");                                 /* ctrl-S: sound off, quietly */
    pop_g_addsound(&g, 5);
    pop_keys(&g);
    CHECK_EQ(g.soundon, 0);
    CHECK_EQ(g.sq.count, 0);                       /* and what was queued is dropped */
    CHECK_EQ(nhooks, 0);
    hw.latch = 0x93;                               /* on again, with a beep */
    pop_keys(&g);
    CHECK_EQ(g.soundon, 1);
    CHECK(strcmp(hooklog, "g") == 0);

    start("\x8E");                                 /* ctrl-N: music */
    pop_keys(&g);
    CHECK_EQ(g.musicon, 0);
    CHECK_EQ(g.soundon, 1);
    CHECK_EQ(nhooks, 0);
    hw.latch = 0x8E;
    pop_keys(&g);
    CHECK_EQ(g.musicon, 1);
    CHECK(strcmp(hooklog, "g") == 0);

    start("\x8B");                                 /* ctrl-K: the keyboard */
    g.joyon = 0xFF;
    pop_keys(&g);
    CHECK_EQ(g.joyon, 0);
    CHECK(strcmp(hooklog, "g") == 0);

    start("\x8A");                                 /* ctrl-J: the joystick, centred where it is */
    hw.padx = 30; hw.pady = 40;
    g.jvert = g.jhoriz = g.jbtns = 1;
    pop_keys(&g);
    CHECK_EQ(g.joyon, 0xFF);
    CHECK_EQ(g.jvert | g.jhoriz | g.jbtns, 0);
    CHECK_EQ(g.jthres[0], 20);                     /* left of 20 is left */
    CHECK_EQ(g.jthres[2], 40);                     /* from 40 on is right */
    CHECK_EQ(g.jthres[1], 25);
    CHECK_EQ(g.jthres[3], 55);
    CHECK(strcmp(hooklog, "g") == 0);

    start("\x8A");                                 /* ... with nothing plugged in */
    hw.padx = 128;
    g.jthres[0] = 7;
    pop_keys(&g);
    CHECK_EQ(g.joyon, 0);                          /* stays with the keyboard */
    CHECK_EQ(g.jthres[0], 7);
    CHECK(strcmp(hooklog, "g") == 0);              /* the beep all the same */

    start("\x98");                                 /* ctrl-X, ctrl-Y: the axes reversed */
    pop_keys(&g);
    CHECK_EQ(g.jhoriz, 1);
    CHECK_EQ(g.jvert, 0);
    hw.latch = 0x99;
    pop_keys(&g);
    CHECK_EQ(g.jvert, 1);
    hw.latch = 0x98;
    pop_keys(&g);
    CHECK_EQ(g.jhoriz, 0);
    CHECK(strcmp(hooklog, "ggg") == 0);

    /* A flag that is negative after its flip is compared with the next
     * keys in place of the key: $98 becomes $99, which is ctrl-Y. */
    start("\x98");
    g.jhoriz = 0x98;
    pop_keys(&g);
    CHECK_EQ(g.jhoriz, 0x99);
    CHECK_EQ(g.jvert, 1);
    CHECK(strcmp(hooklog, "g") == 0);
    start("\x99");                                 /* ... and $92 becomes $93: ctrl-S */
    g.jvert = 0x92;
    pop_keys(&g);
    CHECK_EQ(g.jvert, 0x93);
    CHECK_EQ(g.soundon, 0);

    start("\x96");                                 /* ctrl-V */
    pop_keys(&g);
    CHECK(strcmp(hooklog, "V") == 0);

    start("\xC1");                                 /* "A": not a special key */
    pop_keys(&g);
    CHECK_EQ(nhooks, 0);
    CHECK_EQ(g.timerequest, 0);
    CHECK_EQ(g.soundon, 1);
}

static void test_leaving(void)
{
    start("\x92");                                 /* ctrl-R: back to the title */
    hw.b0 = 0x80;
    pop_strobe(&g);
    CHECK_EQ(g.leave, POP_LEAVE_ATTRACT);
    CHECK(strcmp(hooklog, "A") == 0);
    CHECK_EQ(g.BTN0, 0);                           /* the controller is not read any more */

    start("\x81");                                 /* ctrl-A: the level again */
    g.develment = 1;
    g.keybuf[0] = 0xD2;                            /* (an "R" typed before must not recharge now) */
    g.keybufptr = 9;
    pop_strobe(&g);
    CHECK_EQ(g.leave, POP_LEAVE_RESTART);
    CHECK(strcmp(hooklog, "R") == 0);
    CHECK_EQ(g.ChgKidStr, 0);

    start("\xEC");                                 /* an ordinary pass reads the controller */
    hw.b1 = 0x80;
    pop_strobe(&g);
    CHECK_EQ(g.leave, POP_LEAVE_NONE);
    CHECK_EQ(g.BTN1, 0x80);
}

static void test_freeze(void)
{
    start("\x9B\xEA");                             /* ESC, later "j" */
    pop_keys(&g);
    CHECK_EQ(hw.waits, 1);                         /* waited inside this pass */
    CHECK_EQ(g.SINGSTEP, 0);                       /* and went on */
    CHECK_EQ(g.keypress, 0);                       /* the key that ends the pause is ignored */
    CHECK_EQ(g.kbdX, 0);
    CHECK_EQ(g.keybufptr, 1);                      /* ESC was typed, "j" was not */
    CHECK_EQ(g.keybuf[1], 0x9B);
    CHECK_EQ(g.leave, POP_LEAVE_NONE);

    start("\x9B\x93");                             /* ended by ctrl-S: the sound stays on */
    pop_keys(&g);
    CHECK_EQ(g.soundon, 1);
    CHECK_EQ(g.SINGSTEP, 0);

    start("\x9B\x9B\x9B\xA0");                     /* ESC, ESC, ESC, space */
    pop_keys(&g);                                  /* frozen; the second ESC: one frame */
    CHECK_EQ(g.SINGSTEP, 1);
    CHECK_EQ(g.keypress, 1);                       /* not a key */
    CHECK_EQ(g.leave, POP_LEAVE_NONE);
    pop_keys(&g);                                  /* next pass: waits at once; ESC again */
    CHECK_EQ(g.SINGSTEP, 1);
    CHECK_EQ(g.keypress, 1);
    CHECK_EQ(hw.waits, 2);
    pop_keys(&g);                                  /* space: the pause is over */
    CHECK_EQ(g.SINGSTEP, 0);
    CHECK_EQ(g.keypress, 0);
    CHECK_EQ(g.timerequest, 0);                    /* and space did nothing else */
    CHECK_EQ(g.keybufptr, 1);                      /* nothing typed while frozen */
    pop_keys(&g);
    CHECK_EQ(hw.waits, 3);                         /* running again */

    /* The shell stops waiting (it is closing): the pass is abandoned and
     * the game stays frozen, to wait again if it is called again. */
    start("\x9B");
    g.kid.x = 77;
    CHECK_EQ(pop_mainloop_keys_only(), POP_LEAVE_FROZEN);
    CHECK_EQ(g.SINGSTEP, 1);
    hw.latch = 0xA0;                               /* a key after all */
    g.leave = 0;
    pop_keys(&g);
    CHECK_EQ(g.SINGSTEP, 0);
    CHECK_EQ(g.leave, POP_LEAVE_NONE);

    start("");                                     /* frozen on entry, no key */
    g.SINGSTEP = 0xFF;
    pop_strobe(&g);
    CHECK_EQ(g.leave, POP_LEAVE_FROZEN);
    CHECK_EQ(g.SINGSTEP, 0xFF);
}

static void test_code_words(void)
{
    start("");
    type("SKI");
    CHECK_EQ(g.NextLevel, 1);
    type("P");                                     /* SKIP: the next level, with 15 minutes */
    CHECK_EQ(g.NextLevel, 2);
    CHECK_EQ(fc(), pop_timetable(9));
    CHECK_EQ(g.NextTimeMsg, 18);
    CHECK_EQ(g.develment, 0);

    start("");                                     /* small letters count; other keys before do not matter */
    type("xskip");
    CHECK_EQ(g.NextLevel, 2);
    start("");
    type("SKIQP");                                 /* a key in between spoils it */
    CHECK_EQ(g.NextLevel, 1);

    start("");                                     /* from level 3 it still works, from 4 not */
    g.level = g.NextLevel = 3;
    type("SKIP");
    CHECK_EQ(g.NextLevel, 4);
    start("");
    g.level = g.NextLevel = 4;
    type("SKIP");
    CHECK_EQ(g.NextLevel, 4);
    CHECK_EQ(fc(), 0);

    start("");                                     /* the word wraps around the buffer's end */
    g.keybufptr = 7;
    type("SKIP");
    CHECK_EQ(g.keybufptr, 1);
    CHECK_EQ(g.keybuf[8], 0xD3);
    CHECK_EQ(g.keybuf[1], 0xD0);
    CHECK_EQ(g.NextLevel, 2);

    start("");                                     /* POP: development mode, with a beep */
    g.MaxKidStr = 4;
    type("BOOST");
    CHECK_EQ(g.MaxKidStr, 4);                      /* (not yet) */
    CHECK_EQ(g.ChgKidStr, 0);
    type("POP");
    CHECK_EQ(g.develment, 1);
    CHECK(strcmp(hooklog, "g") == 0);
    g.level = g.NextLevel = 11;                    /* SKIP now reaches level 12 */
    type("SKIP");
    CHECK_EQ(g.NextLevel, 12);
    g.level = g.NextLevel = 12;
    type("SKIP");
    CHECK_EQ(g.NextLevel, 12);

    g.MaxKidStr = 4; g.KidStrength = 1; g.origstrength = 3;
    type("BOOST");                                 /* one more unit, all of them full */
    CHECK_EQ(g.MaxKidStr, 5);
    CHECK_EQ(g.ChgKidStr, 4);
    CHECK_EQ(g.origstrength, 5);
    g.ChgKidStr = 0; g.KidStrength = 2;
    type("R");                                     /* recharge */
    CHECK_EQ(g.ChgKidStr, 3);
    CHECK_EQ(g.MaxKidStr, 5);
    g.OppStrength = 4;
    type("Z");                                     /* the opponent down to one */
    CHECK_EQ(g.ChgOppStr, 0xFD);
    type("ZAP");                                   /* ... to nothing */
    CHECK_EQ(g.ChgOppStr, 0xFC);
    g.FrameCount[0] = g.FrameCount[1] = 0; g.NextTimeMsg = 0;
    type("TINA");                                  /* to the last level */
    CHECK_EQ(g.NextLevel, 14);
    CHECK_EQ(fc(), pop_timetable(9));
    g.NextLevel = 12; g.ChgOppStr = 0;
    type("\x8D");                                  /* Return: development mode off */
    CHECK_EQ(g.develment, 0);
    type("ZAP");
    CHECK_EQ(g.ChgOppStr, 0);

    start("");                                     /* a key buffer pointer beyond the buffer */
    g.keybufptr = 200;
    CHECK_EQ(pop_checkcode(&g, "R"), 0xFF);
    CHECK_EQ(g.unmodelled, 1);
    start("");
    CHECK_EQ(pop_checkcode(&g, ""), 0);            /* the empty word is always there */
    g.keybuf[0] = 0xB1;                            /* a digit matches only itself */
    CHECK_EQ(pop_checkcode(&g, "1"), 0);
    g.keybuf[0] = 0x91;
    CHECK_EQ(pop_checkcode(&g, "1"), 0xFF);
}

static void test_controller(void)
{
    start("");
    g.joyon = 0xFF;
    g.jthres[0] = 25; g.jthres[2] = 45; g.jthres[1] = 20; g.jthres[3] = 50;
    hw.padx = 24; hw.pady = 50;                    /* left of the dead zone; at its lower edge */
    hw.b0 = 0x80; hw.b1 = 0x7F;
    pop_controller(&g);
    CHECK_EQ(g.joyX, 0xFF);
    CHECK_EQ(g.joyY, 1);
    CHECK_EQ(g.BTN0, 0x80);
    CHECK_EQ(g.BTN1, 0x7F);
    hw.padx = 25; hw.pady = 49;                    /* inside it */
    pop_jread(&g);
    CHECK_EQ(g.joyX, 0);
    CHECK_EQ(g.joyY, 0);
    hw.padx = 45; hw.pady = 19;
    pop_jread(&g);
    CHECK_EQ(g.joyX, 1);
    CHECK_EQ(g.joyY, 0xFF);
    g.jhoriz = 1;                                  /* reversed */
    pop_jread(&g);
    CHECK_EQ(g.joyX, 0xFF);
    CHECK_EQ(g.joyY, 0xFF);
    g.jvert = 1;
    pop_jread(&g);
    CHECK_EQ(g.joyY, 1);

    g.joyon = 0;                                   /* keyboard selected: the stick is not read */
    g.joyX = 9; g.joyY = 9;
    pop_jread(&g);
    CHECK_EQ(g.joyX, 9);
    hw.b0 = 0; hw.b1 = 0xFF;                       /* the buttons are, always */
    g.jbtns = 1;                                   /* "switched" switches nothing */
    pop_controller(&g);
    CHECK_EQ(g.BTN0, 0);
    CHECK_EQ(g.BTN1, 0xFF);
}

static void test_saved_games(void)
{
    start("\x87");                                 /* ctrl-G on level 2: not yet; a thud */
    g.level = 2;
    g.savedgame[POP_SAV_STRENGTH] = 99;
    pop_keys(&g);
    CHECK_EQ(g.savedgame[POP_SAV_LEVEL], 2);
    CHECK_EQ(g.savedgame[POP_SAV_STRENGTH], 99);
    CHECK_EQ(g.sq.count, 1);
    CHECK_EQ(g.sq.items[0], POP_SND_SPLAT);
    CHECK_EQ(nhooks, 0);

    start("\x87");                                 /* from level 3 on */
    g.level = 7;
    g.origstrength = 6;
    g.FrameCount[0] = 0x34; g.FrameCount[1] = 0x12;
    g.NextTimeMsg = 8;
    pop_keys(&g);
    CHECK(strcmp(hooklog, "G") == 0);
    CHECK_EQ(g.savedgame[POP_SAV_LEVEL], 7);
    CHECK_EQ(g.savedgame[POP_SAV_STRENGTH], 6);
    CHECK_EQ(g.savedgame[POP_SAV_TIMER], 0x34);
    CHECK_EQ(g.savedgame[POP_SAV_TIMER + 1], 0x12);
    CHECK_EQ(g.savedgame[POP_SAV_NEXTMSG], 8);
    CHECK_EQ(g.sq.count, 0);

    memcpy(hw.saved, g.savedgame, sizeof hw.saved);    /* and back from the disk */
    start("");
    g.SINGSTEP = 1;
    hw.saved[POP_SAV_LEVEL] = 7; hw.saved[POP_SAV_STRENGTH] = 6;
    hw.saved[POP_SAV_TIMER] = 0x34; hw.saved[POP_SAV_TIMER + 1] = 0x12;
    hw.saved[POP_SAV_NEXTMSG] = 8;
    pop_startresume(&g);
    CHECK_EQ(g.leave, POP_LEAVE_RESTART);
    CHECK(strcmp(hooklog, "R") == 0);
    CHECK_EQ(g.level, 7);
    CHECK_EQ(g.NextLevel, 7);
    CHECK_EQ(g.origstrength, 6);
    CHECK_EQ(fc(), 0x1234);
    CHECK_EQ(g.NextTimeMsg, 8);
    CHECK_EQ(g.timerequest, 1);
    CHECK_EQ(g.yellowflag, 0x80);
    CHECK_EQ(g.SINGSTEP, 0);                       /* initgame */
    CHECK_EQ(g.SongCue, 0);                        /* no opening theme */

    start("");                                     /* nothing saved: a new game */
    memset(hw.saved, 0xFF, sizeof hw.saved);
    pop_startresume(&g);
    CHECK_EQ(g.level, 1);
    CHECK_EQ(g.NextLevel, 1);
    CHECK_EQ(g.origstrength, POP_INITMAXSTR);
    CHECK_EQ(fc(), 0);
    CHECK_EQ(g.timerequest, 0);
    CHECK_EQ(g.leave, POP_LEAVE_RESTART);
}

int main(void)
{
    RUN(test_plain_keys);
    RUN(test_legit_keys);
    RUN(test_leaving);
    RUN(test_freeze);
    RUN(test_code_words);
    RUN(test_controller);
    RUN(test_saved_games);
    return pop_test_summary();
}
