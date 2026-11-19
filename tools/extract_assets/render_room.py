#!/usr/bin/env python3
"""Render level screens as the game's full redraw builds them (no characters).

New code for the pop-study project.  Reads a local checkout of the original
repository; output goes to a git-ignored directory.

Usage:
    python3 tools/extract_assets/render_room.py \
        --source /path/to/Prince-of-Persia-Apple-II --out build/rooms \
        --level 1 [--screen 1] [--scale 2]

Without --screen every screen reachable from the kid's start is rendered and
a level overview (screens laid out by the level's own map) is written too.

Output per screen:
    level<NN>/screen<SS>.png     280x192 dots, 1 dot = 1 pixel times --scale
    level<NN>/screen<SS>.json    the background, mid and foreground lists
    level<NN>/level.json         decoded blueprint
    level<NN>/overview.png       all screens placed by MAP (with --screen omitted)

Dots are shown white on black: no colour model is applied (see docs).
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from png_writer import encode_grey_png, scale_rows  # noqa: E402
from pop_imgtable import parse_image_table  # noqa: E402
from pop_level import load_level  # noqa: E402
from pop_room import BgData, RoomBuilder, bgset_file_suffix, draw_room  # noqa: E402

SRC = os.path.join("01 POP Source", "Source")
IMG = os.path.join("01 POP Source", "Images")
LEVELS = os.path.join("01 POP Source", "Levels")
OPACITY_NAMES = {0: "and", 1: "ora", 2: "sta", 3: "eor", 4: "mask"}


def load_bgdata(source):
    read = lambda n: open(os.path.join(source, SRC, n), encoding="ascii").read()  # noqa: E731
    return BgData(read("BGDATA.S"), read("GAMEBG.S"), read("MISC.S"))


def load_tables(source, bg, level_no):
    def table(n, setno):
        name = "IMG.BGTAB%d.%s" % (n, bgset_file_suffix(setno))
        with open(os.path.join(source, IMG, name), "rb") as f:
            return name, parse_image_table(f.read())
    return table(1, bg.level_bgset1[level_no]), table(2, bg.level_bgset2[level_no])


def build_screen(bg, level, level_no, screen, tables):
    builder = RoomBuilder(bg, level, level_no, screen)
    builder.sure()
    page = draw_room(builder, tables[0][1], tables[1][1])
    return builder, page


def page_rows(page):
    return [[255 if d else 0 for d in row] for row in page.dots()]


def entry_json(e):
    return {"x": e.x, "y": e.y, "table": 2 if e.image & 0x80 else 1,
            "image": e.image & 0x7F, "opacity": OPACITY_NAMES[e.opacity],
            "offset": e.offset, "by": e.source}


def layout(level, screens):
    """Place screens on a grid by following MAP from the first screen."""
    pos = {screens[0]: (0, 0)}
    todo = [screens[0]]
    step = {"left": (-1, 0), "right": (1, 0), "above": (0, -1), "below": (0, 1)}
    conflicts = 0
    while todo:
        s = todo.pop(0)
        x, y = pos[s]
        for side, n in level.neighbours(s).items():
            if not n or n not in screens:
                continue
            p = (x + step[side][0], y + step[side][1])
            if n in pos:
                conflicts += pos[n] != p
                continue
            pos[n] = p
            todo.append(n)
    return pos, conflicts


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--source", required=True)
    ap.add_argument("--out", default=os.path.join("build", "rooms"))
    ap.add_argument("--level", type=int, required=True)
    ap.add_argument("--screen", type=int)
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--no-initials", action="store_true",
                    help="keep the editor's BLUESPEC values (skip SETINITIALS)")
    args = ap.parse_args(argv)

    bg = load_bgdata(args.source)
    level = load_level(os.path.join(args.source, LEVELS, "LEVEL%d" % args.level))
    if not args.no_initials:
        level.set_initials()
    tables = load_tables(args.source, bg, args.level)
    out = os.path.join(args.out, "level%02d" % args.level)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "level.json"), "w") as f:
        json.dump(level.to_json(), f, indent=1)
        f.write("\n")

    screens = [args.screen] if args.screen else level.screens_in_use()
    print("level %d: background files %s, %s; %d screen(s)"
          % (args.level, tables[0][0], tables[1][0], len(screens)))
    rendered = {}
    for s in screens:
        builder, page = build_screen(bg, level, args.level, s, tables)
        rendered[s] = page
        with open(os.path.join(out, "screen%02d.png" % s), "wb") as f:
            f.write(encode_grey_png(scale_rows(page_rows(page), args.scale)))
        with open(os.path.join(out, "screen%02d.json" % s), "w") as f:
            json.dump({
                "note": "Derived from the original Prince of Persia Apple II source; "
                        "not redistributed by pop-study.",
                "level": args.level, "screen": s,
                "neighbours": level.neighbours(s),
                "back": [entry_json(e) for e in builder.back],
                "mid": [entry_json(e) for e in builder.mid],
                "fore": [entry_json(e) for e in builder.fore],
                "rejected_offscreen": builder.rejected,
                "overflow_writes": page.overflow_writes,
                "notes": builder.stale_reads,
            }, f, indent=1)
            f.write("\n")
        print("  screen %2d: back %3d, mid %d, fore %2d, dropped (y off screen) %2d, "
              "bytes past column 39: %d%s"
              % (s, len(builder.back), len(builder.mid), len(builder.fore),
                 builder.rejected, page.overflow_writes,
                 "  " + "; ".join(builder.stale_reads) if builder.stale_reads else ""))

    if not args.screen and screens:
        pos, conflicts = layout(level, screens)
        xs = [p[0] for p in pos.values()]
        ys = [p[1] for p in pos.values()]
        w, h = 280 + 4, 192 + 4
        canvas = [[40] * ((max(xs) - min(xs) + 1) * w) for _ in range((max(ys) - min(ys) + 1) * h)]
        for s, (x, y) in pos.items():
            ox, oy = (x - min(xs)) * w + 2, (y - min(ys)) * h + 2
            for r, row in enumerate(page_rows(rendered[s])):
                canvas[oy + r][ox:ox + 280] = row
        with open(os.path.join(out, "overview.png"), "wb") as f:
            f.write(encode_grey_png(canvas))
        print("  overview: %d of %d screens placed%s"
              % (len(pos), len(screens),
                 ", %d map inconsistencies" % conflicts if conflicts else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
