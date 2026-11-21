#!/usr/bin/env python3
"""Image tables in the original's format, from pictures and back.

New code for the pop-study project.  It contains no original code or data.

The other direction of tools/extract_assets/pop_imgtable.py, for
replacement artwork (docs/blender-to-sdl3.md): a directory of 1-bit
pictures named by image number becomes a table the port, and every tool of
this project, reads like an original `IMG.*` file.

The format, as established in docs/graphics-formats.md from HIRES.S
(`setimage` 264-277, `PREPREP` 311-350, `LayGen` 718-812) and from the 16
files themselves:

    byte 0            the number of images, n
    n + 1 pointers    little-endian, as if the file were loaded at $6000;
                      the last is the end of the last image
    per image         width in bytes, height in lines, then the lines from
                      the bottom one up, each left to right, seven dots to
                      the byte with bit 0 leftmost

Bit 7 of every image byte is set: the blitter's shift tables are indexed
by "byte value w/hibit clr" from a base 128 below their start
(HRTABLES.S 46-56, 255-261), and all 16 original files have it set in
every image byte without exception.  That is why a table can be rebuilt
from its dots alone, and what the round-trip test checks: decoding an
original table and encoding the result gives the original bytes back, up
to the table's end.

Usage:
    make_imgtable.py decode <table file> <directory>
        one PNG per image (001.png ...), one pixel per dot
    make_imgtable.py encode <directory> <table file> [--like <table file>]
        the reverse; a picture's width must be a multiple of 7.  With
        --like, the count and every picture's size must be those of the
        given table: the game's rules read the sizes of character images.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
from typing import List, Sequence

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "extract_assets"))

from png_reader import PngError, read_png_dots  # noqa: E402
from png_writer import encode_grey_png  # noqa: E402
from pop_imgtable import DOTS_PER_BYTE, parse_image_table  # noqa: E402

BASE_ADDRESS = 0x6000       # what the pointers of all 16 original files assume
MAX_IMAGES = 255            # the count is one byte


class EncodeError(ValueError):
    """Raised when pictures cannot be made into an image table."""


def encode_image(dots: Sequence[Sequence[int]]) -> bytes:
    """One image (rows of 0/1, top first) as width, height and its bytes."""
    height = len(dots)
    if height == 0 or height > 255:
        raise EncodeError("an image is 1 to 255 lines high, not %d" % height)
    width = len(dots[0])
    if width == 0 or width % DOTS_PER_BYTE != 0 or width // DOTS_PER_BYTE > 255:
        raise EncodeError("an image is a multiple of 7 dots wide (at most 1785), not %d" % width)
    out = bytearray((width // DOTS_PER_BYTE, height))
    for row in reversed(dots):                      # stored bottom line first
        if len(row) != width:
            raise EncodeError("the lines of an image differ in length")
        for at in range(0, width, DOTS_PER_BYTE):
            value = 0x80
            for bit in range(DOTS_PER_BYTE):
                if row[at + bit]:
                    value |= 1 << bit
            out.append(value)
    return bytes(out)


def encode_image_table(images: Sequence[Sequence[Sequence[int]]],
                       base_address: int = BASE_ADDRESS) -> bytes:
    """A whole table from its images, each given as rows of 0/1, top first."""
    if not 1 <= len(images) <= MAX_IMAGES:
        raise EncodeError("a table holds 1 to 255 images, not %d" % len(images))
    bodies = [encode_image(dots) for dots in images]
    at = base_address + 1 + 2 * (len(bodies) + 1)
    pointers = []
    for body in bodies:
        pointers.append(at)
        at += len(body)
    pointers.append(at)
    if at > 0x10000:
        raise EncodeError("the table would end beyond the 64K the pointers can reach")
    return (bytes((len(bodies),)) + b"".join(struct.pack("<H", p) for p in pointers)
            + b"".join(bodies))


def reencode(data: bytes) -> bytes:
    """Decode a table and encode it again (the round trip)."""
    table = parse_image_table(data)
    return encode_image_table([im.dots() for im in table.images], table.base_address)


def numbered_pngs(directory: str) -> List[str]:
    """001.png, 002.png, ... of a directory, in order, without gaps."""
    found = {}
    for name in os.listdir(directory):
        match = re.fullmatch(r"(\d+)\.png", name, re.IGNORECASE)
        if match:
            number = int(match.group(1))
            if number in found:
                raise EncodeError("%s and %s are both image %d" % (found[number], name, number))
            found[number] = name
    if not found:
        raise EncodeError("%s holds no pictures named by image number (001.png ...)" % directory)
    if sorted(found) != list(range(1, len(found) + 1)):
        missing = sorted(set(range(1, max(found) + 1)) - set(found))
        raise EncodeError("image numbers must run from 1 without gaps; missing: %s"
                          % ", ".join(map(str, missing[:10])))
    return [os.path.join(directory, found[n]) for n in sorted(found)]


def encode_directory(directory: str, like: bytes = None) -> bytes:
    images = []
    for path in numbered_pngs(directory):
        with open(path, "rb") as f:
            try:
                images.append(read_png_dots(f.read()))
            except PngError as exc:
                raise EncodeError("%s: %s" % (path, exc)) from None
    if like is not None:
        want = parse_image_table(like)
        if len(images) != want.count:
            raise EncodeError("%d pictures, but the table to match has %d images"
                              % (len(images), want.count))
        for dots, im in zip(images, want.images):
            if (len(dots[0]), len(dots)) != (im.width_dots, im.height):
                raise EncodeError("image %d is %d x %d dots, the table to match has %d x %d"
                                  % (im.index, len(dots[0]), len(dots), im.width_dots, im.height))
    return encode_image_table(images)


def decode_to_directory(data: bytes, directory: str) -> int:
    table = parse_image_table(data)
    os.makedirs(directory, exist_ok=True)
    for im in table.images:
        with open(os.path.join(directory, "%03d.png" % im.index), "wb") as f:
            f.write(encode_grey_png([[255 if d else 0 for d in row] for row in im.dots()]))
    return table.count


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    dec = sub.add_parser("decode", help="a table to one PNG per image")
    dec.add_argument("table")
    dec.add_argument("directory")
    enc = sub.add_parser("encode", help="numbered PNGs to a table")
    enc.add_argument("directory")
    enc.add_argument("table")
    enc.add_argument("--like", help="a table whose image count and sizes must be matched")
    args = parser.parse_args(argv)
    try:
        if args.command == "decode":
            with open(args.table, "rb") as f:
                count = decode_to_directory(f.read(), args.directory)
            print("%s: %d images written to %s" % (args.table, count, args.directory))
        else:
            like = None
            if args.like:
                with open(args.like, "rb") as f:
                    like = f.read()
            blob = encode_directory(args.directory, like)
            with open(args.table, "wb") as f:
                f.write(blob)
            print("%s: %d images, %d bytes" % (args.table, blob[0], len(blob)))
    except (EncodeError, ValueError, OSError) as exc:
        print("make_imgtable: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
