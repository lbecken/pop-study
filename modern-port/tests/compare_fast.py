#!/usr/bin/env python3
"""Differential test of the partial redraw: the C port against the
original drawing code.

New code for the pop-study project.

During play the game does not redraw the screen.  Each frame the game logic
marks, in the "redraw buffers", the blocks in which something changed, and
FAST (FRAMEADV.S 189) turns the marks into display-list entries: rectangles
to clear, whole blocks, the moving parts of a block, a floor piece in front
of a climbing character, front pieces, the top line of the screen, and the
strength meters when a redrawn block lies under them.

Here a screen is first drawn in full on both sides (as in compare_rooms.py),
then several times in a row: random marks are written into the redraw
buffers, some gadget states are changed, strengths are set, and the
original's ZEROLSTS, FAST and DRAWALL are run in the two-bank simulator and
pop_zerolsts, pop_fast and pop_drawall in C.  After each step the 8,192
bytes of the page drawn on, the five display lists (background, foreground,
mid, message, wipe), the 310 bytes of the redraw buffers and the two meter
flags must be equal.

There are no characters in this test: the object list is empty.

Usage:
    compare_fast.py <pop_gfxrun> <data dir> <original checkout> [screens per level] [seed]

POP_FAST_CACHE, if set, names a file in which the original's results are
kept for later runs with the same arguments (mutation_gfx.py).
"""

import os
import pickle
import random
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import compare_rooms as cr  # noqa: E402  (sets the import path for the oracle)
from mos6502 import CpuError  # noqa: E402
from pop_banked import AUX  # noqa: E402
from pop_oracle import BLUEPRINT  # noqa: E402
import render_room as rr  # noqa: E402
from pop_room import bgset_file_suffix  # noqa: E402

REDBUFS = 0x5E00             # EQ.S 24
RB_SIZE = 310
RB = {"half": 60, "red": 90, "fred": 120, "floor": 150, "wipe": 180, "move": 210, "obj": 240,
      "white": 270, "top": 300}
LISTS = cr.LISTS + (("msg", ("msgX", "msgY", "msgIMG", "msgOP", "msgOFF")),
                    ("wipe", ("wipeX", "wipeY", "wipeH", "wipeW", "wipeCOL")))
STEPS = 3                    # partial redraws after each full one
CRCS = ("page", "back", "fore", "mid", "msg", "wipe", "rb")
COUNTS = ("nback", "nfore", "nmid", "nmsg", "nwipe", "redkid", "redopp")


def original_fast(o, page):
    try:
        o.call("zerolsts")
        o.set_vars({"SCRNUM": o.var("VisScrn")})
        o.call("fast", max_cycles=80_000_000)
        o.call("drawall", max_cycles=80_000_000)
    except CpuError:
        return None
    out = {"page": zlib.crc32(o.page(2 if page else 1)),
           "rb": zlib.crc32(o.cpu.peek(REDBUFS, AUX, RB_SIZE)),
           "redkid": o.var("redkidmeter"), "redopp": o.var("redoppmeter")}
    text = []
    for name, arrays in LISTS:
        count = o.var(arrays[0])
        crc = 0
        columns = []
        for a in arrays:
            column = o.cpu.peek(o.addr(a) + 1, AUX, count) if count else b""
            crc = zlib.crc32(column, crc)
            columns.append(column)
        out[name] = crc
        out["n" + name] = count
        text += ["%s %s" % (name, ",".join(str(c[i]) for c in columns)) for i in range(count)]
    out["lists"] = "; ".join(text)       # for a failing case: the lists in full
    return out


def random_marks(rng):
    """Redraw buffers as the game might leave them, and as it would not."""
    rb = bytearray(RB_SIZE)
    density = rng.choice((0.02, 0.05, 0.05, 0.15, 0.15, 0.4, 1.0))
    kinds = [k for k in RB if k != "white" and rng.random() < 0.7] or ["red"]
    for kind in kinds:
        base, n = RB[kind], 10 if kind == "top" else 30
        for i in range(n):
            if rng.random() < density:
                rb[base + i] = rng.choice((1, 1, 2, 2, 2, 3, 255))
    for i in range(30):          # the height of a wipe; rarely more than there is room for
        rb[RB["white"] + i] = rng.choice((63, 63, 3, 16, 60, rng.randrange(1, 64)))
    if rng.random() < 0.02:
        rb[RB["white"] + rng.randrange(30)] = rng.randrange(256)
    if rng.random() < 0.3:       # under a meter only, or everywhere but
        under = rng.choice(((20, 21, 22), (28, 29), (20,), (22,), (28,), (29,), (23,), (27,)))
        kind = rng.choice(("red", "floor", "half", "fred", "wipe", "move", "obj"))
        if rng.random() < 0.5:
            rb[:RB["white"]] = bytes(RB["white"])
        for i in under:
            rb[RB[kind] + i] = rng.choice((1, 2))
    return rb


