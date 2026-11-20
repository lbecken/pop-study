#!/usr/bin/env python3
"""Does fuzz_control.py notice a wrong port?  A check of the test itself.

New code for the pop-study project.  Not part of CTest (it takes minutes).

Each entry below is one small deliberate error in pop_control.c,
pop_collide.c, pop_mover.c, pop_auto.c or pop_top.c: a threshold off by one, a comparison changed, a statement
removed.  The script builds the
runner with that one change and runs fuzz_control.py, which must then report
a mismatch.  A change the comparison does not notice is either a gap in the
test's inputs or a change that makes no difference; those of the second
kind are marked with the reason.  A third kind is hidden from the comparison
by its own rule of skipping cases the port cannot model; for those (marked
"unit:<test file>") the named unit test is built with the error instead and
must fail.

Usage:
    mutation_check.py <data dir> <original checkout> [cases per routine] [seed] [only]

`only` is a comma-separated list; just the changes in a file named there, or
whose original or replacement text contains one of the entries, are tried.

Needs a C compiler as `cc`.  The changes are tried in parallel, one per
processor.
"""

import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, "..")

# (text in pop_control.c, replacement, why it cannot matter or None)
MUTANTS = [
    ("STEP_OFF_BACK = 8", "STEP_OFF_BACK = 9", None),
    ("GRAB_LEAD = 25", "GRAB_LEAD = 24", None),
    ("EST_WIDTH = 13", "EST_WIDTH = 12", None),
    ("DEATH_VELOCITY = 33", "DEATH_VELOCITY = 34", None),
    ("OOF_VELOCITY = 22", "OOF_VELOCITY = 21", None),
    ("THINNER = 3", "THINNER = 2", None),
    ("GATE_MARGIN = 6", "GATE_MARGIN = 5", None),
    ("if ((POP_LOOSE & tempstate) != 0) return 0;", "if (tempstate != 0) return 0;", None),
    ("if (a < 127 - EST_WIDTH)", "if (a < 128 - EST_WIDTH)", None),
    ("bar_l[5] = { 0, 12, 2, 0, 0 };", "bar_l[5] = { 0, 12, 3, 0, 0 };",
     "cmpbarr never returns 2, so BarL[2] is never read"),
    ("if (g->rjumpflag == 44 && pop_getdist(g) < 6)",
     "if (g->rjumpflag == 44 && pop_getdist(g) < 7)", None),
    ("    if (x < 15) {                              /* running", "    if (x < 14) {  /*", None),
    ("if (pop_getdist(g) >= 12 && pop_getbehind(g) == POP_SPIKES)",
     "if (pop_getdist(g) >= 13 && pop_getbehind(g) == POP_SPIKES)", None),
    ("    if (atemp >= 4) goto longjump;", "    if (atemp >= 5) goto longjump;", None),
    ("if (g->rx != 1 && g->ch.repeat != 0) {", "if (g->rx != 1 && g->ch.repeat == 0) {", None),
    ("        if (op != 152) return;", "        if (op != 151) return;", None),
    ("if (pop_decstr(g, 1) == 0) goto hdland1;", "if (pop_decstr(g, 1) != 0) goto hdland1;", None),
    ("    g->stunned = STUN_TIME;", "    g->stunned = STUN_TIME + 1;", None),
    ("    if (a < 4 && g->rx == 1) {                 /* close", "    if (a < 4 && g->rx == 2) {  /*",
     None),
    ("if (a < SWORD_THRES_N && a >= SWORD_THRES) goto safe;",
     "if (a < SWORD_THRES_N && a > SWORD_THRES) goto safe;", None),
    ("    if (a == 3) a = 0xFF;", "    if (a == 3) a = 0xFE;",
     "topej is clamped below 192, so getblocky never returns 3 here"),
    ("    g->height = 35;", "    g->height = 34;", None),
    ("        if (lo == 0) hi = u8(hi + 1);\n", "\n",
     "lo is even before the increment, so it never wraps to 0"),
    ("    if (hi != 0 && lo >= 0xF0) hi = 0xFF;", "    ;", None),
    ("            g->droppedout = 0;\n", "\n", None),
    ("if (g->ch.posn != 7 && g->ch.posn != 11) return;",
     "if (g->ch.posn != 7 && g->ch.posn != 12) return;", None),
    ("if (g->ch.face != 0 && (pop_bluespec(g, g->ry) >> 2) < GCLIMB_THRES) goto fail;",
     "if (g->ch.face != 0 && (pop_bluespec(g, g->ry) >> 2) <= GCLIMB_THRES) goto fail;", None),
    ("        if (a < 0x80) return;", "        if (a < 0x7F) return;",
     "the distance arithmetic gives -14..13 only, never $7F"),
    ("a = u8(a + 14u * g->bufindex - RJ_LEAD_DIST);",
     "a = u8(a + 13u * g->bufindex - RJ_LEAD_DIST);", None),
    ("        if (a >= 0xFA) {", "        if (a >= 0xFB) {", None),
    ("if (u8(g->ch.y + GRAB_LEAD) < floor_under(g)) return;",
     "if (u8(g->ch.y + GRAB_LEAD) <= floor_under(g)) return;", None),
    ("if (g->ch.y_vel >= GRAB_SPEED) return;", "if (g->ch.y_vel > GRAB_SPEED) return;", None),
    ("        if (pop_getspikes(g) < 2) return;", "        if (pop_getspikes(g) < 1) return;", None),
    ("if (pop_getopdist(g) >= BLOCK_THRES) goto blockmiss;",
     "if (pop_getopdist(g) > BLOCK_THRES) goto blockmiss;", None),
    ("    if (a >= 0xFC) goto onalert;", "    if (a >= 0xFD) goto onalert;", None),
]


