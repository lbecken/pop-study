#!/usr/bin/env python3
"""Smoke test of the SDL3 shell without a display or a sound card.

New code for the pop-study project.

SDL's "dummy" video and audio drivers let the shell run where there is no
screen: the window, the renderer and the audio stream exist, nothing is
shown or heard.  What can be checked that way:

  - the shell starts, plays the demo for a number of frames and leaves
    with status 0, having queued sound;
  - the picture it last drew (its --screenshot) is the picture
    pop_headless writes for the same frame: the shell shows what the
    tested code draws;
  - with the clock on, frames take the time they are given (loosely: the
    machine may be busy);
  - with --images and a set of placeholder pictures, its picture is again
    pop_headless's, and is not the original's; a directory without a
    PROVENANCE.txt is refused;
  - keys reach the game's key handler: Escape freezes the game, and with
    no key to follow the shell gives up and leaves, saying so; with a key
    to follow the game goes on and its picture is the unfrozen game's;
    ctrl-R ends the game and the demo begins; SKIP typed leads to level 2.
    The dummy driver has no keyboard, so the keys are given in two ways:
    as Apple II codes (--type), and as key events pushed into SDL's queue
    by name (--press), which then take the shell's own path from an SDL
    key event to an Apple II code.

What it cannot check is everything a person would: that the window looks
right, that the keys feel right, that the sound is right.  Those are
listed as not verified in docs/port/shell.md.

Usage:
    check_sdl.py <pop_sdl> <pop_headless> <data dir> <original checkout>
"""

import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "assets"))

