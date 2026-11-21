#!/usr/bin/env python3
"""Write frames of the self-running demo, as the port draws them, as PNG
files, and a contact sheet of them.

New code for the pop-study project.  The pictures show the original
game's artwork, drawn from the image files of your own checkout: they are
for looking at, and do not belong in this repository.

The port plays level 0 by itself (pop_mainloop, the demo's own movement
script; no input, no sound) and draws every frame with its own drawing
code.  This script only tells pop_worldrun what to do; there is no
original code involved, and nothing is compared.  That the page bytes
equal the original's is the business of tests/replay_frames.py; how they
become colours is docs/port/presentation.md.

Usage:
    render_demo.py <pop_worldrun> <data dir> <original checkout> <out dir>
                   [every=8] [mono|colour] [scale=2]

Writes frame_0000.png ... (every `every`-th frame, and the last) and
sheet.png into <out dir>.
"""

import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "extract_assets"))

from png_writer import encode_rgb_png  # noqa: E402

IMAGES = os.path.join("01 POP Source", "Images")
# level 0: dungeon background, guards (BGDATA.S; MISC.S 774)
TABLES = ((1, "IMG.BGTAB1.DUN"), (2, "IMG.BGTAB2.DUN"), (11, "IMG.CHTAB1"), (12, "IMG.CHTAB2"),
          (13, "IMG.CHTAB3"), (14, "IMG.CHTAB4.GD"), (15, "IMG.CHTAB5"))


def read_png(path):
    """Rows of (r, g, b) from a PNG written by pop_png.h (8-bit RGB, no filter)."""
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
    return [[tuple(raw[y * line + 1 + 3 * x:y * line + 4 + 3 * x]) for x in range(width)]
            for y in range(height)]


def run(runner, data, commands):
    argv = [runner] + [os.path.join(data, n) for n in
                       ("framedef.bin", "seqtable.bin", "imgdims.bin", "autodata.bin")]
    out = subprocess.run(argv, input="\n".join(commands) + "\n", capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("pop_worldrun failed: " + out.stderr[-500:])
    return out.stdout.splitlines()


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    runner, data, source, out_dir = argv[1:5]
    every = int(argv[5]) if len(argv) > 5 else 8
    mode = argv[6] if len(argv) > 6 else "colour"
    scale = int(argv[7]) if len(argv) > 7 else 2
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL0"), "rb") as f:
        level = f.read()
    start = ["V " + os.path.join(data, "bgdata.bin")]
    start += ["T %d %s" % (n, os.path.join(source, IMAGES, name)) for n, name in TABLES]
    # the state of a cold start: everything zero, no opponent, the
    # copy-protection check passed (as in tests/replay_demo.py)
    start += ["K 1", "L " + level.hex(), "S yellowflag=128 ShadFace=86 blue=1",
              "R startgame 0 0 0", "R restartlevel 0 0 0"]

    # first without pictures, to see where the demo ends
    lines = run(runner, data, start + ["R mainloop 0 0 0"] * 600)
    frames = next((n for n, line in enumerate(lines[2:], 1) if "A" in line.split("hooks:")[1].split()[0]),
                  None)
    if frames is None:
        raise SystemExit("the demo did not end within 600 frames")
    if any(" unmodelled=1 " in line for line in lines[:2 + frames]):
        raise SystemExit("the port left its tables")

    commands, written = list(start), []
    for n in range(frames + 1):                  # frame 0 is the first one drawn (by the restart)
        if n:
            commands.append("R mainloop 0 0 0")
        if n % every == 0 or n == frames - 1:    # (the last pass leaves for the title sequence)
            path = os.path.join(out_dir, "frame_%04d.png" % n)
            commands.append("W %s %d %s" % (mode, scale, path))
            written.append(path)
    run(runner, data, commands)

    # contact sheet: six to a row, at the size the frames were written
    pictures = [read_png(p) for p in written]
    width, height = len(pictures[0][0]), len(pictures[0])
    per_row, gap = 6, 4
    rows = (len(pictures) + per_row - 1) // per_row
    sheet = [[(40, 40, 40)] * (per_row * (width + gap) + gap)
             for _ in range(rows * (height + gap) + gap)]
    for i, picture in enumerate(pictures):
        x0, y0 = gap + (i % per_row) * (width + gap), gap + (i // per_row) * (height + gap)
        for y in range(height):
            sheet[y0 + y][x0:x0 + width] = picture[y]
    with open(os.path.join(out_dir, "sheet.png"), "wb") as f:
        f.write(encode_rgb_png(sheet))
    print("%d frames of the demo; %d written to %s (every %d, %s, x%d) and sheet.png"
          % (frames, len(written), out_dir, every, mode, scale))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
