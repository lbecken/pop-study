#!/usr/bin/env python3
"""A complete set of image tables with placeholder pictures.

New code for the pop-study project.  It contains no original code or data.

For every `IMG.*` table of a checkout of the original, this writes a table
with the same number of images of the same sizes, in which every picture
is a drawn-from-nothing placeholder: a frame around the image's box, and
for character images a diagonal as well, so that a figure can be told from
a wall.  Nothing of the original's dots is copied; the only thing taken
from the original is the list of sizes, which the game's rules need
(docs/blender-to-sdl3.md).

What it is for:

  - testing the replacement-picture route of the port (`--images`) end to
    end, which needs a complete set and there is no new artwork yet;
  - looking at the game's geometry: with these tables the screen shows
    the boxes the rules work with instead of the pictures in them.

The result is generated from the user's own checkout and, like everything
derived from it, is not part of this repository.

Usage:
    make_placeholder_set.py <original checkout> <output directory>
"""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "extract_assets"))

from make_imgtable import encode_image_table  # noqa: E402
from pop_imgtable import parse_image_table  # noqa: E402

IMAGES = os.path.join("01 POP Source", "Images")

PROVENANCE = """\
Placeholder image tables for the pop-study reconstruction

Made by:    tools/assets/make_placeholder_set.py of the pop-study project
Made from:  nothing but the number of images in each table of the original
            game and the width and height of each image, read from the
            user's own checkout of jmechner/Prince-of-Persia-Apple-II.
            No picture, and no dot of a picture, is taken from the original.
Pictures:   a frame around each image's box; in the character tables
            (IMG.CHTAB*) a diagonal as well.
Purpose:    testing, and showing the boxes the game's rules work with.
            These are not artwork and are not meant to look like the game.

Screens, pictures and videos made with these tables show placeholders, not
the original game's graphics, and should say so.
"""


def placeholder(width: int, height: int, diagonal: bool):
    """Rows of 0/1, top first: a frame, two dots thick at the sides."""
    rows = []
    for y in range(height):
        if y == 0 or y == height - 1:
            row = [1] * width
        else:
            row = [0] * width
            row[0] = row[1] = row[-1] = row[-2] = 1
            if diagonal and height > 1:
                x = (width - 2) * y // (height - 1)
                row[x] = row[x + 1] = 1
        rows.append(row)
    return rows


def make_set(source: str, out: str):
    os.makedirs(out, exist_ok=True)
    made = []
    directory = os.path.join(source, IMAGES)
    for name in sorted(os.listdir(directory)):
        if not name.startswith("IMG."):
            continue
        with open(os.path.join(directory, name), "rb") as f:
            table = parse_image_table(f.read())
        character = name.startswith("IMG.CHTAB")
        blob = encode_image_table([placeholder(im.width_dots, im.height, character)
                                   for im in table.images])
        with open(os.path.join(out, name), "wb") as f:
            f.write(blob)
        made.append((name, table.count, len(blob)))
    with open(os.path.join(out, "PROVENANCE.txt"), "w") as f:
        f.write(PROVENANCE)
    return made


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    made = make_set(argv[1], argv[2])
    if not made:
        print("make_placeholder_set: no IMG.* files under %s" % os.path.join(argv[1], IMAGES),
              file=sys.stderr)
        return 1
    print("%d placeholder tables, %d images, written to %s"
          % (len(made), sum(m[1] for m in made), argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
