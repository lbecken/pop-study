#!/usr/bin/env python3
"""Differential test: many consecutive kid frames, C port against the original.

New code for the pop-study project.

fuzz_control.py checks single routines on unrelated random states.  This test
checks that the routines work together over time: it puts the kid on a floor
of a real level and plays several hundred frames of random joystick input,
running, for each frame, the part of the original's per-frame kid update that
has been ported, in the original's order (TOPCTRL.S 635-690):

    animmobs, animtrans,
    LoadKidwOp, rereadblocks, PlayerCtrl, animchar, gravity, addfall,
    setupchar, rereadblocks, getedges, checkbarr, collisions, checkgate,
    checkfloor, checkpress, checkspikes, checkimpale, checkslice, shakeloose,
    SaveKid

The same list of calls is made on the original code in the 6502 simulator
and on the C port, and after every call the whole tracked state, the sounds,
the level changes, the redraw buffers and the calls to unported subsystems
must agree.  Level changes persist from frame to frame.

So within a trace gates open and close when plates are stepped on, spikes
spring, slicers run, loose floors shake, fall and crush, in both
implementations alike.

What is NOT in the frame, because it is not ported yet: guards (firstguard,
DoShad), strength changes and death handling, screen changes and drawing.
The joystick flags are set by the test, standing in for the input code, and
so are VisScrn and the neighbouring-screen variables, which the test keeps
pointing at the kid's screen as the screen-change code would.  When the
visible screen changes the test also calls addtorches, as the original does
on entering a screen (TOPCTRL.S 1061).

Usage:
    trace_kidstep.py <pop_worldrun> <data dir> <original checkout> [traces] [seed]
"""

import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fuzz_control as fc  # noqa: E402
from fuzz_control import BLUEPRINT, BUFFERS, BYTES, WORDS, CpuError  # noqa: E402

# NextFrame (TOPCTRL.S 503-506) updates the level's moving objects first
FRAME = [("animmobs", "MOVER", "ANIMMOBS"), ("animtrans", "MOVER", "ANIMTRANS"),
         ("loadkidwop", "CTRLSUBS", "LOADKIDWOP"), ("rereadblocks", "CTRL", "REREADBLOCKS"),
         ("playerctrl", "CTRL", "PLAYERCTRL"), ("animchar", "COLL", "ANIMCHAR"),
         ("gravity", "SUBS", "GRAVITY"), ("addfall", "SUBS", "ADDFALL"),
         ("setupchar", "CTRLSUBS", "SETUPCHAR"), ("rereadblocks", "CTRL", "REREADBLOCKS"),
         ("getedges", "CTRLSUBS", "GETEDGES"), ("checkbarr", "COLL", "CHECKBARR"),
         ("collisions", "COLL", "COLLISIONS"), ("checkgate", "COLL", "CHECKGATE"),
         ("checkfloor", "CTRL", "CHECKFLOOR"), ("checkpress", "CTRL", "CHECKPRESS"),
         ("checkspikes", "CTRLSUBS", "CHECKSPIKES"), ("checkimpale", "CTRL", "CHECKIMPALE"),
         ("checkslice", "COLL", "CHECKSLICE"), ("shakeloose", "TOPCTRL", "shakeloose"),
         ("savekid", "CTRLSUBS", "SAVEKID")]
FRAMES_PER_TRACE = 60
INPUT_VARS = ("JSTKX", "JSTKY", "btn", "clrF", "clrB", "clrU", "clrD", "clrbtn")
FLOORS = (1, 2, 4, 6, 10, 11, 14, 15, 22)         # pieces to stand on at the start
FLOOR_Y = (55, 118, 181)


class TraceHarness(fc.Harness):
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

    def step(self, module, label, a=0):
        o = self.oracle
        o.poke(self.soundtable, 0)
        self.hooks = []
        self.stray = False
        try:
            o.call(label, a, 0, 0, module=module, max_cycles=400_000)
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
        return {"vars": out,
                "snd": bytes(o.peek(self.soundtable + 1, count)).hex() if count else "-",
                "hooks": "".join(self.hooks) or "-", "lvl": lvl,
                "bufs": {n: bytes(o.peek(self.bufaddr[n], size)).hex() for n, size in BUFFERS}}