# The same for pop_collide.c.
COLLIDE_MUTANTS = [
    ("    if (a >= 11) a = 11;", "    if (a >= 12) a = 12;", None),
    ("v = (a < g->CDRightEj) ? 0x0F : 0x00;", "v = (a <= g->CDRightEj) ? 0x0F : 0x00;", None),
    ("if (a > g->CDLeftEj) v |= 0xF0;", "if (a >= g->CDLeftEj) v |= 0xF0;", None),
    ("u8(this_y - 2) == last_y", "u8(this_y - 1) == last_y", None),
    ("u8(this_y + 3) == last_y", "u8(this_y + 2) == last_y", None),
    ("if (g->ch.posn >= 135 && g->ch.posn < 149) return;",
     "if (g->ch.posn >= 135 && g->ch.posn < 148) return;", None),
    ("u8(floor - g->ch.y) >= 15", "u8(floor - g->ch.y) >= 16", None),
    ("    if (g->ch.y_vel >= OOF_VELOCITY) {", "    if (g->ch.y_vel > OOF_VELOCITY) {", None),
    ("(posn >= 40 && posn < 43)", "(posn >= 40 && posn < 44)", None),
    ("    g->ch.x = pop_add_char_x(&g->ch, 0xFC);", "    g->ch.x = pop_add_char_x(&g->ch, 0xFD);",
     None),
    ("if (pop_bluespec(g, g->ry) != POP_SLICER_EXT) return 0;",
     "if ((pop_bluespec(g, g->ry) & 0x7Fu) != POP_SLICER_EXT) return 0;", None),
    ("g->ch.posn >= 39 && g->ch.posn < 44", "g->ch.posn >= 39 && g->ch.posn < 43", None),
    ("(g->tempblockx > g->collX ? 5 : 0xFB)", "(g->tempblockx >= g->collX ? 5 : 0xFB)", None),
    ("(*cd_at(g, POP_CD_THIS, x) & *cd_at(g, POP_CD_LAST, x)) != 0xFF",
     "(*cd_at(g, POP_CD_THIS, x) | *cd_at(g, POP_CD_LAST, x)) != 0xFF", None),
    (">= g->CDRightEj) {\n        return 0;", "> g->CDRightEj) {\n        return 0;", None),
    ("<= g->CDLeftEj) {\n        return 0;", "< g->CDLeftEj) {\n        return 0;", None),
    ("    if (NEG(g->ch.face)) {                     /* :checkR", "    if (!NEG(g->ch.face)) {  /*",
     None),
    ("    if (g->ch.sword < 2) return;", "    if (g->ch.sword < 3) return;", None),
    ("if (s == g->scrnLeft || s == g->scrnBelowL) return", "if (s == g->scrnLeft) return", None),
    ("(g->level == 4) ? POP_SONG_SHADOW", "(g->level == 5) ? POP_SONG_SHADOW", None),
    ("    if (g->ch.posn == 177) return;", "    if (g->ch.posn == 178) return;", None),
    ("        if (id == POP_BLOCK) {                 /* no floor", "        if (id == POP_GATE) {  /*",
     None),
    ("    if (g->ch.life == 0) return;               /* :deadbump */", "    ;",
     "collide has already returned for every CharLife >= 0, so :deadbump is never reached"),
    ("        if (coll_face != g->ch.face) {", "        if (coll_face == g->ch.face) {", None),
]

