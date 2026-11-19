#!/usr/bin/env python3
"""Differential test: C world-query routines against the original code.

New code for the pop-study project.

For every case a random game state is given both to the original routine,
running in the 6502 simulator (tools/oracle), and to the C port (through
pop_worldrun).  The returned value and every tracked variable must agree.

Usage:
    fuzz_world.py <pop_worldrun> <framedef.bin> <original checkout> [cases] [seed]
"""

import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "oracle"))

from pop_oracle import BLUEPRINT, Oracle  # noqa: E402

CHAR_FIELDS = ["Posn", "X", "Y", "Face", "BlockX", "BlockY", "Action", "XVel", "YVel",
               "Scrn", "Repeat", "ID", "Sword", "Life"]
RECORDS = ["Char", "Kid", "Shad", "Op"]
MISC = ["Fimage", "Fsword", "Fdx", "Fdy", "Fcheck", "infrontx", "behindx", "abovey",
        "tempblockx", "tempblocky", "tempscrn", "OFFSET", "weightless"]
BYTES = [r + f for r in RECORDS for f in CHAR_FIELDS] + MISC
WORDS = [r + "Seq" for r in RECORDS]

# routine -> (module holding the label, kind of A/X/Y input, compare X and Y?)
RDBLOCK_FAMILY = ("getunderft", "getinfront", "get2infront", "getbehind", "getabove",
                  "getaboveinf", "getabovebeh")
ROUTINES = {
    "rdblock": ("CTRLSUBS", "block", True),
    "rdblock1": ("CTRLSUBS", "none", True),
    "getleft": ("CTRLSUBS", "byte", False), "getright": ("CTRLSUBS", "byte", False),
    "getup": ("CTRLSUBS", "byte", False), "getdown": ("CTRLSUBS", "byte", False),
    "getbasex": ("CTRLSUBS", "none", False), "getdist": ("CTRLSUBS", "none", False),
    "getdist1": ("CTRLSUBS", "byte", False), "getblockej": ("CTRLSUBS", "edge", False),
    "getblockx": ("CTRLSUBS", "byte", False), "getblockxp": ("CTRLSUBS", "byte", False),
    "getblocky": ("CTRLSUBS", "byte", False), "getblockyp": ("CTRLSUBS", "byte", False),
    "facedx": ("CTRLSUBS", "byte", False), "addcharx": ("CTRLSUBS", "byte", False),
    "cmpspace": ("CTRLSUBS", "piece", False), "cmpbarr": ("CTRLSUBS", "piece", False),
    "cmpwall": ("CTRLSUBS", "piece", False),
    "getbaseblock": ("CTRLSUBS", "none", False), "getframeinfo": ("CTRLSUBS", "none", False),
    "rereadblocks": ("CTRL", "none", False),
    "gravity": ("SUBS", "none", False), "addfall": ("SUBS", "none", False),
    "loadkid": ("CTRLSUBS", "none", False), "savekid": ("CTRLSUBS", "none", False),
    "loadshad": ("CTRLSUBS", "none", False), "saveshad": ("CTRLSUBS", "none", False),
    "loadkidwop": ("CTRLSUBS", "none", False), "savekidwop": ("CTRLSUBS", "none", False),
    "loadshadwop": ("CTRLSUBS", "none", False), "saveshadwop": ("CTRLSUBS", "none", False),
}
for name in RDBLOCK_FAMILY:
    ROUTINES[name] = ("CTRLSUBS", "none", True)
VOID = ("gravity", "addfall", "getbaseblock", "getframeinfo", "rereadblocks", "loadkid", "savekid", "loadshad",
        "saveshad", "loadkidwop", "savekidwop", "loadshadwop", "saveshadwop")


def signed_byte(rng, lo, hi):
    return rng.randint(lo, hi) & 0xFF


def random_state(rng):
    s = {n: rng.randrange(256) for n in BYTES}
    for rec in RECORDS:
        s[rec + "Face"] = rng.choice((0x00, 0xFF))
        s[rec + "Scrn"] = rng.randint(0, 24)
        s[rec + "BlockX"] = signed_byte(rng, -6, 15)
        s[rec + "BlockY"] = signed_byte(rng, -2, 4)
        cid = rng.choice((0, 0, 0, 1, 2, 4, 5, 6, 24))
        s[rec + "ID"] = cid
        # frame numbers the frame tables actually cover
        s[rec + "Posn"] = rng.randint(1, 90) if cid in (5, 6) else rng.randint(1, 240)
    s["weightless"] = rng.choice((0, 0, 1, 10))
    s["CharAction"] = rng.choice((0, 1, 2, 3, 4, 4, 4, 5, 6, 7))
    s["tempscrn"] = rng.randint(0, 24)
    s["tempblockx"] = signed_byte(rng, -12, 22)
    s["tempblocky"] = signed_byte(rng, -5, 7)
    for w in WORDS:
        s[w] = rng.randrange(0x10000)
    s["blue"] = rng.randint(1, 24)
    return s


