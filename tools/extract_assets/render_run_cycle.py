#!/usr/bin/env python3
"""Compose the kid's run animation at its real screen positions.

New code for the pop-study project.  Reads a local checkout of the original
repository; writes derived images and data to a git-ignored directory.

Usage:
    python3 tools/extract_assets/render_run_cycle.py \
        --source /path/to/Prince-of-Persia-Apple-II --out build/run_cycle

Output:
    timeline.png     one band per game tick, every band showing the same
                     window of the screen, so sideways drift is real movement
    pivot.png        the same frames in cells aligned on the foot ("base x")
    mask_demo.png    one frame drawn over a filled page, showing the mask
    run_cycle.json   per-tick numbers (also printed as a table)

Each frame is drawn by the byte-level blitter model in pop_hires.py with
parameters computed by the ports in pop_charpos.py, i.e. the pictures are
what those routines produce, not a hand-placed composition.

Markers:  red    base x (weight-bearing foot, GETBASEX)
          blue   CharX, the coordinate the sequence table moves
          green  line under CharY, the floor line the character stands on
          grey   block edges (every 14 game units)
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from merlin_data import assemble, parse_equates  # noqa: E402
from png_writer import encode_rgb_png  # noqa: E402
from pop_animdata import decode_frame_tables, decode_sequence_table, trace  # noqa: E402
from pop_charpos import (BLOCK_WIDTH, FACE_LEFT, FACE_RIGHT, FLOOR_Y,  # noqa: E402
                         SCRN_LEFT, add_char_x, placement)
from pop_hires import HiresPage, lay_mask  # noqa: E402
from pop_imgtable import parse_image_table  # noqa: E402

SRC = os.path.join("01 POP Source", "Source")
IMG = os.path.join("01 POP Source", "Images")
CHTABLE_FILE = {0: "IMG.CHTAB1", 1: "IMG.CHTAB2", 2: "IMG.CHTAB3", 4: "IMG.CHTAB5"}

BLACK, WHITE = (0, 0, 0), (255, 255, 255)
RED, BLUE, GREEN, GREY, DIM = (255, 64, 64), (80, 160, 255), (64, 200, 64), (70, 70, 70), (150, 150, 150)

DIGITS = {  # 3x5 font, new artwork
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111",
    "3": "111001111001111", "4": "101101111001001", "5": "111100111001111",
    "6": "111100111101111", "7": "111001001001001", "8": "111101111101111",
    "9": "111101111001111", "-": "000000111000000", " ": "000000000000000",
}


def draw_text(canvas, x, y, text, colour):
    for ch in text:
        glyph = DIGITS.get(ch, DIGITS[" "])
        for i, bit in enumerate(glyph):
            if bit == "1":
                yy, xx = y + i // 3, x + i % 3
                if 0 <= yy < len(canvas) and 0 <= xx < len(canvas[0]):
                    canvas[yy][xx] = colour
        x += 4


def scale(canvas, k):
    out = []
    for row in canvas:
        wide = [px for px in row for _ in range(k)]
        out.extend([wide] * k)
    return out


def load(source):
    read = lambda n: open(os.path.join(source, SRC, n), encoding="ascii").read()  # noqa: E731
    frames = decode_frame_tables(assemble(read("FRAMEDEF.S")))
    table = decode_sequence_table(assemble(read("SEQTABLE.S")))
    names = parse_equates(read("SEQDATA.S"))
    images = {}
    for t, fname in CHTABLE_FILE.items():
        with open(os.path.join(source, IMG, fname), "rb") as f:
            images[t] = parse_image_table(f.read())
    return frames, table, names, images


def simulate(frames, table, names, images, sequence, ticks, start_x, row, face,
             lead_frame=15):
    """Apply the sequence's chx/chy to CharX/CharY tick by tick and compute
    where each frame is drawn.  Only the sequence data moves the character:
    no input, collision or gravity is modelled."""
    tr = trace(table, table.entries[names[sequence] - 1])
    main = {f.number: f for f in frames.main}
    char_x, char_y = start_x, FLOOR_Y[row]
    out = []

    def record(n, frame_no, ops, dx, dy):
        f = main[frame_no]
        im = images[f.table].images[f.image - 1]
        p = placement(char_x, char_y, face, f.raw[2], f.raw[3], f.raw[4],
                      im.width_bytes, im.height)
        out.append({
            "tick": n, "frame": frame_no, "name": f.name,
            "ops": ops, "chx": dx, "chy": dy,
            "char_x": char_x, "char_y": char_y,
            "fdx": f.dx, "fdy": f.dy, "foot": f.foot, "odd": f.odd,
            "floor_check": f.floor_check, "thin": f.thin,
            "image": f.image, "chtable": f.table + 1,
            "width_bytes": im.width_bytes, "height": im.height,
            "fchar_x": p.fchar_x, "fchar_y": p.fchar_y,
            "xco": p.xco, "offset": p.offset, "mirror": p.mirror,
            "left_dot": p.left_dot, "right_dot": p.right_dot, "top_line": p.top_line,
            "base_x": p.base_x, "base_dot": p.base_dot,
            "_image": im, "_placement": p,
        })

    if lead_frame:
        record(0, lead_frame, [], 0, 0)
    i = 0
    for n in range(1, ticks + 1):
        k = tr.ticks[i]
        for op in k.ops:
            if op.op == "chx":
                char_x = add_char_x(char_x, op.args[0], face)
            elif op.op == "chy":
                char_y = (char_y + op.args[0]) & 0xFF
        ops = ["%s %s" % (o.op, ",".join(map(str, o.args))) if o.op not in ("goto", "ifwtless")
               else o.op for o in k.ops]
        record(n, k.frame, ops, k.dx, k.dy)
        i += 1
        if i >= len(tr.ticks):
            i = tr.loop_to
    return out


def draw_page(step, fill=0x00):
    page = HiresPage(fill)
    im, p = step["_image"], step["_placement"]
    lay_mask(page, im.data, im.width_bytes, im.height, p.xco, p.offset, p.yco, p.mirror)
    return page


def to_rgb(dots):
    return [[WHITE if d else BLACK for d in row] for row in dots]


def timeline(steps, band_top, band_h, x0, x1, label_w=18):
    width = label_w + (x1 - x0)
    canvas = []
    for s in steps:
        dots = draw_page(s).dots()
        band = [[BLACK] * width for _ in range(band_h)]
        for y in range(band_h):
            src = dots[band_top + y]
            for x in range(x0, x1):
                if src[x]:
                    band[y][label_w + x - x0] = WHITE
        floor = s["char_y"] - band_top + 1
        # block edges and floor line (only where nothing is drawn)
        for gx in range(SCRN_LEFT - BLOCK_WIDTH, SCRN_LEFT + 141, BLOCK_WIDTH):
            dx = 2 * (gx - SCRN_LEFT)
            if x0 <= dx < x1:
                for y in range(band_h):
                    if band[y][label_w + dx - x0] == BLACK:
                        band[y][label_w + dx - x0] = GREY
        for x in range(label_w, width):
            band[floor][x] = GREEN
        for colour, dot, y_from in ((RED, s["base_dot"], floor + 1),
                                    (BLUE, 2 * (s["char_x"] - SCRN_LEFT), floor + 4)):
            if x0 <= dot < x1:
                for y in range(y_from, min(y_from + 3, band_h)):
                    for xx in (dot, dot + 1):
                        if x0 <= xx < x1:
                            band[y][label_w + xx - x0] = colour
        draw_text(band, 1, 2, "%2d" % s["tick"], DIM)
        draw_text(band, 1, 10, "%3d" % s["frame"], WHITE)
        if any(o.startswith("tap") for o in s["ops"]):
            draw_text(band, 5, 18, "-", RED)
        band[band_h - 1] = [GREY] * width
        canvas.extend(band)
    return canvas


def pivot_sheet(steps, cell_w=64, cell_h=52, pivot=40, per_row=8):
    """Each frame in its own cell with base x at the same column."""
    rows_needed = (len(steps) + per_row - 1) // per_row
    canvas = [[BLACK] * (cell_w * per_row) for _ in range(cell_h * rows_needed)]
    for i, s in enumerate(steps):
        ox, oy = (i % per_row) * cell_w, (i // per_row) * cell_h
        dots = draw_page(s).dots()
        floor = cell_h - 8
        shift_x = pivot - s["base_dot"]
        shift_y = floor - 1 - s["char_y"]
        for y in range(s["top_line"], s["fchar_y"] + 1):
            for x in range(s["left_dot"], min(s["right_dot"] + 7, 280)):
                if dots[y][x]:
                    cx, cy = x + shift_x, y + shift_y
                    if 0 <= cx < cell_w and 0 <= cy < cell_h:
                        canvas[oy + cy][ox + cx] = WHITE
        for x in range(cell_w):
            canvas[oy + floor][ox + x] = GREEN
        for y in range(floor + 1, floor + 4):
            canvas[oy + y][ox + pivot] = RED
            canvas[oy + y][ox + pivot + 1] = RED
        draw_text(canvas, ox + 2, oy + 2, "%d" % s["frame"], DIM)
        for y in range(cell_h):
            canvas[oy + y][ox + cell_w - 1] = GREY
        for x in range(cell_w):
            canvas[oy + cell_h - 1][ox + x] = GREY
    return canvas


def mask_demo(step):
    """The frame over a filled page: cleared dots show the mask's reach."""
    dots = draw_page(step, fill=0x7F).dots()
    plain = draw_page(step).dots()
    x0, x1 = step["left_dot"] - 6, step["right_dot"] + 12
    y0, y1 = step["top_line"] - 4, step["fchar_y"] + 5
    out = []
    for y in range(y0, y1):
        row = []
        for x in range(x0, x1):
            if plain[y][x]:
                row.append(WHITE)          # image dot
            elif dots[y][x]:
                row.append((90, 90, 160))  # background left untouched
            else:
                row.append(BLACK)          # background cleared by the mask
        out.append(row)
    return out