# The same for pop_mover.c.
MOVER_MUTANTS = [
    ("g->RNDseed * 5u + 23u", "g->RNDseed * 5u + 24u", None),
    ("    if (g->numtrans == MAXTR) return;", "    if (g->numtrans == MAXTR - 1) return;", None),
    ("if (s != 0 && s < POP_SLICER_RET) return;  /* in mid-slice",
     "if (s != 0 && s <= POP_SLICER_RET) return;  /*", None),
    ("    if (s == 0xFF) return;                     /* jammed */", "    ;", None),
    ("    if (!NEG(s) && s != 0) return;             /* already triggered",
     "    if (s != 0) return;  /*", None),
    ("if (pop_bluetype(g, g->ry) & POP_REQMASK) return;", ";", None),
    ("    if (t == 31) return;                       /* permanently down */", "    ;", None),
    ("    if (t < 2) {                               /* a fresh plate */", "    if (t < 3) {",
     None),
    ("        if (a < POP_GMAXVAL) return;\n        pop_set_bluespec(g, g->ry, POP_GATETIMER);",
     "        if (a <= POP_GMAXVAL) return;\n        pop_set_bluespec(g, g->ry, POP_GATETIMER);",
     None),
    ("    if (a == 0) { stopobj(g); return; }        /* :lower", "    if (a == 1) { stopobj(g); return; }  /*",
     None),
    ("TRDIREC = (pop_bluespec(g, g->ry) != 0) ? 0xFF : 1;",
     "TRDIREC = (pop_bluespec(g, g->ry) != 0) ? 1 : 0xFF;", None),
    ("        if (g->blueprint[POP_LINKLOC + x] & 0x80u) return;", "        if (g->blueprint[POP_LINKLOC + x] & 0x40u) return;",
     None),
    ("unsigned lo = (g->blueprint[POP_LINKLOC + x] & 0x60u) >> 2;",
     "unsigned lo = (g->blueprint[POP_LINKLOC + x] & 0x60u) >> 3;", None),
    ("    return before < gatevel[x];", "    return before <= gatevel[x];", None),
    ("            if (g->mv_state >= POP_EMAXVAL) {", "            if (g->mv_state > POP_EMAXVAL) {", None),
    ("        if (a < POP_GMAXVAL) pop_addlowersound(g, a);", "        if (a <= POP_GMAXVAL) pop_addlowersound(g, a);",
     "the sound is only made for odd states and the limit, 188, is even"),
    ("    if (t >= 2) return;                        /* the timer stops at 1 */", "    if (t >= 1) return;",
     None),
    ("        if (n >= POP_SLICETIMER + 1) n = 1;", "        if (n >= POP_SLICETIMER) n = 1;", None),
    ("&& (NEG(g->kid.life) || (g->mv_state & 0x80u)))) {", "&& NEG(g->kid.life))) {", None),
    ("    return frame >= POP_BUBB_LAST + 1 ? 1 : frame;", "    return frame >= POP_BUBB_LAST ? 1 : frame;",
     None),
    ("    if (r != state && r < POP_TORCH_LAST + 1) return r;", "    if (r < POP_TORCH_LAST + 1) return r;",
     None),
    ("    if (a == POP_SPIKE_EXT) {", "    if (a == POP_SPIKE_EXT + 1) {", None),
    ("        if ((g->mv_state & 0x7Fu) != 0) return;", "        if (g->mv_state != 0) return;", None),
    ("        if (a >= WIGGLETIME + 0x80) {", "        if (a > WIGGLETIME + 0x80) {", None),
    ("    if (a < POP_FFALLING) goto red;", "    if (a <= POP_FFALLING) goto red;", None),
    ("    MOBY = pop_blockbot(g, u8(row + 1));", "    MOBY = pop_blockbot(g, row);", None),
    ("    pop_set_bluespec(g, TRLOC, g->mv_state);\n    g->ry = TRLOC;",
     "    pop_calcblue(g, TRSCRN);\n    pop_set_bluespec(g, TRLOC, g->mv_state);\n    g->ry = TRLOC;",
     None),
    ("    if (a < FF_TERMVEL) {", "    if (a <= FF_TERMVEL) {", None),
    ("        if (MOBY >= 192 + 17) MOBVEL", "        if (MOBY > 192 + 17) MOBVEL", None),
    ("    if (a >= 0xE2) return;", "    if (a >= 0xE3) return;", None),
    ("    MOBVEL >>= 1;", "    ;", None),
    ("    MOBY = u8(MOBY + 6);", "    MOBY = u8(MOBY + 5);", None),
    ("    if (MOBLEVEL < 3) return;", "    if (MOBLEVEL < 4) return;", None),
    ("        if (id == POP_UPRESSPLATE) pop_set_bluetype(g, g->ry, POP_RUBBLE);", "        ;", None),
    ("if (g->level != 13 && g->ch.posn >= 5 && g->ch.posn < 15) return;",
     "if (g->level != 13 && g->ch.posn >= 5 && g->ch.posn < 16) return;", None),
    ("    if (MOBY >= g->ch.y) return;", "    if (MOBY > g->ch.y) return;", None),
    ("    if (u8(g->ch.y - CRUSH_DIST) >= MOBY) return;", "    if (u8(g->ch.y - CRUSH_DIST) > MOBY) return;",
     None),
    ("    if ((p >= 87 && p < 100) || (p >= 135 && p < 141)) {", "    if ((p >= 87 && p < 100) || (p >= 135 && p < 140)) {",
     None),
    ("        if (a != 7 && a != 5 && a >= 2) return;", "        if (a != 7 && a >= 2) return;", None),
    ("            if (g->tempscrn == 0 || g->tempscrn != g->ch.scrn) break;", "            if (g->tempscrn == 0) break;",
     None),
    ("    pop_shakem(g, NEG(jar) ? g->ch.block_y : u8(g->ch.block_y - 1));",
     "    pop_shakem(g, NEG(jar) ? u8(g->ch.block_y - 1) : g->ch.block_y);", None),
    ("        state = u8(state - SLICERSYNC);", "        state = u8(state - SLICERSYNC - 1);", None),
    ("    if (g->VisScrn != 23 && g->VisScrn != 16) return;", "    if (g->VisScrn != 23) return;", None),
    ("    if (y == 9 || y == 19 || y == 29) return vis_no(g);\n        g->ry = u8(y + 1);",
     "    if (y == 9 || y == 19) return vis_no(g);\n        g->ry = u8(y + 1);", None),
    ("    if (TRSCRN != g->scrnAbove) return TRSCRN >= g->scrnAbove;",
     "    if (TRSCRN != g->scrnAbove) return 1;", None),
]

