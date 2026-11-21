/* pop_keys.c - the keyboard's special keys and the controller.  See pop_keys.h. */

#include "pop_keys.h"

#include "pop_auto.h"
#include "pop_control.h"
#include "pop_loop.h"

#define NEG(v) (((v) & 0x80) != 0)
#define HOOK(name) do { if (g->hooks.name) g->hooks.name(g->hooks.ctx); } while (0)

/* --- the keyboard as hardware -------------------------------------------------------------- */

static uint8_t latch(pop_game *g, int waiting)             /* lda $C000 */
{
    return g->hooks.key_latch ? g->hooks.key_latch(g->hooks.ctx, waiting) : 0;
}

static uint8_t strobe(pop_game *g)                         /* lda $C010, or sta */
{
    return g->hooks.key_strobe ? g->hooks.key_strobe(g->hooks.ctx) : 0;
}

/* --- code words ---------------------------------------------------------------------------- */

void pop_addkey(pop_game *g, uint8_t key)
{
    uint8_t x = (uint8_t)(g->keybufptr + 1);
    if (x >= POP_KEYBUFLEN) x = 0;                         /* wrap around */
    g->keybufptr = x;
    g->keybuf[x] = key;
}

uint8_t pop_checkcode(pop_game *g, const char *word)
{
    /* The original stores each word backwards, high bits set, and walks it
     * and the key buffer from the last key back. */
    uint8_t x = g->keybufptr;
    size_t n = 0;
    while (word[n] != 0) n++;
    while (n > 0) {
        uint8_t c = (uint8_t)((uint8_t)word[--n] | 0x80);
        if (x >= POP_KEYBUFLEN) {                          /* keybuf,x beyond the buffer */
            g->unmodelled = 1;
            return 0xFF;
        }
        if (c != g->keybuf[x]) {
            if (c < 0xC1 || c >= 0xDB) return 0xFF;        /* not a letter */
            if ((uint8_t)(c | 0x20) != g->keybuf[x]) return 0xFF;      /* nor typed small */
        }
        x--;
        if (NEG(x)) x = POP_KEYBUFLEN - 1;                 /* wrap around */
    }
    return 0;
}

void pop_checkcodes(pop_game *g)
{
    if (pop_checkcode(g, "BOOST") == 0) {
        pop_boostmeter(g);
        g->origstrength = g->MaxKidStr;
    } else if (pop_checkcode(g, "R") == 0) {
        pop_rechargemeter(g);
    } else if (pop_checkcode(g, "ZAP") == 0) {             /* the opponent down to nothing */
        g->ChgOppStr = (uint8_t)(0 - g->OppStrength);
    } else if (pop_checkcode(g, "Z") == 0) {               /* ... down to one */
        g->ChgOppStr = (uint8_t)(1 - g->OppStrength);
    } else if (pop_checkcode(g, "TINA") == 0) {
        g->NextLevel = 14;
        pop_shortentime(g);
    }
}

/* --- KEYS ---------------------------------------------------------------------------------- */

/* KEYS2: the key is known.  Returns 0 if it is not a new one. */
static int keys2(pop_game *g, uint8_t key)
{
    g->keypress = key;
    g->keydown = strobe(g);                                /* (clears the keyboard strobe) */
    pop_kread(g);
    return NEG(g->keypress);
}

/* freeze: wait for a key.  ESC lets one frame pass and freezes again; any
 * other key ends the freeze and is itself ignored. */
static void freeze(pop_game *g)
{
    uint8_t a = latch(g, 1);
    if (!NEG(a)) {
        /* The original loops until a key comes.  A shell returns without
         * one only to end the session: abandon the pass, still frozen. */
        if (g->SINGSTEP == 0) g->SINGSTEP = 1;
        g->leave = POP_LEAVE_FROZEN;
        return;
    }
    if (a == POP_KEY_ESC) {
        g->SINGSTEP = 1;
        (void)strobe(g);
        g->keypress = 1;
        return;
    }
    g->SINGSTEP = 0;
    (void)keys2(g, 0);                                     /* "ignore the keypress that breaks ESC" */
}

void pop_legitkeys(pop_game *g)
{
    uint8_t a = g->keypress;
    if (a == POP_KEY_ESC) { freeze(g); return; }
    if (a == POP_KEY_RESTART) { g->leave = POP_LEAVE_ATTRACT; HOOK(go_attract); return; }
    if (a == POP_KEY_ABORT) { g->leave = POP_LEAVE_RESTART; HOOK(restart); return; }
    if (a == POP_KEY_SETKBD) { g->joyon = 0; HOOK(tone); return; }
    if (a == POP_KEY_SETJSTK) { pop_setcenter(g); HOOK(tone); return; }
    /* From here a flag that turns out negative falls through to the next
     * comparison with its own value in place of the key. */
    if (a == POP_KEY_FLIPX) {
        a = g->jhoriz ^= 1;
        if (!NEG(a)) { HOOK(tone); return; }
    }
    if (a == POP_KEY_FLIPY) {
        a = g->jvert ^= 1;
        if (!NEG(a)) { HOOK(tone); return; }
    }
    if (a == POP_KEY_SOUND) {
        pop_zerosound(&g->sq);
        g->soundon ^= 1;
        if (g->soundon != 0) HOOK(tone);
        return;
    }
    if (a == POP_KEY_MUSIC) {
        g->musicon ^= 1;
        if (g->musicon != 0) HOOK(tone);
        return;
    }
    if (a == POP_KEY_VERSION) { HOOK(show_version); return; }
    if (a == POP_KEY_SAVEGAME) {
        g->savedgame[POP_SAV_LEVEL] = g->level;
        pop_dosavegame(g);
        return;
    }
    if (a == POP_KEY_SHOWTIME) g->timerequest = 3;
}

