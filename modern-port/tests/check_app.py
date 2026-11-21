#!/usr/bin/env python3
"""Does the game as the shell runs it draw what the tested runner draws?

New code for the pop-study project.  No original code runs here.

The SDL3 shell and pop_headless run the game through pop_app (src/app):
it loads levels and image tables itself, starts games, and feeds the keys
in.  The comparisons with the original (replay_frames.py) go through
another program, pop_worldrun, which is told every step by the test.  This
check ties the two together: the same games through both must leave the
same bytes in the two hi-res pages after every frame, and touch the same
display switches.

  - the demo of level 0, from the cold start to its end;
  - each of the levels 1 to 14 from its start, with random keys held for
    random stretches, until 200 frames have passed or the level is left for
    another.  Somewhere on the way ctrl-A is typed, which starts the level
    again, and where the kid dies and the button is pressed it starts
    again too: the game goes on through these restarts in both programs.
    (pop_app loads the level again by itself; the runner is told to, at
    the frames where a first run shows it asking.)

Usage:
    check_app.py <pop_headless> <pop_worldrun> <data dir> <original checkout> [seed]
"""

import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "extract_assets"))

import render_room as rr  # noqa: E402
from pop_room import bgset_file_suffix  # noqa: E402

IMAGES = os.path.join("01 POP Source", "Images")
CHSET = (0, 0, 0, 1, 2, 2, 3, 2, 2, 2, 2, 2, 4, 5, 5)         # MISC.S 774
CHSET_FILE = ("GD", "SKEL", "GD", "FAT", "SHAD", "VIZ")
FRAMES = 200
CTRL_A = 0x81


def runner_commands(source, data, bg, level_no, inputs, images=None, restarts=()):
    """What pop_worldrun is told, to do what pop_app does by itself.

    `images`: a directory to take the image tables from instead of the
    checkout (check_images.py).  `restarts`: the inputs (by index) whose
    pass ends with the level being started again."""
    images = images or os.path.join(source, IMAGES)
    set1, set2 = bg.level_bgset1[level_no], bg.level_bgset2[level_no]
    tables = [(1, "IMG.BGTAB1." + bgset_file_suffix(set1)),
              (2, "IMG.BGTAB2." + bgset_file_suffix(set2)), (11, "IMG.CHTAB1"),
              (12, "IMG.CHTAB2"), (13, "IMG.CHTAB3"),
              (14, "IMG.CHTAB4." + CHSET_FILE[CHSET[level_no]]), (15, "IMG.CHTAB5")]
    with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL%d" % level_no), "rb") as f:
        level = f.read()
    commands = ["V " + os.path.join(data, "bgdata.bin")]
    commands += ["T %d %s" % (n, os.path.join(images, name)) for n, name in tables]
    commands += ["K 1", "L " + level.hex(), "S yellowflag=128 ShadFace=86 blue=1",
                 "R startgame %d 0 0" % level_no, "S BGset1=%d" % set1, "R restartlevel 0 0 0"]
    # RESTART: the level as it is on disk, started again
    again = ["L " + level.hex(), "R restartlevel 0 0 0"]
    for n, (x, y, button, key) in enumerate(inputs):
        if key == CTRL_A:                              # the pass that reads the key ends in KEYS
            commands += ["I %d 0 0 0 0 0 - -" % CTRL_A, "R mainloopk 0 0 0"] + again
            continue
        commands.append("S joyon=255 joyX=%d joyY=%d BTN0=%d BTN1=%d keypress=%d keydown=%d"
                        % (x & 0xFF, y & 0xFF, 128 * button, 128 * button, 0x8D * key,
                           0x80 if (x or y or button or key) else 0))
        commands.append("R mainloop 0 0 0")
        if n in restarts:
            commands += again
    return commands