# The same for pop_auto.c.
AUTO_MUTANTS = [
    ("    g->ChgOppStr = u8(0 - g->OppStrength);", "    g->ChgOppStr = u8(1 - g->OppStrength);", None),
    ("    if (g->MaxKidStr < POP_MAXMAXSTR) g->MaxKidStr++;", "    if (g->MaxKidStr <= POP_MAXMAXSTR) g->MaxKidStr++;",
     None),
    ("void pop_markoppmeter(pop_game *g) { mark_from(g, 28, 2); }",
     "void pop_markoppmeter(pop_game *g) { mark_from(g, 27, 2); }", None),
    ("    } else if (g->ch.id != 1) {                /* (no tune for the shadow) */", "    } else {", None),
    ("    else if (g->heroic != 0) song = POP_SONG_HEROIC;", "    else if (g->heroic == 0) song = POP_SONG_HEROIC;",
     None),
    ("    if (!NEG(g->ch.life) || g->ch.scrn != 0) return;", "    if (g->ch.scrn != 0) return;", None),
    ("    if (a <= g->MaxKidStr) g->KidStrength = a;", "    if (a < g->MaxKidStr) g->KidStrength = a;", None),
    ("        if (NEG(g->ChgKidStr)) g->ChgOppStr = g->ChgKidStr;", "        if (g->ChgKidStr) g->ChgOppStr = g->ChgKidStr;",
     None),
    ("    if (pop_getopdist(g) < 0xF1) return;", "    if (pop_getopdist(g) < 0xF2) return;", None),
    ("        if (g->KidStrength == g->MaxKidStr) return;", "        ;", None),
    ("        g->ag.weightless = 200;", "        g->ag.weightless = 199;", None),
    ("    if (g->ch.id == 4) goto wounded;", "    if (g->ch.id == 2) goto wounded;", None),
    ("        if (d >= 4) {\n            g->ch.x = pop_add_char_x(&g->ch, u8(d - 14));",
     "        if (d >= 5) {\n            g->ch.x = pop_add_char_x(&g->ch, u8(d - 14));", None),
    ("    if (!NEG(g->ch.face)) d = u8((d ^ 0xFFu) + 14);", "    if (NEG(g->ch.face)) d = u8((d ^ 0xFFu) + 14);",
     None),
    ("    g->dmirr = u8(d - 2);", "    g->dmirr = u8(d - 3);", None),
    ("    if (top >= g->FCharY) return;", "    if (top > g->FCharY) return;", None),
    ("    if (g->kid.block_x != POP_SKELTRIG && g->kid.block_x != POP_SKELTRIG + 1) return;",
     "    if (g->kid.block_x != POP_SKELTRIG) return;", None),
    ("    if (id != POP_BONES) return;", "    ;", None),
    ("    if (g->shad.id == 1 && g->level != 12) return;", "    if (g->shad.id == 1) return;", None),
    ("    if (!NEG(g->kid.life & g->shad.life)) return;", "    if (!NEG(g->kid.life | g->shad.life)) return;",
     None),
    ("    if (RD(lo) == POP_SLICER) lo = u8(lo + 14);", "    ;", None),
    ("    if (RD(hi) == POP_GATE) hi = u8(hi - 14);", "    ;", None),
    ("(id == POP_GATE && pop_bluespec(g, g->ry) < 28 * 4)", "(id == POP_GATE && pop_bluespec(g, g->ry) <= 28 * 4)",
     None),
    ("    if (g->guardprog != 0 && g->gdtimer != 0) return;", "    if (g->gdtimer != 0) return;", None),
    ("    if (p != 152 && p != 153 && p != 162) return;", "    if (p != 152 && p != 153) return;", None),
    ("g->justblocked ? POP_FT_IMPBLOCK : POP_FT_BLOCK", "g->justblocked ? POP_FT_BLOCK : POP_FT_IMPBLOCK", None),
    ("    if (g->op.posn == 169 || g->op.posn == 151) return;", "    if (g->op.posn == 169) return;", None),
    ("(g->ch.posn == 161 || g->ch.posn == 150) ? POP_FT_RESTRIKE", "(g->ch.posn == 161) ? POP_FT_RESTRIKE",
     None),
    ("    if (d < POP_BLOCKTHRES1 || d >= POP_BLOCKRANGE2) { maybe_advance(g); return; }",
     "    if (d < POP_BLOCKTHRES1 + 1 || d >= POP_BLOCKRANGE2) { maybe_advance(g); return; }",
     "GenFight is only reached at 12 or more, so its lower limit of 10 never decides"),
    ("    if (d < POP_STRIKERANGE1 || d >= POP_STRIKERANGE2) { maybe_advance(g); return; }",
     "    if (d < POP_STRIKERANGE1 || d > POP_STRIKERANGE2) { maybe_advance(g); return; }",
     "the test before it has already sent 29 and above elsewhere"),
    ("    if (pop_getopdist(g) < POP_STRIKERANGE2) do_press(g);\n    else do_fwd(g);",
     "    if (pop_getopdist(g) <= POP_STRIKERANGE2) do_press(g);\n    else do_fwd(g);", None),
    ("    if (g->op.action == 2 || g->op.action == 6) return;", "    if (g->op.action == 2) return;", None),
    ("    if (id == POP_SPIKES || id == POP_LOOSE || pop_cmpbarr(id) != 0) goto stopped;",
     "    if (id == POP_SPIKES || pop_cmpbarr(id) != 0) goto stopped;", None),
    ("    if (u8(g->ch.block_y + 1) != g->op.block_y) goto stopped;", "    ;", None),
    ("    if (p == 166 || p < 150) return;", "    if (p < 150) return;", None),
    ("        if (g->ch.id == 4) return;             /* (a skeleton", "        /* (a skeleton", None),
    ("    if (!NEG(d) && d >= 12 && g->op.posn >= 102 && g->op.posn < 118 && g->op.action == 5) return;",
     "    if (!NEG(d) && d >= 13 && g->op.posn >= 102 && g->op.posn < 118 && g->op.action == 5) return;",
     None),
    ("    if (d >= POP_TOOFAR) {", "    if (d > POP_TOOFAR) {", None),
    ("            if (p >= 7 && p < 15) {", "            if (p >= 7 && p < 14) {", None),
    ("                if (pop_getopdist(g) < POP_RUNTHRES) do_press(g);",
     "                if (pop_getopdist(g) <= POP_RUNTHRES) do_press(g);", None),
    ("                if (pop_getopdist(g) < POP_JUMPTHRES) do_press(g);",
     "                if (pop_getopdist(g) <= POP_JUMPTHRES) do_press(g);", None),
    ("(g->ch.sword >= 2 ? POP_TOOCLOSE : POP_OFFGUARDTHRES)) {", "(g->ch.sword >= 2 ? POP_TOOCLOSE : POP_TOOCLOSE)) {",
     "EnGarde is only ever entered with the sword out, so its off-guard threshold is never used"),
    ("    if (g->op.block_y == g->ch.block_y && d >= 0xF8) goto eng;", "    if (d >= 0xF8) goto eng;", None),
    ("        if (d >= 0xFC) return;                 /* overlapping", "        if (d >= 0xFD) return;  /*", None),
    ("    if (g->level == 13 && g->SongCue != 0) return;", "    ;", None),
    ("        if (g->op.x >= 150) {", "        if (g->op.x > 150) {", None),
    ("        if ((p >= 3 && p < 15) || (p >= 127 && p < 133)) do_fwd(g);",
     "        if ((p >= 3 && p < 15) || (p >= 127 && p < 132)) do_fwd(g);", None),
    ("        if (g->ch.x < 80) pop_vanishchar(g);", "        if (g->ch.x <= 80) pop_vanishchar(g);", None),
    ("            if (pop_bluespec(g, g->ry) < 20) return;", "            if (pop_bluespec(g, g->ry) <= 20) return;",
     None),
    ("        if (g->kid.posn != 43 || g->kid.x >= 0x80) return;", "        if (g->kid.posn != 43) return;", None),
    ("        if (g->ch.x >= 166) return;", "        if (g->ch.x > 166) return;", None),
    ("    if (g->justblocked != 0) g->justblocked--;", "    ;", None),
    ("    if (NEG(g->ch.life) && g->OppStrength == 0) {", "    if (g->OppStrength == 0) {", None),
    ("        if (g->ch.x >= POP_SCRN_LEFT - 14 && g->ch.x < SCRN_RIGHT + 14) {",
     "        if (g->ch.x >= POP_SCRN_LEFT - 14 && g->ch.x <= SCRN_RIGHT + 14) {", None),
    ("    if (g->PlayCount >= 254) return;", "    if (g->PlayCount >= 255) return;", None),
    ("    if (g->PlayCount >= prog[y]) {", "    if (g->PlayCount > prog[y]) {", None),
    ("    if (d < POP_BLOCKRANGE2 && (g->op.posn == 161 || g->op.posn == 150)) {",
     "    if (d <= POP_BLOCKRANGE2 && (g->op.posn == 161 || g->op.posn == 150)) {", None),
    ("        if (g->ch.id != 0) g->justblocked = POP_BLOCKTIME;", "        g->justblocked = POP_BLOCKTIME;",
     None),
    ("    if (d < (g->op.sword >= 2 ? POP_STRIKERANGE1 : POP_OFFGUARDTHRES)) return;",
     "    if (d < (g->op.sword >= 2 ? POP_STRIKERANGE1 : POP_OFFGUARDTHRES + 1)) return;", None),
    ("        if (g->kid.action == 99) g->kid.action = 1;", "        ;", None),
    ("        g->ch.y = u8(g->ch.y - 189);", "        g->ch.y = u8(g->ch.y - 192);", None),
    ("        if (y < TOP_CUT_EDGE_PL || y >= TOP_CUT_EDGE_MI) { dir = 2; goto cut; }",
     "        if (y <= TOP_CUT_EDGE_PL || y >= TOP_CUT_EDGE_MI) { dir = 2; goto cut; }", None),
    ("        if (g->level == 6 && g->ch.scrn == 1) return 0xFF;", "        ;", None),
    ("(p >= 150 && p < 163)", "(p >= 150 && p < 162)", None),
    ("        if (g->leftej <= LEFT_CUT_EDGE) goto left;", "        if (g->leftej < LEFT_CUT_EDGE) goto left;", None),
    ("    if (g->rightej <= POP_SCRN_LEFT - 1) goto left;", "    if (g->rightej < POP_SCRN_LEFT - 1) goto left;",
     None),
    ("    if (g->exitopen == 0 || g->exitopen == 77 || g->level != 4) return;",
     "    if (g->exitopen == 0 || g->level != 4) return;", None),
    ("    if (NEG(g->shad.life)) {\n        *gd(g, POP_GD_SEQH, s) = 0;", "    if (!NEG(g->shad.life)) {\n        *gd(g, POP_GD_SEQH, s) = 0;",
     None),
    ("        if (dir == 0) transfer = g->shad.x < 256 - SCRN_WIDTH - 25;",
     "        if (dir == 0) transfer = g->shad.x <= 256 - SCRN_WIDTH - 25;", None),
    ("        else if (dir == 1) transfer = g->shad.x >= SCRN_WIDTH + 25;",
     "        else if (dir == 1) transfer = g->shad.x > SCRN_WIDTH + 25;", None),
    ("    if (g->shad.face == POP_NO_CHAR || g->shad.y < BOT_CUT_EDGE) return;",
     "    if (g->shad.face == POP_NO_CHAR || g->shad.y <= BOT_CUT_EDGE) return;", None),
    ("        if (g->shad.scrn == 3) {", "        if (g->shad.scrn == 4) {", None),
    ("    if (b >= 30) return;                       /* no guard on this screen */", "    if (b > 30) return;", None),
    ("    g->ch.id = (g->level == 3) ? 4 : 2;", "    g->ch.id = (g->level == 4) ? 4 : 2;", None),
    ("    if (g->ch.posn == 185 || g->ch.posn == 177 || g->ch.posn == 178) {",
     "    if (g->ch.posn == 185 || g->ch.posn == 177) {", None),
    ("                                           + fight(g, POP_FT_EXTRASTR, g->guardprog));",
     "                                           + fight(g, POP_FT_EXTRASTR, *gd(g, POP_GD_PROG, g->VisScrn)));",
     None),
    ("    g->guardprog = prog < POP_NUMPROGS ? prog : 3;", "    g->guardprog = prog <= POP_NUMPROGS ? prog : 3;", None),
    ("        if (g->exitopen != 0 || g->mergetimer != 0 || g->VisScrn != POP_SWORDSCRN) return;",
     "        if (g->exitopen != 0 || g->VisScrn != POP_SWORDSCRN) return;", None),
    ("        *gd(g, POP_GD_SEQH, scrn) = 0;\n    }\n}", "    }\n}", None),
]

