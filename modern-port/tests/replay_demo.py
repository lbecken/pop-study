#!/usr/bin/env python3
"""End-to-end test: the self-running demo, C port against the original.

New code for the pop-study project.

Level 0 of the original is a demo that plays itself: the kid follows a
script (run, jump two pits, grab a ledge, climb), meets a guard and fights
him under the same routines that drive the guards.  No input is needed, so
the whole thing can be replayed frame by frame in both implementations, as
the game runs it (TOPCTRL.S):

    StartGame with level 0;
    RESTART, from the point where the level is in memory;
    MainLoop, pass after pass,

until the demo ends itself (GOATTRACT, or the script's "end of demo").
After every pass the whole tracked state, the level changes, the redraw
buffers and the calls out (with the sounds handed to playback) must agree.

Stand-ins, the same on both sides: the keyboard (no key is pressed),
drawing, and playing the sounds.

Usage:
    replay_demo.py <pop_worldrun> <data dir> <original checkout> [max frames]
"""

import os
import pickle
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fuzz_control as fc  # noqa: E402
from fuzz_control import BUFFERS, BYTES, WORDS  # noqa: E402
from trace_kidstep import TraceHarness  # noqa: E402

def replay(h, runner, data, source, max_frames, first_commands=()):
    """Play the demo in the original (harness h) and in the port; compare
    after every call.  Returns (frames, seen, story, ended) or None."""
    cache = os.environ.get("POP_DEMO_CACHE")     # the original's run, kept for other runs
    if cache and os.path.exists(cache):
        with open(cache, "rb") as f:
            commands, expected, summary = pickle.load(f)
        return compare(h, runner, data, commands, expected, summary, max_frames)
    with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL0"), "rb") as f:
        level = f.read()

    # the state the game has when it starts from cold: everything zero
    # (INITSYSTEM), no opponent, the copy-protection check passed
    s = dict.fromkeys(BYTES + WORDS, 0)
    s.update(yellowflag=0x80, ShadFace=86, blue=1)
    bufs = {name: bytes(size) for name, size in BUFFERS}
    h.begin(level, s, bufs)
    commands = list(first_commands)
    commands += ["K 1", "L " + level.hex(), "S " + " ".join("%s=%d" % kv for kv in s.items())]
    commands += ["H %s %s" % (name, bufs[name].hex()) for name, _ in BUFFERS]
    expected = []

    def step(name, call):
        want = call()
        commands.append("R %s 0 0 0" % name)
        expected.append((name, want))
        return want

    step("startgame", lambda: h.step("TOPCTRL", "StartGame", 0))
    want = step("restartlevel", lambda: h.step_at(h.restart_level))
    story, ended, frames = [], None, 0
    seen = {"screens": [], "frames": set(), "guard": 0, "fight": 0, "sounds": set()}
    while frames < max_frames and not isinstance(want, str):
        want = step("mainloop", h.frame)
        if isinstance(want, str):
            break
        frames += 1
        v = want["vars"]
        if not seen["screens"] or seen["screens"][-1] != v["KidScrn"]:
            seen["screens"].append(v["KidScrn"])
        seen["frames"].add(v["KidPosn"])
        seen["guard"] += v["ShadFace"] != 86
        seen["fight"] += v["KidSword"] == 2
        seen["sounds"].update(int(h.played[i:i + 2], 16) for i in range(0, len(h.played), 2))
        if v["KidLife"] < 0x80 and "kid dies" not in story:
            story.append("kid dies")
            story.append("(frame %d)" % frames)
        if v["milestone"] and "guard dies" not in story:
            story.append("guard dies")
            story.append("(frame %d)" % frames)
        if h.left:
            ended = "GOATTRACT" if "A" in want["hooks"] else "the script's end-of-demo"
            break
    summary = (frames, seen, story, ended)
    if cache:
        with open(cache, "wb") as f:
            pickle.dump((commands, expected, summary), f)
    return compare(h, runner, data, commands, expected, summary, max_frames)


def compare(h, runner, data, commands, expected, summary, max_frames):
    frames, seen, story, ended = summary
    argv_c = [runner] + [os.path.join(data, n) for n in
                         ("framedef.bin", "seqtable.bin", "imgdims.bin", "autodata.bin")]
    out = subprocess.run(argv_c, input="\n".join(commands) + "\n", capture_output=True,
                         text=True)
    if out.returncode != 0:
        print("pop_worldrun failed: " + out.stderr[-500:])
        return None
    lines = out.stdout.splitlines()
    if len(lines) != len(expected):
        print("expected %d result lines, got %d" % (len(expected), len(lines)))
        return None

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
            return None
        problems = []
        if got["blue"] != want["vars"]["blue"]:
            problems.append("blueprint screen: C %d, original %d"
                            % (got["blue"], want["vars"]["blue"]))
        problems += ["%s: C %d, original %d" % (k, got[k], want["vars"][k])
                     for k in BYTES + WORDS if got[k] != want["vars"][k] and k not in h.ignore]
        problems += ["%s: C %s, original %s" % (k, extra[k], want[k])
                     for k in ("snd", "hooks", "lvl") + tuple(h.extra) if extra[k] != want[k]]
        problems += ["%s: C %s, original %s" % (k, extra[k], want["bufs"][k])
                     for k, _ in BUFFERS if extra[k] != want["bufs"][k] and k not in h.ignore]
        if problems:
            print("MISMATCH at call %d (%s), frame %d" % (n, name, n - 1))
            for p in problems[:12]:
                print("   " + p)
            return None

    if not ended:
        print("the demo did not end within %d frames" % max_frames)
        return None
    return frames, seen, story, ended


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    runner, data, source = argv[1:4]
    max_frames = int(argv[4]) if len(argv) > 4 else 600
    result = replay(TraceHarness(source), runner, data, source, max_frames)
    if result is None:
        return 1
    frames, seen, story, ended = result
    print("demo replayed: %d frames (passes of MainLoop), C port and original code agree "
          "after every one" % frames)
    print("   in the original's run: the kid passed through screens %s, %d distinct frames; "
          "a guard was present in %d frames, the kid's sword was out in %d; sounds %s; %s; "
          "ended by %s"
          % (seen["screens"], len(seen["frames"]), seen["guard"], seen["fight"],
             sorted(seen["sounds"]), " ".join(story) or "nobody dies", ended))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
