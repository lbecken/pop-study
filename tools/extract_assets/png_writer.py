"""Minimal dependency-free PNG writer (8-bit greyscale).  New code."""

from __future__ import annotations

import struct
import zlib
from typing import List, Sequence


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def encode_grey_png(rows: Sequence[Sequence[int]]) -> bytes:
    """Encode rows of 0-255 grey values (all rows the same length) as PNG."""
    height = len(rows)
    width = len(rows[0]) if height else 0
    if width == 0 or height == 0:
        raise ValueError("cannot encode an empty image")
    raw = bytearray()
    for row in rows:
        if len(row) != width:
            raise ValueError("ragged rows")
        raw.append(0)  # filter type 0 (None)
        raw.extend(row)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr)
            + _chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + _chunk(b"IEND", b""))


def scale_rows(rows: Sequence[Sequence[int]], factor: int) -> List[List[int]]:
    """Nearest-neighbour integer upscale."""
    if factor <= 1:
        return [list(r) for r in rows]
    out = []
    for row in rows:
        wide = [v for v in row for _ in range(factor)]
        out.extend([wide] * factor)
    return out
