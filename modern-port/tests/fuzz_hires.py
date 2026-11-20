#!/usr/bin/env python3
"""Differential test of the blitter: the C port against the original HIRES.S.

New code for the pop-study project.

The original blitter is assembled from the user's checkout and run in a
6502 simulator that models the Apple IIe's two memory banks
(tools/oracle/pop_gfx_oracle.py).  It is called the way the game calls it:
from auxiliary memory, through the GRAFIX entry points.  The same call is
made on the C port (pop_hiresrun).  After every call the two hi-res pages
(16,384 bytes), the save-under buffers (4,096 bytes) and the save-under
variables must be equal.

Each case starts from two pages of pseudo-random bytes, so that every draw
mode's dependence on what is already on the screen is exercised.  Images
are random too: widths 0-12 (now and then up to 40), heights 0-70, placed
anywhere from well off one edge to well off the other, with random usable
areas (the "cut" parameters), all seven dot offsets, all draw modes, plain
and mirrored.  A save-under, a draw and a restore are run as one chain.

Cases in which the port reports that the original would leave the memory
it models (an image byte without its high bit, a write outside the pages)
are counted and skipped.

Usage:
    fuzz_hires.py <pop_hiresrun> <original checkout> [cases] [seed]
"""

import os
import random
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "oracle"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "extract_assets"))

from mos6502 import CpuError  # noqa: E402
from pop_banked import AUX, MAIN  # noqa: E402
from pop_gfx_oracle import GfxOracle  # noqa: E402

TABLE_AT = 0x6000            # a one-image table is built here for each case
ROUTINES = ("lay", "lay", "lay", "chain", "fastlay", "fastmask", "fastblack", "cls",
            "copyscrn", "inverty")
FULL = {"LEFTCUT": 0, "RIGHTCUT": 40, "TOPCUT": 0, "BOTCUT": 192}


def background(seed):
    """xorshift32, as in pop_hiresrun.c."""
    x = seed or 1
    out = bytearray(0x4000)
    for i in range(0x4000):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[i] = x >> 24
    return bytes(out)


def random_image(rng, small=False):
    w = rng.choice((1, 1, 2, 2, 3, 3, 4, 5, 6, 8, 12)) if rng.random() < (0.96 if small else 0.9) \
        else (0 if small else rng.choice((0, 0, 20, 39, 40)))
    h = rng.choice((1, 2, 3, 8, 20, 33, 41, 63, 70)) if rng.random() < 0.95 else 0
    stray = rng.random() < 0.03                      # a byte without its high bit
    data = bytes((rng.randrange(256) | (0 if stray and rng.random() < 0.1 else 0x80))
                 for _ in range(w * h))
    return bytes([w, h]) + data


def random_params(rng, image, fast=False):
    w, h = image[0], image[1]
    p = dict(FULL)
    p["PAGE"] = rng.choice((0, 0, 0x20))
    p["OFFSET"] = rng.randrange(7) if rng.random() < 0.97 else rng.choice((7, 200))
    if fast:
        p["XCO"] = rng.randint(0, max(0, 40 - w))
        p["YCO"] = rng.randint(0, 191)
        p["OFFSET"] = 0
        return p
    p["XCO"] = rng.choice((rng.randint(0, 39), rng.randint(0, 39), rng.randint(-14, 54) & 0xFF))
    p["YCO"] = rng.choice((rng.randint(0, 191), rng.randint(0, 191), rng.randrange(256)))
    if rng.random() < 0.3:                           # a smaller usable area
        p.update(LEFTCUT=rng.choice((0, 0, 2, 9, 20)), RIGHTCUT=rng.choice((40, 40, 38, 30, 21)),
                 TOPCUT=rng.choice((0, 0, 1, 40, 100)), BOTCUT=rng.choice((192, 192, 190, 120)))
    if rng.random() < 0.6 and p["BOTCUT"] < 192:     # the bottom line at the area's edge
        p["YCO"] = (p["BOTCUT"] + rng.choice((-1, 0, 1, 1, 2, 2, 3, h - 2, h - 1, h, h + 1))) & 0xFF
    if rng.random() < 0.03 and 0 < h < 8:            # right of the last byte of a line whose
        p.update(RIGHTCUT=60, BOTCUT=192, TOPCUT=0,  # address ends in $D0: the carry goes
                 XCO=rng.randint(48, 54), YCO=191)   # into the high byte
    if rng.random() < 0.06:                          # beyond the screen
        p.update(RIGHTCUT=rng.choice((41, 60, 60)), BOTCUT=rng.choice((192, 193, 200, 255)))
        if rng.random() < 0.6:                       # right of the line's last byte: the
            p["XCO"] = rng.randint(41, 56)           # address carries into the next page
            p["YCO"] = rng.randint(0, 191)
    return p