def runner_frames(runner, data, commands):
    argv = [runner] + [os.path.join(data, n) for n in
                       ("framedef.bin", "seqtable.bin", "imgdims.bin", "autodata.bin")]
    out = subprocess.run(argv, input="\n".join(commands) + "\n", capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("pop_worldrun failed: " + out.stderr[-400:])
    frames = []
    runs = [c for c in commands if c.startswith("R ")]
    lines = out.stdout.splitlines()
    level = lines[0].split(" level=")[1].split()[0]
    for n in range(1, len(lines)):                     # (line 0 is `startgame`)
        tokens = dict(t.split(":", 1) for t in lines[n].split() if ":" in t and "=" not in t)
        if n + 1 < len(runs) and runs[n + 1].startswith("R restartlevel"):
            # a pass that ended with the level starting again: not a frame;
            # the frame is the restart
            if "R" not in tokens["hooks"]:
                raise SystemExit("pop_worldrun was told of a restart that did not happen")
            continue
        left = any(c in tokens["hooks"] for c in "ALWR")
        # left for this same level again, nothing else: a restart to be told of
        again = "R" in tokens["hooks"] and not any(c in tokens["hooks"] for c in "ALWx") \
            and lines[n].split(" level=")[1].split()[0] == level
        frames.append((tokens["pg1"], tokens["pg2"], tokens["scr"].split(".")[-1], left,
                       " unmodelled=1 " in lines[n], again))
    return frames


def app_frames(headless, data, source, level_no, inputs, work):
    script = os.path.join(work, "input.txt")
    with open(script, "w") as f:
        f.write("".join("%d %d %d %d\n" % i for i in inputs))
    out = subprocess.run([headless, data, source, str(level_no), str(len(inputs)), "--input",
                          script], capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("pop_headless failed: " + out.stderr[-400:])
    frames = []
    restart_at = [n + 1 for n, i in enumerate(inputs) if i[3] == CTRL_A]
    for n, line in enumerate(out.stdout.splitlines()):
        tokens = dict(t.split("=") for t in line.split()[2:])
        left = tokens["leave"] != "0"
        if n in restart_at and tokens["leave"] != "4":
            raise SystemExit("ctrl-A at frame %d did not restart the level" % n)
        if tokens["leave"] == "4" and tokens["level"] == str(level_no):
            left = False                               # started again: the game goes on
        frames.append((tokens["pg1"], tokens["pg2"], tokens["sw"], left, False))
    return frames, "left what the port models" in out.stderr


def random_inputs(rng, count, restart=False):
    inputs, held = [], (0, 0, 0)
    for _ in range(count):
        if rng.random() < 0.25:
            held = (rng.choice((-1, -1, 0, 1, 1)), rng.choice((-1, 0, 0, 1)),
                    rng.choice((0, 0, 1)))
        inputs.append(held + (0,))
    if restart:                                        # ctrl-A somewhere on the way
        inputs[random.Random(rng.random()).randrange(20, count * 2 // 3)] = (0, 0, 0, CTRL_A)
    return inputs


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    headless, runner, data, source = argv[1:5]
    rng = random.Random(int(argv[5]) if len(argv) > 5 else 1989)
    bg = rr.load_bgdata(source)
    compared = total = restarted = 0
    with tempfile.TemporaryDirectory() as work:
        for level_no in range(15):
            if level_no == 0:
                inputs = [(0, 0, 0, 0)] * 420    # the demo plays itself; it ends before this
            else:
                inputs = random_inputs(rng, FRAMES, restart=True)
            restarts = set()
            while True:
                want = runner_frames(runner, data, runner_commands(source, data, bg, level_no,
                                                                   inputs, restarts=restarts))
                again = next((n for n, f in enumerate(want) if f[5]), None)
                if again is None or len(restarts) >= 20:
                    break
                restarts.add(again - 1)                # (frame n is the pass of input n - 1)
            restarted += len(restarts) + sum(1 for i in inputs if i[3] == CTRL_A)
            got, strayed = app_frames(headless, data, source, level_no, inputs, work)
            # as far as both went: the runner cannot load another level, and
            # the headless player stops where the unported parts begin
            upto = next((n for n, f in enumerate(want) if f[3] or f[4]), len(want) - 1)
            if len(got) <= upto and not strayed:
                print("level %d: the app stopped after %d frames, the runner went on to %d"
                      % (level_no, len(got) - 1, upto))
                return 1
            for n in range(min(upto + 1, len(got))):
                if want[n][4]:
                    break
                total += 1
                if n == upto and want[n][3]:
                    # The runner stops where the level is left for good; the
                    # app has by then started what comes next, or stopped too.
                    if not got[n][3] and n + 1 < len(got) and level_no != 0:
                        print("level %d frame %d: the runner left the level, the app did not"
                              % (level_no, n))
                        return 1
                    continue
                if got[n][:3] != want[n][:3]:
                    print("level %d frame %d: app %s, runner %s" % (level_no, n, got[n][:3],
                                                                    want[n][:3]))
                    return 1
            compared += 1
    print("%d games (the demo and levels 1-14 with random keys), %d frames, %d restarts of a "
          "level on the way (ctrl-A, or the kid dead and the button pressed): the game as the "
          "shell runs it and the runner that is compared with the original leave the same "
          "bytes in both hi-res pages and touch the same display switches"
          % (compared, total, restarted))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