def original_results(source, per_level, seed):
    bg = rr.load_bgdata(source)
    orig = cr.Original(source)
    o = orig.o
    commands, expected = [], []
    for level_no in range(15):
        with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL%d" % level_no), "rb") as f:
            level = f.read()
        set1, set2 = bg.level_bgset1[level_no], bg.level_bgset2[level_no]
        start = ["L " + level.hex(),
                 "T 1 " + os.path.join(source, cr.IMAGES, "IMG.BGTAB1." + bgset_file_suffix(set1)),
                 "T 2 " + os.path.join(source, cr.IMAGES, "IMG.BGTAB2." + bgset_file_suffix(set2)),
                 "S level=%d BGset1=%d KidScrn=0 KidBlockX=0 KidBlockY=0 PAGE=0" % (level_no, set1),
                 "R setinitials"]
        screens = level[2048] - 1
        rng = random.Random("%d/%d/fast" % (seed, level_no))

        def change_states(share, screen):
            now = o.cpu.peek(BLUEPRINT, AUX, 2304)
            pokes = {}
            for at in range(720):
                piece = now[at] & 0x1F
                if piece in cr.STATES and rng.random() < share:
                    pokes[720 + at] = rng.choice(cr.STATES[piece])
            if rng.random() < 0.5:           # other pieces on this screen and in the row above it
                above = level[1952 + 4 * (screen - 1) + 2]
                for _ in range(rng.randint(1, 6)):
                    if above and rng.random() < 0.3:
                        at = (above - 1) * 30 + 20 + rng.randrange(10)
                    else:
                        at = (screen - 1) * 30 + rng.randrange(30)
                    piece = rng.randrange(30)
                    pokes[at] = piece
                    if piece in cr.STATES:
                        pokes[720 + at] = rng.choice(cr.STATES[piece])
            for at, value in pokes.items():
                o.cpu.poke(BLUEPRINT + at, AUX, value)
            if pokes:
                commands.append("P " + " ".join("%d=%d" % kv for kv in pokes.items()))

        for _ in range(per_level):
            # the level afresh for every screen, so that a case in which the
            # original writes outside the modelled memory spoils only the
            # steps on that screen
            orig.start_level(level, level_no, set1, set2)
            commands += start
            expected.append((level_no, 0, "setinitials", {}))
            screen = rng.randint(1, screens)
            page = rng.choice((0, 0x20))
            change_states(0.3, screen)
            right = level[1952 + 4 * (screen - 1) + 1]
            kid = {"KidScrn": rng.choice((screen, screen, right, 0)), "KidBlockX": rng.randrange(10),
                   "KidBlockY": rng.randrange(3), "PAGE": page}
            o.set_vars(kid)
            o.set_vars({"VisScrn": screen})
            commands.append("S " + " ".join("%s=%d" % kv for kv in kid.items()))
            commands.append("R room %d" % screen)
            try:
                o.call("getscrns")
                o.call("DoSure", module="TOPCTRL", max_cycles=80_000_000)
                drawn = True
            except CpuError:
                drawn = False
            expected.append((level_no, screen, "room", {} if drawn else None))
            def poke(pokes):
                for where, value in pokes.items():
                    o.cpu.poke(BLUEPRINT + where, AUX, value)
                commands.append("P " + " ".join("%d=%d" % kv for kv in pokes.items()))

            def fast(rb):
                o.cpu.load_bank(REDBUFS, bytes(rb), AUX)
                strength = {"KidStrength": rng.choice((0, 1, 1, 2, 3, 4, 5, 6, 7, 9, 10, 12)),
                            "MaxKidStr": rng.choice((0, 3, 3, 4, 5, 7, 10, 12)),
                            "OppStrength": rng.choice((0, 1, 1, 2, 3, 4, 5, 6, 8, 12)),
                            "ShadID": rng.choice((0, 1, 1, 2, 2, 4, 24, 3))}
                if rng.random() < 0.03:      # more units than the meter has places for
                    strength[rng.choice(("KidStrength", "MaxKidStr", "OppStrength"))] = \
                        rng.choice((13, 14, 200))
                o.set_vars(strength)
                commands.append("H " + rb.hex())
                commands.append("S " + " ".join("%s=%d" % kv for kv in strength.items()))
                commands.append("R fast")
                expected.append((level_no, screen, "fast", original_fast(o, page) if drawn else None))

            for step in range(STEPS):
                if rng.random() < 0.5:
                    change_states(0.05, screen)
                rb = random_marks(rng)
                kind = rng.random()
                if kind < 0.1:
                    # A torch whose state is beyond the flame table is drawn
                    # with the image number and mode last used (SETUPFLAME
                    # sets neither).  Make its flame the first thing added,
                    # so that those are what the last DRAWALL left behind:
                    # from an image or, when it drew nothing else, a wipe.
                    if rng.random() < 0.15:
                        rb = bytearray(RB_SIZE)
                        clear = tuple(range(20)) + (23, 24, 25, 26, 27)      # not under a meter
                        if rng.random() < 0.5:       # more than the wipe list holds
                            blocks = clear
                        else:
                            blocks = [rng.choice(clear) for _ in range(rng.randint(1, 3))]
                        for block in blocks:
                            rb[RB["wipe"] + block] = 1
                            rb[RB["white"] + block] = rng.randrange(1, 64)
                        fast(rb)
                    at = (screen - 1) * 30 + rng.randrange(3) * 10 + rng.randrange(9)
                    poke({at: 19, 720 + at: rng.choice((18, 19, 21, 255))})
                    rb = bytearray(RB_SIZE)
                    rb[RB["move"] + at % 30 + 1] = 1
                elif kind < 0.25:
                    # the floor piece under a character who climbs up or
                    # drops: each kind of piece that has, or lacks, a short
                    # version, with empty space to its left or not
                    at = (screen - 1) * 30 + rng.randrange(3) * 10 + rng.randrange(1, 10)
                    piece = rng.choice((1, 19, 16, 3, 25, 6, 15, 11, 14, 2, 8, 10))
                    pokes = {at: piece, at - 1: rng.choice((0, 0, 0, 1, 20))}
                    if piece in cr.STATES:
                        pokes[720 + at] = rng.choice(cr.STATES[piece])
                    poke(pokes)
                    rb = bytearray(RB_SIZE)
                    rb[RB[rng.choice(("half", "half", "floor"))] + at % 30] = rng.choice((1, 2))
                    rb[RB["half"] + at % 30 - 1] = rng.choice((0, 1))
                    if rng.random() < 0.5:   # after the block itself: its pieces move the column
                        rb[RB[rng.choice(("red", "red", "move", "wipe"))] + at % 30] = 1
                fast(rb)
    return commands, expected