def start_state(rng, level):
    """The kid standing on a floor piece with open space above it."""
    spots = []
    for scrn in range(1, 25):
        for row in range(3):
            for col in range(1, 9):
                i = (scrn - 1) * 30 + row * 10 + col
                if level[i] & 0x1F in FLOORS and (row == 0 or level[i - 10] & 0x1F in (0, 19)):
                    spots.append((scrn, row, col))
    scrn, row, col = rng.choice(spots)
    s = fc.fw.random_state(rng)
    for n in fc.EXTRA + fc.COLL_VARS + fc.MOVER_VARS:
        s[n] = 0
    s["RNDseed"] = rng.randrange(256)
    s["BGset1"] = rng.choice((0, 0, 1))
    s["collideL"] = s["collideR"] = 0xFF
    s["FCharX"] = 0
    for rec in ("Kid", "Char", "Shad", "Op"):
        for f in fc.fw.CHAR_FIELDS:
            s[rec + f] = 0
        s[rec + "Seq"] = 0
    s.update(KidPosn=15, KidX=58 + 14 * col + rng.randint(2, 11), KidY=FLOOR_Y[row],
             KidFace=rng.choice((0x00, 0xFF)), KidBlockX=col, KidBlockY=row, KidScrn=scrn,
             KidLife=0xFF, KidID=0, KidAction=0)
    s.update(level=rng.choice((1, 2, 3, 12)), KidStrength=3, OppStrength=3,
             gotsword=rng.choice((0, 1)), weightless=0, timebomb=0, blue=scrn)
    return s


def screen_vars(level, scrn):
    """VisScrn and its neighbours, from the level map (4 bytes per screen:
    left, right, up, down)."""
    def link(s, which):
        return level[1952 + (s - 1) * 4 + which] if 1 <= s <= 24 else 0
    below = link(scrn, 3)
    return {"VisScrn": scrn, "scrnLeft": link(scrn, 0), "scrnRight": link(scrn, 1),
            "scrnAbove": link(scrn, 2), "scrnBelow": below,
            "scrnBelowL": link(below, 0), "scrnBelowR": link(below, 1)}


