#!/usr/bin/env python3
"""Do compare_rooms.py, compare_fast.py and replay_frames.py notice wrong
drawing?  A check of the tests themselves.

New code for the pop-study project.  Not part of CTest.

Each entry is one small deliberate error in src/video/pop_frameadv.c,
pop_lists.c or pop_screen.c.  The script builds pop_gfxrun and pop_worldrun
with that one change and runs the comparisons (full redraws, partial
redraws, the demo and random-input traces drawn frame by frame), one of
which must then report a mismatch.  Entries
with a reason are changes that cannot make a difference for what the
comparisons draw; for those both must still pass.  The original's results
are computed once and reused (POP_ROOMS_CACHE, POP_FAST_CACHE,
POP_DEMO_CACHE, POP_TRACE_CACHE).  A reason
that starts with "unit:<test file>" names a unit test that is built with
the error instead and must fail: for errors the comparisons cannot see
because the case leaves the modelled memory and is skipped.

Usage:
    mutation_gfx.py <data dir> <original checkout> [variations per level]
                    [seed] [screens per level for compare_fast] [traces]
"""

import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, "..")

FRAMEADV = [
    ("static const uint8_t BLOCKBOT[5] = { 2, 65, 128, 191, 254 };", "static const uint8_t BLOCKBOT[5] = { 2, 65, 128, 190, 254 };", None),
    ("        return (g->blueprint[POP_LINKMAP + v->state] & 0x1F) >= 2 ? POP_DPRESSPLATE",
     "        return (g->blueprint[POP_LINKMAP + v->state] & 0x1F) >= 3 ? POP_DPRESSPLATE", None),
    ("            v->state = 0;\n            return POP_FLOOR;", "            return POP_FLOOR;", None),
    ("    if (g->SCRNUM == 0) return POP_SPACE;      /* GOnull", "    /*",
     "the wrong version reads a level block with no screen selected, which is outside the "
     "modelled memory, and the comparison skips the screen; test_gfx.c draws screen 0"),
    ("    v->OPACITY = POP_OP_MASK;\n    pop_addfore(v);\n    v->OPACITY = POP_OP_ORA;\n    pop_addfore(v);",
     "    v->OPACITY = POP_OP_ORA;\n    pop_addfore(v);", None),
    ("        for (i = 0; i < 3; i++) { v->PREV[i] = POP_BLOCK; v->sprev[i] = 0; }",
     "        for (i = 0; i < 3; i++) { v->PREV[i] = POP_SPACE; v->sprev[i] = 0; }", None),
    ("        for (i = 0; i < 3; i++) { v->PREV[i] = POP_BLOCK; v->sprev[i] = 0; }",
     "        for (i = 0; i < 3; i++) { v->PREV[i] = POP_BLOCK; }", None),
    ("    static const uint8_t last[3] = { 9, 19, 29 };", "    static const uint8_t last[3] = { 9, 19, 28 };", None),
    ("        v->BELOW[0] = v->PREV[x + 1];\n        v->SBELOW[0] = v->sprev[x + 1];",
     "        v->BELOW[0] = v->PREV[x + 1];", None),
    ("        y = u8(v->yindex + 10);", "        y = u8(v->yindex + 9);", None),
    ("        for (i = 1; i < 10; i++) v->BELOW[i] = POP_FLOOR;", "        for (i = 1; i < 10; i++) v->BELOW[i] = POP_SPACE;",
     "neither floor nor empty space has a C-section in the piece tables, and neither is a gate"),
    ("        v->BELOW[0] = (g->level == 12) ? POP_SPACE : POP_BLOCK;", "        v->BELOW[0] = POP_BLOCK;", None),
    ("        v->BELOW[0] = getobjid(v, g, 9);\n        v->SBELOW[0] = v->state;", "        v->BELOW[0] = getobjid(v, g, 9);", None),
    ("    return o == POP_SPACE || o == POP_PILLARTOP || o == POP_PANELWOF || o >= POP_ARCHTOP1;",
     "    return o == POP_SPACE || o == POP_PILLARTOP || o == POP_PANELWOF || o > POP_ARCHTOP1;", None),
    ("    return o == POP_SPACE || o == POP_PILLARTOP || o == POP_PANELWOF || o >= POP_ARCHTOP1;",
     "    return o == POP_SPACE || o == POP_PANELWOF || o >= POP_ARCHTOP1;", None),
    ("        if (y >= K(numblox)) y = 0;\n        img = T(blockc, y);", "        img = T(blockc, y);", None),
    ("            if (y >= K(numpans)) return;\n            img = T(panelc, y);", "            if (y > K(numpans)) return;\n            img = T(panelc, y);",
     "the byte after the three panel entries is 0, which means 'nothing' as well"),
    ("    v->IMAGE = img;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    v->OPACITY = POP_OP_ORA;\n    add(v);\n}\n\nstatic void domaskb",
     "    v->IMAGE = img;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    v->OPACITY = POP_OP_STA;\n    add(v);\n}\n\nstatic void domaskb", None),
    ("    uint8_t m = T(maskb, v->PRECED);", "    uint8_t m = T(maskb, v->objid);", None),
    ("    v->IMAGE = m;\n    v->YCO = v->Dy;\n    v->OPACITY = POP_OP_AND;\n    add(v);\n}\n\nstatic void drawc",
     "    v->IMAGE = m;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    v->OPACITY = POP_OP_AND;\n    add(v);\n}\n\nstatic void drawc",
     "the mask is added first thing in a block, when XCO still holds the block's column"),
    ("    dodrawc(v);\n    domaskb(v);\n}", "    domaskb(v);\n    dodrawc(v);\n}", None),
    ("    if (gate >= K(gmaxval)) gate = K(gmaxval);\n    v->gateposn = gate >> 2;", "    v->gateposn = gate >> 2;", None),
    ("    v->IMAGE = T(gate8c, v->gateposn & 7);", "    v->IMAGE = T(gate8c, v->gateposn & 3);", None),
    ("    if (o != POP_SPACE && o != POP_PANELWOF && o != POP_PILLARTOP) return;\n    if (v->BELOW[v->colno] == POP_GATE) drawgatec(v);",
     "    if (v->BELOW[v->colno] == POP_GATE) drawgatec(v);", None),
    ("    if (v->objid == POP_BLOCK) return;\n    if (x == POP_SPACE) {", "    if (x == POP_SPACE) {", None),
    ("        if (y >= K(numbpans) + 1) return;\n        img = T(spaceb, y);", "        if (y >= K(numbpans)) return;\n        img = T(spaceb, y);", None),
    ("        if (y >= K(numbpans) + 1) y = 0;\n        img = T(floorb, y);", "        if (y >= K(numbpans) + 1) return;\n        img = T(floorb, y);", None),
    ("        drawb_at(v, u8(T(floorby, y) + v->Ay));", "        drawb_at(v, u8(T(spaceby, y) + v->Ay));", None),
    ("        return;                                /* no stripe after a panel */", "        ;", None),
    ("    if (g->BGset1 != 1) return;                /* the stripe: palace only */", "    ;", None),
    ("    drawb_at(v, u8(v->Ay - 32));", "    drawb_at(v, u8(v->Ay - 31));", None),
    ("    if (a >= K(Ffalling) + 1) a = 1;", "    if (a >= K(Ffalling)) a = 1;", None),
    ("    if (!NEG(a)) return a;\n    a &= 0x7F;", "    a &= 0x7F;", None),
    ("    if (x == POP_LOOSE) return T(loosea, getloosey(v, v->state));", "    if (x == POP_LOOSE) return T(loosea, v->state);", None),
    ("    v->YCO = u8(v->Ay + T(pieceay, x));", "    v->YCO = v->Ay;", None),
    ("        if (v->objid == POP_PANELWOF) { adda1(v, K(archpanel), v->objid); return; }", "", None),
    ("    if (p == POP_PANELWIF || p == POP_PANELWOF || p == POP_PILLARTOP || p == POP_BLOCK) {",
     "    if (p == POP_PANELWIF || p == POP_PANELWOF || p == POP_BLOCK) {", None),
    ("        if (x == POP_PANELWOF) v->OPACITY = POP_OP_ORA;", "", None),
    ("    v->IMAGE = img;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    add(v);\n    return 1;",
     "    v->IMAGE = img;\n    v->XCO = v->blockxco;\n    v->YCO = v->Ay;\n    add(v);\n    return 1;", None),
    ("    uint8_t img = T(loosed, getloosey(v, v->state));", "    uint8_t img = T(loosed, getloosey(v, v->spreced));", None),
    ("    uint8_t y = getloosey(v, v->spreced);\n    v->IMAGE = K(looseb);", "    uint8_t y = getloosey(v, v->state);\n    v->IMAGE = K(looseb);", None),
    ("    v->YCO = u8(v->Ay - 1);\n    v->OPACITY = POP_OP_ORA;", "    v->YCO = v->Ay;\n    v->OPACITY = POP_OP_ORA;", None),
    ("static uint8_t spikeframe(const pop_gfx *v, uint8_t s) { return NEG(s) ? K(spikeExt) : s; }",
     "static uint8_t spikeframe(const pop_gfx *v, uint8_t s) { (void)v; return s & 0x7F; }", None),
    ("    if (v->blockxco == 0) return;              /* no flame", "    /*", None),
    ("    if (x < POP_TORCHLAST + 1) {", "    if (x < POP_TORCHLAST) {", None),
    ("        v->XCO = u8(v->XCO + 1);\n        v->YCO = u8(v->YCO - 43);", "        v->XCO = u8(v->XCO + 1);\n        v->YCO = u8(v->YCO - 42);", None),
    ("        if (kind == 0x40) {                    /* the tall flask */\n            v->YCO = u8(v->YCO - 4);\n        } else if (kind > 0x40) {\n            v->OFFSET++;",
     "        if (kind == 0x40) {\n            v->YCO = u8(v->YCO - 4);\n        } else if (kind > 0x40) {", None),
    ("        if (x >= POP_BUBBLAST + 1) x = 0;", "        if (x >= POP_BUBBLAST) x = 0;", None),
    ("    v->XCO = u8(v->XCO + 2);\n    v->YCO = u8(v->YCO - 14);", "    v->XCO = u8(v->XCO + 2);\n    v->YCO = u8(v->YCO - 13);", None),
    ("    pop_addmidezo(v, POP_MID_LAY);", "    pop_addmidezo(v, POP_MID_FASTLAY);", None),
    ("    v->IMAGE = (v->state == 1) ? K(swordgleam1) : K(swordgleam0);", "    v->IMAGE = (v->state != 0) ? K(swordgleam1) : K(swordgleam0);", None),
    ("    if (x >= K(slicerRet)) x = K(slicerRet);", "    if (x > K(slicerRet)) x = K(slicerRet);",
     "equal values: assigning changes nothing"),
    ("    if (x >= K(slicerRet)) x = K(slicerRet);", "", None),
    ("    if (NEG(v->state)) img = T(slicerbot2, x); /* bloodied */", "", None),
    ("    v->YCO = u8(v->Ay - T(slicergap, x));", "    v->YCO = u8(v->Ay + T(slicergap, x));", None),
    ("    if (v->blockxco >= 36) return;             /* it cannot", "    if (v->blockxco > 36) return;  /* it cannot", None),
    ("    if (g->SCRNUM != g->blueprint[POP_KIDSTART_SCRN]) add(v);", "    add(v);", None),
    ("    a = u8(v->Dy - 67);", "    a = u8(v->Dy - 66);", None),
    ("    a = u8(v->Ay - 14 - v->gateposn);", "    a = u8(v->Ay - 13 - v->gateposn);", None),
    ("        a = u8(v->YCO - 4);\n        if (a < v->blockthr) break;", "        a = u8(v->YCO - 4);\n        if (a <= v->blockthr) break;", None),
    ("    a = u8(v->Ay - 64);\n    if (a >= 192) return;", "    a = u8(v->Ay - 63);\n    if (a >= 192) return;", None),
    ("    v->blockthr = u8(v->Dy - 62);", "    v->blockthr = u8(v->Dy - 61);", None),
    ("    v->gateposn = u8((gate >> 2) + 1);", "    v->gateposn = u8(gate >> 2);", None),
    ("    if (checkc(v)) dodrawc(v);\n    drawa(v);", "    drawa(v);", None),
    ("    if (u8(a + 12) < v->Ay) {", "    if (u8(a + 12) <= v->Ay) {", None),
    ("        v->YCO = u8(v->gatebot - 2);\n        v->IMAGE = K(gatebotORA);\n        v->OPACITY = POP_OP_ORA;\n        pop_addback(v);",
     "        v->YCO = v->gatebot;\n        v->IMAGE = K(gatebotORA);\n        v->OPACITY = POP_OP_ORA;\n        pop_addback(v);", None),
    ("        if (a < 7) break;                      /* would stick out of the block */", "        ;", None),
    ("        if (u8(a - 7) < v->blockthr) break;\n        pop_addback(v);", "        if (u8(a - 8) < v->blockthr) break;\n        pop_addback(v);", None),
    ("    if (h == 0 || h >= 9) return;", "    if (h >= 9) return;", None),
    ("    v->IMAGE = T(gate8b, h - 1u);", "    v->IMAGE = T(gate8b, h);", None),
    ("    v->YCO = u8(v->gatebot - 2);\n    v->IMAGE = K(gatebotORA);\n    pop_addfore(v);", "    v->YCO = v->gatebot;\n    v->IMAGE = K(gatebotORA);\n    pop_addfore(v);", None),
    ("    case POP_LOOSE:  drawlooseb(v); break;", "", None),
    ("    case POP_EXIT:   drawexitb(v, g); break;", "    case POP_EXIT2:  drawexitb(v, g); break;", None),
    ("    if (v->PRECED == POP_GATE && v->rowno == g->kid.block_y\n        && u8(v->colno - 1) == g->kid.block_x && g->scrnRight != g->kid.scrn) {",
     "    if (v->PRECED == POP_GATE && v->rowno == g->kid.block_y\n        && u8(v->colno - 1) == g->kid.block_x) {", None),
    ("        && u8(v->colno - 1) == g->kid.block_x && g->scrnRight != g->kid.scrn) {", "        && v->colno == g->kid.block_x && g->scrnRight != g->kid.scrn) {", None),
    ("        if (s != 0xA0 && s >= 0x40) img = K(specialflask);", "        if (s >= 0x40) img = K(specialflask);", None),
    ("    v->XCO = u8(v->blockxco + T(frontx, x));", "    v->XCO = v->blockxco;", None),
    ("    if (x >= POP_ARCHTOP2) {\n        plain = 1;", "    if (x > POP_ARCHTOP2) {\n        plain = 1;", None),
    ("    } else if (g->BGset1 != 1 && x == POP_POSTS) {", "    } else if (x == POP_POSTS) {", None),
    ("        v->IMAGE = T(blockfr, y);\n        plain = 1;", "        v->IMAGE = T(blockfr, y);", None),
    ("    drawb(v, g);\n    drawmb(v, g);\n    (void)drawd(v);", "    drawmb(v, g);\n    drawb(v, g);\n    (void)drawd(v);", None),
    ("    drawa(v);\n    drawma(v);\n    drawfrnt(v, g);\n}", "    drawma(v);\n    drawa(v);\n    drawfrnt(v, g);\n}", None),
    ("    (void)drawd(v);\n    drawmd(v);\n    drawfrnt(v, g);\n}\n\n/* --- what SURE", "    (void)drawd(v);\n    drawfrnt(v, g);\n}\n\n/* --- what SURE", None),
    ("    for (row = 2; row >= 0; row--) {           /* bottom row first */", "    for (row = 0; row <= 2; row++) {", None),
    ("    v->Ay = u8(v->Dy - 3);\n    v->yindex = u8(row * 10);", "    v->Ay = u8(v->Dy - 2);\n    v->yindex = u8(row * 10);", None),
    ("    v->PRECED = v->objid;\n    v->spreced = v->state;\n    v->yindex++;", "    v->PRECED = v->objid;\n    v->yindex++;", None),
    ("    v->PRECED = v->objid;\n    v->spreced = v->state;\n    v->yindex++;", "    v->spreced = v->state;\n    v->yindex++;", None),
    ("    v->Dy = 2;\n    v->Ay = 0xFF;", "    v->Dy = 3;\n    v->Ay = 0xFF;", None),
    ("    v->PRECED = 0;                             /* (spreced keeps its last value) */", "    v->PRECED = 0;\n    v->spreced = 0;",
     "in the row above, B-sections lie above the top of the screen (their line wraps past 191) and are dropped, whatever pattern the stale value selects"),
    ("    getbelow(v, g, g->SCRNUM, g->scrnLeft);    /* \"below\" that row is this screen */",
     "    getbelow(v, g, g->scrnBelow, g->scrnBelowL);", None),
    ("        if (g->scrnAbove == 0) v->objid = POP_FLOOR;         /* (`state` keeps its value) */",
     "        if (g->scrnAbove == 0) { v->objid = POP_FLOOR; v->state = 0; }",
     "in the row above, B-sections lie above the top of the screen (their line wraps past 191) and are dropped, whatever pattern the stale value selects"),
]