def public(step):
    return {k: v for k, v in step.items() if not k.startswith("_")}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--source", required=True)
    ap.add_argument("--out", default=os.path.join("build", "run_cycle"))
    ap.add_argument("--sequence", default="startrun")
    ap.add_argument("--ticks", type=int, default=23)
    ap.add_argument("--start-x", type=int, default=168,
                    help="initial CharX in game units (default 168)")
    ap.add_argument("--face", choices=("left", "right"), default="left")
    ap.add_argument("--scale", type=int, default=3)
    args = ap.parse_args(argv)

    frames, table, names, images = load(args.source)
    face = FACE_LEFT if args.face == "left" else FACE_RIGHT
    steps = simulate(frames, table, names, images, args.sequence, args.ticks,
                     args.start_x, 2, face)

    lo = min(s["left_dot"] for s in steps) - 8
    hi = max(s["right_dot"] for s in steps) + 14
    lo, hi = max(lo, 0), min(hi, 280)
    top = min(s["top_line"] for s in steps) - 3
    band_h = FLOOR_Y[2] + 9 - top

    os.makedirs(args.out, exist_ok=True)
    k = args.scale
    with open(os.path.join(args.out, "timeline.png"), "wb") as f:
        f.write(encode_rgb_png(scale(timeline(steps, top, band_h, lo, hi), k)))
    with open(os.path.join(args.out, "pivot.png"), "wb") as f:
        f.write(encode_rgb_png(scale(pivot_sheet(steps), k)))
    with open(os.path.join(args.out, "mask_demo.png"), "wb") as f:
        f.write(encode_rgb_png(scale(mask_demo(steps[8]), 6)))
    with open(os.path.join(args.out, "run_cycle.json"), "w") as f:
        json.dump({
            "note": "Derived from the original Prince of Persia Apple II source; "
                    "not redistributed by pop-study.",
            "sequence": args.sequence, "face": args.face, "start_x": args.start_x,
            "steps": [public(s) for s in steps],
        }, f, indent=1)
        f.write("\n")

    print("tick frame name          chx CharX Fdx foot baseX  FCharX xco off  WxH    ops")
    for s in steps:
        print("%4d %5d %-13s %4d %5d %3d %4d %5d %7d %3d %3d  %dx%-3d %s"
              % (s["tick"], s["frame"], s["name"][:13], s["chx"], s["char_x"], s["fdx"],
                 s["foot"], s["base_x"], s["fchar_x"], s["xco"], s["offset"],
                 s["width_bytes"], s["height"], "; ".join(s["ops"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