# The same for pop_top.c.
TOP_MUTANTS = [
    ("return (posn == 185 || posn == 177 || posn == 178) ? 0 : 1;",
     "return (posn == 185 || posn == 177) ? 0 : 1;", None),
    ("posn == 229 || posn == 109 || posn == 171", "posn == 229 || posn == 110 || posn == 171", None),
    ("    for (x = 0; x < 10; x++) {\n        g->SN[POP_CD_LAST]",
     "    for (x = 0; x < 9; x++) {\n        g->SN[POP_CD_LAST]", None),
    ("    g->BlockYlast = 0xFF;\n", "", None),
    ("    g->scrnBelowL = pop_getleft(g, g->scrnBelow);", "    g->scrnBelowL = pop_getleft(g, g->scrnAbove);", None),
    ("    g->scrnAboveR = pop_getright(g, g->scrnAbove);", "    g->scrnAboveR = pop_getleft(g, g->scrnAbove);", None),
    ("        if (a < POP_WTLFLASH) x = g->vibes ^ 0xFF;", "        if (a <= POP_WTLFLASH) x = g->vibes ^ 0xFF;", None),
    ("    if (a != 0) {\n        x = 0xFF;", "    if (a != 1) {\n        x = 0xFF;", None),
    ("        if (g->mergetimer == 0) g->mergetimer = 0xFF;", "", None),
    ("    if (g->mergetimer != 0 && !NEG(g->mergetimer)) {", "    if (g->mergetimer != 0) {", None),
    ("    if (g->level == 8 && g->ch.scrn == 16 && g->exitopen != 0) {",
     "    if (g->level == 8 && g->ch.scrn == 15 && g->exitopen != 0) {", None),
    ("        if (g->exitopen < POP_MOUSETIMER) {", "        if (g->exitopen <= POP_MOUSETIMER) {", None),
    ("            pop_mouserescue(g);\n            g->exitopen++;", "            pop_mouserescue(g);", None),
    ("    if (g->kid.posn == 167) pop_g_addsound(g, POP_SND_SWORDCLASH1);",
     "    if (g->kid.posn == 168) pop_g_addsound(g, POP_SND_SWORDCLASH1);", None),
    ("    else if (g->shad.posn == 167)", "    if (g->shad.posn == 167)", None),
    ("        g->clrF = 0xFF;\n        g->JSTKX = 0xFF;", "        g->clrF = 0xFF;\n        g->JSTKX = 1;", None),
    ("        g->guardprog = 10;\n", "        g->guardprog = 9;\n", None),
    ("        pop_autoctrl(g);\n        g->guardprog = 11;", "        pop_autoctrl(g);", None),
    ("    if (g->cutscrn == 0 || g->cutscrn == g->VisScrn) return;",
     "    if (g->cutscrn == g->VisScrn) return;",
     "unit:test_top.c: the wrong port goes on to read screen 0, which is outside the"
     " modelled memory, so the comparison skips the case"),
    ("    if (g->VisScrn == 5 && g->level == 14) {", "    if (g->VisScrn == 5 && g->level == 13) {", None),
    ("    g->cutplan = 1;\n", "", None),
    ("    pop_addtorches(g);\n    pop_crumble(g);", "    pop_addtorches(g);", None),
    ("    pop_crumble(g);\n    pop_addguard(g);", "    pop_crumble(g);", None),
    ("    g->cutscrn = g->kid.scrn;\n    pop_prepcut(g);", "    g->cutscrn = g->ch.scrn;\n    pop_prepcut(g);", None),
    ("    pop_kill0(g);\n    pop_playerctrl(g);", "    pop_playerctrl(g);", None),
    ("    if (pop_cold(g->ch.posn) != 0) return;", "    if (pop_static(g->ch.posn) != 0) return;", None),
    ("    if (g->ch.life == 0) pop_deathsong(g);", "    if (g->ch.life == 1) pop_deathsong(g);", None),
    ("    if (g->ch.life < POP_DEADENOUGH) {", "    if (g->ch.life <= POP_DEADENOUGH) {", None),
    ("    if (g->level == 0) { HOOK(go_attract); return; }         /* his death",
     "    if (g->level == 1) { HOOK(go_attract); return; }  /*", None),
    ("    if (g->SongCue != 0) return;               /* let the song", "    /*", None),
    ("    if ((g->MinLeft | g->SecLeft) == 0) { HOOK(you_lose); return; }",
     "    if ((g->MinLeft & g->SecLeft) == 0) { HOOK(you_lose); return; }", None),
    ("    if (g->message == POP_MSG_CONT && g->msgtimer != 0) {", "    if (g->msgtimer != 0) {", None),
    ("        g->msgtimer = 255;\n        a = 255;", "        g->msgtimer = 254;\n        a = 255;", None),
    ("    if (a == 1) { HOOK(go_attract); return; }", "    if (a == 2) { HOOK(go_attract); return; }", None),
    ("    if (!NEG(g->BTN0 | g->BTN1)) return;", "    if (!NEG(g->BTN0)) return;", None),
    ("    pop_unholy(g);\n    pop_ctrlplayer(g);", "    pop_ctrlplayer(g);", None),
    ("    if (g->invert != 0 && !NEG(g->ch.life)) {", "    if (g->invert != 0) {", None),
    ("        g->redrawflg = 2;\n        g->invert = 0;", "        g->redrawflg = 1;\n        g->invert = 0;", None),
    ("        HOOK(invert_y);\n        return;", "        HOOK(invert_y);", None),
    ("    pop_wtlessflash(g);\n    if (g->ch.scrn != 0) {", "    if (g->ch.scrn != 0) {", None),
    ("        pop_firstguard(g);\n", "", None),
    ("        pop_checkgate(g);\n        pop_checkfloor(g);", "        pop_checkfloor(g);\n        pop_checkgate(g);", None),
    ("        pop_checkimpale(g);\n        pop_checkslice(g);", "        pop_checkslice(g);\n        pop_checkimpale(g);", None),
    ("        pop_shakeloose(g);\n    }\n    pop_savekid(g);", "    }\n    pop_savekid(g);", None),
    ("    pop_animmobs(g);\n    pop_animtrans(g);", "    pop_animtrans(g);\n    pop_animmobs(g);", None),
    ("    pop_bonesrise(g);\n    pop_checkalert(g);", "    pop_checkalert(g);", None),
    ("    pop_dokid(g);\n    pop_doshad(g);", "    pop_doshad(g);\n    pop_dokid(g);", None),
    ("    pop_checkstab(g);\n    pop_addsfx(g);\n    pop_chgmeters(g);",
     "    pop_checkstab(g);\n    pop_chgmeters(g);", None),
    ("    pop_cutcheck(g);\n    pop_prepcut(g);\n    pop_cutguard(g);",
     "    pop_cutcheck(g);\n    pop_cutguard(g);\n    pop_prepcut(g);", None),
    ("        if (g->kid.scrn == 24) { HOOK(go_attract); return; }",
     "        if (g->kid.scrn == 23) { HOOK(go_attract); return; }", None),
    ("        if (g->kid.scrn == 1 && g->kid.y < 20) {", "        if (g->kid.scrn == 1 && g->kid.y < 21) {", None),
    ("            g->kid.y = 0xFF;\n", "", None),
    ("        if (g->kid.scrn == 23) {\n            g->NextLevel++;",
     "        if (g->kid.scrn == 22) {\n            g->NextLevel++;", None),
    ("            g->skipmessage = 1;", "            g->skipmessage = 0;", None),
    ("    if (g->level < 13 || (g->level == 13 && g->exitopen == 0)) HOOK(keep_time);",
     "    if (g->level < 13 || g->level == 13) HOOK(keep_time);", None),
    ("    if (g->level >= 13) return;                /* one chance", "    if (g->level > 13) return;  /*", None),
    ("    if ((g->MinLeft | g->SecLeft) != 0) return;\n    HOOK(you_lose);",
     "    if (g->MinLeft != 0) return;\n    HOOK(you_lose);", None),
    ("initsettings[2] = { POP_GMAXVAL, 0 };", "initsettings[2] = { POP_GMAXVAL, 1 };", None),
    ("uint8_t scrn = u8(g->blueprint[POP_INFO] - 1);", "uint8_t scrn = u8(g->blueprint[POP_INFO]);", None),
    ("            } else if (id == POP_LOOSE) {\n                pop_set_bluespec(g, u8(y), 0);",
     "            } else if (id == POP_LOOSE) {", None),
    ("pop_set_bluespec(g, u8(y), u8(spec << 5));", "pop_set_bluespec(g, u8(y), u8(spec << 4));", None),
    ("    g->ch.y = pop_floory(g, u8(g->ch.block_y + 1));", "    g->ch.y = pop_floory(g, g->ch.block_y);", None),
    ("    g->ch.life = 0xFF;\n    g->ch.id = 0;", "    g->ch.life = 0xFF;", None),
    ("    g->droppedout = 0;\n    g->ch.sword = 0;", "    g->droppedout = 0;", None),
    ("    g->offguard = 0;\n    pop_g_animchar(g);", "    g->offguard = 0;", None),
    ("        if (!NEG(g->yellowflag)) g->timebomb = 0x40;", "        if (NEG(g->yellowflag)) g->timebomb = 0x40;", None),
    ("        if (g->ch.scrn == 17) pop_cut(g, 3);", "        if (g->ch.scrn == 17) pop_cut(g, 2);", None),
    ("    if (g->level == 3 && g->milestone != 0) {", "    if (g->level == 3) {", None),
    ("        g->blueprint[POP_KIDSTART_BLOCK] = 6;", "        g->blueprint[POP_KIDSTART_BLOCK] = 7;", None),
    ("        (void)pop_rdblock(g, 7, 4, 0);", "        (void)pop_rdblock(g, 7, 5, 0);", None),
    ("    g->ch.x = u8(pop_getblockej(g, g->ch.block_x) + POP_ANGLE + 7);",
     "    g->ch.x = u8(pop_getblockej(g, g->ch.block_x) + POP_ANGLE + 6);", None),
    ("    g->ch.face = g->blueprint[POP_KIDSTART_FACE] ^ 0xFF;", "    g->ch.face = g->blueprint[POP_KIDSTART_FACE];", None),
    ("(g->level == 0) ? 4 : g->origstrength;", "(g->level == 0) ? 3 : g->origstrength;", None),
    ("        (void)pop_rdblock(g, 5, 2, 0);\n        pop_pushpp(g);", "        (void)pop_rdblock(g, 5, 2, 0);", None),
    ("    } else if (g->level == 13) {\n        pop_g_jumpseq(g, POP_SQ_RUNNING);",
     "    } else if (g->level == 12) {\n        pop_g_jumpseq(g, POP_SQ_RUNNING);", None),
    ("        if ((pop_bluetype(g, u8(y)) & POP_IDMASK) == POP_EXIT) {",
     "        if ((pop_bluetype(g, u8(y)) & POP_IDMASK) == POP_EXIT2) {", None),
    ("    g->exitopen = 0;\n    g->lightning = 0;", "    g->exitopen = 0;", None),
    ("    g->numtrans = 0;\n    g->nummob = 0;", "    g->numtrans = 0;", None),
    ("    g->OppStrength = 0;                        /* no opponent */\n", "", None),
    ("    g->PreRecPtr = 0;\n    g->PlayCount = 0;", "    g->PreRecPtr = 0;", None),
    ("    if (g->SongCue != POP_SONG_DANGER) g->SongCue = 0;", "    g->SongCue = 0;", None),
    ("    pop_zerosound(&g->sq);\n", "", None),
    ("    g->gotsword = 1;\n    g->cutorder = 0xFF;", "    g->gotsword = 1;", None),
    ("    g->shad.face = POP_NO_CHAR;\n", "", None),
    ("    if (g->level == 1) g->gotsword = 0;", "    if (g->level == 2) g->gotsword = 0;", None),
    ("    if (g->level != 0 && g->level != 14) {     /* announce", "    if (g->level != 0) {  /*", None),
    ("        if (g->level == 13 && g->skipmessage != 0) {", "        if (g->skipmessage != 0) {", None),
    ("            g->msgtimer = POP_LEVELTIMER;", "            g->msgtimer = POP_LEVELTIMER + 1;", None),
    ("    pop_entrance(g);\n    pop_firstframe(g);", "    pop_firstframe(g);", None),
]