LISTS = [
    ("    if (v->YCO >= 192) return;                 /* below the screen, or wrapped above it */", "    ;", None),
    ("    v->bgX[x] = v->XCO;\n    if (v->YCO >= 192) return;", "    if (v->YCO > 192) return;\n    v->bgX[x] = v->XCO;",
     "an entry on line 192 would be drawn outside the pages and the comparison skips the "
     "screen; test_gfx.c checks that it is dropped"),
    ("    if (x >= POP_MAXFORE) return;\n    v->fgX[x] = v->XCO;\n    if (v->YCO >= 192) return;",
     "    if (x >= POP_MAXFORE) return;\n    v->fgX[x] = v->XCO;", None),
    ("    v->midOFF[x] = v->OFFSET;", "    v->midOFF[x] = 0;", None),
    ("    v->midCR[x] = 40;\n    v->midCD[x] = 192;", "    v->midCR[x] = 39;\n    v->midCD[x] = 192;", None),
    ("    pop_image im = pop_imgtab_image(&v->bgtable[NEG(coded) ? 1 : 0], coded & 0x7F);",
     "    pop_image im = pop_imgtab_image(&v->bgtable[0], coded & 0x7F);", None),
    ("    if (v->genCLS != 0) pop_hr_cls(&v->hr);", "", None),
    ("        mode(v, v->bgOP[x]);\n        pop_hr_fastlay(&v->hr, im);", "        mode(v, POP_OP_ORA);\n        pop_hr_fastlay(&v->hr, im);", None),
    ("    v->IMAGE = coded & 0x7F;\n    v->TABLE = 0;", "    v->IMAGE = coded;\n    v->TABLE = 0;", None),
    ("        v->IMAGE = v->wipeH[x];\n", "",
     "unit:test_gfx.c: the height left behind differs from the last wipe added only when the wipe "
     "list overflowed, and it can be read only by the stale-torch path, which then also finds the "
     "wipe's colour as its draw mode and leaves the modelled memory"),
    ("    v->hr.XCO = v->XCO = x;", "    v->hr.XCO = x;",
     "nothing in a redraw without characters uses the column the drawing left behind"),
    ("    v->hr.YCO = v->YCO = y;", "    v->hr.YCO = y;",
     "nothing uses the line the drawing left behind: the one stale read, the flame of a torch with a "
     "state beyond the table, sets its own position"),
    ("    v->hr.OPACITY = v->OPACITY = opacity;", "    v->hr.OPACITY = opacity;", None),
    ("        if (v->fgOP[x] == POP_OP_MASK) {\n            pop_hr_fastmask(&v->hr, im);", "        if (v->fgOP[x] == POP_OP_AND) {\n            pop_hr_fastmask(&v->hr, im);", None),
    ("        case POP_MID_LAY:\n            setaddl(v, x);\n            pop_hr_lay(&v->hr, im);", "        case POP_MID_LAY:\n            pop_hr_lay(&v->hr, im);", None),
    ("    v->hr.RIGHTCUT = v->midCR[x];", "    v->hr.RIGHTCUT = 40;", None),
    ("    drawback(v);\n    drawmid(v);\n    drawfore(v);", "    drawmid(v);\n    drawback(v);\n    drawfore(v);", None),
    ("    drawmid(v);\n    drawfore(v);\n    drawmsg(v);", "    drawfore(v);\n    drawmid(v);\n    drawmsg(v);", None),
    ("    sngpeel(v);                                /* the peel list set up two frames ago */\n", "", None),
    ("    drawwipe(v);\n", "", None),
    ("    pop_im = pop_imgtab_image", "    pop_im = pop_imgtab_image", "placeholder"),
]
LISTS = [m for m in LISTS if m[2] != "placeholder"]

