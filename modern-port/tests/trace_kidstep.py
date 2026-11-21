#!/usr/bin/env python3
"""Differential test: many consecutive frames of play, C port against the
original.

New code for the pop-study project.

fuzz_control.py checks single routines on unrelated random states.  This test
checks that they work together over time.  It puts the kid on a floor of a
real level, half the time on a screen with a guard, and plays 60 frames of
random keyboard input.  Each frame is one whole pass of the original's
MainLoop (TOPCTRL.S 366-399):

    rnd; clear ChgKidStr / ChgOppStr; strobe; demokeys; misctimers;
    NextFrame; flashon; FrameAdv; playback; zerosound; flashoff; songcues;
    and LoadNextLevel if the level has been left

run on the original code in the 6502 simulator and, as pop_mainloop, on the
C port.  After each pass the whole tracked state, the level changes, the
redraw buffers and the calls out (with the sounds handed to playback) must
agree.  Everything persists from frame to frame.

A trace starts with INITIALGUARDS and FirstFrame, which enters the kid's
screen (neighbouring screens, slicers, torches, the guard) and draws it.

Stand-ins, the same on both sides: reading the keyboard (the test sets
kbdX, kbdY and BTN0 where `strobe` would), drawing, playing sounds and
songs, and the jumps out of the level (restart, next level, title
sequence), at which a trace ends.

Usage:
    trace_kidstep.py <pop_worldrun> <data dir> <original checkout> [traces] [seed]
"""

import os
import pickle
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fuzz_control as fc  # noqa: E402
from fuzz_control import BLUEPRINT, BUFFERS, BYTES, WORDS, CpuError  # noqa: E402

FRAMES_PER_TRACE = 60
FLOORS = (1, 2, 4, 6, 10, 11, 14, 15, 22)         # pieces to stand on at the start
FLOOR_Y = (55, 118, 181)


