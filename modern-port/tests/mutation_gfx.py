#!/usr/bin/env python3
"""Does compare_rooms.py notice a wrong redraw?  A check of the test itself.

New code for the pop-study project.  Not part of CTest.

Each entry is one small deliberate error in src/video/pop_frameadv.c or
pop_lists.c.  The script builds pop_gfxrun with that one change and runs
compare_rooms.py, which must then report a mismatch.  Entries with a reason
are changes that cannot make a difference for what the comparison draws;
for those it must still pass.  The original's results are computed once
and reused (POP_ROOMS_CACHE).

Usage:
    mutation_gfx.py <data dir> <original checkout> [variations per level] [seed]
"""

import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, "..")
NO_CHARS = "the screens compared have no characters, messages or wipes in their lists"

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
    ("    v->IMAGE = img;\n    v->XCO = v->blockxco;\n    v->YCO = v->Dy;\n    add(v);\n}\n\n/* --- pieces with state",
     "    v->IMAGE = img;\n    v->XCO = v->blockxco;\n    v->YCO = v->Ay;\n    add(v);\n}\n\n/* --- pieces with state", None),
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
    ("    if (h == 0 || h >= 9) return;", "    if (h >= 9) return;",
     "the wrong version then indexes the table at -1, outside the piece tables, and the "
     "comparison skips the screen; test_gfx.c has the case (a gate 28 units open)"),
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
    ("    drawb(v, g);\n    drawmb(v, g);\n    drawd(v);", "    drawmb(v, g);\n    drawb(v, g);\n    drawd(v);", None),
    ("    drawa(v);\n    drawma(v);\n    drawfrnt(v, g);\n}", "    drawma(v);\n    drawa(v);\n    drawfrnt(v, g);\n}", None),
    ("    drawd(v);\n    drawmd(v);\n    drawfrnt(v, g);\n}", "    drawd(v);\n    drawfrnt(v, g);\n}", None),
    ("    for (row = 2; row >= 0; row--) {           /* bottom row first */", "    for (row = 0; row <= 2; row++) {", None),
    ("        v->Ay = u8(v->Dy - 3);\n        v->yindex = u8(row * 10);", "        v->Ay = u8(v->Dy - 2);\n        v->yindex = u8(row * 10);", None),
    ("            v->PRECED = v->objid;\n            v->spreced = v->state;\n            v->yindex++;\n        }\n    }",
     "            v->PRECED = v->objid;\n            v->yindex++;\n        }\n    }", None),
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
    ("        v->hr.OPACITY = v->bgOP[x];\n        pop_hr_fastlay(&v->hr, im);", "        v->hr.OPACITY = POP_OP_ORA;\n        pop_hr_fastlay(&v->hr, im);", None),
    ("        if (v->fgOP[x] == POP_OP_MASK) {\n            pop_hr_fastmask(&v->hr, im);", "        if (v->fgOP[x] == POP_OP_AND) {\n            pop_hr_fastmask(&v->hr, im);", None),
    ("        case POP_MID_LAY:\n            setaddl(v, x);\n            pop_hr_lay(&v->hr, im);", "        case POP_MID_LAY:\n            pop_hr_lay(&v->hr, im);", None),
    ("    v->hr.RIGHTCUT = v->midCR[x];", "    v->hr.RIGHTCUT = 40;", "every mid entry of the compared screens has the whole screen as its area"),
    ("    drawback(v);\n    drawmid(v);\n    drawfore(v);", "    drawmid(v);\n    drawback(v);\n    drawfore(v);", None),
    ("    drawmid(v);\n    drawfore(v);\n    drawmsg(v);", "    drawfore(v);\n    drawmid(v);\n    drawmsg(v);",
     "the only mid entries of a screen without characters are flask bubbles, which no "
     "foreground piece overlaps; the order will matter once characters are drawn"),
    ("    sngpeel(v);                                /* the peel list set up two frames ago */\n", "", NO_CHARS),
    ("    drawwipe(v);\n", "", NO_CHARS),
    ("    pop_im = pop_imgtab_image", "    pop_im = pop_imgtab_image", "placeholder"),
]
LISTS = [m for m in LISTS if m[2] != "placeholder"]

FILES = (("pop_frameadv.c", FRAMEADV), ("pop_lists.c", LISTS))


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
    video = ("pop_hires.c", "pop_lists.c", "pop_frameadv.c")
    includes = ["-I" + os.path.join(PORT, "src", d) for d in ("core", "video", "assets")]
    cache = os.path.join(tempfile.mkdtemp(), "rooms.cache")
    env = dict(os.environ, POP_ROOMS_CACHE=cache)

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
            jobs.append((name, text.replace(old, new), old, new, why))

    def build_and_run(name, source_text):
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
            subprocess.run(["cc", "-std=c17", "-O1"] + includes + sources + fixed
                           + ["-o", exe, "-lm"], check=True)
            r = subprocess.run([sys.executable, os.path.join(HERE, "compare_rooms.py"), exe, data,
                                source, variations, seed], capture_output=True, text=True, env=env)
            return r.returncode
        finally:
            shutil.rmtree(work)

    # the unchanged code first: fills the cache, and must pass
    with open(os.path.join(PORT, "src", "video", "pop_lists.c")) as f:
        if build_and_run("pop_lists.c", f.read()) != 0:
            print("the comparison fails on the unchanged code")
            return 1

    caught = same = wrong = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        results = pool.map(lambda j: build_and_run(j[0], j[1]) != 0, jobs)
        for job, noticed in zip(jobs, results):
            name, _, old, new, why = job
            label = "%s: %s" % (name, (new.strip().replace("\n", " ")
                                       or "(removed) " + old.strip())[:100])
            if noticed and why is None:
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
    print("%d changes: %d caught, %d make no difference, %d unexpected"
          % (len(jobs) + stale, caught, same, wrong + stale))
    return 1 if wrong or stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
