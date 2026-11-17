#!/usr/bin/env python3
"""Decode Prince of Persia (Apple II) image tables into PNGs + JSON metadata.

New code for the pop-study project.  It reads a *local checkout* of the
original repository and writes derived files to an output directory that is
git-ignored by default: decoded artwork is original, copyrighted material
and is not redistributed by this project.

Usage:
    python3 tools/extract_assets/extract_images.py \
        --source /path/to/Prince-of-Persia-Apple-II --out build/extracted

Output, per table (e.g. IMG.CHTAB1):
    <out>/IMG.CHTAB1/001.png ...   one PNG per image, raw dots, 1 dot = 1 px
    <out>/IMG.CHTAB1/sheet.png     contact sheet (images in table order)
    <out>/IMG.CHTAB1/table.json    metadata (dimensions, offsets, hashes)

Rendering is deliberately literal: every set dot is white, every clear dot
is black, bit 7 is ignored.  No colour-artifact, mask or aspect-ratio
interpretation is applied at this stage (see docs/unknowns.md).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from png_writer import encode_grey_png, scale_rows  # noqa: E402
from pop_imgtable import ImageTableError, parse_image_table  # noqa: E402

IMAGES_SUBDIR = os.path.join("01 POP Source", "Images")
SHEET_GAP = 4          # px between images on the contact sheet
SHEET_BG = 64          # grey level of contact-sheet background
SHEET_MAX_WIDTH = 640  # px, before scaling


def image_rows(image):
    return [[255 if d else 0 for d in line] for line in image.dots()]


def contact_sheet(images):
    """Lay images out left-to-right, wrapping; bottoms aligned per row."""
    rows_of_images, current, width = [], [], SHEET_GAP
    for im in images:
        w = im.width_dots + SHEET_GAP
        if current and width + w > SHEET_MAX_WIDTH:
            rows_of_images.append(current)
            current, width = [], SHEET_GAP
        current.append(im)
        width += w
    if current:
        rows_of_images.append(current)

    sheet_w = max(SHEET_GAP + sum(i.width_dots + SHEET_GAP for i in r)
                  for r in rows_of_images)
    canvas = []
    for r in rows_of_images:
        band_h = max(i.height for i in r)
        band = [[SHEET_BG] * sheet_w for _ in range(band_h + SHEET_GAP)]
        x = SHEET_GAP
        for im in r:
            px = image_rows(im)
            top = SHEET_GAP + band_h - im.height
            for y, line in enumerate(px):
                band[top + y][x:x + len(line)] = line
            x += im.width_dots + SHEET_GAP
        canvas.extend(band)
    canvas.extend([[SHEET_BG] * sheet_w for _ in range(SHEET_GAP)])
    return canvas


def extract_table(path, out_dir, scale):
    with open(path, "rb") as f:
        data = f.read()
    table = parse_image_table(data)
    os.makedirs(out_dir, exist_ok=True)
    for im in table.images:
        if im.width_bytes == 0 or im.height == 0:
            continue
        png = encode_grey_png(scale_rows(image_rows(im), scale))
        with open(os.path.join(out_dir, "%03d.png" % im.index), "wb") as f:
            f.write(png)
    drawable = [i for i in table.images if i.width_bytes and i.height]
    with open(os.path.join(out_dir, "sheet.png"), "wb") as f:
        f.write(encode_grey_png(scale_rows(contact_sheet(drawable), scale)))
    meta = {
        "source_file": os.path.basename(path),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "source_size": len(data),
        "image_count": table.count,
        "pointer_base_address": "$%04X" % table.base_address,
        "used_length": table.used_length,
        "trailing_bytes": table.trailing_bytes,
        "row_order": "stored bottom-to-top; PNGs are top-to-bottom",
        "dot_order": "bit 0 = leftmost dot, 7 dots per byte, bit 7 ignored",
        "render_scale": scale,
        "images": [
            {
                "index": im.index,
                "address": "$%04X" % im.address,
                "file_offset": im.file_offset,
                "width_bytes": im.width_bytes,
                "width_dots": im.width_dots,
                "height": im.height,
                "hibit_clear_bytes": im.hibit_clear_count(),
            }
            for im in table.images
        ],
    }
    with open(os.path.join(out_dir, "table.json"), "w") as f:
        json.dump(meta, f, indent=1)
        f.write("\n")
    return table


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--source", required=True,
                    help="path to a checkout of jmechner/Prince-of-Persia-Apple-II")
    ap.add_argument("--out", default=os.path.join("build", "extracted"),
                    help="output directory (default: build/extracted)")
    ap.add_argument("--scale", type=int, default=1,
                    help="integer nearest-neighbour upscale (default 1)")
    ap.add_argument("--only", action="append", default=[],
                    help="table file name to extract (repeatable); default all")
    args = ap.parse_args(argv)

    src = os.path.join(args.source, IMAGES_SUBDIR)
    if not os.path.isdir(src):
        ap.error("not found: %s" % src)
    names = sorted(n for n in os.listdir(src) if n.startswith("IMG."))
    if args.only:
        names = [n for n in names if n in args.only]
    status = 0
    for name in names:
        try:
            table = extract_table(os.path.join(src, name),
                                  os.path.join(args.out, name), args.scale)
        except ImageTableError as e:
            print("%-18s FAILED: %s" % (name, e))
            status = 1
            continue
        print("%-18s %3d images, %d trailing byte(s)"
              % (name, table.count, table.trailing_bytes))
    return status


if __name__ == "__main__":
    sys.exit(main())