def next_input(rng, held):
    """Joystick state for one frame: mostly keep doing what was being done."""
    if held is None or rng.random() < 0.25:
        held = {"JSTKX": rng.choice((0x00, 0xFF, 0xFF, 0xFF, 0x01)),
                "JSTKY": rng.choice((0x00, 0x00, 0xFF, 0x01)),
                "btn": rng.choice((0x00, 0x00, 0xFF))}
    return held


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    runner, data, source = argv[1:4]
    traces = int(argv[4]) if len(argv) > 4 else 60
    rng = random.Random(int(argv[5]) if len(argv) > 5 else 1989)

    h = TraceHarness(source)
    stand = h.oracle.addr("stand")
    levels = []
    for n in range(15):
        with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL%d" % n), "rb") as f:
            levels.append(f.read())

    commands, expected = ["K 1"], []
    seen_frames, seen_actions, ended_early, frames_run = set(), set(), 0, 0
    sounds, busiest_list, falling_floors, level_changes = set(), 0, 0, 0
    for trace in range(traces):
        level = levels[rng.randrange(1, 15)]
        s = start_state(rng, level)
        bufs = {name: bytes(size) for name, size in BUFFERS}
        h.begin(level, s, bufs)
        commands.append("L " + level.hex())
        commands.append("S " + " ".join("%s=%d" % kv for kv in s.items()))
        for name, _ in BUFFERS:
            commands.append("H %s %s" % (name, bufs[name].hex()))
        # start the "stand" sequence in the Kid record: load, jumpseq, save
        prelude = [("loadkid", "CTRLSUBS", "LOADKID", 0), ("jumpseq", "CTRLSUBS", "JUMPSEQ", stand),
                   ("savekid", "CTRLSUBS", "SAVEKID", 0)]
        held, prev, alive = None, {"JSTKX": 0, "JSTKY": 0, "btn": 0}, True
        vis_before = 0
        for name, module, label, a in prelude:
            want = h.step(module, label, a)
            commands.append("R %s %d 0 0" % (name, a))
            expected.append((trace, -1, name, want))
        for frame in range(FRAMES_PER_TRACE):
            held = next_input(rng, held)
            # a direction or the button newly pressed sets its flag negative
            # until the control code uses it (it stores 1)
            flags = {n: h.oracle.peek(h.addr[n]) for n in ("clrF", "clrB", "clrU", "clrD", "clrbtn")}
            for flag, var, value in (("clrF", "JSTKX", 0xFF), ("clrB", "JSTKX", 0x01),
                                     ("clrU", "JSTKY", 0xFF), ("clrD", "JSTKY", 0x01),
                                     ("clrbtn", "btn", 0xFF)):
                if held[var] != value:
                    flags[flag] = 0
                elif prev[var] != value:
                    flags[flag] = 0xFF
            values = dict(held, **flags)
            prev = held
            scrn_now = h.oracle.peek(h.addr["KidScrn"])
            values.update(screen_vars(h.level, scrn_now))
            h.set(values)
            commands.append("S " + " ".join("%s=%d" % kv for kv in values.items()))
            steps = list(FRAME)
            if scrn_now != vis_before and 1 <= scrn_now <= 24:
                steps.insert(0, ("addtorches", "SUBS", "ADDTORCHES"))
            vis_before = scrn_now
            for name, module, label in steps:
                want = h.step(module, label)
                commands.append("R %s 0 0 0" % name)
                expected.append((trace, frame, name, want))
                if isinstance(want, str):
                    alive = False
                    break
                if want["snd"] != "-":
                    sounds.update(want["snd"][i:i + 2] for i in range(0, len(want["snd"]), 2))
                level_changes += want["lvl"] != "-"
            if not alive:
                ended_early += 1
                break
            frames_run += 1
            seen_frames.add(want["vars"]["KidPosn"])
            seen_actions.add(want["vars"]["KidAction"])
            busiest_list = max(busiest_list, want["vars"]["numtrans"])
            falling_floors += want["vars"]["nummob"] > 0

    argv_c = [runner, os.path.join(data, "framedef.bin"), os.path.join(data, "seqtable.bin"),
              os.path.join(data, "imgdims.bin")]
    out = subprocess.run(argv_c, input="\n".join(commands) + "\n", capture_output=True,
                         text=True)
    if out.returncode != 0:
        print("pop_worldrun failed: " + out.stderr[-500:])
        return 1
    lines = out.stdout.splitlines()
    if len(lines) != len(expected):
        print("expected %d result lines, got %d" % (len(expected), len(lines)))
        return 1

    failures, compared, dead_trace, off_model = 0, 0, -1, 0
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
            continue
        problems = []
        if got["blue"] != want["vars"]["blue"]:
            problems.append("blueprint screen: C %d, original %d"
                            % (got["blue"], want["vars"]["blue"]))
        for n in BYTES + WORDS:
            if got[n] != want["vars"][n]:
                problems.append("%s: C %d, original %d" % (n, got[n], want["vars"][n]))
        for key in ("snd", "hooks", "lvl"):
            if extra[key] != want[key]:
                problems.append("%s: C %s, original %s" % (key, extra[key], want[key]))
        for n, _ in BUFFERS:
            if extra[n] != want["bufs"][n]:
                problems.append("%s: C %s, original %s" % (n, extra[n], want["bufs"][n]))
        compared += 1
        if problems:
            failures += 1
            dead_trace = trace                       # later frames would only repeat it
            if failures <= 5:
                print("MISMATCH trace %d frame %d after %s" % (trace, frame, name))
                for p in problems[:10]:
                    print("   " + p)
    if failures:
        print("%d traces differ" % failures)
        return 1
    if (ended_early + off_model) * 4 > traces:
        print("too many traces cut short: %d of %d" % (ended_early + off_model, traces))
        return 1
    print("%d traces, %d calls compared: C port and original code agree; "
          "%d distinct frames and actions %s seen; %d traces cut short "
          "(a read outside the modelled tables)"
          % (traces, compared, len(seen_frames), sorted(seen_actions),
             ended_early + off_model))
    print("   in the original's run: %d calls changed the level, sounds %s were queued, "
          "up to %d objects in transition at once, a floor was falling in %d frames"
          % (level_changes, sorted(int(x, 16) for x in sounds), busiest_list, falling_floors))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