# file, its deliberate errors, and the group of routines whose comparison
# should notice them (running only that group keeps the check short)
FILES = (("pop_control.c", MUTANTS, "@control"),
         ("pop_collide.c", COLLIDE_MUTANTS, "@collision"),
         ("pop_mover.c", MOVER_MUTANTS, "@mover"),
         ("pop_auto.c", AUTO_MUTANTS, "@auto"),
         ("pop_top.c", TOP_MUTANTS, "@top"))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    data, source = argv[1:3]
    cases = argv[3] if len(argv) > 3 else "400"
    seed = argv[4] if len(argv) > 4 else "1989"
    only = argv[5].split(",") if len(argv) > 5 else None
    core = ("pop_control.c", "pop_collide.c", "pop_mover.c", "pop_auto.c", "pop_top.c",
            "pop_seq.c", "pop_sound.c", "pop_world.c")
    others = [os.path.join(PORT, *p) for p in (
        ("src", "assets", "pop_assets.c"), ("src", "audio", "pop_sfx.c"),
        ("tools", "pop_worldrun.c"))]
    includes = ["-I" + os.path.join(PORT, "src", d) for d in ("core", "assets", "audio")]

    jobs = []
    stale = 0
    for name, mutants, group in FILES:
        with open(os.path.join(PORT, "src", "core", name)) as f:
            text = f.read()
        for old, new, why in mutants:
            if only is not None and name not in only and not any(k in new or k in old
                                                                  for k in only):
                continue
            if text.count(old) != 1:
                print("STALE   pattern occurs %d times: %s" % (text.count(old), old.strip()))
                stale += 1
                continue
            jobs.append((name, group, text.replace(old, new), old, new, why))

    def run(job):
        name, group, mutated, old, new, why = job
        work = tempfile.mkdtemp()
        try:
            mutant = os.path.join(work, name)
            with open(mutant, "w") as f:
                f.write(mutated)
            sources = [os.path.join(PORT, "src", "core", c) for c in core if c != name]
            exe = os.path.join(work, "run")
            if why is not None and why.startswith("unit:"):
                test = os.path.join(HERE, why.split(":")[1])
                subprocess.run(["cc", "-std=c17", "-O1", "-I" + HERE] + includes
                               + [test, mutant] + sources + ["-o", exe, "-lm"], check=True)
                return subprocess.run([exe], capture_output=True).returncode != 0
            subprocess.run(["cc", "-std=c17", "-O1"] + includes + [mutant] + sources
                           + others + ["-o", exe, "-lm"], check=True)
            r = subprocess.run([sys.executable, os.path.join(HERE, "fuzz_control.py"), exe,
                                data, source, cases, seed, group],
                               capture_output=True, text=True)
            return r.returncode != 0
        finally:
            shutil.rmtree(work)

    caught = same = wrong = by_unit = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        for job, noticed in zip(jobs, pool.map(run, jobs)):
            name, _, _, old, new, why = job
            label = "%s: %s" % (name, new.strip().replace("\n", " ") or "(removed) "
                                + old.strip())
            if why is not None and why.startswith("unit:"):
                if noticed:
                    by_unit += 1
                    print("unit    %s   [%s]" % (label, why[5:]), flush=True)
                else:
                    wrong += 1
                    print("MISSED (by the unit test too)  " + label, flush=True)
            elif noticed and why is None:
                caught += 1
                print("caught  " + label, flush=True)
            elif not noticed and why is not None:
                same += 1
                print("same    %s   [%s]" % (label, why), flush=True)
            else:
                wrong += 1
                print("%s  %s" % ("MISSED" if why is None else "UNEXPECTEDLY CAUGHT", label),
                      flush=True)
    print("%d changes: %d caught, %d caught by a unit test only, %d make no difference, "
          "%d unexpected" % (len(jobs) + stale, caught, by_unit, same, wrong + stale))
    return 1 if wrong or stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
