#!/usr/bin/env python3
"""Differential test of the full-screen redraw: the C port against the
original drawing code.

New code for the pop-study project.

For every screen that the 15 level files declare, the original's own redraw
(SETINITIALS, GETSCRNS, then DoSure: SURE fills the display lists and
DRAWALL draws them through the blitter) is run in the two-bank 6502
simulator (tools/oracle/pop_gfx_oracle.py), and the same is done in C
(pop_gfxrun: pop_setinitials, pop_getscrns, pop_sure, pop_drawall).  The
8,192 bytes of the finished hi-res page must be equal, and so must the
three display lists (background, foreground, mid).

A level as it starts shows each gadget in one state only.  So each level is
also redrawn a number of times with random states written into the level
on both sides: gates and exit doors at any height, loose floors at any
stage of falling, spikes, slicers, torches and flasks at any frame, pressure
plates up or down, any wall and panel pattern, and the kid placed where the
gate bars are drawn in front of him.

Usage:
    compare_rooms.py <pop_gfxrun> <data dir> <original checkout> [variations per level] [seed]

If the environment variable POP_ROOMS_CACHE names a file, the original's
results are kept there and reused by later runs with the same arguments
(mutation_gfx.py runs the comparison many times against one set of them).
"""

import os
import pickle
import random
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "oracle"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "extract_assets"))

from mos6502 import CpuError  # noqa: E402
from pop_banked import AUX  # noqa: E402
from pop_gfx_oracle import GfxOracle  # noqa: E402
from pop_oracle import BLUEPRINT  # noqa: E402
import render_room as rr  # noqa: E402
from pop_room import bgset_file_suffix  # noqa: E402

IMAGES = os.path.join("01 POP Source", "Images")
LISTS = (("back", ("bgX", "bgY", "bgIMG", "bgOP")), ("fore", ("fgX", "fgY", "fgIMG", "fgOP")),
         ("mid", ("midX", "midY", "midIMG", "midOP", "midOFF")))
# piece -> states worth drawing (BlueSpec values)
GATE_STATES = tuple(range(0, 196, 4)) + (1, 2, 3, 189, 200, 255)
STATES = {
    0: (0, 1, 2, 3, 4, 5, 255), 1: (0, 1, 2, 3, 4, 5, 255),          # space, floor: panels
    2: tuple(range(0, 10)) + (0x80, 0xFF),                           # spikes
    4: GATE_STATES, 16: GATE_STATES,                                 # gate, exit
    6: tuple(range(0, 8)), 15: tuple(range(0, 8)),                   # plates: a link index
    10: tuple(k << 5 | f for k in range(8) for f in (0, 1, 5, 8, 9, 31)),    # flask
    11: tuple(range(0, 13)) + tuple(0x80 | f for f in range(0, 13)),         # loose floor
    18: tuple(range(0, 16)) + tuple(0x80 | f for f in range(0, 16)),         # slicer
    19: tuple(range(0, 22)),                                         # torch
    20: (0, 1, 2, 3, 4, 255), 22: (0, 1, 2),                         # block, sword
    7: (0, 1, 2, 3, 4, 255), 12: (0, 1, 2, 3, 4, 255),               # panels
}


class Original:
    def __init__(self, source):
        self.o = GfxOracle(source)

    def start_level(self, data, number, set1, set2):
        o = self.o
        o.reset()
        o.load_table("bgtable1", "IMG.BGTAB1." + bgset_file_suffix(set1))
        o.load_table("bgtable2", "IMG.BGTAB2." + bgset_file_suffix(set2))
        o.load_level(data)
        o.set_vars({"level": number, "BGset1": set1, "BGset2": set2, "PAGE": 0})
        o.call("setinitials")

    def room(self, screen):
        o = self.o
        o.set_vars({"VisScrn": screen, "PAGE": 0})
        try:
            o.call("getscrns")
            o.call("DoSure", module="TOPCTRL", max_cycles=80_000_000)
        except CpuError:
            return None
        out = {"page": zlib.crc32(o.page(1))}
        for name, arrays in LISTS:
            count = o.var(arrays[0])
            crc = 0
            for a in arrays:
                crc = zlib.crc32(o.cpu.peek(o.addr(a) + 1, AUX, count) if count else b"", crc)
            out[name] = crc
            out["n" + name] = count
        return out


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    runner, data, source = argv[1:4]
    variations = int(argv[4]) if len(argv) > 4 else 40
    seed = int(argv[5]) if len(argv) > 5 else 1989
    cache = os.environ.get("POP_ROOMS_CACHE")
    if cache and os.path.exists(cache):
        with open(cache, "rb") as f:
            key, commands, expected = pickle.load(f)
        if key != (source, variations, seed):
            print("the cache %s was made with other arguments" % cache)
            return 2
    else:
        commands, expected = original_results(source, variations, seed)
        if cache:
            with open(cache, "wb") as f:
                pickle.dump(((source, variations, seed), commands, expected), f)
    return compare(runner, data, commands, expected)