# --- the partial redraw and the meters (compare_fast.py) ---
FAST = [
    ("    if (v->addmid_sel) {\n        v->OFFSET = 0;", "    if (v->addmid_sel) {", None),
    ("        pop_addmidezo(v, POP_MID_FASTLAY);\n    } else {", "        pop_addmidezo(v, POP_MID_LAY);\n    } else {", None),
    ("    if (v->addmid_sel) {", "    if (!v->addmid_sel) {", None),
    ("    v->genCLS = 1;\n    v->addmid_sel = 0;                         /* setback */", "    v->genCLS = 1;",
     "FAST always leaves the selector on the background list, so SURE never finds it elsewhere"),
    ("        a |= *rb(g, POP_RB_RED, y) | *rb(g, POP_RB_FLOOR, y) | *rb(g, POP_RB_HALF, y)",
     "        a |= *rb(g, POP_RB_FLOOR, y) | *rb(g, POP_RB_HALF, y)", None),
    ("        a |= *rb(g, POP_RB_RED, y) | *rb(g, POP_RB_FLOOR, y) | *rb(g, POP_RB_HALF, y)",
     "        a |= *rb(g, POP_RB_RED, y) | *rb(g, POP_RB_HALF, y)", None),
    ("        a |= *rb(g, POP_RB_RED, y) | *rb(g, POP_RB_FLOOR, y) | *rb(g, POP_RB_HALF, y)",
     "        a |= *rb(g, POP_RB_RED, y) | *rb(g, POP_RB_FLOOR, y)", None),
    ("             | *rb(g, POP_RB_FRED, y) | *rb(g, POP_RB_WIPE, y);", "             | *rb(g, POP_RB_WIPE, y);", None),
    ("             | *rb(g, POP_RB_FRED, y) | *rb(g, POP_RB_WIPE, y);", "             | *rb(g, POP_RB_FRED, y);", None),
    ("             | *rb(g, POP_RB_FRED, y) | *rb(g, POP_RB_WIPE, y);",
     "             | *rb(g, POP_RB_FRED, y) | *rb(g, POP_RB_WIPE, y) | *rb(g, POP_RB_MOVE, y);", None),
    ("    v->IMAGE = lines;                          /* height = IMAGE */", "    v->IMAGE = u8(lines + 1);", None),
    ("    v->width = 4;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    pop_addwipe(v, 0x80);",
     "    v->width = 3;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    pop_addwipe(v, 0x80);", None),
    ("    v->width = 4;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    pop_addwipe(v, 0x80);",
     "    v->width = 4;\n    v->YCO = v->Dy;\n    pop_addwipe(v, 0x80);",
     "a wipe is the first thing added in a block, when XCO still holds the block's column"),
    ("    v->width = 4;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    pop_addwipe(v, 0x80);",
     "    v->width = 4;\n    v->XCO = v->blockxco;\n    v->YCO = v->Ay;\n    pop_addwipe(v, 0x80);", None),
    ("    v->width = 4;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    pop_addwipe(v, 0x80);",
     "    v->width = 4;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    pop_addwipe(v, 0x00);", None),
    ("    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_PANELWOF || o == POP_BLOCK) return;\n    wipe(v, 3);",
     "    if (o == POP_PANELWIF || o == POP_PANELWOF || o == POP_BLOCK) return;\n    wipe(v, 3);", None),
    ("    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_PANELWOF || o == POP_BLOCK) return;\n    wipe(v, 3);",
     "    if (o == POP_PILLARTOP || o == POP_PANELWOF || o == POP_BLOCK) return;\n    wipe(v, 3);", None),
    ("    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_PANELWOF || o == POP_BLOCK) return;\n    wipe(v, 3);",
     "    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_BLOCK) return;\n    wipe(v, 3);", None),
    ("    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_PANELWOF || o == POP_BLOCK) return;\n    wipe(v, 3);",
     "    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_PANELWOF) return;\n    wipe(v, 3);", None),
    ("    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_PANELWOF || o == POP_BLOCK) return;\n    wipe(v, 3);",
     "    if (o == POP_PILLARTOP || o == POP_PANELWIF || o == POP_PANELWOF || o == POP_BLOCK) return;\n    wipe(v, 2);", None),
    ("    addamask(v);\n    adda(v);\n    drawma(v);\n    (void)drawd(v);\n}", "    adda(v);\n    drawma(v);\n    (void)drawd(v);\n}", None),
    ("    addamask(v);\n    adda(v);\n    drawma(v);\n    (void)drawd(v);\n}", "    addamask(v);\n    drawma(v);\n    (void)drawd(v);\n}", None),
    ("    addamask(v);\n    adda(v);\n    drawma(v);\n    (void)drawd(v);\n}", "    addamask(v);\n    adda(v);\n    (void)drawd(v);\n}", None),
    ("    addamask(v);\n    adda(v);\n    drawma(v);\n    (void)drawd(v);\n}", "    addamask(v);\n    adda(v);\n    drawma(v);\n}", None),
    ("    if (v->PRECED != 0) return;                /* only with empty space to the left */", "    ;", None),
    ("    v->IMAGE = K(CUmask);\n    v->XCO = v->blockxco;", "    v->IMAGE = K(CUmask);", None),
    ("    v->YCO = v->Ay;\n    if (v->objid == POP_DPRESSPLATE) v->YCO++; /* (\"quick trick\") */", "    v->YCO = v->Ay;", None),
    ("    v->YCO = v->Ay;\n    if (v->objid == POP_DPRESSPLATE) v->YCO++; /* (\"quick trick\") */",
     "    v->YCO = v->Dy;\n    if (v->objid == POP_DPRESSPLATE) v->YCO++; /* (\"quick trick\") */", None),
    ("    v->OPACITY = POP_OP_AND;\n    add(v);\n}\n\nstatic void drawhalf", "    v->OPACITY = POP_OP_ORA;\n    add(v);\n}\n\nstatic void drawhalf", None),
    ("    uint8_t x = v->objid, img;\n    if (v->PRECED != 0) return;", "    uint8_t x = v->objid, img;", None),
    ("    if (x == POP_FLOOR || x == POP_TORCH || x == POP_DPRESSPLATE || x == POP_EXIT) {",
     "    if (x == POP_TORCH || x == POP_DPRESSPLATE || x == POP_EXIT) {", None),
    ("    if (x == POP_FLOOR || x == POP_TORCH || x == POP_DPRESSPLATE || x == POP_EXIT) {",
     "    if (x == POP_FLOOR || x == POP_DPRESSPLATE || x == POP_EXIT) {", None),
    ("    if (x == POP_FLOOR || x == POP_TORCH || x == POP_DPRESSPLATE || x == POP_EXIT) {",
     "    if (x == POP_FLOOR || x == POP_TORCH || x == POP_EXIT) {", None),
    ("    if (x == POP_FLOOR || x == POP_TORCH || x == POP_DPRESSPLATE || x == POP_EXIT) {",
     "    if (x == POP_FLOOR || x == POP_TORCH || x == POP_DPRESSPLATE) {", None),
    ("        if (g->BGset1 != 1 || (x != POP_POSTS && x != POP_ARCHBOT)) { drawflr(v); return; }",
     "        if ((x != POP_POSTS && x != POP_ARCHBOT)) { drawflr(v); return; }", None),
    ("        if (g->BGset1 != 1 || (x != POP_POSTS && x != POP_ARCHBOT)) { drawflr(v); return; }",
     "        if (g->BGset1 != 1 || (x != POP_ARCHBOT)) { drawflr(v); return; }", None),
    ("        if (g->BGset1 != 1 || (x != POP_POSTS && x != POP_ARCHBOT)) { drawflr(v); return; }",
     "        if (g->BGset1 != 1 || (x != POP_POSTS)) { drawflr(v); return; }", None),
    ("        halfmask(v);\n        img = K(CUpost);", "        halfmask(v);\n        img = K(CUpiece);", None),
    ("        halfmask(v);\n        img = K(CUpost);", "        img = K(CUpost);", None),
    ("    v->IMAGE = img;\n    v->OPACITY = POP_OP_ORA;\n    add(v);\n    (void)drawd(v);\n}",
     "    v->IMAGE = img;\n    v->OPACITY = POP_OP_STA;\n    add(v);\n    (void)drawd(v);\n}", None),
    ("    v->IMAGE = img;\n    v->OPACITY = POP_OP_ORA;\n    add(v);\n    (void)drawd(v);\n}",
     "    v->IMAGE = img;\n    v->OPACITY = POP_OP_ORA;\n    add(v);\n}", None),
    ("        (*rb(g, POP_RB_WIPE, y))--;\n", "", None),
    ("        wipe(v, *rb(g, POP_RB_WHITE, y));      /* wipesq: as high as whitebuf says */", "        wipe(v, 63);", None),
    ("        (*rb(g, POP_RB_RED, y))--;\n        v->addmid_sel = 0;\n        redblocksure(v, g);",
     "        v->addmid_sel = 0;\n        redblocksure(v, g);", None),
    ("        (*rb(g, POP_RB_RED, y))--;\n        v->addmid_sel = 0;\n        redblocksure(v, g);",
     "        (*rb(g, POP_RB_RED, y))--;\n        redblocksure(v, g);", None),
    ("    } else if (*rb(g, POP_RB_MOVE, y) != 0) {  /* only its moving parts */", "    }\n    if (*rb(g, POP_RB_MOVE, y) != 0) {", None),
    ("        (*rb(g, POP_RB_MOVE, y))--;\n        v->addmid_sel = 0;\n        drawmc(v);",
     "        (*rb(g, POP_RB_MOVE, y))--;\n        drawmc(v);", None),
    ("        v->addmid_sel = 0;\n        drawmc(v);\n        drawmb(v, g);\n        drawma(v);\n    }",
     "        v->addmid_sel = 0;\n        drawmb(v, g);\n        drawma(v);\n    }", None),
    ("        v->addmid_sel = 0;\n        drawmc(v);\n        drawmb(v, g);\n        drawma(v);\n    }",
     "        v->addmid_sel = 0;\n        drawmc(v);\n        drawma(v);\n    }", None),
    ("        v->addmid_sel = 0;\n        drawmc(v);\n        drawmb(v, g);\n        drawma(v);\n    }",
     "        v->addmid_sel = 0;\n        drawmc(v);\n        drawmb(v, g);\n    }", None),
    ("        (*rb(g, POP_RB_FLOOR, y))--;\n        v->addmid_sel = 1;\n        drawfloor(v);",
     "        (*rb(g, POP_RB_FLOOR, y))--;\n        drawfloor(v);", None),
    ("    } else if (*rb(g, POP_RB_HALF, y) != 0) {", "    }\n    if (*rb(g, POP_RB_HALF, y) != 0) {", None),
    ("        (*rb(g, POP_RB_HALF, y))--;\n        v->addmid_sel = 1;\n        drawhalf(v, g);",
     "        (*rb(g, POP_RB_HALF, y))--;\n        drawhalf(v, g);", None),
    ("        *rb(g, POP_RB_OBJ, y) = 0;", "        (*rb(g, POP_RB_OBJ, y))--;", None),
    ("        (*rb(g, POP_RB_FRED, y))--;\n        drawfrnt(v, g);", "        (*rb(g, POP_RB_FRED, y))--;", None),
    ("    if (*mark == 0) return;\n    (*mark)--;\n    wiped(v);", "    if (*mark == 0) return;\n    wiped(v);", None),
    ("    (*mark)--;\n    wiped(v);\n    drawc(v);", "    (*mark)--;\n    drawc(v);", None),
    ("    wiped(v);\n    drawc(v);\n    drawmc(v);\n    drawb(v, g);\n    if (drawd(v)) pop_addfore(v);",
     "    wiped(v);\n    drawmc(v);\n    drawb(v, g);\n    if (drawd(v)) pop_addfore(v);", None),
    ("    wiped(v);\n    drawc(v);\n    drawmc(v);\n    drawb(v, g);\n    if (drawd(v)) pop_addfore(v);",
     "    wiped(v);\n    drawc(v);\n    drawb(v, g);\n    if (drawd(v)) pop_addfore(v);", None),
    ("    wiped(v);\n    drawc(v);\n    drawmc(v);\n    drawb(v, g);\n    if (drawd(v)) pop_addfore(v);",
     "    wiped(v);\n    drawc(v);\n    drawmc(v);\n    if (drawd(v)) pop_addfore(v);", None),
    ("    if (drawd(v)) pop_addfore(v);              /* redrawd: on both planes */\n    drawmd(v);\n    drawfrnt(v, g);",
     "    (void)drawd(v);\n    drawmd(v);\n    drawfrnt(v, g);", None),
    ("    if (drawd(v)) pop_addfore(v);              /* redrawd: on both planes */\n    drawmd(v);\n    drawfrnt(v, g);",
     "    (void)drawd(v); pop_addfore(v);\n    drawmd(v);\n    drawfrnt(v, g);", None),
    ("    if (drawd(v)) pop_addfore(v);              /* redrawd: on both planes */\n    drawmd(v);\n    drawfrnt(v, g);",
     "    if (drawd(v)) pop_addfore(v);\n    drawfrnt(v, g);", None),
    ("    if (drawd(v)) pop_addfore(v);              /* redrawd: on both planes */\n    drawmd(v);\n    drawfrnt(v, g);",
     "    if (drawd(v)) pop_addfore(v);\n    drawmd(v);", None),
    ("static const uint8_t KIDSTRX[12] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 };",
     "static const uint8_t KIDSTRX[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12 };", None),
    ("static const uint8_t KIDSTROFF[12] = { 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4 };",
     "static const uint8_t KIDSTROFF[12] = { 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 5 };", None),
    ("static const uint8_t OPPSTRX[12] = { 39, 38, 37, 36, 35, 34, 32, 31, 30, 29, 28, 27 };",
     "static const uint8_t OPPSTRX[12] = { 39, 38, 37, 36, 35, 34, 33, 31, 30, 29, 28, 27 };", None),
    ("static const uint8_t OPPSTROFF[12] = { 5, 4, 3, 2, 1, 0, 6, 5, 4, 3, 2, 1 };",
     "static const uint8_t OPPSTROFF[12] = { 5, 4, 3, 2, 1, 0, 6, 5, 4, 3, 2, 0 };", None),
    ("    v->XCO = xs[unit];\n    v->OFFSET = offs[unit];", "    v->XCO = xs[unit];", None),
    ("    return left >= 4 ? 3 : left >= 3 ? 2 : left >= 2 ? 1 : 0;", "    return left >= 3 ? 3 : left >= 3 ? 2 : left >= 2 ? 1 : 0;", None),
    ("    return left >= 4 ? 3 : left >= 3 ? 2 : left >= 2 ? 1 : 0;", "    return left >= 4 ? 3 : left >= 3 ? 1 : left >= 2 ? 1 : 0;", None),
    ("    return left >= 4 ? 3 : left >= 3 ? 2 : left >= 2 ? 1 : 0;", "    return left >= 4 ? 3 : left >= 3 ? 2 : left >= 1 ? 1 : 0;", None),
    ("    v->YCO = 191;\n    v->OPACITY = POP_OP_STA;\n    v->xsave = 0;                              /* units drawn so far */",
     "    v->YCO = 190;\n    v->OPACITY = POP_OP_STA;\n    v->xsave = 0;                              /* units drawn so far */", None),
    ("    v->YCO = 191;\n    v->OPACITY = POP_OP_STA;\n    v->xsave = 0;                              /* units drawn so far */",
     "    v->YCO = 191;\n    v->OPACITY = POP_OP_ORA;\n    v->xsave = 0;                              /* units drawn so far */", None),
    ("            if (g->KidStrength >= 2 || v->hr.PAGE != 0) {", "            if (g->KidStrength >= 2) {", None),
    ("            if (g->KidStrength >= 2 || v->hr.PAGE != 0) {", "            if (g->KidStrength >= 2 || v->hr.PAGE == 0) {", None),
    ("            if (g->KidStrength >= 2 || v->hr.PAGE != 0) {", "            if (g->KidStrength >= 1 || v->hr.PAGE != 0) {", None),
    ("        v->IMAGE = T(bline, n - 1u);\n        unit = v->xsave;\n        v->xsave = (uint8_t)(v->xsave + n);\n        meterimg(v, KIDSTRX, KIDSTROFF, unit);",
     "        v->IMAGE = T(bline, n);\n        unit = v->xsave;\n        v->xsave = (uint8_t)(v->xsave + n);\n        meterimg(v, KIDSTRX, KIDSTROFF, unit);", None),
    ("    v->OPACITY = POP_OP_AND;                   /* :darkpart: empty units up to the maximum */",
     "    v->OPACITY = POP_OP_STA;", None),
    ("    while (v->xsave < g->MaxKidStr) {", "    while (v->xsave <= g->MaxKidStr) {", None),
    ("        v->xsave++;\n        if (v->xsave == 0) break;", "        v->xsave++;",
     "the count would have to pass 255, and the meter has 12 places: the case is outside the modelled memory long before"),
    ("    if (g->OppStrength == 0) return;\n", "", None),
    ("    if (g->shad.id == 24 || g->shad.id == 4) return;         /* no meter for mouse or skeleton */",
     "    if (g->shad.id == 24) return;", None),
    ("    if (g->shad.id == 24 || g->shad.id == 4) return;         /* no meter for mouse or skeleton */",
     "    if (g->shad.id == 4) return;", None),
    ("    if (g->shad.id == 1 && g->level != 12) return;           /* the shadow's only on level 12 */", "    ;", None),
    ("    if (g->shad.id == 1 && g->level != 12) return;           /* the shadow's only on level 12 */",
     "    if (g->shad.id == 1) return;", None),
    ("    v->OPACITY = POP_OP_STA | POP_OP_MIRROR;", "    v->OPACITY = POP_OP_STA;", None),
    ("            if (g->OppStrength >= 2 || v->hr.PAGE != 0) {", "            if (g->OppStrength >= 2) {", None),
    ("                return;                        /* (no empty unit after it) */", "                break;", None),
    ("    v->OPACITY = POP_OP_AND | POP_OP_MIRROR;   /* :darkpart: one empty unit */", "    v->OPACITY = POP_OP_AND;", None),
    ("    v->IMAGE = K(blank);\n    meterimg(v, OPPSTRX, OPPSTROFF, v->xsave);", "    v->IMAGE = K(bullet);\n    meterimg(v, OPPSTRX, OPPSTROFF, v->xsave);", None),
    ("    getprev(v, g);\n    blue(g, g->SCRNUM);\n    v->redkidmeter", "    blue(g, g->SCRNUM);\n    v->redkidmeter", None),
    ("    v->redkidmeter = metbufs(g, 20, 3);", "    v->redkidmeter = metbufs(g, 20, 2);", None),
    ("    v->redkidmeter = metbufs(g, 20, 3);", "    v->redkidmeter = metbufs(g, 21, 3);", None),
    ("    v->redoppmeter = metbufs(g, 28, 2);", "    v->redoppmeter = metbufs(g, 28, 1);", None),
    ("    v->redoppmeter = metbufs(g, 28, 2);", "    v->redoppmeter = metbufs(g, 27, 2);", None),
    ("    v->addmid_sel = 0;                         /* :cont setback */\n", "", None),
    ("    if (g->scrnAbove != 0) {                   /* (unlike SURE: nothing without a screen above) */\n        pop_calcblue(g, g->scrnAbove);",
     "    {\n        pop_calcblue(g, g->scrnAbove);", None),
    ("    if (v->redkidmeter != 0) pop_drawkidmeter(v, g);         /* UPDATEMETERS */", "    pop_drawkidmeter(v, g);", None),
    ("    if (v->redoppmeter != 0) pop_drawoppmeter(v, g);", "    pop_drawoppmeter(v, g);", None),
]

