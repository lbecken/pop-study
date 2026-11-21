#!/usr/bin/env python3
"""Can this directory of replacement image tables stand in for the original's?

New code for the pop-study project.  It contains no original code or data.

The port can read its pictures from a directory of new image tables
instead of the user's checkout of the original (`--images`,
docs/blender-to-sdl3.md).  This is the mechanical part of checking such a
directory, against the checkout:

  errors (exit status 1)
    - no PROVENANCE.txt, or an empty one
    - a table the port loads during play is missing
    - a file is not an image table, or has image bytes without bit 7
    - a table has another number of images than the original's, or an
      image of another size: the game's rules read the sizes of character
      images, and background pieces are placed by their size

  reported, for a person to judge
    - images whose dots are exactly the original's: a directory of new
      pictures should not contain the original's unlabelled
    - lit dots with no lit neighbour to the left or right: on a colour
      monitor such a dot is not white but takes the colour of its column
      (docs/port/presentation.md), and it gets a black outline

It cannot say whether the pictures are any good, or whether feet stand
where the rules think they do: see docs/blender-to-sdl3.md, section 5.

Usage:
    check_imgset.py <replacement directory> <original checkout>
"""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "extract_assets"))

from pop_imgtable import ImageTableError, parse_image_table  # noqa: E402

IMAGES = os.path.join("01 POP Source", "Images")
PROVENANCE = "PROVENANCE.txt"

# The tables the port loads in play (modern-port/src/app/pop_app.c).  The
# others belong to parts that are not ported (title, princess scenes).
IN_PLAY = ("IMG.BGTAB1.DUN", "IMG.BGTAB1.PAL", "IMG.BGTAB2.DUN", "IMG.BGTAB2.PAL",
           "IMG.CHTAB1", "IMG.CHTAB2", "IMG.CHTAB3", "IMG.CHTAB4.GD", "IMG.CHTAB4.SKEL",
           "IMG.CHTAB4.FAT", "IMG.CHTAB4.SHAD", "IMG.CHTAB4.VIZ", "IMG.CHTAB5")
NOT_IN_PLAY = ("IMG.CHTAB6.A", "IMG.CHTAB6.B", "IMG.CHTAB7")


def lone_dots(image) -> int:
    count = 0
    for row in image.dots():
        for x, d in enumerate(row):
            if d and not (x > 0 and row[x - 1]) and not (x + 1 < len(row) and row[x + 1]):
                count += 1
    return count


def check_table(new: bytes, original: bytes):
    """Returns (errors, notes) for one table."""
    errors, notes = [], []
    try:
        table = parse_image_table(new)
    except ImageTableError as exc:
        return ["not an image table: %s" % exc], notes
    want = parse_image_table(original)
    if table.count != want.count:
        errors.append("%d images, the original has %d" % (table.count, want.count))
        return errors, notes
    wrong = [(a.index, a.width_bytes, a.height, b.width_bytes, b.height)
             for a, b in zip(table.images, want.images)
             if (a.width_bytes, a.height) != (b.width_bytes, b.height)]
    for index, w, h, ow, oh in wrong[:5]:
        errors.append("image %d is %d bytes x %d lines, the original %d x %d"
                      % (index, w, h, ow, oh))
    if len(wrong) > 5:
        errors.append("... and %d more images of another size" % (len(wrong) - 5))
    clear = sum(im.hibit_clear_count() for im in table.images)
    if clear:
        errors.append("%d image bytes without bit 7, which the blitter's tables need" % clear)
    same = sum(1 for a, b in zip(table.images, want.images) if a.data == b.data)
    if same:
        notes.append("%d of %d images have exactly the original's dots" % (same, table.count))
    lone = sum(lone_dots(im) for im in table.images)
    if lone:
        notes.append("%d lit dots without a lit neighbour beside them" % lone)
    return errors, notes


def check_set(directory: str, source: str):
    """Returns (number of errors, lines of report)."""
    report, failed = [], 0
    path = os.path.join(directory, PROVENANCE)
    if not os.path.isfile(path) or os.path.getsize(path) == 0:
        failed += 1
        report.append("error: no %s saying who made the pictures, with what, and under "
                      "what licence" % PROVENANCE)
    for name in IN_PLAY + NOT_IN_PLAY:
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            if name in IN_PLAY:
                failed += 1
                report.append("error: %s is missing" % name)
            else:
                report.append("note:  %s is not there (not used by the port)" % name)
            continue
        with open(path, "rb") as f:
            new = f.read()
        with open(os.path.join(source, IMAGES, name), "rb") as f:
            original = f.read()
        errors, notes = check_table(new, original)
        failed += len(errors)
        report += ["error: %s: %s" % (name, e) for e in errors]
        report += ["note:  %s: %s" % (name, n) for n in notes]
        if not errors:
            report.append("ok:    %s: %d images of the original's sizes" % (name, new[0]))
    known = set(IN_PLAY + NOT_IN_PLAY + (PROVENANCE,))
    for name in sorted(set(os.listdir(directory)) - known):
        report.append("note:  %s is not a file the port reads" % name)
    return failed, report


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    failed, report = check_set(argv[1], argv[2])
    print("\n".join(report))
    if failed:
        print("%d errors: the port will refuse this directory, or behave differently with it"
              % failed)
        return 1
    print("The directory can stand in for the original's tables as far as sizes go.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
