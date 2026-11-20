/* test_auto.c - opponents, sword hits, strength and screen changes on
 * synthetic data.
 *
 * New code; uses the made-up state of pop_fixture.h (including made-up fight
 * tables).  Expected values were worked out by hand from AUTO.S and MISC.S
 * (see pop_auto.h for the line ranges).  The same routines are compared with
 * the original code on random states (tests/fuzz_control.py) and inside the
 * multi-frame trace (tests/trace_kidstep.py) when the original source is
 * available.
 */

#include <string.h>

#include "pop_auto.h"
#include "pop_collide.h"
#include "pop_fixture.h"
#include "pop_mover.h"
#include "pop_test.h"

/* A guard in column `col` facing `face`, as the acting character, with the
 * kid in column `kidcol` of the same row as his opponent. */
static void guard_and_kid(int col, uint8_t face, int kidcol)
{
    setup(col, face);
    g.ch.id = 2;
    g.ch.posn = 166;                               /* standing alert */
    g.ch.action = 1;
    g.op = g.ch;
    g.op.id = 0;
    g.op.posn = 15;
    g.op.action = 0;
    g.op.x = (uint8_t)(POP_SCRN_LEFT + POP_BLOCK_WIDTH * kidcol + 7);
    g.op.block_x = (uint8_t)kidcol;
    g.op.face = (uint8_t)(face ^ 0xFF);
    g.kid = g.op;
    g.shad = g.ch;
    g.EnemyAlert = 2;
    g.guardprog = 1;
}

static void test_guard_notices(void)
{
    guard_and_kid(5, POP_FACE_LEFT, 2);            /* the kid 42 units in front of him */
    CHECK_EQ(pop_getopdist(&g), 42 + 13);
    pop_autoctrl(&g);                              /* in sight: "down and forward" = draw */
    CHECK_EQ(g.clrD, 0xFF);
    CHECK_EQ(g.clrF, 0xFF);
    CHECK_EQ(g.JSTKX, 0xFF);
    pop_genctrl(&g);                               /* the same control code as the kid's */
    CHECK_EQ(g.ch.sword, 2);
    CHECK_EQ(g.ch.seq, seq_addr(POP_SQ_GUARDENGARDE));

    guard_and_kid(2, POP_FACE_LEFT, 5);            /* the kid behind him, silent: nothing */
    pop_autoctrl(&g);
    CHECK_EQ(g.clrD | g.clrF | g.clrB | g.clrU, 0);
    g.ag.alertguard = 1;                           /* a noise: he turns round */
    pop_autoctrl(&g);
    CHECK_EQ(g.clrD, 0xFF);
    CHECK_EQ(g.JSTKY, 1);
    CHECK_EQ(g.ag.alertguard, 0);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* a dead kid is of no interest */
    g.kid.life = 0;
    pop_autoctrl(&g);
    CHECK_EQ(g.clrD | g.clrF, 0);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* nor one he cannot get at */
    g.EnemyAlert = 0;
    pop_autoctrl(&g);
    CHECK_EQ(g.clrD | g.clrF, 0);
}

static void en_garde_at(uint8_t gap, uint8_t kid_sword, uint8_t kid_posn)
{
    guard_and_kid(5, POP_FACE_LEFT, 2);
    g.ch.sword = 2;
    g.ch.posn = 158;
    g.op.x = (uint8_t)(g.ch.x - gap + 13);         /* getopdist adds 13 when they face each other */
    g.op.sword = kid_sword;
    g.op.posn = kid_posn;
    pop_autoctrl(&g);
}

