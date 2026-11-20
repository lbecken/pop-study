#!/usr/bin/env python3
"""End-to-end test: the self-running demo, C port against the original.

New code for the pop-study project.

Level 0 of the original is a demo that plays itself: the kid follows a
script (run, jump two pits, grab a ledge, climb), meets a guard and fights
him under the same routines that drive the guards.  No input is needed, so
the whole thing can be replayed frame by frame in both implementations:

    RESTART (from the point where the level is in memory), then per frame,
    as MainLoop does (TOPCTRL.S 366-393):
        rnd; clear ChgKidStr / ChgOppStr; misctimers; NextFrame;
        [draw]; zerosound

until the demo ends itself (GOATTRACT, or the script's "end of demo").
After every call the whole tracked state, the sounds, the level changes and
the calls out must agree.

The drawing step is not ported.  The three things it does to the game's
state are done by the test on both sides: after a screen change it clears
cutplan and sets CUTTIMER to 2 (drawbg, TOPCTRL.S 918-923); it counts
`lightning` down (flashoff, 1000-1004); and it calls REFLECTION, which the
original reaches through the character-drawing code (addchars, 747-760).
The clock (keeptime, showtime) is a hook on both sides.

Usage:
    replay_demo.py <pop_worldrun> <data dir> <original checkout> [max frames]
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fuzz_control as fc  # noqa: E402
from fuzz_control import BUFFERS, BYTES, WORDS  # noqa: E402
from trace_kidstep import TraceHarness  # noqa: E402

FRAME = [("rnd", "GRAFIX", "RND"), ("misctimers", "TOPCTRL", "misctimers"),
         ("nextframe", "TOPCTRL", "NextFrame"), ("reflection", "MISC", "REFLECTION"),
         ("zerosound", "SPECIALK", "ZEROSOUND")]


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    runner, data, source = argv[1:4]
    max_frames = int(argv[4]) if len(argv) > 4 else 600

    h = TraceHarness(source)
    with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL0"), "rb") as f:
        level = f.read()

    # the state the game has when it starts level 0 from cold: everything
    # zero, except what initgame and the caller of RESTART set
    s = dict.fromkeys(BYTES + WORDS, 0)
    s.update(level=0, NextLevel=0, yellowflag=0x80, MinLeft=0xFF, SecLeft=0xFF,
             origstrength=3, ShadFace=86, blue=1)
    bufs = {name: bytes(size) for name, size in BUFFERS}
    h.begin(level, s, bufs)
    commands = ["K 1", "L " + level.hex(), "S " + " ".join("%s=%d" % kv for kv in s.items())]
    commands += ["H %s %s" % (name, bufs[name].hex()) for name, _ in BUFFERS]
    expected = []

    def step(name, call):
        want = call()
        commands.append("R %s 0 0 0" % name)
        expected.append((name, want))
        return want

    want = step("restartlevel", lambda: h.step_at(h.restart_level))
    story, ended, frames = [], None, 0
    seen = {"screens": [], "frames": set(), "guard": 0, "fight": 0}
    while frames < max_frames and not isinstance(want, str):
        o = h.oracle
        values = {"ChgKidStr": 0, "ChgOppStr": 0}
        if o.peek(h.addr["cutplan"]):                        # the new screen has been drawn
            values.update(cutplan=0, CUTTIMER=2)
        if o.peek(h.addr["lightning"]):
            values["lightning"] = o.peek(h.addr["lightning"]) - 1
        h.set(values)
        commands.append("S " + " ".join("%s=%d" % kv for kv in values.items()))
        for name, module, label in FRAME:
            want = step(name, lambda: h.step(module, label))
            if isinstance(want, str):
                break
            if "A" in want["hooks"] or "a" in want["hooks"]:
                ended = "GOATTRACT" if "A" in want["hooks"] else "the script's end-of-demo"
        if isinstance(want, str):
            break
        frames += 1
        v = want["vars"]
        if not seen["screens"] or seen["screens"][-1] != v["KidScrn"]:
            seen["screens"].append(v["KidScrn"])
        seen["frames"].add(v["KidPosn"])
        seen["guard"] += v["ShadFace"] != 86
        seen["fight"] += v["KidSword"] == 2
        if v["KidLife"] < 0x80 and "kid dies" not in story:
            story.append("kid dies")
            story.append("(frame %d)" % frames)
        if v["milestone"] and "guard dies" not in story:
            story.append("guard dies")
            story.append("(frame %d)" % frames)
        if ended:
            break

    argv_c = [runner] + [os.path.join(data, n) for n in
                         ("framedef.bin", "seqtable.bin", "imgdims.bin", "autodata.bin")]
    out = subprocess.run(argv_c, input="\n".join(commands) + "\n", capture_output=True,
                         text=True)
    if out.returncode != 0:
        print("pop_worldrun failed: " + out.stderr[-500:])
        return 1
    lines = out.stdout.splitlines()
    if len(lines) != len(expected):
        print("expected %d result lines, got %d" % (len(expected), len(lines)))
        return 1

    for n, (line, (name, want)) in enumerate(zip(lines, expected)):
        got, extra = {}, {}
        for tok in line.split():
            if "=" in tok:
                k, v = tok.split("=")
                got[k] = int(v)
            else:
                k, v = tok.split(":")
                extra[k] = v
        if isinstance(want, str) or got["unmodelled"]:
            print("call %d (%s): outside the modelled tables (original: %s, port: %s)"
                  % (n, name, want if isinstance(want, str) else "ok",
                     "stopped" if got["unmodelled"] else "ok"))
            return 1
        problems = []
        if got["blue"] != want["vars"]["blue"]:
            problems.append("blueprint screen: C %d, original %d"
                            % (got["blue"], want["vars"]["blue"]))
        problems += ["%s: C %d, original %d" % (k, got[k], want["vars"][k])
                     for k in BYTES + WORDS if got[k] != want["vars"][k]]
        problems += ["%s: C %s, original %s" % (k, extra[k], want[k])
                     for k in ("snd", "hooks", "lvl") if extra[k] != want[k]]
        problems += ["%s: C %s, original %s" % (k, extra[k], want["bufs"][k])
                     for k, _ in BUFFERS if extra[k] != want["bufs"][k]]
        if problems:
            print("MISMATCH at call %d (%s), frame %d" % (n, name, (n - 1) // len(FRAME) + 1))
            for p in problems[:12]:
                print("   " + p)
            return 1

    if not ended:
        print("the demo did not end within %d frames" % max_frames)
        return 1
    print("demo replayed: %d frames, %d calls, C port and original code agree at every one"
          % (frames, len(expected)))
    print("   in the original's run: the kid passed through screens %s, %d distinct frames; "
          "a guard was present in %d frames, the kid's sword was out in %d; %s; "
          "ended by %s"
          % (seen["screens"], len(seen["frames"]), seen["guard"], seen["fight"],
             " ".join(story) or "nobody dies", ended))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
