/* pop_top.c - see pop_top.h.  New code; ports of original routines.
 *
 * The code follows the original branch by branch, with the original labels
 * in comments; docs/port/game-frame.md explains the order of a frame.
 */

#include "pop_top.h"

#include "pop_auto.h"
#include "pop_collide.h"
#include "pop_control.h"
#include "pop_mover.h"
#include "pop_world.h"

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))
#define HOOK(name) do { if (g->hooks.name) g->hooks.name(g->hooks.ctx); } while (0)

/* --- small things ------------------------------------------------------------------------ */

uint8_t pop_cold(uint8_t posn)
{
    return (posn == 185 || posn == 177 || posn == 178) ? 0 : 1;   /* dead, impaled, halved */
}

uint8_t pop_static(uint8_t posn)
{
    if (posn == 0 || posn == 15 || posn == 229 || posn == 109 || posn == 171 || posn == 166) {
        return 0;                              /* standing, crouching, en garde ... */
    }
    return pop_cold(posn);
}

void pop_initcdbuf(pop_game *g)
{
    unsigned x;
    for (x = 0; x < 10; x++) {
        g->SN[POP_CD_LAST][x] = g->SN[POP_CD_THIS][x] = 0xFF;
        g->SN[POP_CD_BELOW][x] = g->SN[POP_CD_ABOVE][x] = 0xFF;
    }
    g->BlockYlast = 0xFF;
}

void pop_getscrns(pop_game *g)
{
    g->scrnLeft = pop_getleft(g, g->VisScrn);
    g->scrnRight = pop_getright(g, g->VisScrn);
    g->scrnAbove = pop_getup(g, g->VisScrn);
    g->scrnBelow = pop_getdown(g, g->VisScrn);
    g->scrnBelowL = pop_getleft(g, g->scrnBelow);
    g->scrnBelowR = pop_getright(g, g->scrnBelow);
    g->scrnAboveL = pop_getleft(g, g->scrnAbove);
    g->scrnAboveR = pop_getright(g, g->scrnAbove);
}

void pop_wtlessflash(pop_game *g)
{
    uint8_t a = g->ag.weightless, x = 0;
    if (a == 0) return;
    a--;
    g->ag.weightless = a;
    if (a != 0) {
        x = 0xFF;
        if (a < POP_WTLFLASH) x = g->vibes ^ 0xFF;           /* flicker as it wears off */
    }
    g->vibes = x;
}

void pop_misctimers(pop_game *g)
{
    if (g->mergetimer != 0 && !NEG(g->mergetimer)) {
        g->mergetimer--;
        if (g->mergetimer == 0) g->mergetimer = 0xFF;        /* from 1 straight to -1 */
    }
    /* Level 8: once the exit is open, a while spent on screen 16 brings the mouse */
    if (g->level == 8 && g->ch.scrn == 16 && g->exitopen != 0) {
        if (g->exitopen < POP_MOUSETIMER) {
            g->exitopen++;
        } else if (g->exitopen == POP_MOUSETIMER) {
            pop_mouserescue(g);
            g->exitopen++;
        }
    }
}

void pop_addsfx(pop_game *g)
{
    if (g->kid.posn == 167) pop_g_addsound(g, POP_SND_SWORDCLASH1);
    else if (g->shad.posn == 167) pop_g_addsound(g, POP_SND_SWORDCLASH2);
}

/* --- the demo ------------------------------------------------------------------------------ */

void pop_democtrl(pop_game *g)
{
    const pop_autodata *a = g->autodata;
    if (g->milestone != 0) {                   /* :finish the guard is dead: run off screen */
        g->clrbtn = pop_clrall(g);
        g->clrF = 0xFF;
        g->JSTKX = 0xFF;
        return;
    }
    if (g->ch.sword != 0) {                    /* fighting: the kid is a "guard" too */
        g->guardprog = 10;
        pop_autoctrl(g);
        g->guardprog = 11;
        return;
    }
    if (!a || !a->demoprog) { g->unmodelled = 1; return; }
    pop_autoplayback(g, a->demoprog, a->demoprog_len);       /* DEMO */
}

/* --- a new screen ---------------------------------------------------------------------------- */

void pop_prepcut(pop_game *g)
{
    if (g->cutscrn == 0 || g->cutscrn == g->VisScrn) return; /* never to screen 0 */
    g->VisScrn = g->cutscrn;
    if (g->VisScrn == 5 && g->level == 14) {   /* the princess's room */
        HOOK(you_win);
        return;
    }
    g->cutplan = 1;
    pop_getscrns(g);
    pop_loadkid(g);
    pop_addslicers(g);
    pop_addtorches(g);
    pop_crumble(g);
    pop_addguard(g);
}

void pop_firstframe(pop_game *g)
{
    g->cutscrn = g->kid.scrn;
    pop_prepcut(g);
    HOOK(clean_cut);
}