static void test_guard_fights(void)
{
    en_garde_at(60, 2, 158);                       /* too far: advance (solid floor ahead) */
    CHECK_EQ(g.clrF, 0xFF);
    CHECK_EQ(g.clrbtn, 0);

    en_garde_at(8, 2, 158);                        /* too close to hit: step in anyway? no: */
    CHECK_EQ(g.clrF, 0xFF);                        /* facing each other he advances (AUTO.S 624-628) */

    en_garde_at(20, 0, 15);                        /* an unarmed kid within reach: strike */
    CHECK_EQ(g.clrbtn, 0xFF);
    CHECK_EQ(g.btn, 0xFF);

    en_garde_at(20, 2, 152);                       /* the kid is thrusting: type 1 always blocks */
    CHECK_EQ(g.clrU, 0xFF);
    CHECK_EQ(g.RNDseed, 138);                      /* two numbers drawn: block, then strike? */
    CHECK_EQ(g.clrbtn, 0);                         /* 138 is not below 100: no strike */

    en_garde_at(20, 2, 158);                       /* both ready: 23 is below 100: he strikes */
    CHECK_EQ(g.RNDseed, 23);
    CHECK_EQ(g.clrbtn, 0xFF);

    en_garde_at(20, 2, 158);                       /* type 0 never strikes of its own accord */
    g.guardprog = 0;
    g.RNDseed = 0;
    pop_autoctrl(&g);
    CHECK_EQ(g.clrbtn, 0);

    en_garde_at(20, 2, 158);                       /* just hit: no striking back yet */
    g.refract = 5;
    g.RNDseed = 0;
    pop_autoctrl(&g);
    CHECK_EQ(g.refract, 4);
    CHECK_EQ(g.clrbtn, 0);
}

static void test_sword_contact(void)
{
    guard_and_kid(5, POP_FACE_LEFT, 2);            /* kid at full thrust, guard en garde, 20 apart */
    g.kid.sword = g.shad.sword = 2;
    g.kid.posn = 154;
    g.shad.posn = 158;
    g.kid.x = (uint8_t)(g.shad.x - 20 + 13);
    pop_checkstrike(&g);
    CHECK_EQ(g.shad.action, 99);                   /* run through */
    pop_checkstab(&g);
    CHECK_EQ(g.ChgOppStr, 0xFF);                   /* one unit */
    CHECK_EQ(g.shad.posn, POP_SQ_STABBED);
    CHECK_EQ(g.refract, 20);                       /* dazed, by his type's table entry */
    CHECK_EQ(g.sq.items[0], POP_SND_SPLAT);
    pop_chgmeters(&g);
    CHECK_EQ(g.OppStrength, 2);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* the same thrust, blocked */
    g.kid.sword = g.shad.sword = 2;
    g.kid.posn = 154;
    g.shad.posn = 150;
    g.kid.x = (uint8_t)(g.shad.x - 20 + 13);
    pop_checkstrike(&g);
    CHECK_EQ(g.shad.action, 1);
    CHECK_EQ(g.shad.posn, 161);                    /* "successful block" */
    CHECK_EQ(g.kid.posn, POP_SQ_BLOCKEDSTRIKE);
    CHECK_EQ(g.justblocked, 0);                    /* (only a guard's own blocked strike sets it) */

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* out of reach: nothing */
    g.kid.sword = g.shad.sword = 2;
    g.kid.posn = 154;
    g.kid.x = (uint8_t)(g.shad.x - 29 + 13);
    pop_checkstrike(&g);
    CHECK_EQ(g.shad.action, 1);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* an unarmed kid dies of one thrust */
    g.shad.sword = 2;
    g.shad.posn = 154;
    g.kid.x = (uint8_t)(g.shad.x - 20 + 13);
    pop_checkstrike(&g);
    CHECK_EQ(g.kid.action, 99);
    pop_checkstab(&g);
    CHECK_EQ(g.ChgKidStr, 0xFD);
    CHECK_EQ(g.kid.posn, POP_SQ_STABKILL);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* both at once: the player wins the tie */
    g.kid.action = g.shad.action = 99;
    g.kid.sword = g.shad.sword = 2;
    pop_checkstab(&g);
    CHECK_EQ(g.kid.action, 1);
    CHECK_EQ(g.ChgKidStr, 0);
    CHECK_EQ(g.ChgOppStr, 0xFF);
}

