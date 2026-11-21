#!/usr/bin/env python3
"""The special keys through the game as the shell runs it.

New code for the pop-study project.  No original code runs here.

The key handler itself (pop_keys.c) is compared with the original's
routine by routine and inside whole passes of the main loop
(fuzz_control.py @keys).  This checks the other end: that keys typed at
pop_app (what the SDL shell and pop_headless run) reach that handler and
have their effect on the game that is drawn and heard.

  - Escape freezes the game: no frame passes until a key comes; Escape
    again lets one frame pass; another key ends it.  The frames drawn are
    those of the same game without the keys.
  - space puts up the time left, which goes away again;
  - ctrl-S turns the sound off (no effects are played) and on (a beep);
  - ctrl-A starts the level again, ctrl-R ends the game;
  - SKIP typed takes level 1 to level 2; POP, then TINA, to level 14;
  - ctrl-K selects the keyboard: the joystick is ignored, the keys steer;
  - ctrl-G saves from level 3 on (a thud before that), and the saved game
    can be resumed.

Usage:
    check_keys.py <pop_headless> <data dir> <original checkout> <work dir>
"""

import os
import shutil
import subprocess
import sys

ESC, SPACE, CTRL_S, CTRL_A, CTRL_R, CTRL_K, CTRL_G = 0x9B, 0xA0, 0x93, 0x81, 0x92, 0x8B, 0x87
IDLE = (0, 0, 0, 0, 0)


def key(code, held=1):
    return (0, 0, 0, code, held)


def word(text):
    return [key(ord(c) | 0x80) for c in text]


class Game:
    def __init__(self, exe, data, source, work):
        self.exe, self.data, self.source, self.work = exe, data, source, work

    def run(self, level, lines, frames=None, extra=()):
        """Frames as token dicts, and what was written to standard error."""
        script = os.path.join(self.work, "input.txt")
        with open(script, "w") as f:
            f.write("".join("%d %d %d %d %d\n" % line for line in lines))
        out = subprocess.run([self.exe, self.data, self.source, str(level),
                              str(len(lines) if frames is None else frames), "--input", script]
                             + list(extra), capture_output=True, text=True)
        if out.returncode != 0:
            raise SystemExit("pop_headless failed: " + out.stderr[-400:])
        frames = [dict(t.split("=") for t in line.split()[2:]) for line in out.stdout.splitlines()]
        return frames, out.stderr


def pages(frames):
    return [(f["pg1"], f["pg2"]) for f in frames]