def compare(runner, data, commands, expected):
    out = subprocess.run([runner, os.path.join(data, "bgdata.bin")],
                         input="\n".join(commands) + "\n", capture_output=True, text=True)
    if out.returncode != 0:
        print("pop_gfxrun failed: " + out.stderr[-500:])
        return 1
    lines = out.stdout.splitlines()
    if len(lines) != len(expected):
        print("expected %d result lines, got %d" % (len(expected), len(lines)))
        return 1
    which = [i for i, c in enumerate(commands) if c.startswith("R ")]   # one result line each
    failures = skipped = compared = 0
    most = dict.fromkeys(COUNTS[:5], 0)
    meters = 0
    tainted = False
    for number, (line, (level_no, screen, what, want)) in enumerate(zip(lines, expected)):
        got = {}
        for tok in line.split():
            k, v = tok.split("=")
            got[k] = int(v, 16) if k in CRCS else int(v)
        if what == "setinitials":
            tainted = False              # both sides start afresh
            continue
        # Where the original would write outside the memory the port models
        # (a wipe higher than the screen above it, say), its later results
        # on the same screen may not mean anything.
        if want is None or got["unmodelled"]:
            tainted = True
        if what != "fast":
            continue
        if tainted:
            skipped += 1
            continue
        compared += 1
        for k in most:
            most[k] = max(most[k], want[k])
        meters += want["nmsg"] > 0
        problems = ["%s: C %x, original %x" % (k, got[k], want[k])
                    for k in COUNTS + CRCS if got[k] != want[k]]
        if problems:
            failures += 1
            if failures <= 8:
                print("MISMATCH level %d screen %d" % (level_no, screen))
                for p in problems:
                    print("   " + p)
            if failures == 1:            # the first one in full
                again = subprocess.run([runner, os.path.join(data, "bgdata.bin")],
                                       input="\n".join(commands[:which[number] + 1] + ["R lists"]) + "\n",
                                       capture_output=True, text=True)
                print("   original: " + want["lists"])
                print("   C:        " + again.stdout.splitlines()[-1])
                print("   " + "\n   ".join(c[:300] for c in commands[which[number] - 4:which[number] + 1]
                                          if c[0] != "L"))
    if failures:
        print("%d of %d compared partial redraws differ" % (failures, compared))
        return 1
    if skipped * 4 > compared + skipped:
        print("too many skipped: %d of %d" % (skipped, compared + skipped))
        return 1
    print("%d partial redraws compared: C and original agree on every byte of the page, on the "
          "five display lists, on the redraw buffers afterwards and on the meter flags; "
          "%d skipped (outside the modelled memory)" % (compared, skipped))
    print("   longest lists in the original's runs: %d background, %d foreground, %d mid, "
          "%d message, %d wipe entries; %d redraws drew a strength meter"
          % (most["nback"], most["nfore"], most["nmid"], most["nmsg"], most["nwipe"], meters))
    return 0


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    runner, data, source = argv[1:4]
    per_level = int(argv[4]) if len(argv) > 4 else 20
    seed = int(argv[5]) if len(argv) > 5 else 1989
    cache = os.environ.get("POP_FAST_CACHE")
    if cache and os.path.exists(cache):
        with open(cache, "rb") as f:
            key, commands, expected = pickle.load(f)
        if key != (source, per_level, seed):
            print("the cache %s was made with other arguments" % cache)
            return 2
    else:
        commands, expected = original_results(source, per_level, seed)
        if cache:
            with open(cache, "wb") as f:
                pickle.dump(((source, per_level, seed), commands, expected), f)
    return compare(runner, data, commands, expected)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
