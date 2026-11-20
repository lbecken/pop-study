#!/usr/bin/env python3
"""Does fuzz_hires.py notice a wrong blitter?  A check of the test itself.

New code for the pop-study project.  Not part of CTest (it takes minutes).

Each entry is one small deliberate error in src/video/pop_hires.c.  The
script builds pop_hiresrun with that one change and runs fuzz_hires.py,
which must then report a mismatch.  Entries with a reason are changes that
cannot make a difference; for those the comparison must still pass.

Usage:
    mutation_hires.py <original checkout> [cases] [seed]
"""

import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, "..")

MUTANTS = [
    ("SHIFT[n][b] = u8(((b << n) & 0x7F) | 0x80);", "SHIFT[n][b] = u8(((b << n) & 0x7F));", None),
    ("CARRY[n][b] = u8(b >> (7 - n));", "CARRY[n][b] = u8(b >> (6 - n));", None),
    ("if (b & (1u << k)) m |= 1u << (6 - k);", "if (b & (1u << k)) m |= 1u << (7 - k);", None),
    ("MASKTAB[b] = u8(0x80 | (~(b | (b << 1) | (b >> 1)) & 0x7F));",
     "MASKTAB[b] = u8(0x80 | (~(b | (b << 1)) & 0x7F));", None),
    ("{ 0x80, 0x81, 0x83, 0x87, 0x8F, 0x9F, 0xBF };", "{ 0x80, 0x81, 0x83, 0x87, 0x8F, 0x9F, 0x9F };", None),
    ("{ 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0 };", "{ 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xE0 };", None),
    ("    return y < POP_HR_ROWS ? h->ylo[y] : h->yhi[y - POP_HR_ROWS];", "    return h->ylo[y % POP_HR_ROWS];",
     "for a line of 192 or more the high address byte comes from the SHIFT0 table and the "
     "address is outside the pages whatever the low byte: the case is skipped either way"),
    ("    unsigned hi = (unsigned)yhi(h, y) + h->PAGE + (lo >> 8);", "    unsigned hi = (unsigned)yhi(h, y) + h->PAGE;", None),
    ("    if (a < h->BOTCUT) {                       /* the bottom line is inside */",
     "    if (a <= h->BOTCUT) {", None),
    ("        if (a < 191) {                         /* :topok */", "        if (a < 192) {",
     "the wrong version goes on to draw lines 255 down to 192, which are outside the pages, so "
     "the comparison skips the case; test_hires.c has it (an image one line taller than fits)"),
    ("            if (h->TOPCUT != 0 && u8(h->TOPCUT - 1) >= s->TOPEDGE) s->TOPEDGE = u8(h->TOPCUT - 1);",
     "            if (h->TOPCUT != 0 && u8(h->TOPCUT - 1) > s->TOPEDGE) s->TOPEDGE = u8(h->TOPCUT - 1);",
     "equal values: assigning changes nothing"),
    ("            if (h->TOPCUT != 0 && u8(h->TOPCUT - 1) >= s->TOPEDGE) s->TOPEDGE = u8(h->TOPCUT - 1);",
     "            if (h->TOPCUT != 0) s->TOPEDGE = u8(h->TOPCUT - 1);", None),
    ("            s->TOPEDGE = u8(h->TOPCUT - 1);\n        }\n    } else {", "            s->TOPEDGE = h->TOPCUT;\n        }\n    } else {", None),
    ("        a = u8(u8(a - s->HEIGHT) + 1);         /* top line */", "        a = u8(a - s->HEIGHT);", None),
    ("        if (a >= h->BOTCUT) return 0;\n", "        if (a > h->BOTCUT) return 0;\n",
     "as above: the wrong version wraps around through the lines outside the pages and the case "
     "is skipped; test_hires.c has it (an image whose top line is the first one below the area)"),
    ("        } while (x >= h->BOTCUT);", "        } while (x > h->BOTCUT);", None),
    ("    if (NEG(a) || a < h->LEFTCUT) {            /* :leftoff */", "    if (a < h->LEFTCUT) {", None),
    ("        if (NEG(a)) return 0;\n        s->VISWIDTH = a;", "        if (a == 0) return 0;\n        s->VISWIDTH = a;", None),
    ("    if (a >= h->RIGHTCUT) return 0;\n    a = u8(a + s->WIDTH);", "    if (a > h->RIGHTCUT) return 0;\n    a = u8(a + s->WIDTH);", None),
    ("    if (a < h->RIGHTCUT) {                     /* :bothok */", "    if (a <= h->RIGHTCUT) {", None),
    ("    s->OFFRIGHT = u8(s->RMOST + 1);", "    s->OFFRIGHT = s->RMOST;", None),
    ("    s->VISWIDTH = u8(h->RIGHTCUT - s->XCO);", "    s->VISWIDTH = u8(h->RIGHTCUT - s->XCO - 1);", None),
    ("    case POP_OP_AND: case POP_OP_MASK: return a & screen;", "    case POP_OP_AND: case POP_OP_MASK: return a | screen;", None),
    ("    case POP_OP_AND: case POP_OP_MASK: return a & screen;\n    case POP_OP_ORA: return a | screen;\n    case POP_OP_STA: case POP_OP_VISMASK: return a;",
     "    case POP_OP_AND: case POP_OP_MASK: case POP_OP_VISMASK: return a & screen;\n    case POP_OP_ORA: return a | screen;\n    case POP_OP_STA: return a;", None),
    ("    return x >= 6 ? u8(x - 1) : u8(x + 1);", "    return u8(x + 1 > 6 ? 6 : x + 1);", None),
    # lay_gen
    ("            b = img(h, &s, (size_t)s.OFFLEFT - 1);\n            CARRYv = tab(h, carry, b);\n            s.ptr += s.OFFLEFT;\n            s.WIDTH = s.VISWIDTH;\n            if (s.VISWIDTH == 0) goto extra;\n        } else {                               /* (b) ... from the screen */",
     "            CARRYv = 0;\n            s.ptr += s.OFFLEFT;\n            s.WIDTH = s.VISWIDTH;\n            if (s.VISWIDTH == 0) goto extra;\n        } else {", None),
    ("            CARRYv = rd(h, base) & amask;\n        }\n        do {                                   /* :3 */",
     "            CARRYv = 0;\n        }\n        do {", None),
    ("            a = u8((rd(h, u16(base + yy)) & bmask) | CARRYv);\n            a = combine(h, op, a, rd(h, u16(base + yy)));\n            wr(h, u16(base + yy), a);\n        }\n        s.ptr += s.WIDTH;                      /* :5 next line up */",
     "            a = CARRYv;\n            a = combine(h, op, a, rd(h, u16(base + yy)));\n            wr(h, u16(base + yy), a);\n        }\n        s.ptr += s.WIDTH;", None),
    # lay_mask
    ("            CARRYv = amask;\n            carryim = amask & rd(h, base);", "            CARRYv = amask;\n            carryim = 0;",
     "the screen dots left of the image are kept by the mask byte's own left part already; "
     "ORing them in again changes nothing"),
    ("            imbyte = tab(h, shift, b) | carryim;             /* the image, shifted */\n            carryim = tab(h, carry, b);\n            m = tab(h, MASKTAB, b);\n            a = tab(h, shift, m) | CARRYv;                   /* its mask, shifted */\n            a = combine(h, op, a, rd(h, u16(base + yy)));\n            wr(h, u16(base + yy), a | imbyte);",
     "            imbyte = tab(h, shift, b) | carryim;\n            carryim = tab(h, carry, b);\n            m = tab(h, MASKTAB, b);\n            a = tab(h, shift, m) | CARRYv;\n            a = combine(h, op, a, rd(h, u16(base + yy)));\n            wr(h, u16(base + yy), a);", None),
    ("            a = bmask | CARRYv;\n            a = combine(h, op, a, rd(h, u16(base + yy)));\n            wr(h, u16(base + yy), a | carryim);",
     "            a = bmask;\n            a = combine(h, op, a, rd(h, u16(base + yy)));\n            wr(h, u16(base + yy), a | carryim);", None),
    # lay_xor
    ("    amask = AMASKS[x1];                        /* (of the second offset, as in the original) */",
     "    amask = AMASKS[h->OFFSET];",
     "the first byte is ORed with the whole screen byte, so which of its dots the left-edge "
     "mask picks out beforehand makes no difference"),
    ("            wr(h, u16(base + yy), u8((a ^ imbyte) | 0x80));\n            CARRYv = tab(h, carry, b);",
     "            wr(h, u16(base + yy), u8(a ^ imbyte));\n            CARRYv = tab(h, carry, b);", None),
    ("            a = CARRYv | rd(h, u16(base + yy));\n            wr(h, u16(base + yy), u8((a ^ carryim) | 0x80));",
     "            a = CARRYv | rd(h, u16(base + yy));\n            wr(h, u16(base + yy), u8(a | 0x80));", None),
    # mirrored
    ("    b.hi = u8(yhi(h, y) + h->PAGE);", "    b.hi = yhi(h, y);", None),
    ("    prep(h, &s, im);\n    s.XCO = u8(s.XCO - s.WIDTH);\n    if (!crop(h, &s)) return;\n    if (h->OFFSET > 6) { h->unmodelled = 1; return; }\n    shift = SHIFT[h->OFFSET];\n    carry = CARRY[h->OFFSET];\n    amask = AMASKS[h->OFFSET];\n    bmask = BMASKS[h->OFFSET];\n    y = s.YCO;\n    do {                                       /* :0 */",
     "    prep(h, &s, im);\n    s.XCO = u8(s.XCO - s.WIDTH + 1);\n    if (!crop(h, &s)) return;\n    if (h->OFFSET > 6) { h->unmodelled = 1; return; }\n    shift = SHIFT[h->OFFSET];\n    carry = CARRY[h->OFFSET];\n    amask = AMASKS[h->OFFSET];\n    bmask = BMASKS[h->OFFSET];\n    y = s.YCO;\n    do {", None),
    ("            if (yy == s.RMOST) break;          /* the rest is off the right edge */", "", None),
    ("            CARRYv = rd(h, mat(base, s.XCO)) & amask;\n            yy = u8(s.WIDTH - 1);",
     "            CARRYv = rd(h, mat(base, s.XCO)) & amask;\n            yy = s.WIDTH;", None),
    ("            a = u8((rd(h, at) & bmask) | CARRYv);            /* (not quite as lay_mask does it) */",
     "            a = u8(bmask | CARRYv);", None),      # differs only in "visible mask" mode
    ("            carryim = amask & rd(h, mat(base, s.XCO));\n            yy = u8(s.WIDTH - 1);",
     "            carryim = 0;\n            yy = u8(s.WIDTH - 1);",
     "as in the unmirrored routine: those dots are kept by the mask byte already"),
    ("            wr(h, at, u8((a ^ imbyte) | 0x80));\n            CARRYv = tab(h, carry, m);",
     "            wr(h, at, u8((a | imbyte) | 0x80));\n            CARRYv = tab(h, carry, m);", None),
    ("    if (NEG(whole)) {                          /* MLAY; the routines see OPACITY without bit 7 */\n        h->OPACITY = op;",
     "    if (NEG(whole)) {\n", "OPACITY is not among the compared results, and the routines mask it themselves"),
    ("    else if (op >= POP_OP_MASK) lay_mask(h, im);\n    else lay_gen(h, im);\n}", "    else lay_gen(h, im);\n}", None),
    # save-under
    ("    if (NEG(h->OPACITY)) s.XCO = u8(s.XCO - s.WIDTH);\n    s.WIDTH++;", "    if (NEG(h->OPACITY)) s.XCO = u8(s.XCO - s.WIDTH);", None),
    ("    if (NEG(h->OPACITY)) s.XCO = u8(s.XCO - s.WIDTH);\n", "", None),
    ("    h->PEELXCO = s.XCO;\n    h->PEELYCO = s.YCO;", "    h->PEELXCO = h->XCO;\n    h->PEELYCO = s.YCO;", None),
    ("    h->PEELXCO = s.XCO;\n    h->PEELYCO = s.YCO;", "    h->PEELXCO = s.XCO;\n    h->PEELYCO = h->YCO;", None),
    ("    wr(h, u16(h->PEELBUF + 1), u8(s.YCO - s.TOPEDGE));", "    wr(h, u16(h->PEELBUF + 1), s.HEIGHT);", None),
    ("        h->PEELBUF = u16(h->PEELBUF + s.VISWIDTH);", "        h->PEELBUF = u16(h->PEELBUF + s.WIDTH);", None),
    ("    h->PEELIMG &= 0x00FF;                      /* high byte 0: nothing saved */", "    h->PEELIMG = 0;", None),
    # fast
    ("    borrow = (width == 0) ? 1u : 0u;", "    borrow = 0u;",
     "with a width of 0 the routine reads the image 255 bytes on, outside the image, and the "
     "case is skipped either way"),
    ("    else top = 0xFF;                           /* \"limited Y-clipping\"", "    else top = 0;  /* \"", None),
    ("                a = tab(h, MASKTAB, b) & rd(h, u16(base + y));", "                a = tab(h, MASKTAB, b);", None),
    ("        ptr += width;\n        x--;\n    } while (x != top);\n    (void)peel;", "        ptr += width;\n        x--;\n    } while (x > top);\n    (void)peel;", None),
    ("         (h->OPACITY == POP_OP_STA) ? FAST_STA : FAST_OP);", "         FAST_OP);",
     "the general path stores the same bytes; the original's separate store routine is for speed"),
    ("    uint8_t top = u8(h->YCO - height - (width == 0 ? 1 : 0));    /* (no clipping here) */",
     "    uint8_t top = u8(h->YCO - height);", None),
    ("    unsigned i, at = ((unsigned)h->PAGE + 0x20u) << 8;", "    unsigned i, at = 0x2000u;", None),
    ("        wr(h, u16(((unsigned)to << 8) + i), rd(h, u16(((unsigned)from << 8) + i)));",
     "        wr(h, u16(((unsigned)from << 8) + i), rd(h, u16(((unsigned)to << 8) + i)));", None),
    ("    for (; high < 96; high++, low--) {", "    for (; high < 95; high++, low--) {", None),
]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    source = argv[1]
    cases = argv[2] if len(argv) > 2 else "1500"
    seed = argv[3] if len(argv) > 3 else "1989"
    path = os.path.join(PORT, "src", "video", "pop_hires.c")
    with open(path) as f:
        text = f.read()
    jobs, stale = [], 0
    for old, new, why in MUTANTS:
        if text.count(old) != 1:
            print("STALE   pattern occurs %d times: %s" % (text.count(old), old.strip()[:70]))
            stale += 1
            continue
        jobs.append((text.replace(old, new), old, new, why))

    def run(job):
        mutated, old, new, why = job
        work = tempfile.mkdtemp()
        try:
            mutant = os.path.join(work, "pop_hires.c")
            with open(mutant, "w") as f:
                f.write(mutated)
            exe = os.path.join(work, "run")
            subprocess.run(["cc", "-std=c17", "-O1", "-I" + os.path.join(PORT, "src", "video"),
                            mutant, os.path.join(PORT, "tools", "pop_hiresrun.c"), "-o", exe],
                           check=True)
            r = subprocess.run([sys.executable, os.path.join(HERE, "fuzz_hires.py"), exe, source,
                                cases, seed], capture_output=True, text=True)
            return r.returncode != 0
        finally:
            shutil.rmtree(work)

    caught = same = wrong = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        for job, noticed in zip(jobs, pool.map(run, jobs)):
            _, old, new, why = job
            label = (new.strip().replace("\n", " ") or "(removed) " + old.strip())[:110]
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
    print("%d changes: %d caught, %d make no difference, %d unexpected"
          % (len(jobs) + stale, caught, same, wrong + stale))
    return 1 if wrong or stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