/* --- the kid ------------------------------------------------------------------------------------ */

void pop_ctrlplayer(pop_game *g)
{
    uint8_t a;
    pop_kill0(g);
    pop_playerctrl(g);
    if (NEG(g->ch.life)) return;               /* alive */

    /* Dead: CharLife counts 0, 1, ... once he has stopped moving. */
    if (pop_cold(g->ch.posn) != 0) return;
    if (g->ch.life == 0) pop_deathsong(g);
    if (g->ch.life < POP_DEADENOUGH) {
        g->ch.life++;
        return;
    }
    /* :deadenough */
    if (g->level == 0) { HOOK(go_attract); return; }         /* his death ends the demo */
    if (g->SongCue != 0) return;               /* let the song finish first */
    if ((g->MinLeft | g->SecLeft) == 0) { HOOK(you_lose); return; }
    if (g->message == POP_MSG_CONT && g->msgtimer != 0) {
        a = g->msgtimer;
    } else {
        g->message = POP_MSG_CONT;             /* "Press button to continue" */
        g->msgtimer = 255;
        a = 255;
    }
    if (a == 1) { HOOK(go_attract); return; }  /* he did not: game over */
    if (!NEG(g->BTN0 | g->BTN1)) return;
    HOOK(restart);
}

void pop_dokid(pop_game *g)
{
    pop_loadkidwop(g);
    pop_rereadblocks(g);
    pop_unholy(g);
    pop_ctrlplayer(g);
    if (g->invert != 0 && !NEG(g->ch.life)) {  /* dead: the screen turns right side up */
        g->redrawflg = 2;
        g->invert = 0;
        HOOK(invert_y);
        return;                                /* (a jump: the rest of this frame is skipped) */
    }
    pop_wtlessflash(g);
    if (g->ch.scrn != 0) {
        pop_g_animchar(g);
        pop_gravity(g);
        pop_addfall(g);
        pop_setupchar(g);
        pop_rereadblocks(g);
        pop_getedges(g);
        pop_firstguard(g);
        pop_checkbarr(g);
        pop_collisions(g);
        pop_checkgate(g);
        pop_checkfloor(g);
        pop_checkpress(g);
        pop_checkspikes(g);
        pop_checkimpale(g);
        pop_checkslice(g);
        pop_shakeloose(g);
    }
    pop_savekid(g);
}

/* --- a frame ---------------------------------------------------------------------------------- */

void pop_nextframe(pop_game *g)
{
    pop_animmobs(g);
    pop_animtrans(g);
    pop_bonesrise(g);
    pop_checkalert(g);
    pop_dokid(g);
    pop_doshad(g);
    pop_checkstrike(g);
    pop_checkstab(g);
    pop_addsfx(g);
    pop_chgmeters(g);
    pop_cutcheck(g);
    pop_prepcut(g);
    pop_cutguard(g);

    if (g->level == 0) {                       /* the demo ends when the kid leaves screen 24 */
        if (g->kid.scrn == 24) { HOOK(go_attract); return; }
    } else if (g->level == 6) {                /* falling off screen 1 leads to the next level */
        if (g->kid.scrn == 1 && g->kid.y < 20) {
            g->kid.y = 0xFF;
            g->NextLevel++;
        }
    } else if (g->level == 12) {               /* and so does leaving screen 23 */
        if (g->kid.scrn == 23) {
            g->NextLevel++;
            g->skipmessage = 1;                /* (without announcing the level or the time) */
            HOOK(load_next);
            return;
        }
    }
    /* the clock runs until the vizier is dead */
    if (g->level < 13 || (g->level == 13 && g->exitopen == 0)) HOOK(keep_time);
    HOOK(show_time);
    if (g->level >= 13) return;                /* one chance to finish level 13 after time is up */
    if ((g->MinLeft | g->SecLeft) != 0) return;
    HOOK(you_lose);
}

/* --- a level begins ------------------------------------------------------------------------------ */

void pop_setinitials(pop_game *g)
{
    static const uint8_t initsettings[2] = { POP_GMAXVAL, 0 };    /* FRAMEADV.S 22-23: up, down */
    uint8_t scrn = u8(g->blueprint[POP_INFO] - 1);           /* INFO: number of screens + 1 */
    g->SCRNUM = scrn;
    do {
        int y;
        pop_calcblue(g, g->SCRNUM);
        for (y = 29; y >= 0; y--) {            /* GETINITOBJ */
            uint8_t id = pop_bluetype(g, u8(y)) & POP_IDMASK, spec = pop_bluespec(g, u8(y));
            if (id == POP_GATE) {
                if (spec < 1 || spec > 2) { g->unmodelled = 1; continue; }
                pop_set_bluespec(g, u8(y), initsettings[spec - 1]);
            } else if (id == POP_LOOSE) {
                pop_set_bluespec(g, u8(y), 0);
            } else if (id == POP_FLASK) {
                pop_set_bluespec(g, u8(y), u8(spec << 5));   /* its kind, into the top bits */
            }
        }
        g->ry = 0xFF;
        g->SCRNUM--;
    } while (g->SCRNUM != 0);
}