# --- the objects (compare_fast.py) ---
OBJS = [
    ("    g->FCharX = (uint16_t)((g->FCharX & 0xFF00u) | g->objX[x]);      /* (the low byte only) */", "    ;", None),
    ("    v->XCO = g->objX[x];\n    v->OFFSET = g->objOFF[x];", "    v->XCO = g->objX[x];", None),
    ("    g->FCharY = v->YCO = g->objY[x];", "    v->YCO = g->objY[x];", None),
    ("    g->FCharY = v->YCO = g->objY[x];", "    g->FCharY = g->objY[x];", None),
    ("    v->IMAGE = g->objIMG[x];\n    v->TABLE = g->objTAB[x];", "    v->IMAGE = g->objIMG[x];", None),
    ("    g->FCharFace = g->objFACE[x];\n", "", None),
    ("    g->FCharCU = g->objCU[x];\n", "", None),
    ("    g->FCharCD = g->objCD[x];\n", "", None),
    ("    g->FCharCL = g->objCL[x];\n", "", None),
    ("    g->FCharCR = g->objCR[x];\n", "", None),
    ("    pop_addmid(v, g, POP_MID_LAYSAVE | POP_MID_CHARTABLE);", "    pop_addmid(v, g, POP_MID_LAY | POP_MID_CHARTABLE);", None),
    ("    pop_addmid(v, g, POP_MID_LAYSAVE | POP_MID_CHARTABLE);", "    pop_addmid(v, g, POP_MID_LAYSAVE);", None),
    ("    if (g->backtolife != 0 && v->hr.PAGE == 0) return;", "    if (g->backtolife != 0) return;", None),
    ("    if (g->backtolife != 0 && v->hr.PAGE == 0) return;", "    ;", None),
    ("    if (!NEG(g->mergetimer) && (g->mergetimer & 1)) addchar(v, g, POP_OP_EOR);",
     "    if ((g->mergetimer & 1)) addchar(v, g, POP_OP_EOR);", None),
    ("    if (!NEG(g->mergetimer) && (g->mergetimer & 1)) addchar(v, g, POP_OP_EOR);",
     "    if (!NEG(g->mergetimer) && (g->mergetimer & 2)) addchar(v, g, POP_OP_EOR);", None),
    ("    else addchar(v, g, POP_OP_MASK);", "    else addchar(v, g, POP_OP_ORA);", None),
    ("    if (g->GuardColor != 0) {                  /* DrawShifted", "    if (g->GuardColor == 0) {                  /* DrawShifted", None),
    ("        if (a >= 7) {\n            v->XCO++;", "        if (a >= 7) {", None),
    ("        if (a >= 7) {\n            v->XCO++;", "        if (a > 7) {\n            v->XCO++;", None),
    ("            a = u8(a - 7);", "            a = u8(a - 6);", None),
    ("    g->FCharFace = 0xFF;                       /* not mirrored */", "    ;", None),
    ("    v->YCO = u8(g->FCharY - 3);                /* A-section: mask, then piece */", "    v->YCO = u8(g->FCharY - 2);", None),
    ("    v->IMAGE = T(maska, POP_FLOOR);\n    v->OPACITY = POP_OP_AND;\n    pop_addmid(v, g, POP_MID_LAYSAVE);",
     "    v->IMAGE = T(maska, POP_FLOOR);\n    v->OPACITY = POP_OP_AND;\n    pop_addmid(v, g, POP_MID_LAY);", None),
    ("    v->IMAGE = T(maska, POP_FLOOR);\n    v->OPACITY = POP_OP_AND;", "    v->IMAGE = T(maska, POP_LOOSE);\n    v->OPACITY = POP_OP_AND;",
     "the loose floor's mask is the floor's in the piece tables (BGDATA.S 47: both 3)"),
    ("    v->IMAGE = T(loosea, g->FCharImage);\n    v->OPACITY = POP_OP_ORA;\n    pop_addmid(v, g, POP_MID_LAY);",
     "    v->IMAGE = T(loosea, g->FCharImage);\n    v->OPACITY = POP_OP_ORA;\n    pop_addmid(v, g, POP_MID_LAYSAVE);", None),
    ("    v->IMAGE = T(loosea, g->FCharImage);\n    v->OPACITY = POP_OP_ORA;",
     "    v->IMAGE = T(loosea, g->FCharImage);\n    v->OPACITY = POP_OP_STA;", None),
    ("    v->IMAGE = T(loosed, g->FCharImage);       /* D-section */\n    v->YCO = g->FCharY;",
     "    v->IMAGE = T(loosed, g->FCharImage);", None),
    ("    v->IMAGE = T(loosed, g->FCharImage);       /* D-section */", "    v->IMAGE = T(loosea, g->FCharImage);", None),
    ("    v->XCO = u8(u8(g->FCharX) + 4);            /* B-section, in the next block */", "    v->XCO = u8(u8(g->FCharX) + 3);", None),
    ("    v->YCO = u8(g->FCharY - 4);\n    v->IMAGE = K(looseb);", "    v->YCO = u8(g->FCharY - 3);\n    v->IMAGE = K(looseb);", None),
    ("    v->IMAGE = K(looseb);\n    v->OPACITY = POP_OP_ORA;\n    pop_addmid(v, g, POP_MID_LAYSAVE);",
     "    v->IMAGE = K(looseb);\n    v->OPACITY = POP_OP_ORA;\n    pop_addmid(v, g, POP_MID_LAY);", None),
    ("    case POP_OBJ_REFLECT: drawkid(v, g); break;", "    case POP_OBJ_REFLECT: addchar(v, g, POP_OP_MASK); break;", None),
    ("    case POP_OBJ_SHAD:    addchar(v, g, POP_OP_EOR); break;          /* DRAWSHAD */",
     "    case POP_OBJ_SHAD:    addchar(v, g, POP_OP_MASK); break;", None),
    ("    case POP_OBJ_COMIX:   addchar(v, g, POP_OP_MASK); break;         /* DRAWSWORD */",
     "    case POP_OBJ_COMIX:   drawkid(v, g); break;", None),
    ("    case POP_OBJ_GD:      drawguard(v, g); break;", "    case POP_OBJ_GD:      addchar(v, g, POP_OP_MASK); break;", None),
    ("        if (g->objTYP[obj1] != POP_OBJ_SHAD && g->objY[obj1] > g->objY[obj2]) {",
     "        if (g->objY[obj1] > g->objY[obj2]) {", None),
    ("        if (g->objTYP[obj1] != POP_OBJ_SHAD && g->objY[obj1] > g->objY[obj2]) {",
     "        if (g->objTYP[obj1] != POP_OBJ_SHAD && g->objY[obj1] >= g->objY[obj2]) {", None),
    ("        if (g->objTYP[obj1] != POP_OBJ_SHAD && g->objY[obj1] > g->objY[obj2]) {",
     "        if (g->objTYP[obj1] != POP_OBJ_SHAD && g->objY[obj1] < g->objY[obj2]) {", None),
    ("        if (g->objTYP[obj1] != POP_OBJ_SHAD && g->objY[obj1] > g->objY[obj2]) {",
     "        if (g->objTYP[obj2] != POP_OBJ_SHAD && g->objY[obj1] > g->objY[obj2]) {", None),
    ("    uint8_t x = v->sortX[0];\n    while (x != 1) {", "    uint8_t x = v->sortX[0];\n    while (x != 2 && x != 1) {", None),
    ("        x--;\n        if (x == 0) break;\n    }\n}\n\n/* drawobjs", "        x--;\n        if (x == 0) break;\n    }\n    if (v->sortX[0] > 2) sortlist(v, g);\n}\n\n/* drawobjs",
     "placeholder"),
    ("        if (g->objINDX[x] == v->yindex) {\n            y++;", "        if (g->objINDX[x] <= v->yindex) {\n            y++;", None),
    ("    } while (x <= n);\n    if (y == 0) return;", "    } while (x < n);\n    if (y == 0) return;", None),
    ("    sortlist(v, g);\n    x = 1;", "    x = 1;", None),
    ("        drawobjx(v, g, v->sortX[x]);\n        x = u8(v->xsave + 1);\n    } while (x <= v->sortX[0]);",
     "        drawobjx(v, g, v->sortX[x]);\n        x = u8(v->xsave + 1);\n    } while (x < v->sortX[0]);", None),
    ("        *rb(g, POP_RB_OBJ, y) = 0;\n        drawobjs(v, g);\n        v->XCO = v->blockxco;",
     "        *rb(g, POP_RB_OBJ, y) = 0;\n        v->XCO = v->blockxco;", None),
    ("        *rb(g, POP_RB_OBJ, y) = 0;\n        drawobjs(v, g);\n        v->XCO = v->blockxco;",
     "        *rb(g, POP_RB_OBJ, y) = 0;\n        drawobjs(v, g);", None),
    ("    v->yindex = 30;                            /* characters off the screen first */\n    drawobjs(v, g);",
     "    v->yindex = 30;", None),
    ("    v->yindex = 0xFF;                          /* impact stars on top of everything */\n    drawobjs(v, g);",
     "    v->yindex = 0xFF;", None),
    ("    v->yindex = 0xFF;                          /* impact stars on top of everything */", "    v->yindex = 0xFE;", None),
]
OBJS = [m for m in OBJS if m[2] != "placeholder"]

