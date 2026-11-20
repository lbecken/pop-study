#!/usr/bin/env python3
"""Does fuzz_control.py notice a wrong port?  A check of the test itself.

New code for the pop-study project.  Not part of CTest (it takes minutes).

Each entry below is one small deliberate error in pop_control.c or
pop_collide.c: a threshold off by one, a comparison changed, a statement
removed.  The script builds the
runner with that one change and runs fuzz_control.py, which must then report
a mismatch.  A change the comparison does not notice is either a gap in the
test's inputs or a change that makes no difference; those of the second
kind are marked with the reason.

Usage:
    mutation_check.py <data dir> <original checkout> [cases per routine] [seed] [only]

`only` is a comma-separated list; just the changes in a file named there, or
whose replacement text contains one of the entries, are tried.

Needs a C compiler as `cc`.
"""

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

# file, its deliberate errors, and the group of routines whose comparison
# should notice them (running only that group keeps the check short)
FILES = (("pop_control.c", MUTANTS, "@control"),
         ("pop_collide.c", COLLIDE_MUTANTS, "@collision"))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    data, source = argv[1:3]
    cases = argv[3] if len(argv) > 3 else "400"
    seed = argv[4] if len(argv) > 4 else "1989"
    only = argv[5].split(",") if len(argv) > 5 else None
    core = ("pop_control.c", "pop_collide.c", "pop_seq.c", "pop_sound.c", "pop_world.c")
    others = [os.path.join(PORT, *p) for p in (
        ("src", "assets", "pop_assets.c"), ("src", "audio", "pop_sfx.c"),
        ("tools", "pop_worldrun.c"))]
    includes = ["-I" + os.path.join(PORT, "src", d) for d in ("core", "assets", "audio")]
    work = tempfile.mkdtemp()
    total = caught = same = wrong = 0
    try:
        for name, mutants, group in FILES:
            with open(os.path.join(PORT, "src", "core", name)) as f:
                text = f.read()
            sources = [os.path.join(PORT, "src", "core", c) for c in core if c != name]
            for old, new, why in mutants:
                if only is not None and name not in only and not any(k in new for k in only):
                    continue
                total += 1
                if text.count(old) != 1:
                    print("STALE   pattern occurs %d times: %s" % (text.count(old), old.strip()))
                    wrong += 1
                    continue
                mutant = os.path.join(work, name)
                with open(mutant, "w") as f:
                    f.write(text.replace(old, new))
                exe = os.path.join(work, "run")
                subprocess.run(["cc", "-std=c17", "-O1"] + includes + [mutant] + sources
                               + others + ["-o", exe, "-lm"], check=True)
                r = subprocess.run([sys.executable, os.path.join(HERE, "fuzz_control.py"), exe,
                                    data, source, cases, seed, group],
                                   capture_output=True, text=True)
                noticed = r.returncode != 0
                label = "%s: %s" % (name, new.strip().replace("\n", " ") or "(removed) "
                                    + old.strip())
                if noticed and why is None:
                    caught += 1
                    print("caught  " + label)
                elif not noticed and why is not None:
                    same += 1
                    print("same    %s   [%s]" % (label, why))
                else:
                    wrong += 1
                    print("%s  %s" % ("MISSED" if why is None else "UNEXPECTEDLY CAUGHT", label))
    finally:
        shutil.rmtree(work)
    print("%d changes: %d caught, %d make no difference, %d unexpected"
          % (total, caught, same, wrong))
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