void pop_startkid1(pop_game *g)
{
    if (g->ch.block_y == 0xFF) { g->unmodelled = 1; return; }     /* FloorY+1,x */
    g->ch.y = pop_floory(g, u8(g->ch.block_y + 1));
    g->ch.life = 0xFF;
    g->ch.id = 0;
    g->ch.x_vel = g->ch.y_vel = 0;
    g->waitingtojump = 0;
    g->ag.weightless = 0;
    g->invert = 0;
    g->ag.jarabove = 0;
    g->droppedout = 0;
    g->ch.sword = 0;
    g->offguard = 0;
    pop_g_animchar(g);
    if (g->level == 7) {                       /* the weightless level: he falls into the screen */
        if (!NEG(g->yellowflag)) g->timebomb = 0x40;         /* (copy protection) */
        if (g->ch.scrn == 17) pop_cut(g, 3);
    }
    pop_savekid(g);
}

void pop_startkid(pop_game *g)
{
    uint8_t row;
    if (g->level == 3 && g->milestone != 0) {  /* past the first gate: start just inside it */
        g->blueprint[POP_KIDSTART_FACE] = 0xFF;
        g->blueprint[POP_KIDSTART_SCRN] = 2;
        g->blueprint[POP_KIDSTART_BLOCK] = 6;
        (void)pop_rdblock(g, 7, 4, 0);
        pop_set_bluetype(g, g->ry, POP_SPACE); /* (the loose floor there has fallen) */
    }
    g->ch.scrn = g->blueprint[POP_KIDSTART_SCRN];
    g->ch.block_x = pop_unindex(g->blueprint[POP_KIDSTART_BLOCK], &row);
    g->ch.block_y = row;
    g->ch.x = u8(pop_getblockej(g, g->ch.block_x) + POP_ANGLE + 7);
    g->ch.face = g->blueprint[POP_KIDSTART_FACE] ^ 0xFF;
    g->MaxKidStr = g->KidStrength = (g->level == 0) ? 4 : g->origstrength;

    if (g->level == 1) {                       /* he drops in, and the gate slams behind him */
        (void)pop_rdblock(g, 5, 2, 0);
        pop_pushpp(g);
        pop_g_jumpseq(g, POP_SQ_STEPFALL);
    } else if (g->level == 13) {
        pop_g_jumpseq(g, POP_SQ_RUNNING);
    } else {
        pop_g_jumpseq(g, POP_SQ_TURN);
    }
    pop_startkid1(g);
}

void pop_entrance(pop_game *g)
{
    int y;
    pop_calcblue(g, g->kid.scrn);
    for (y = 29; y >= 0; y--) {                /* the door he came in by */
        if ((pop_bluetype(g, u8(y)) & POP_IDMASK) == POP_EXIT) {
            g->ry = u8(y);
            pop_closeexit(g, g->kid.scrn);
            return;
        }
    }
    g->ry = 0xFF;
}

void pop_restart_level(pop_game *g)
{
    pop_setinitials(g);
    pop_initialguards(g);

    g->vibes = 0;
    g->AMtimer = 0;
    g->VisScrn = 0;
    g->exitopen = 0;
    g->lightning = 0;
    g->mergetimer = 0;
    g->numtrans = 0;
    g->nummob = 0;
    g->EnemyAlert = 0;
    g->createshad = 0;
    g->stunned = 0;
    g->heroic = 0;
    g->ChgKidStr = 0;
    g->OppStrength = 0;                        /* no opponent */
    g->msgtimer = 0;
    g->PreRecPtr = 0;
    g->PlayCount = 0;
    if (g->SongCue != POP_SONG_DANGER) g->SongCue = 0;

    pop_zerosound(&g->sq);
    HOOK(zero_peels);
    pop_initcdbuf(g);
    HOOK(init_input);

    g->gotsword = 1;
    g->cutorder = 0xFF;
    g->shad.id = 2;                            /* by default the opponent is a guard */
    g->shad.face = POP_NO_CHAR;
    pop_startkid(g);
    if (g->level == 1) g->gotsword = 0;        /* level 1 starts without the sword */

    if (g->level != 0 && g->level != 14) {     /* announce the level */
        if (g->level == 13 && g->skipmessage != 0) {
            g->skipmessage = 0;                /* (not when 13 follows straight from 12) */
        } else {
            g->message = POP_MSG_LEVEL;
            g->msgtimer = POP_LEVELTIMER;
        }
    }
    pop_entrance(g);
    pop_firstframe(g);
}