static void test_meters(void)
{
    setup(3, POP_FACE_LEFT);
    g.KidStrength = 2;
    g.ChgKidStr = 0xFF;
    g.ChgOppStr = 5;                               /* above the maximum: not applied */
    pop_chgmeters(&g);
    CHECK_EQ(g.KidStrength, 1);
    CHECK_EQ(g.OppStrength, 3);
    pop_rechargemeter(&g);
    CHECK_EQ(g.ChgKidStr, 2);
    pop_boostmeter(&g);
    CHECK_EQ(g.MaxKidStr, 4);
    CHECK_EQ(g.ChgKidStr, 3);
    g.MaxKidStr = POP_MAXMAXSTR;
    pop_boostmeter(&g);
    CHECK_EQ(g.MaxKidStr, POP_MAXMAXSTR);
    pop_markmeters(&g);                            /* bottom row: blocks 20-22 and 28-29 */
    CHECK_EQ(g.rb[POP_RB_RED + 20], 2);
    CHECK_EQ(g.rb[POP_RB_RED + 22], 2);
    CHECK_EQ(g.rb[POP_RB_RED + 23], 0);
    CHECK_EQ(g.rb[POP_RB_RED + 28], 2);
    CHECK_EQ(g.rb[POP_RB_WHITE + 29], 4);
}

static void test_potions(void)
{
    setup(3, POP_FACE_LEFT);
    g.KidStrength = 2;
    g.lastpotion = 1;                              /* one unit back */
    pop_potioneffect(&g);
    CHECK_EQ(g.ChgKidStr, 1);
    CHECK_EQ(g.SongCue, POP_SONG_SHORTPOT);
    CHECK_EQ(g.lightning, 2);
    g.lastpotion = 2;                              /* a larger meter, filled */
    pop_potioneffect(&g);
    CHECK_EQ(g.MaxKidStr, 4);
    CHECK_EQ(g.ChgKidStr, 2);
    g.lastpotion = 3;                              /* weightless for 200 frames */
    pop_potioneffect(&g);
    CHECK_EQ(g.ag.weightless, 200);
    g.lastpotion = 4;                              /* the screen turns over */
    pop_potioneffect(&g);
    CHECK_EQ(g.invert, 0xFF);
    CHECK(strcmp(hooklog, "v") == 0);
    g.lastpotion = 5;                              /* poison */
    pop_potioneffect(&g);
    CHECK_EQ(g.ChgKidStr, 0xFF);
    g.lastpotion = 0xFF;                           /* "potion -1" is the sword */
    pop_potioneffect(&g);
    CHECK_EQ(g.gotsword, 1);
    CHECK_EQ(g.SongCue, POP_SONG_SWORD);

    setup(3, POP_FACE_LEFT);                       /* at full strength the small one does nothing */
    g.lastpotion = 1;
    pop_potioneffect(&g);
    CHECK_EQ(g.SongCue, 0);
}

static void test_checkalert(void)
{
    guard_and_kid(5, POP_FACE_LEFT, 2);            /* floor all the way between them */
    g.EnemyAlert = 0;
    pop_checkalert(&g);
    CHECK_EQ(g.EnemyAlert, 2);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* a gap: they see each other, no more */
    put(1, 1, 4, POP_SPACE, 0);
    pop_checkalert(&g);
    CHECK_EQ(g.EnemyAlert, 1);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* a wall: not even that */
    put(1, 1, 4, POP_BLOCK, 0);
    pop_checkalert(&g);
    CHECK_EQ(g.EnemyAlert, 0);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* a gate: by how far it is open */
    put(1, 1, 4, POP_GATE, 111);
    pop_checkalert(&g);
    CHECK_EQ(g.EnemyAlert, 1);
    put(1, 1, 4, POP_GATE, 112);
    pop_checkalert(&g);
    CHECK_EQ(g.EnemyAlert, 2);

    guard_and_kid(5, POP_FACE_LEFT, 2);            /* different rows */
    g.kid.block_y = 0;
    pop_checkalert(&g);
    CHECK_EQ(g.EnemyAlert, 0);
}

