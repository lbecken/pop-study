"""Minimal dependency-free PNG reader.  New code for the pop-study project.

Reads what a paint program or a renderer writes when asked for a small
image: 8-bit-per-channel greyscale, greyscale with alpha, RGB, RGBA and
palette images, and 1-, 2- and 4-bit greyscale and palette images; not
interlaced.  16-bit channels and Adam7 interlacing are refused rather
than guessed at.

The result is what the image-table encoder needs: for every pixel, is it
lit.  A pixel is lit when it is more than half opaque and its luminance is
at least half of full.
"""

from __future__ import annotations

import struct
import zlib
from typing import List, Tuple

SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}       # colour type -> samples per pixel


class PngError(ValueError):
    """Raised for files this reader does not understand."""


def _chunks(data: bytes):
    if data[:8] != SIGNATURE:
        raise PngError("not a PNG file")
    at = 8
    while at + 8 <= len(data):
        length, tag = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + length]
        if len(body) != length or at + 12 + length > len(data):
            raise PngError("truncated chunk")
        if zlib.crc32(tag + body) & 0xFFFFFFFF != struct.unpack_from(">I", data, at + 8 + length)[0]:
            raise PngError("bad checksum in chunk %s" % tag.decode("latin-1"))
        yield tag, body
        at += 12 + length
        if tag == b"IEND":
            return
    raise PngError("no IEND chunk")


def _unfilter(raw: bytes, height: int, stride: int, bpp: int) -> List[bytearray]:
    """Undo PNG's per-line filters.  `bpp`: bytes per whole pixel, at least 1."""
    if len(raw) != height * (stride + 1):
        raise PngError("image data has the wrong length")
    lines, prev = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if kind == 1:                                   # Sub
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif kind == 2:                                 # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif kind == 3:                                 # Average
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif kind == 4:                                 # Paeth
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif kind != 0:
            raise PngError("unknown filter type %d" % kind)
        lines.append(line)
        prev = line
    return lines


def read_png(data: bytes) -> Tuple[int, int, List[List[Tuple[int, int, int, int]]]]:
    """Decode a PNG file.  Returns (width, height, rows of (r, g, b, a))."""
    header, palette, alpha, idat = None, None, None, bytearray()
    for tag, body in _chunks(data):
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif tag == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body) - 2, 3)]
        elif tag == b"tRNS":
            alpha = body
        elif tag == b"IDAT":
            idat += body
    if header is None:
        raise PngError("no IHDR chunk")
    width, height, depth, ctype, compression, flt, interlace = header
    if width == 0 or height == 0:
        raise PngError("empty image")
    if ctype not in CHANNELS or compression != 0 or flt != 0:
        raise PngError("unknown PNG variant")
    if interlace != 0:
        raise PngError("interlaced PNG files are not supported: save without interlacing")
    if depth == 16 or depth not in (1, 2, 4, 8) or (depth < 8 and ctype not in (0, 3)):
        raise PngError("%d-bit channels are not supported: save with 8 bits per channel" % depth)
    if ctype == 3 and palette is None:
        raise PngError("palette image without a palette")
    channels = CHANNELS[ctype]
    stride = (width * channels * depth + 7) // 8
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as exc:
        raise PngError("damaged image data: %s" % exc) from None
    lines = _unfilter(raw, height, stride, max(1, channels * depth // 8))

    rows = []
    top = (1 << depth) - 1
    for line in lines:
        if depth == 8:
            samples = line
        else:
            samples = [(line[(i * depth) // 8] >> (8 - depth - (i * depth) % 8)) & top
                       for i in range(width * channels)]
        row = []
        for x in range(width):
            s = samples[x * channels:(x + 1) * channels]
            if ctype == 0:
                v = s[0] * 255 // top
                px = (v, v, v, 255)
            elif ctype == 2:
                px = (s[0], s[1], s[2], 255)
            elif ctype == 3:
                if s[0] >= len(palette):
                    raise PngError("colour %d is not in the palette" % s[0])
                px = palette[s[0]] + (alpha[s[0]] if alpha and s[0] < len(alpha) else 255,)
            elif ctype == 4:
                px = (s[0], s[0], s[0], s[1])
            else:
                px = (s[0], s[1], s[2], s[3])
            row.append(px)
        rows.append(row)
    return width, height, rows


def read_png_dots(data: bytes) -> List[List[int]]:
    """Decode a PNG file to rows (top first) of 0/1: is the pixel lit."""
    _, _, rows = read_png(data)
    return [[1 if a >= 128 and 299 * r + 587 * g + 114 * b >= 127500 else 0
             for r, g, b, a in row] for row in rows]