# --- messages and the screen (replay_frames.py) ---
SCREEN = [
    ("enum { MY = 90, LOWMY = 153, HICONTY = 73, LOWCONTY = 168 };", "enum { MY = 91, LOWMY = 153, HICONTY = 73, LOWCONTY = 168 };", None),
    ("enum { MY = 90, LOWMY = 153, HICONTY = 73, LOWCONTY = 168 };", "enum { MY = 90, LOWMY = 152, HICONTY = 73, LOWCONTY = 168 };", None),
    ("enum { MY = 90, LOWMY = 153, HICONTY = 73, LOWCONTY = 168 };", "enum { MY = 90, LOWMY = 153, HICONTY = 74, LOWCONTY = 168 };", None),
    ("enum { MY = 90, LOWMY = 153, HICONTY = 73, LOWCONTY = 168 };", "enum { MY = 90, LOWMY = 153, HICONTY = 73, LOWCONTY = 167 };", None),
    ("static const box CONTBOX  = { HICONTY, 13, 0, 0x7C };", "static const box CONTBOX  = { HICONTY, 12, 0, 0x7C };", None),
    ("static const box MSGBOX   = { MY, 15, 0, 0x7B };", "static const box MSGBOX   = { MY, 15, 1, 0x7B };", None),
    ("static const box LEVELMSG = { MY - 5, 16, 3, 0x7A };", "static const box LEVELMSG = { MY - 5, 16, 2, 0x7A };", None),
    ("static const box TIMELEFT = { MY, 11, 0, 0x7D };", "static const box TIMELEFT = { MY, 11, 0, 0x7B };", None),
    ("static const box SECONDS  = { MY - 5, 14, 0, 0x7F };", "static const box SECONDS  = { MY - 5, 15, 0, 0x7F };", None),
    ("static const uint8_t DIGIT1[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x71, 0x71, 0x71 };",
     "static const uint8_t DIGIT1[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x71, 0x71, 0x72 };", None),
    ("static const uint8_t DIGIT2[13] = { 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,",
     "static const uint8_t DIGIT2[13] = { 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x79, 0x78,", None),
    ("    v->OFFSET = b->offset;\n", "", None),
    ("    v->OPACITY = POP_OP_STA | 0x40;", "    v->OPACITY = POP_OP_STA;", None),
    ("    return g->level >= 13 ? 12 : g->level;", "    return g->level >= 14 ? 12 : g->level;",
     "unit:test_gfx.c: the wrong version looks up digit 13, which the tables do not have, and "
     "the case is skipped"),
    ("    if (getlevelno(g) >= 10) v->OFFSET = 0;    /* two digits", "    if (getlevelno(g) >= 11) v->OFFSET = 0;    /* two digits", None),
    ("    v->OPACITY = POP_OP_ORA;\n    pop_addmsg(v);\n    v->XCO = u8(v->XCO + 6);", "    v->OPACITY = POP_OP_STA;\n    pop_addmsg(v);\n    v->XCO = u8(v->XCO + 6);", None),
    ("    v->XCO = u8(v->XCO + 6);", "    v->XCO = u8(v->XCO + 5);", None),
    ("    if (a != 0) {\n        v->IMAGE = a;", "    {\n        v->IMAGE = a;",
     "unit:test_gfx.c: the wrong version asks for image 0 on levels 0 to 9, which no table has, "
     "and the case is skipped"),
    ("        pop_addmsg(v);\n        v->XCO = u8(v->XCO + 1);\n    }\n    v->IMAGE = DIGIT2[x];", "        pop_addmsg(v);\n    }\n    v->IMAGE = DIGIT2[x];", None),
    ("    if (g->MinLeft < 2 && g->kid.action != 3 && g->kid.action != 4 && g->kid.block_y == 1) {",
     "    if (g->kid.action != 3 && g->kid.action != 4 && g->kid.block_y == 1) {", None),
    ("    if (g->MinLeft < 2 && g->kid.action != 3 && g->kid.action != 4 && g->kid.block_y == 1) {",
     "    if (g->MinLeft < 2 && g->kid.action != 4 && g->kid.block_y == 1) {", None),
    ("    if (g->MinLeft < 2 && g->kid.action != 3 && g->kid.action != 4 && g->kid.block_y == 1) {",
     "    if (g->MinLeft < 2 && g->kid.action != 3 && g->kid.block_y == 1) {", None),
    ("    if (g->MinLeft < 2 && g->kid.action != 3 && g->kid.action != 4 && g->kid.block_y == 1) {",
     "    if (g->MinLeft < 2 && g->kid.action != 3 && g->kid.action != 4) {", None),
    ("    superim1(v);\n    v->YCO = u8(v->YCO - 5);\n    v->XCO = u8(v->XCO + 1);", "    superim1(v);\n    v->YCO = u8(v->YCO - 4);\n    v->XCO = u8(v->XCO + 1);", None),
    ("    v->YCO = u8(v->YCO - 5);\n    v->XCO = u8(v->XCO + 1);\n    v->OPACITY = POP_OP_ORA;", "    v->YCO = u8(v->YCO - 5);\n    v->OPACITY = POP_OP_ORA;", None),
    ("    v->OPACITY = POP_OP_ORA;\n    pop_getminleft(g);", "    v->OPACITY = POP_OP_ORA;", None),
    ("    if (temp < 2) temp = g->SecLeft;", "    if (temp < 1) temp = g->SecLeft;", None),
    ("    if ((temp >> 4) != 0) {\n        v->IMAGE = digit2(v, temp >> 4);", "    {\n        v->IMAGE = digit2(v, temp >> 4);", None),
    ("        pop_addmsg(v);\n    }\n    v->XCO = u8(v->XCO + 1);\n    v->IMAGE = digit2(v, temp & 0x0F);", "        pop_addmsg(v);\n    }\n    v->IMAGE = digit2(v, temp & 0x0F);", None),
    ("    if (g->MinLeft >= 2) return;\n", "", None),
    ("    setupimage(v, &SECONDS);\n    v->YCO = y;", "    setupimage(v, &SECONDS);", None),
    ("    v->YCO = y;\n    v->OPACITY = POP_OP_STA;", "    v->YCO = y;", None),
    ("    if ((g->kid.block_x & 1) == 0) v->YCO = LOWCONTY;", "    if ((g->kid.block_x & 1) != 0) v->YCO = LOWCONTY;", None),
    ("    case POP_SHOW_CONTINUE: pop_continuemsg(v, g); break;", "    case POP_SHOW_CONTINUE: break;", None),
    ("    case POP_SHOW_LEVEL:    pop_printlevel(v, g); break;", "    case POP_SHOW_LEVEL:    pop_timeleftmsg(v, g); break;", None),
    ("    v->scrncolor = value;                      /* (only filled anew if the value changes) */", "    (void)value;", None),
    ("    v->hr.PAGE = 0x20;\n    pop_lrclse(v, 0xA0);", "    v->hr.PAGE = 0;\n    pop_lrclse(v, 0xA0);", None),
    ("    pop_lrclse(v, 0xA0);                       /* spaces */\n", "", None),
    ("    page2(v, 0);\n    text(v, 1);", "    page2(v, 0);", None),
    ("    page2(v, 0);\n    text(v, 1);", "    text(v, 1);", None),
    ("    page2(v, 0);\n    text(v, 1);", "    text(v, 1);\n    page2(v, 0);", None),
    ("    v->hr.PAGE = 0;\n    to = u8(v->hr.PAGE + 0x20);", "    to = u8(v->hr.PAGE + 0x20);", None),
    ("    from = to ^ 0x60;", "    from = to;", None),
    ("    g->height = v->IMAGE = from;", "    v->IMAGE = from;",
     "the game logic sets the height before it uses it, and the next drawing routine starts "
     "from the game's value"),
    ("    pop_hr_copyscrn(&v->hr, from, to);", "    pop_hr_copyscrn(&v->hr, to, from);", None),
    ("    if (v->hr.PAGE == 0) {\n        v->hr.PAGE = 0x20;\n        page2(v, 0);", "    if (v->hr.PAGE != 0) {\n        v->hr.PAGE = 0x20;\n        page2(v, 0);", None),
    ("        v->hr.PAGE = 0x20;\n        page2(v, 0);                           /* show page 1, draw on page 2 */", "        v->hr.PAGE = 0x20;\n        page2(v, 1);", None),
    ("        v->hr.PAGE = 0;\n        page2(v, 1);\n    }\n    hires(v, 1);", "        v->hr.PAGE = 0;\n        page2(v, 0);\n    }\n    hires(v, 1);", None),
    ("    hires(v, 1);\n    text(v, 0);", "    text(v, 0);", None),
    ("    hires(v, 1);\n    text(v, 0);", "    hires(v, 1);", None),
    ("    hires(v, 1);\n    text(v, 0);", "    text(v, 0);\n    hires(v, 1);", None),
    ("    pop_lrclse(v, colour);\n    page2(v, 0);\n    hires(v, 0);", "    page2(v, 0);\n    hires(v, 0);", None),
    ("    pop_lrclse(v, colour);\n    page2(v, 0);\n    hires(v, 0);", "    pop_lrclse(v, colour);\n    hires(v, 0);", None),
    ("    pop_lrclse(v, colour);\n    page2(v, 0);\n    hires(v, 0);", "    pop_lrclse(v, colour);\n    page2(v, 0);", None),
    ("    if (v->hr.PAGE == 0) page2(v, 1);\n    hires(v, 1);\n}", "    page2(v, 1);\n    hires(v, 1);\n}", None),
    ("    if (v->hr.PAGE == 0) page2(v, 1);\n    hires(v, 1);\n}", "    if (v->hr.PAGE == 0) page2(v, 1);\n}", None),
    ("static void page2(pop_gfx *v, uint8_t on) { v->sw_page2 = on; note(v, on ? '2' : '1'); }",
     "static void page2(pop_gfx *v, uint8_t on) { v->sw_page2 = on; note(v, on ? '1' : '2'); }", None),
    ("static void hires(pop_gfx *v, uint8_t on) { v->sw_hires = on; note(v, on ? 'h' : 'l'); }",
     "static void hires(pop_gfx *v, uint8_t on) { note(v, on ? 'h' : 'l'); }", None),
]