def original_results(source, variations, seed):
    """The commands for the C runner and what the original produced for each."""
    bg = rr.load_bgdata(source)
    orig = Original(source)
    commands, expected = [], []

    def room(level_no, screen, what):
        commands.append("R room %d" % screen)
        expected.append((level_no, screen, what, orig.room(screen)))

    for level_no in range(15):
        with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL%d" % level_no), "rb") as f:
            level = f.read()
        set1, set2 = bg.level_bgset1[level_no], bg.level_bgset2[level_no]
        orig.start_level(level, level_no, set1, set2)
        commands += ["L " + level.hex(),
                     "T 1 " + os.path.join(source, IMAGES, "IMG.BGTAB1." + bgset_file_suffix(set1)),
                     "T 2 " + os.path.join(source, IMAGES, "IMG.BGTAB2." + bgset_file_suffix(set2)),
                     "S level=%d BGset1=%d KidScrn=0 KidBlockX=0 KidBlockY=0 PAGE=0" % (level_no, set1),
                     "R setinitials"]
        expected.append((level_no, 0, "setinitials", {}))
        screens = level[2048] - 1
        for screen in range(0, screens + 1):         # 0: the "null screen" beyond the map
            room(level_no, screen, "as the level starts")
        rng = random.Random("%d/%d" % (seed, level_no))
        for _ in range(variations):
            screen = rng.randint(1, screens)
            now = orig.o.cpu.peek(BLUEPRINT, AUX, 2304)
            pokes = {}
            for at in range(720):                    # any gadget anywhere may be seen from here
                piece = now[at] & 0x1F
                if piece in STATES and rng.random() < 0.5:
                    pokes[720 + at] = rng.choice(STATES[piece])
            for link in range(8):                    # plates read LINKMAP through their state
                pokes[1696 + link] = (now[1696 + link] & 0xE0) | rng.choice((0, 0, 1, 2, 5, 31))
            # pieces that are not there in the file, on this screen and where
            # its neighbours show: the left one's last column, the lower
            # one's top row, the upper one's bottom row
            left, right, above, below = (level[1952 + 4 * (screen - 1) + k] for k in range(4))
            if rng.random() < 0.6:
                for _ in range(rng.randint(1, 5)):
                    where = rng.choice(("here", "here", "here", "left", "below", "above"))
                    if where == "left" and left:
                        at = (left - 1) * 30 + rng.choice((9, 19, 29))
                    elif where == "below" and below:
                        at = (below - 1) * 30 + rng.randrange(10)
                    elif where == "above" and above:
                        at = (above - 1) * 30 + 20 + rng.randrange(10)
                    else:
                        at = (screen - 1) * 30 + rng.randrange(30)
                    piece = rng.randrange(30)
                    pokes[at] = piece
                    if piece in STATES:
                        pokes[720 + at] = rng.choice(STATES[piece])
            kid = {"KidScrn": rng.choice((screen, screen, right, 0)), "KidBlockX": rng.randrange(10),
                   "KidBlockY": rng.randrange(3)}
            for at, value in pokes.items():
                orig.o.cpu.poke(BLUEPRINT + at, AUX, value)
            orig.o.set_vars(kid)
            commands.append("P " + " ".join("%d=%d" % kv for kv in pokes.items()))
            commands.append("S " + " ".join("%s=%d" % kv for kv in kid.items()))
            room(level_no, screen, "with random piece states")
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

    failures = skipped = 0
    compared = {"as the level starts": 0, "with random piece states": 0}
    longest = {"nback": 0, "nfore": 0, "nmid": 0}
    for line, (level_no, screen, what, want) in zip(lines, expected):
        got = {}
        for tok in line.split():
            k, v = tok.split("=")
            got[k] = int(v, 16) if k in ("page", "back", "fore", "mid") else int(v)
        if what == "setinitials":
            if got["unmodelled"]:
                print("level %d: SETINITIALS left the modelled memory" % level_no)
                failures += 1
            continue
        if want is None or got["unmodelled"]:
            skipped += 1
            continue
        compared[what] += 1
        for k in longest:
            longest[k] = max(longest[k], want[k])
        problems = ["%s: C %x, original %x" % (k, got[k], want[k])
                    for k in ("nback", "nfore", "nmid", "back", "fore", "mid", "page")
                    if got[k] != want[k]]
        if problems:
            failures += 1
            if failures <= 8:
                print("MISMATCH level %d screen %d, %s" % (level_no, screen, what))
                for p in problems:
                    print("   " + p)
    total = sum(compared.values())
    if failures:
        print("%d of %d compared screens differ" % (failures, total))
        return 1
    if skipped * 10 > len(expected):
        print("too many screens skipped: %d of %d" % (skipped, len(expected)))
        return 1
    print("%d screens compared (%d as the levels start, %d with random piece states): C redraw "
          "and original agree on every byte of the page and on the three display lists; "
          "%d skipped (outside the modelled memory)"
          % (total, compared["as the level starts"], compared["with random piece states"], skipped))
    print("   longest lists in the original's runs: %d background, %d foreground, %d mid entries"
          % (longest["nback"], longest["nfore"], longest["nmid"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