void pop_develkeys(pop_game *g)
{
    if (g->develment == 0) return;
    pop_checkcodes(g);
    if (g->keypress == POP_KEY_CLEAN) g->develment = 0;
}

void pop_keys(pop_game *g)
{
    uint8_t key;
    if (g->SINGSTEP != 0) {
        freeze(g);                                         /* frozen since an earlier pass */
        return;
    }
    key = latch(g, 0);                                     /* KEYS1 */
    if (!keys2(g, key)) return;
    pop_addkey(g, g->keypress);
    if (pop_checkcode(g, "POP") == 0) {                    /* development mode on */
        g->develment = 1;
        HOOK(tone);
        return;
    }
    if (pop_checkcode(g, "SKIP") == 0) {
        /* to the next level: up to level 4, or 12 in development mode */
        uint8_t limit = g->develment != 0 ? 11 : 3;
        if (limit >= g->level) {
            g->NextLevel++;
            pop_shortentime(g);
        }
    }
    pop_legitkeys(g);
    if (g->leave != 0) return;                             /* (jumped away, or still frozen) */
    pop_develkeys(g);
}

/* --- the controller ------------------------------------------------------------------------ */

static void cvtpdl(pop_game *g, uint8_t *raw, int axis)
{
    if (*raw < g->jthres[axis]) *raw = 0xFF;
    else if (*raw < g->jthres[2 + axis]) *raw = 0;
    else *raw = 1;
}

void pop_jread(pop_game *g)
{
    if (g->joyon == 0) return;
    HOOK(read_paddles);
    cvtpdl(g, &g->joyX, 0);
    cvtpdl(g, &g->joyY, 1);
    if (g->jvert != 0) g->joyY = (uint8_t)(0 - g->joyY);
    if (g->jhoriz != 0) g->joyX = (uint8_t)(0 - g->joyX);
}

void pop_bread(pop_game *g)
{
    /* jbtns, "buttons switched", chooses between two pieces of code that
     * load the same button into the same variable (GRAFIX.S 1125-1136). */
    HOOK(read_buttons);
}

void pop_controller(pop_game *g)
{
    pop_jread(g);
    pop_bread(g);
}

void pop_setcenter(pop_game *g)
{
    g->joyon = 0xFF;
    g->jvert = g->jhoriz = g->jbtns = 0;
    HOOK(read_paddles);
    if (NEG(g->joyX | g->joyY)) {                          /* no joystick connected */
        g->joyon = 0;
        return;
    }
    g->jthres[0] = (uint8_t)(g->joyX - POP_CWIDTHX);
    g->jthres[2] = (uint8_t)(g->joyX + POP_CWIDTHX);
    g->jthres[1] = (uint8_t)(g->joyY - POP_CWIDTHY);
    g->jthres[3] = (uint8_t)(g->joyY + POP_CWIDTHY);
}

void pop_strobe(pop_game *g)
{
    pop_keys(g);
    if (g->leave != 0) return;                             /* KEYS did not come back */
    pop_controller(g);
}

/* --- saved games --------------------------------------------------------------------------- */

void pop_dosavegame(pop_game *g)
{
    if (g->level < POP_FIRST_SIDE_B) {                     /* "must have reached side B" */
        pop_g_addsound(g, POP_SND_SPLAT);
        return;
    }
    g->savedgame[POP_SAV_STRENGTH] = g->origstrength;
    g->savedgame[POP_SAV_TIMER] = g->FrameCount[0];
    g->savedgame[POP_SAV_TIMER + 1] = g->FrameCount[1];
    g->savedgame[POP_SAV_NEXTMSG] = g->NextTimeMsg;
    HOOK(save_game);
}

void pop_startresume(pop_game *g)
{
    pop_startgame(g, 4);                                   /* "arbitrary value >1" */
    HOOK(load_game);
    if (NEG(g->savedgame[POP_SAV_LEVEL])) {                /* no game saved: a new one */
        g->level = g->NextLevel = 1;
    } else {
        g->origstrength = g->savedgame[POP_SAV_STRENGTH];
        g->FrameCount[1] = g->savedgame[POP_SAV_TIMER + 1];
        g->FrameCount[0] = g->savedgame[POP_SAV_TIMER];
        g->NextTimeMsg = g->savedgame[POP_SAV_NEXTMSG];
        g->timerequest = 1;                                /* show the time remaining */
        g->yellowflag = 0x80;                              /* "pass copy prot. test" */
        g->level = g->NextLevel = g->savedgame[POP_SAV_LEVEL];
    }
    g->leave = POP_LEAVE_RESTART;
    HOOK(restart);
}