FILES = (("pop_frameadv.c", FRAMEADV), ("pop_frameadv.c", FAST), ("pop_frameadv.c", OBJS),
         ("pop_lists.c", LISTS), ("pop_screen.c", SCREEN))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    data, source = argv[1:3]
    variations = argv[3] if len(argv) > 3 else "80"
    seed = argv[4] if len(argv) > 4 else "1989"
    core = sorted(os.listdir(os.path.join(PORT, "src", "core")))
    fixed = [os.path.join(PORT, "src", "core", c) for c in core if c.endswith(".c")] \
        + [os.path.join(PORT, "src", "assets", "pop_gfxfiles.c"),
           os.path.join(PORT, "tools", "pop_gfxrun.c")]
    # the whole-frame comparisons use the other runner
    world = [f for f in fixed if not f.endswith("pop_gfxrun.c")] \
        + [os.path.join(PORT, "src", "assets", "pop_assets.c"),
           os.path.join(PORT, "src", "audio", "pop_sfx.c"),
           os.path.join(PORT, "src", "video", "pop_present.c"),
           os.path.join(PORT, "tools", "pop_worldrun.c")]
    video = ("pop_hires.c", "pop_lists.c", "pop_frameadv.c", "pop_screen.c")
    includes = ["-I" + os.path.join(PORT, "src", d) for d in ("core", "video", "assets", "audio")]
    per_level = argv[5] if len(argv) > 5 else "30"
    traces = argv[6] if len(argv) > 6 else "30"
    cache = os.path.join(tempfile.mkdtemp(), "rooms.cache")
    env = dict(os.environ, POP_ROOMS_CACHE=cache, POP_FAST_CACHE=cache + ".fast",
               POP_DEMO_CACHE=cache + ".demo", POP_TRACE_CACHE=cache + ".trace")

    only = os.environ.get("POP_MUT_ONLY")    # rerun the entries whose text contains this
    jobs, stale = [], 0
    for name, mutants in FILES:
        with open(os.path.join(PORT, "src", "video", name)) as f:
            text = f.read()
        for old, new, why in mutants:
            if text.count(old) != 1:
                print("STALE   %s: pattern occurs %d times: %s"
                      % (name, text.count(old), old.strip()[:70]))
                stale += 1
                continue
            if only is None or only in old or only in new or only == name:
                jobs.append((name, text.replace(old, new), old, new, why))

    def build_and_run(name, source_text, unit=None):
        work = tempfile.mkdtemp()
        try:
            sources = []
            for v in video:
                if v == name:
                    path = os.path.join(work, v)
                    with open(path, "w") as f:
                        f.write(source_text)
                    sources.append(path)
                else:
                    sources.append(os.path.join(PORT, "src", "video", v))
            exe = os.path.join(work, "run")
            if unit:
                subprocess.run(["cc", "-std=c17", "-O1", "-I" + HERE] + includes
                               + [os.path.join(HERE, unit)] + sources
                               + [f for f in fixed if os.sep + "core" + os.sep in f]
                               + ["-o", exe, "-lm"], check=True)
                return subprocess.run([exe], capture_output=True).returncode
            subprocess.run(["cc", "-std=c17", "-O1"] + includes + sources + fixed
                           + ["-o", exe, "-lm"], check=True)
            r = subprocess.run([sys.executable, os.path.join(HERE, "compare_rooms.py"), exe, data,
                                source, variations, seed], capture_output=True, text=True, env=env)
            if r.returncode != 0:
                return r.returncode
            r = subprocess.run([sys.executable, os.path.join(HERE, "compare_fast.py"), exe, data,
                                source, per_level, seed], capture_output=True, text=True, env=env)
            if r.returncode != 0:
                return r.returncode
            exe2 = os.path.join(work, "world")
            subprocess.run(["cc", "-std=c17", "-O1"] + includes + sources + world
                           + ["-o", exe2, "-lm"], check=True)
            frames = [sys.executable, os.path.join(HERE, "replay_frames.py"), exe2, data, source]
            r = subprocess.run(frames + ["demo"], capture_output=True, text=True, env=env)
            if r.returncode != 0:
                return r.returncode
            r = subprocess.run(frames + ["traces", traces, seed], capture_output=True, text=True,
                               env=env)
            return r.returncode
        finally:
            shutil.rmtree(work)

    # the unchanged code first: fills the cache, and must pass
    with open(os.path.join(PORT, "src", "video", "pop_lists.c")) as f:
        if build_and_run("pop_lists.c", f.read()) != 0:
            print("the comparison fails on the unchanged code")
            return 1

    caught = same = wrong = by_unit = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        results = pool.map(lambda j: build_and_run(
            j[0], j[1], j[4].split(":")[1] if j[4] and j[4].startswith("unit:") else None) != 0, jobs)
        for job, noticed in zip(jobs, results):
            name, _, old, new, why = job
            label = "%s: %s" % (name, (new.strip().replace("\n", " ")
                                       or "(removed) " + old.strip())[:100])
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
    shutil.rmtree(os.path.dirname(cache))
    print("%d changes: %d caught, %d caught by a unit test only, %d make no difference, "
          "%d unexpected" % (len(jobs) + stale, caught, by_unit, same, wrong + stale))
    return 1 if wrong or stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