class TraceHarness(fc.Harness):
    budget = 6_000_000                       # cycles for one pass of MainLoop
    extra = ()                               # more result keys (replay_frames.py)
    ignore = ()                              # variables not compared (the same)

    def more(self, out):
        """Hook for a subclass: add to the outcome of a step."""

    def start_trace(self, level_no, s):
        """Hook for a subclass: called before a trace on that level begins;
        may change the start state, returns commands for the runner."""
        return []

    def begin(self, level, s, bufs):
        o = self.oracle
        o.reset()
        o.load_level(level)
        self.level = bytes(level)
        for n in BYTES:
            o.poke(self.addr[n], s[n])
        for w in WORDS:
            o.poke(self.addr[w], s[w] & 0xFF)
            o.poke(self.addr[w] + 1, s[w] >> 8)
        base = BLUEPRINT + (s["blue"] - 1) * 30
        for name, v in (("BlueType", base), ("BlueSpec", base + 720)):
            a = o.addr(name)
            o.poke(a, v & 0xFF)
            o.poke(a + 1, v >> 8)
        for name, _ in BUFFERS:
            o.poke(self.bufaddr[name], bufs[name])

    def set(self, values):
        for n, v in values.items():
            self.oracle.poke(self.addr[n], v)

    def step_at(self, address):
        """Like step, for a place in the code that has no label."""
        return self.step(None, None, address=address)

    def frame(self):
        """One pass of MainLoop."""
        return self.step(None, "MainLoop")

    def step(self, module, label, a=0, address=None):
        o = self.oracle
        o.poke(self.soundtable, 0)
        self.hooks = []
        self.stray = False
        self.left = False                            # the original jumped out of the level
        self.played = ""
        o.cpu.mem[0xC061] = o.peek(self.addr["BTN0"])        # the buttons themselves
        o.cpu.mem[0xC062] = o.peek(self.addr["BTN1"])
        try:
            if label == "MainLoop":                  # entered after its `jsr rnd`
                o.call("RND", module="GRAFIX")
                o.cpu.sp = 0xFF
                o.cpu.call(self.mainloop + 3, self.budget)
            elif address is None:
                o.call(label, a, 0, 0, module=module, max_cycles=self.budget // 2)
            else:
                o.cpu.sp = 0xFF
                o.cpu.call(address, self.budget // 2)
        except fc.Left as jump:
            self.left = bool(jump.args[0])           # not the loop's own way round
        except CpuError:
            return "error"
        if self.stray:
            return "stray"
        out = {n: o.peek(self.addr[n]) for n in BYTES}
        for w in WORDS:
            out[w] = o.peek(self.addr[w]) | (o.peek(self.addr[w] + 1) << 8)
        a = o.addr("BlueType")
        ptr = o.peek(a) | (o.peek(a + 1) << 8)
        out["blue"] = (ptr - BLUEPRINT) // 30 + 1 if (ptr - BLUEPRINT) % 30 == 0 else -1
        count = o.peek(self.soundtable)
        now = o.peek(BLUEPRINT, len(self.level))
        lvl = ",".join("%d.%d" % (i, now[i]) for i in range(len(now))
                       if now[i] != self.level[i]) or "-"
        self.level = now
        result = {"vars": out,
                  "snd": bytes(o.peek(self.soundtable + 1, count)).hex() if count else "-",
                  "hooks": "".join(self.hooks) or "-", "lvl": lvl,
                  "bufs": {n: bytes(o.peek(self.bufaddr[n], size)).hex() for n, size in BUFFERS}}
        self.more(result)
        return result


def start_state(rng, level, level_no):
    """The kid standing on a floor piece with open space above it."""
    spots = []
    for scrn in range(1, 25):
        for row in range(3):
            for col in range(1, 9):
                i = (scrn - 1) * 30 + row * 10 + col
                if level[i] & 0x1F in FLOORS and (row == 0 or level[i - 10] & 0x1F in (0, 19)):
                    spots.append((scrn, row, col))
    # half the time, on a screen where a guard waits on the same row
    guarded = [(sc, r, c) for sc, r, c in spots
               if level[2119 - 1 + sc] < 30 and level[2119 - 1 + sc] // 10 == r]
    scrn, row, col = rng.choice(guarded if guarded and rng.random() < 0.5 else spots)
    s = fc.fw.random_state(rng)
    for n in fc.EXTRA + fc.COLL_VARS + fc.MOVER_VARS + fc.AUTO_VARS + fc.TOP_VARS \
            + fc.LOOP_VARS + fc.OBJ_VARS + fc.KEY_VARS:
        s[n] = 0
    s.update(yellowflag=0x80, MinLeft=0xFF, SecLeft=0xFF, NextLevel=level_no)
    s["MaxKidStr"] = 3
    s["RNDseed"] = rng.randrange(256)
    s["BGset1"] = rng.choice((0, 0, 1))
    s["collideL"] = s["collideR"] = 0xFF
    s["FCharX"] = 0
    for rec in ("Kid", "Char", "Shad", "Op"):
        for f in fc.fw.CHAR_FIELDS:
            s[rec + f] = 0
        s[rec + "Seq"] = 0
    s["ShadFace"] = 86                               # no opponent until ADDGUARD finds one
    s.update(KidPosn=15, KidX=58 + 14 * col + rng.randint(2, 11), KidY=FLOOR_Y[row],
             KidFace=rng.choice((0x00, 0xFF)), KidBlockX=col, KidBlockY=row, KidScrn=scrn,
             KidLife=0xFF, KidID=0, KidAction=0)
    s.update(level=level_no, KidStrength=3, OppStrength=0,
             gotsword=rng.choice((0, 1, 1)), weightless=0, timebomb=0, blue=scrn)
    return s


def next_input(rng, held):
    """The keys held in one frame: mostly keep doing what was being done."""
    if held is None or rng.random() < 0.25:
        held = {"kbdX": rng.choice((0x00, 0xFF, 0xFF, 0xFF, 0x01)),
                "kbdY": rng.choice((0x00, 0x00, 0xFF, 0x01)),
                "BTN0": rng.choice((0x00, 0x00, 0xFF))}
    return held


def main(argv, harness=None, what="C port and original code agree"):
    if len(argv) < 4:
        print(__doc__)
        return 2
    runner, data, source = argv[1:4]
    traces = int(argv[4]) if len(argv) > 4 else 60
    rng = random.Random(int(argv[5]) if len(argv) > 5 else 1989)

    h = harness or TraceHarness(source)
    stand = h.oracle.addr("stand")
    levels = []
    for n in range(15):
        with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL%d" % n), "rb") as f:
            levels.append(f.read())

    commands, expected = ["K 1"], []
    seen_frames, seen_actions, ended_early, frames_run = set(), set(), 0, 0
    sounds, busiest_list, falling_floors, level_changes = set(), 0, 0, 0
    guard_frames = fight_frames = strength_lost = 0
    counts = {"failures": 0, "compared": 0, "off_model": 0}
    left_level = 0
    cut_at = {}
    endings = {"A": 0, "L": 0, "W": 0, "R": 0, "?": 0}

    cache = os.environ.get("POP_TRACE_CACHE")    # the original's runs, kept for other runs
    batches = []

    def flush():
        """Run the port on the traces gathered so far and compare.  Done in
        batches so that memory use does not grow with the number of traces."""
        if cache:
            batches.append((list(commands), list(expected)))
        failures, compared, off_model = counts["failures"], counts["compared"], counts["off_model"]
        dead_trace = -1
        argv_c = [runner, os.path.join(data, "framedef.bin"), os.path.join(data, "seqtable.bin"),
                  os.path.join(data, "imgdims.bin"), os.path.join(data, "autodata.bin")]
        out = subprocess.run(argv_c, input="\n".join(commands) + "\n", capture_output=True,
                             text=True)
        if out.returncode != 0:
            print("pop_worldrun failed: " + out.stderr[-500:])
            raise SystemExit(1)
        lines = out.stdout.splitlines()
        if len(lines) != len(expected):
            print("expected %d result lines, got %d" % (len(expected), len(lines)))
            raise SystemExit(1)

        for line, (trace, frame, name, want) in zip(lines, expected):
            if trace == dead_trace:
                continue
            got, extra = {}, {}
            for tok in line.split():
                if "=" in tok:
                    k, v = tok.split("=")
                    got[k] = int(v)
                else:
                    k, v = tok.split(":")
                    extra[k] = v
            if isinstance(want, str):
                cut_at[name + " (original: %s)" % want] = cut_at.get(name + " (original: %s)" % want, 0) + 1
                if want == "stray" and not got["unmodelled"]:
                    failures += 1
                    print("MISMATCH trace %d frame %d %s: the original asked for a missing "
                          "image, the port did not" % (trace, frame, name))
                dead_trace = trace
                continue
            if got["unmodelled"]:
                # The port reached a table index the original has no entry for
                # (the kid has run off the screen: nothing changes screens yet).
                # The original went on with whatever memory holds there; nothing
                # further in this trace can be compared.
                dead_trace = trace
                off_model += 1
                cut_at[name + " (port)"] = cut_at.get(name + " (port)", 0) + 1
                continue
            problems = []
            if got["blue"] != want["vars"]["blue"]:
                problems.append("blueprint screen: C %d, original %d"
                                % (got["blue"], want["vars"]["blue"]))
            for n in BYTES + WORDS:
                if got[n] != want["vars"][n] and n not in h.ignore:
                    problems.append("%s: C %d, original %d" % (n, got[n], want["vars"][n]))
            for key in ("snd", "hooks", "lvl") + tuple(h.extra):
                if extra[key] != want[key]:
                    problems.append("%s: C %s, original %s" % (key, extra[key], want[key]))
            for n, _ in BUFFERS:
                if extra[n] != want["bufs"][n] and n not in h.ignore:
                    problems.append("%s: C %s, original %s" % (n, extra[n], want["bufs"][n]))
            compared += 1
            if problems:
                failures += 1
                dead_trace = trace                       # later frames would only repeat it
                if failures <= 5:
                    print("MISMATCH trace %d frame %d after %s" % (trace, frame, name))
                    for p in problems[:10]:
                        print("   " + p)
        counts.update(failures=failures, compared=compared, off_model=off_model)
        del commands[1:]
        del expected[:]

    if cache and os.path.exists(cache):
        with open(cache, "rb") as f:
            key, kept = pickle.load(f)
        if key != tuple(argv[4:]):
            print("the cache %s was made with other arguments" % cache)
            return 2
        for batch_commands, batch_expected in kept:
            commands[:] = batch_commands
            expected[:] = batch_expected
            flush()
        if counts["failures"]:
            print("%d traces differ" % counts["failures"])
            return 1
        print("%d calls compared with the original's kept results: %s" % (counts["compared"], what))
        return 0

    for trace in range(traces):
        level_no = rng.randrange(1, 15)
        level = levels[level_no]
        s = start_state(rng, level, level_no)
        bufs = {name: bytes(size) for name, size in BUFFERS}
        commands += h.start_trace(level_no, s)
        h.begin(level, s, bufs)
        commands.append("L " + level.hex())
        commands.append("S " + " ".join("%s=%d" % kv for kv in s.items()))
        for name, _ in BUFFERS:
            commands.append("H %s %s" % (name, bufs[name].hex()))
        # start the "stand" sequence in the Kid record: load, jumpseq, save
        prelude = [("loadkid", "CTRLSUBS", "LOADKID", 0), ("jumpseq", "CTRLSUBS", "JUMPSEQ", stand),
                   ("savekid", "CTRLSUBS", "SAVEKID", 0),
                   ("initialguards", "SUBS", "INITIALGUARDS", 0),   # as at level start
                   ("initcdbuf", "TOPCTRL", "initCDbuf", 0),
                   ("firstframe", "TOPCTRL", "FirstFrame", 0)]
        held = None
        for name, module, label, a in prelude:
            want = h.step(module, label, a)
            commands.append("R %s %d 0 0" % (name, a))
            expected.append((trace, -1, name, want))
        for frame in range(FRAMES_PER_TRACE):
            if h.left or isinstance(want, str):
                break
            held = next_input(rng, held)
            h.set(held)
            commands.append("S " + " ".join("%s=%d" % kv for kv in held.items()))
            want = h.frame()
            commands.append("R mainloop 0 0 0")
            expected.append((trace, frame, "mainloop", want))
            if isinstance(want, str):
                ended_early += 1
                break
            sounds.update(h.played[i:i + 2] for i in range(0, len(h.played), 2))
            level_changes += want["lvl"] != "-"
            frames_run += 1
            seen_frames.add(want["vars"]["KidPosn"])
            seen_actions.add(want["vars"]["KidAction"])
            busiest_list = max(busiest_list, want["vars"]["numtrans"])
            guard_frames += want["vars"]["ShadFace"] != 86
            fight_frames += want["vars"]["ShadSword"] == 2 and want["vars"]["ShadFace"] != 86
            strength_lost += want["vars"]["KidStrength"] < 3
            falling_floors += want["vars"]["nummob"] > 0
            if h.left:                               # restart, next level or title sequence
                left_level += 1
                endings["".join(c for c in want["hooks"] if c in "ALWR")[-1:] or "?"] += 1
        if (trace + 1) % 20 == 0:
            flush()
    flush()
    failures, compared, off_model = counts["failures"], counts["compared"], counts["off_model"]
    if cache:
        with open(cache, "wb") as f:
            pickle.dump((tuple(argv[4:]), batches), f)

    if failures:
        print("%d traces differ" % failures)
        return 1
    if os.environ.get("POP_FUZZ_VERBOSE") or (ended_early + off_model) * 4 > traces:
        for k in sorted(cut_at, key=cut_at.get, reverse=True):
            print("   cut short at %-40s %d" % (k, cut_at[k]))
    if (ended_early + off_model) * 4 > traces:
        print("too many traces cut short: %d of %d" % (ended_early + off_model, traces))
        return 1
    print("%d traces, %d frames (%d calls) compared: %s; "
          "%d distinct frames and actions %s seen; %d traces cut short "
          "(a read outside the modelled tables)"
          % (traces, frames_run, compared, what, len(seen_frames), sorted(seen_actions),
             ended_early + off_model))
    print("   in the original's run: %d frames changed the level, sounds %s were played, "
          "up to %d objects in transition at once, a floor was falling in %d frames"
          % (level_changes, sorted(int(x, 16) for x in sounds), busiest_list, falling_floors))
    print("   %d traces ended by leaving the level (restart or next level %d, title "
          "sequence %d, time up %d, won %d)"
          % (left_level, endings["R"], endings["A"], endings["L"], endings["W"]))
    print("   an opponent was present in %d frames and en garde in %d; the kid was below "
          "full strength in %d" % (guard_frames, fight_frames, strength_lost))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