class Original:
    def __init__(self, source):
        self.o = GfxOracle(source)

    def start(self, seed):
        o = self.o
        o.reset()
        o.cpu.poke(0x2000, MAIN, background(seed))
        self.set_peel(0xD000, 0)

    def set_peel(self, peelbuf, peelimg):
        o = self.o
        o.cpu.poke(o.addr("PEELBUF"), AUX, bytes([peelbuf & 0xFF, peelbuf >> 8]))
        o.cpu.poke(o.addr("PEELIMG"), AUX, bytes([peelimg & 0xFF, peelimg >> 8]))

    def set_image(self, image, bank):
        o = self.o
        first = TABLE_AT + 5
        end = first + len(image)
        table = bytes([1, first & 0xFF, first >> 8, end & 0xFF, end >> 8]) + image
        o.cpu.poke(TABLE_AT, bank, table)
        o.cpu.poke(o.addr("TABLE"), AUX, bytes([TABLE_AT & 0xFF, TABLE_AT >> 8]))
        o.cpu.poke(o.addr("IMAGE"), AUX, bytes([1, 0]))
        o.set_vars({"BANK": 3 if bank == AUX else 2})

    def run(self, routine, a=0, b=0):
        o = self.o
        try:
            if routine == "peel":
                at = o.addr("PEELIMG")
                o.cpu.poke(o.addr("IMAGE"), AUX, o.cpu.peek(at, AUX, 2))
            elif routine == "fastblack":
                o.cpu.poke(o.addr("IMAGE"), AUX, bytes([b, a]))      # height, width
            elif routine == "copyscrn":
                o.cpu.poke(o.addr("IMAGE"), AUX, bytes([a, b]))      # from, to
                routine = "copy2000"
            o.call(routine, max_cycles=30_000_000)
        except CpuError:
            return None
        peelbuf = o.cpu.peek(o.addr("PEELBUF"), AUX, 2)
        peelimg = o.cpu.peek(o.addr("PEELIMG"), AUX, 2)
        return {"pages": zlib.crc32(o.cpu.peek(0x2000, MAIN, 0x4000)),
                "peel": zlib.crc32(o.cpu.peek(0xD000, MAIN, 0x1000)),
                "PEELBUF": peelbuf[0] | (peelbuf[1] << 8),
                "PEELIMG": peelimg[0] | (peelimg[1] << 8),
                "PEELXCO": o.var("PEELXCO"), "PEELYCO": o.var("PEELYCO")}


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    runner, source = argv[1:3]
    cases = int(argv[3]) if len(argv) > 3 else 1500
    seed = int(argv[4]) if len(argv) > 4 else 1989
    orig = Original(source)
    commands, expected = [], []

    def step(case, routine, what, a=0, b=0):
        want = orig.run(routine, a, b)
        commands.append("R %s %d %d" % (routine, a, b))
        expected.append((case, routine, what, want))
        return want

    for case in range(cases):
        rng = random.Random("%d/%d" % (seed, case))
        routine = rng.choice(ROUTINES)
        bg = rng.randrange(1, 1 << 32)
        orig.start(bg)
        commands.append("B %d" % bg)
        fast = routine in ("fastlay", "fastmask")
        image = random_image(rng, small=fast)
        p = random_params(rng, image, fast=fast or routine in ("fastblack",))
        if routine in ("lay", "chain"):
            p["OPACITY"] = rng.choice((0, 1, 2, 3, 4, 4, 4, 5)) | rng.choice((0, 0x80))
            if rng.random() < 0.02:
                p["OPACITY"] = rng.choice((6, 9, 0x86))
        elif routine == "fastlay":
            p["OPACITY"] = rng.choice((0, 1, 2, 2, 3))
        elif routine == "fastblack":
            p["OPACITY"] = rng.choice((0x80, 0x00, 0xFF, 0x2A))      # the colour
        else:
            p["OPACITY"] = 0
        # the fast routines read their image from auxiliary memory (SETFASTAUX)
        orig.set_image(image, AUX if fast or rng.random() < 0.5 else MAIN)
        orig.o.set_vars(p)
        commands.append("I " + image.hex())
        commands.append("S " + " ".join("%s=%d" % kv for kv in p.items()))
        what = (image[0], image[1], dict(p))
        if routine == "chain":                       # save under, draw, put back
            if rng.random() < 0.5:                   # part of the buffer is in use already
                peel = (0xD000 + rng.randrange(0x400), 0xD000 + rng.randrange(0x400))
                orig.set_peel(*peel)
                commands.append("S PEELBUF=%d PEELIMG=%d" % peel)
            saved = step(case, "layrsave", what)
            drawn = step(case, "lay", what)
            if saved and drawn and saved["PEELIMG"] >> 8:
                back = {"XCO": saved["PEELXCO"], "YCO": saved["PEELYCO"]}
                orig.o.set_vars(back)
                commands.append("S " + " ".join("%s=%d" % kv for kv in back.items()))
                step(case, "peel", what)
        elif routine == "fastblack":
            w, h = rng.choice((1, 2, 5, 40, 0)), rng.choice((1, 3, 30, 63, 0))
            p["YCO"] = rng.randint(min(191, h), 191)
            orig.o.set_vars({"YCO": p["YCO"]})
            commands.append("S YCO=%d" % p["YCO"])
            step(case, "fastblack", what, w, h)
        elif routine == "copyscrn":
            a, b = rng.choice(((0x20, 0x40), (0x40, 0x20)))
            step(case, "copyscrn", what, a, b)
        elif routine == "inverty":                   # everything after is drawn upside down
            step(case, "inverty", what)
            p["OPACITY"] = rng.choice((1, 2, 4, 0x84))
            orig.o.set_vars(p)
            commands.append("S OPACITY=%d" % p["OPACITY"])
            step(case, "lay", what)
        else:
            step(case, routine, what)

    out = subprocess.run([runner], input="\n".join(commands) + "\n", capture_output=True,
                         text=True)
    if out.returncode != 0:
        print("pop_hiresrun failed: " + out.stderr[-500:])
        return 1
    lines = out.stdout.splitlines()
    if len(lines) != len(expected):
        print("expected %d result lines, got %d" % (len(expected), len(lines)))
        return 1

    failures, compared, skipped, dead = 0, {}, 0, -1
    for line, (case, routine, what, want) in zip(lines, expected):
        if case == dead:
            continue
        got = {}
        for tok in line.split():
            k, v = tok.split("=")
            got[k] = int(v, 16) if k in ("pages", "peel") else int(v)
        if want is None or got["unmodelled"]:
            skipped += 1
            dead = case                              # the rest of a chain cannot be compared
            continue
        compared[routine] = compared.get(routine, 0) + 1
        problems = ["%s: C %x, original %x" % (k, got[k], want[k])
                    for k in ("pages", "peel", "PEELBUF", "PEELIMG", "PEELXCO", "PEELYCO")
                    if got[k] != want[k]]
        if problems:
            failures += 1
            dead = case
            if failures <= 6:
                print("MISMATCH case %d, %s; image %dx%d, %r" % ((case, routine) + what))
                for p in problems:
                    print("   " + p)
    total = sum(compared.values())
    if failures:
        print("%d of %d compared calls differ" % (failures, total))
        return 1
    if skipped * 5 > len(expected):
        print("too many calls skipped: %d of %d" % (skipped, len(expected)))
        return 1
    print("%d calls compared (%s): C blitter and original agree on every byte of both pages "
          "and of the save-under buffers; %d calls skipped (outside the modelled memory)"
          % (total, ", ".join("%s %d" % kv for kv in sorted(compared.items())), skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