def fail(text):
    print(text)
    return 1


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    work = argv[4]
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)
    game = Game(argv[1], argv[2], argv[3], work)
    done = []

    # the game left alone, to compare with
    idle, _ = game.run(1, [IDLE] * 120)
    if any(f["beeps"] != "0" or f["leave"] != "0" for f in idle):
        return fail("the game left alone beeps or leaves the level")

    # Escape: frozen until a key; Escape again: one frame; space: go on
    lines = [IDLE] * 20 + [key(ESC)] + [IDLE] * 7 + [key(ESC)] + [IDLE] * 5 + [key(ESC)] \
        + [IDLE] * 3 + [key(SPACE)] + [IDLE] * 40
    frozen, err = game.run(1, lines, frames=60)
    # frame 21 begins with Escape and waits, reading lines until the next
    # Escape, then passes; frame 22 waits again, and so does frame 23, which
    # space releases.  60 frames pass all the same: time stood still.
    if len(frozen) != 61 or "frozen" in err:
        return fail("with the game frozen three times: %d frames; %s"
                    % (len(frozen) - 1, err.strip()))
    if pages(frozen) != pages(idle)[:61]:
        return fail("freezing the game changed what was drawn")
    done.append("Escape freezes, steps and releases the game without changing a frame")

    # frozen, and no key ever comes
    stuck, err = game.run(1, [IDLE] * 10 + [key(ESC)] + [IDLE] * 30)
    if len(stuck) != 11 or "frozen" not in err:
        return fail("frozen for good: %d frames, and it said: %s" % (len(stuck) - 1, err.strip()))
    done.append("a game frozen for good draws nothing more")

    # space: the time left appears, and is gone again later
    timed, _ = game.run(1, [IDLE] * 70 + [key(SPACE, 0)] + [IDLE] * 49)
    differing = [n for n, (a, b) in enumerate(zip(pages(timed), pages(idle))) if a != b]
    if not differing or differing[0] < 71 or differing[0] > 75 or differing[-1] > 115 \
            or len(differing) < 10:
        return fail("space: frames %s differ from the game left alone" % differing)
    done.append("space shows the time left for a while (frames %d to %d)"
                % (differing[0], differing[-1]))

    # ctrl-S: no effects while the sound is off; a beep when it comes back.
    # Running into the wall on the left makes sounds.
    run_left = [(-1, 0, 0, 0, 0)] * 60
    loud, _ = game.run(1, run_left)
    switch = (-1, 0, 0, CTRL_S, 0)
    quiet, _ = game.run(1, [switch] + run_left[1:40] + [switch] + run_left[41:])
    noisy = [n for n, f in enumerate(loud) if f["sounds"] != "0"]
    if not noisy or not any(n <= 40 for n in noisy):
        return fail("the test's run makes no sound to switch off")
    if any(f["sounds"] != "0" for f in quiet[1:41]) or quiet[1]["beeps"] != "0":
        return fail("sound off, and still something is played")
    if quiet[41]["beeps"] != "1":
        return fail("sound on again without its beep")
    if pages(quiet) != pages(loud):
        return fail("switching the sound off changed what was drawn")
    done.append("ctrl-S silences the effects and beeps when it brings them back")

    # ctrl-A: the level again; ctrl-R: the game is over
    again, _ = game.run(1, [(-1, 0, 0, 0, 0)] * 30 + [key(CTRL_A, 0)] + [IDLE] * 20)
    if again[31]["leave"] != "4" or again[31]["level"] != "1" or len(again) != 52:
        return fail("ctrl-A: frame 31 is %s" % again[31])
    over, _ = game.run(1, [IDLE] * 30 + [key(CTRL_R, 0)] + [IDLE] * 20)
    if len(over) != 32 or over[31]["leave"] != "1":
        return fail("ctrl-R: %d frames, the last %s" % (len(over) - 1, over[-1]))
    done.append("ctrl-A starts the level again, ctrl-R ends the game")

    # code words
    skipped, _ = game.run(1, [IDLE] * 10 + word("skip") + [IDLE] * 10)
    if skipped[14]["leave"] != "4" or skipped[14]["level"] != "2" or skipped[13]["level"] != "1":
        return fail("SKIP: frames 13 and 14 are %s, %s" % (skipped[13], skipped[14]))
    far, _ = game.run(1, [IDLE] * 5 + word("TINA") + [IDLE] * 5 + word("pop") + word("TINA")
                      + [IDLE] * 5)
    if any(f["level"] != "1" for f in far[:21]) or far[17]["beeps"] != "1" \
            or far[21]["level"] != "14":
        return fail("POP and TINA: levels %s, beeps %s"
                    % ([f["level"] for f in far], [f["beeps"] for f in far]))
    done.append("SKIP leads to level 2; TINA to level 14, but only after POP")

    # ctrl-K: the keyboard steers, the joystick does not
    stick = [(-1, 0, 0, 0, 0)] * 30
    by_stick, _ = game.run(1, [IDLE] + stick)
    no_stick, _ = game.run(1, [key(CTRL_K, 0)] + stick)
    by_key, _ = game.run(1, [key(CTRL_K, 0)] + [key(0xEA)] + [(0, 0, 0, 0, 1)] * 29)
    if no_stick[1]["beeps"] != "1":
        return fail("ctrl-K does not beep")
    if pages(no_stick) != pages(idle)[:32]:
        return fail("with the keyboard selected the joystick still steers")
    if pages(by_stick) == pages(idle)[:32] or pages(by_key) == pages(idle)[:32]:
        return fail("neither the joystick nor the J key moves the kid")
    if pages(by_key) != pages(by_stick):
        return fail("J held on the keyboard and the joystick held left give different games")
    done.append("ctrl-K hands the steering to the keyboard, where J held runs left as the "
                "joystick does")

    # ctrl-G and resuming
    saved = os.path.join(work, "saved.bin")
    early, _ = game.run(1, [IDLE] * 10 + [key(CTRL_G, 0)] + [IDLE] * 5, extra=["--save", saved])
    if os.path.exists(saved) or early[11]["sounds"] != "1":
        return fail("ctrl-G on level 1 saved a game, or made no sound")
    late, _ = game.run(3, [IDLE] * 25 + [key(CTRL_G, 0)] + [IDLE] * 5, extra=["--save", saved])
    if not os.path.exists(saved):
        return fail("ctrl-G on level 3 saved nothing")
    with open(saved, "rb") as f:
        data = f.read()
    # level 3, three units of strength, 25 frames played (the 26th saves
    # before it is counted), the first time message already given
    if len(data) != 16 or data[0] != 3 or data[1] != 3 or data[3] | (data[4] << 8) != 25:
        return fail("the saved game is %s" % data.hex())
    resumed, _ = game.run(1, [IDLE] * 30, extra=["--save", saved, "--resume"])
    if resumed[0]["level"] != "3" or pages(resumed)[0] != pages(late)[0]:
        return fail("the saved game resumes on level %s, or not at the level's start"
                    % resumed[0]["level"])
    fresh, _ = game.run(5, [IDLE] * 3, extra=["--save", os.path.join(work, "none.bin"),
                                               "--resume"])
    if fresh[0]["level"] != "1":
        return fail("with no saved game, resuming starts level %s" % fresh[0]["level"])
    done.append("ctrl-G saves from level 3 on, and the game resumes there; with nothing "
                "saved a new game starts")

    print("keys typed at the game as the shell runs it: " + "; ".join(done))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