static void test_guards_on_screens(void)
{
    setup(3, POP_FACE_LEFT);
    g.level = 2;
    g.blueprint[POP_GD_BLOCK] = 15;                /* screen 1: a guard in row 1, column 5 */
    g.blueprint[POP_GD_X] = 255;                   /* as level files have it: not yet set */
    g.blueprint[POP_GD_SEQH] = 0xFF;
    g.blueprint[POP_GD_FACE] = 0xFF;
    g.blueprint[POP_GD_PROG] = 1;
    pop_initialguards(&g);
    CHECK_EQ(g.blueprint[POP_GD_X], 128 + 14);     /* middle of his block */
    CHECK_EQ(g.blueprint[POP_GD_SEQH], 0);

    g.guardprog = 0;
    pop_addguard(&g);                              /* the kid arrives on screen 1 */
    CHECK_EQ(g.shad.scrn, 1);
    CHECK_EQ(g.shad.x, 142);
    CHECK_EQ(g.shad.y, 118);
    CHECK_EQ(g.shad.block_x, 5);
    CHECK_EQ(g.shad.block_y, 1);
    CHECK_EQ(g.shad.id, 2);
    CHECK_EQ(g.shad.posn, POP_SQ_ALERTSTAND);
    CHECK_EQ(g.shad.life, 0xFF);
    CHECK_EQ(g.guardprog, 1);
    CHECK_EQ(g.GuardColor, 1);                     /* level 2: colour 0, type 1: the other one */
    /* Strength: 3 for the level, plus the extra unit of the type that was
     * in guardprog BEFORE this guard's own type was stored (type 0: none).
     * His own type would have given 4.  AUTO.S 1905 calls getgdstrength
     * before line 1928 sets guardprog. */
    CHECK_EQ(g.OppStrength, 3);
    CHECK_EQ(g.MaxOppStr, 3);

    /* the kid leaves; the guard, not en garde, stays and is remembered */
    g.kid = g.ch;
    g.kid.x = 50;                                  /* off the left edge */
    g.blueprint[POP_MAP + 0] = 2;                  /* screen 2 lies to the left */
    g.shad.x = 120;
    pop_cutcheck(&g);
    CHECK_EQ(g.kid.scrn, 2);
    CHECK_EQ(g.kid.x, 190);
    CHECK_EQ(g.cutscrn, 2);
    CHECK_EQ(g.shad.face, POP_NO_CHAR);
    CHECK_EQ(g.blueprint[POP_GD_X], 120);
    CHECK_EQ(g.blueprint[POP_GD_BLOCK], 10);       /* his row; the column is rebuilt from x */
    CHECK_EQ(g.OppStrength, 0);
}

static void test_cut(void)
{
    setup(3, POP_FACE_LEFT);
    g.blueprint[POP_MAP + 1] = 2;                  /* right of 1 */
    g.blueprint[POP_MAP + 2] = 3;                  /* above 1 */
    g.blueprint[POP_MAP + 3] = 4;                  /* below 1 */
    pop_cut(&g, 1);
    CHECK_EQ(g.ch.scrn, 2);
    CHECK_EQ(g.ch.x, (uint8_t)(107 - 140));
    g.ch.scrn = 1;
    pop_cut(&g, 2);                                /* up: three rows and 189 lines further down */
    CHECK_EQ(g.ch.scrn, 3);
    CHECK_EQ(g.ch.block_y, 4);
    CHECK_EQ(g.ch.y, (uint8_t)(118 + 189));
    g.ch.scrn = 1;
    pop_cut(&g, 3);
    CHECK_EQ(g.ch.scrn, 4);
    CHECK_EQ(g.ch.block_y, 1);
    CHECK_EQ(g.ch.y, 118);

    setup(3, POP_FACE_LEFT);                       /* no cut while standing well on screen */
    pop_setupchar(&g);
    pop_getedges(&g);
    CHECK_EQ(pop_cutchar(&g), 0xFF);
    g.ch.y = 220;                                  /* fallen below the bottom edge */
    g.blueprint[POP_MAP + 3] = 4;
    CHECK_EQ(pop_cutchar(&g), 3);
    CHECK_EQ(g.ch.y, 31);
}