import make_placeholder_set  # noqa: E402


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    at, idat, size = 8, b"", None
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            size = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        at += 12 + length
    raw = zlib.decompress(idat)
    width, height = size
    line = 1 + 3 * width
    return width, height, [raw[y * line + 1:(y + 1) * line] for y in range(height)]


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    shell, headless, data, source = argv[1:5]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    frames = 150
    with tempfile.TemporaryDirectory() as work:
        shot = os.path.join(work, "shot.png")
        out = subprocess.run([shell, "--source", source, "--data", data, "--level", "0",
                              "--frames", str(frames), "--no-wait", "--screenshot", shot],
                             capture_output=True, text=True, env=env, timeout=300)
        if out.returncode != 0:
            print("pop_sdl failed (%d): %s" % (out.returncode, out.stderr[-400:]))
            return 1
        words = out.stdout.split()
        if int(words[0]) != frames or int(words[3]) == 0:
            print("unexpected report: " + out.stdout.strip())
            return 1
        if "left what the port models" in out.stdout:
            print("the game left what the port models: " + out.stdout.strip())
            return 1
        subprocess.run([headless, data, source, "0", str(frames), "--png", work, str(frames)],
                       capture_output=True, text=True, check=True)
        w1, h1, got = read_png(shot)
        w2, h2, big = read_png(os.path.join(work, "frame_%04d.png" % frames))
        want = [bytes(b for x in range(w1) for b in row[6 * x:6 * x + 3]) for row in big[::2]]
        if (w1, h1, w2, h2) != (280, 192, 560, 384) or got != want:
            print("the shell's picture of frame %d is not pop_headless's" % frames)
            return 1

        # replacement pictures
        placeholders = os.path.join(work, "placeholders")
        make_placeholder_set.make_set(source, placeholders)
        out = subprocess.run([shell, "--source", source, "--data", data, "--level", "0",
                              "--frames", "40", "--no-wait", "--mute", "--screenshot", shot,
                              "--images", placeholders],
                             capture_output=True, text=True, env=env, timeout=300)
        if out.returncode != 0:
            print("pop_sdl --images failed (%d): %s" % (out.returncode, out.stderr[-400:]))
            return 1
        _, _, replaced = read_png(shot)
        pictures = {}
        for name, extra in (("new", ["--images", placeholders]), ("original", [])):
            subprocess.run([headless, data, source, "0", "40", "--png", work, "40"] + extra,
                           capture_output=True, text=True, check=True)
            _, _, big = read_png(os.path.join(work, "frame_0040.png"))
            pictures[name] = [bytes(b for x in range(w1) for b in row[6 * x:6 * x + 3])
                              for row in big[::2]]
        if replaced != pictures["new"] or replaced == pictures["original"]:
            print("with --images the shell's picture is not pop_headless's with --images, "
                  "or it is the original's")
            return 1
        # keys: Escape at frame 10, and nothing after it
        base = [shell, "--source", source, "--data", data, "--level", "1", "--no-wait", "--mute"]
        out = subprocess.run(base + ["--frames", "40", "--type", "10:9B"],
                             capture_output=True, text=True, env=env, timeout=300)
        if out.returncode != 0 or not out.stdout.startswith("10 frames shown") \
                or "frozen" not in out.stdout:
            print("frozen for good: status %d, %s" % (out.returncode, out.stdout.strip()))
            return 1
        # Escape, Escape (one frame), space (go on): 40 frames as without keys
        shot2 = os.path.join(work, "shot2.png")
        for keys, target in (("10:9B,10:9B,10:A0", shot), ("99:A0", shot2)):
            out = subprocess.run(base + ["--frames", "40", "--type", keys, "--screenshot", target],
                                 capture_output=True, text=True, env=env, timeout=300)
            if out.returncode != 0 or not out.stdout.startswith("40 frames shown") \
                    or "frozen" in out.stdout:
                print("keys %s: status %d, %s" % (keys, out.returncode, out.stdout.strip()))
                return 1
        if read_png(shot) != read_png(shot2):
            print("freezing and releasing the game changed its picture")
            return 1
        # ctrl-R on level 1: the game ends, the demo (level 0) begins
        out = subprocess.run(base + ["--frames", "30", "--type", "10:92"],
                             capture_output=True, text=True, env=env, timeout=300)
        if out.returncode != 0 or "level 0" not in out.stdout:
            print("ctrl-R: status %d, %s" % (out.returncode, out.stdout.strip()))
            return 1

        # The same through SDL's event queue, as key events with names: this
        # is the way a real keyboard's keys come in, short of the keyboard.
        for presses, expect in (("10:Escape", "10 frames shown"),            # frozen for good
                                ("10:Escape,10:Space", "40 frames shown"),   # and released
                                ("10:R+c", "level 0"),                       # ctrl-R: game over
                                ("5:S,6:K,7:I,8:P", "level 2"),              # the code word
                                ("5:S+s,6:K+s,7:I+s,8:P+s", "level 2"),      # ... in capitals
                                ("10:Q+c", "10 frames shown"),               # the shell's own: leave
                                ("10:Up,11:F1,12:Left", "40 frames shown")): # not keys to the game
            out = subprocess.run(base + ["--frames", "40", "--press", presses],
                                 capture_output=True, text=True, env=env, timeout=300)
            frozen = "frozen" in out.stdout
            if out.returncode != 0 or expect not in out.stdout \
                    or frozen != (presses == "10:Escape"):
                print("key events %s: status %d, %s" % (presses, out.returncode,
                                                        out.stdout.strip()))
                return 1

        os.remove(os.path.join(placeholders, "PROVENANCE.txt"))
        out = subprocess.run([shell, "--source", source, "--data", data, "--frames", "1",
                              "--no-wait", "--mute", "--images", placeholders],
                             capture_output=True, text=True, env=env, timeout=300)
        if out.returncode == 0 or "PROVENANCE.txt" not in out.stderr:
            print("pop_sdl accepted an image directory without a PROVENANCE.txt")
            return 1

        # with the clock: 60 frames at ten times the game's rate is about half a second
        start = time.monotonic()
        out = subprocess.run([shell, "--source", source, "--data", data, "--level", "1",
                              "--frames", "60", "--rate", "7250", "--mute"],
                             capture_output=True, text=True, env=env, timeout=300)
        elapsed = time.monotonic() - start
        if out.returncode != 0 or not 0.45 <= elapsed <= 5.0:
            print("timed run: status %d, %.2f s for 60 frames at 7250 a minute (0.50 s)"
                  % (out.returncode, elapsed))
            return 1
    print("the SDL3 shell ran without a display: %d frames of the demo, %s sound samples "
          "queued, its last picture equal to pop_headless's; 60 timed frames took %.2f s "
          "(0.50 s by the clock, plus starting up); with placeholder pictures it shows "
          "pop_headless's picture of them, and refuses them unlabelled; keys typed by the "
          "test freeze the game, release it and end it"
          % (frames, words[3], elapsed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