def random_args(rng, kind):
    if kind == "block":
        return rng.randint(0, 24), signed_byte(rng, -12, 22), signed_byte(rng, -5, 7)
    if kind == "byte":
        return rng.randrange(256), rng.randrange(256), rng.randrange(256)
    if kind == "edge":
        return signed_byte(rng, -5, 34), 0, 0
    if kind == "piece":
        return rng.randint(0, 31), 0, 0
    return rng.randrange(256), rng.randrange(256), rng.randrange(256)


def apply_state(oracle, s):
    for n in BYTES:
        oracle.poke(n, s[n])
    for w in WORDS:
        a = oracle.addr(w)
        oracle.poke(a, s[w] & 0xFF)
        oracle.poke(a + 1, s[w] >> 8)
    base = BLUEPRINT + (s["blue"] - 1) * 30
    for name, v in (("BlueType", base), ("BlueSpec", base + 720)):
        a = oracle.addr(name)
        oracle.poke(a, v & 0xFF)
        oracle.poke(a + 1, v >> 8)


def read_state(oracle):
    out = {n: oracle.peek(n) for n in BYTES}
    for w in WORDS:
        a = oracle.addr(w)
        out[w] = oracle.peek(a) | (oracle.peek(a + 1) << 8)
    a = oracle.addr("BlueType")
    ptr = oracle.peek(a) | (oracle.peek(a + 1) << 8)
    out["blue"] = (ptr - BLUEPRINT) // 30 + 1 if (ptr - BLUEPRINT) % 30 == 0 else -1
    return out


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    runner, framedef, source = argv[1:4]
    per_routine = int(argv[4]) if len(argv) > 4 else 400
    rng = random.Random(int(argv[5]) if len(argv) > 5 else 1989)

    oracle = Oracle(source)
    levels = []
    for n in range(15):
        with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL%d" % n), "rb") as f:
            levels.append(f.read())

    commands, expected = [], []
    names = sorted(ROUTINES)
    for round_no in range(per_routine):
        if round_no % 20 == 0:                       # change level now and then
            level = levels[rng.randrange(15)]
            oracle.load_level(level)
            commands.append("L " + level.hex())
        for routine in names:
            module, kind, _ = ROUTINES[routine]
            s = random_state(rng)
            a, x, y = random_args(rng, kind)
            apply_state(oracle, s)
            oracle.load_level(level)                 # routines here never write it; be sure
            ra, rx, ry, _ = oracle.call(routine.upper(), a, x, y, module=module)
            commands.append("S " + " ".join("%s=%d" % kv for kv in s.items()))
            commands.append("R %s %d %d %d" % (routine, a, x, y))
            expected.append((routine, (a, x, y), s, ra, rx, ry, read_state(oracle)))

    out = subprocess.run([runner, framedef], input="\n".join(commands) + "\n",
                         capture_output=True, text=True)
    if out.returncode != 0:
        print("pop_worldrun failed: " + out.stderr[-500:])
        return 1
    lines = out.stdout.splitlines()
    if len(lines) != len(expected):
        print("expected %d result lines, got %d" % (len(expected), len(lines)))
        return 1

    failures = 0
    for line, (routine, args, before, ra, rx, ry, after) in zip(lines, expected):
        got = dict((k, int(v)) for k, v in (tok.split("=") for tok in line.split()))
        problems = []
        if got["unmodelled"]:
            problems.append("C reported an unmodelled case")
        if routine not in VOID and got["a"] != ra:
            problems.append("A: C %d, original %d" % (got["a"], ra))
        if ROUTINES[routine][2]:
            if got["X"] != rx:
                problems.append("X: C %d, original %d" % (got["X"], rx))
            if got["Y"] != ry:
                problems.append("Y: C %d, original %d" % (got["Y"], ry))
            if got["blue"] != after["blue"]:
                problems.append("blueprint screen: C %d, original %d"
                                % (got["blue"], after["blue"]))
        for n in BYTES + WORDS:
            if got[n] != after[n]:
                problems.append("%s: C %d, original %d (was %d)"
                                % (n, got[n], after[n], before[n]))
        if problems:
            failures += 1
            if failures <= 5:
                print("MISMATCH %s%r" % (routine, args))
                for p in problems[:8]:
                    print("   " + p)
                print("   state: " + " ".join("%s=%d" % (k, before[k]) for k in
                                              ("CharX", "CharFace", "CharBlockX", "CharBlockY",
                                               "CharScrn", "CharPosn", "CharID", "Fdx",
                                               "Fcheck", "tempscrn", "tempblockx",
                                               "tempblocky")))
    if failures:
        print("%d of %d cases differ" % (failures, len(expected)))
        return 1
    print("%d cases over %d routines: C port and original code agree"
          % (len(expected), len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