static void test_script(void)
{
    /* One entry is taken per frame, as soon as the frame count has reached
     * its number: the entry for "frame 0" runs on the first call, and the
     * one for frame 1 on the second. */
    int i;
    setup(3, POP_FACE_LEFT);
    g.JSTKX = 0xFF;
    pop_autoplayback(&g, fx_script, sizeof fx_script);       /* call 1: entry 0, release */
    CHECK_EQ(g.JSTKX, 0);
    CHECK_EQ(g.PlayCount, 1);
    pop_autoplayback(&g, fx_script, sizeof fx_script);       /* call 2: forward */
    CHECK_EQ(g.JSTKX, 0xFF);
    g.JSTKX = 0;
    for (i = 0; i < 2; i++) pop_autoplayback(&g, fx_script, sizeof fx_script);
    CHECK_EQ(g.JSTKX, 0xFF);                       /* calls 3-4: the last command again */
    CHECK_EQ(g.btn, 0);
    pop_autoplayback(&g, fx_script, sizeof fx_script);       /* call 5: button */
    CHECK_EQ(g.btn, 0xFF);
    for (i = 0; i < 4; i++) pop_autoplayback(&g, fx_script, sizeof fx_script);
    CHECK_EQ(g.btn, 0);                            /* call 9: all released */
    CHECK_EQ(g.JSTKX, 0);
    for (i = 0; i < 300; i++) pop_autoplayback(&g, fx_script, sizeof fx_script);
    CHECK_EQ(g.PlayCount, 254);                    /* the count stops there, so an entry
                                                    * for frame 255 is never reached */
    CHECK_EQ(g.unmodelled, 0);
}

static void test_shadow_from_mirror(void)
{
    setup(3, POP_FACE_LEFT);
    g.kid = g.ch;
    g.createshad = 0xFF;                           /* the kid has just jumped through it */
    g.MaxKidStr = 5;
    g.KidStrength = 5;
    pop_reflection(&g);
    CHECK_EQ(g.createshad, 0);
    CHECK_EQ(g.shad.id, 1);
    CHECK_EQ(g.shad.face, POP_FACE_RIGHT);         /* his mirror image */
    CHECK_EQ(g.OppStrength, 5);                    /* takes his strength */
    CHECK_EQ(g.KidStrength, 1);                    /* and leaves him one unit */
    CHECK_EQ(g.sq.items[0], POP_SND_MIRRORCRACK);
}

static void test_small_things(void)
{
    setup(3, POP_FACE_LEFT);
    g.OppStrength = 3;
    pop_vanishchar(&g);
    CHECK_EQ(g.ch.face, POP_NO_CHAR);
    CHECK_EQ(g.ChgOppStr, 0xFD);

    setup(3, POP_FACE_LEFT);                       /* on "screen 0" a character dies */
    g.ch.scrn = 0;
    pop_kill0(&g);
    CHECK_EQ(g.ch.life, 0);
    CHECK_EQ(g.ch.posn, 185);
    CHECK_EQ(g.ChgKidStr, 0xFD);

    setup(3, POP_FACE_LEFT);
    pop_deathsong(&g);
    CHECK_EQ(g.SongCue, POP_SONG_ACCID);
    CHECK_EQ(g.SongCount, 255);
    g.heroic = 1;
    pop_deathsong(&g);
    CHECK_EQ(g.SongCue, POP_SONG_HEROIC);

    setup(3, POP_FACE_LEFT);                       /* a guard's death: the victory tune */
    g.ch.id = 2;
    pop_deadenemy(&g);
    CHECK_EQ(g.SongCue, POP_SONG_VICT);
}

int main(void)
{
    RUN(test_guard_notices);
    RUN(test_guard_fights);
    RUN(test_sword_contact);
    RUN(test_meters);
    RUN(test_potions);
    RUN(test_checkalert);
    RUN(test_guards_on_screens);
    RUN(test_cut);
    RUN(test_script);
    RUN(test_shadow_from_mirror);
    RUN(test_small_things);
    return pop_test_summary();
}
