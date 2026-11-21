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
    machine may be busy).

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
          "(0.50 s by the clock, plus starting up)" % (frames, words[3], elapsed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
