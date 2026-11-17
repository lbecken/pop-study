"""Parser for Prince of Persia (Apple II, 1989) hi-res image tables (IMG.* files).

This module is NEW code written for the pop-study reconstruction project.
It contains no original game code or data.

Format evidence (paths relative to the original repository,
jmechner/Prince-of-Persia-Apple-II @ 25243550):

* "01 POP Source/Source/HIRES.S" lines 264-277, ``setimage``:
    image N (1-based) is located through a little-endian 16-bit pointer
    stored at table offsets 2N-1 and 2N.
* HIRES.S lines 311-350, ``PREPREP``:
    the pointer addresses a 2-byte header -- WIDTH (in bytes), HEIGHT (in
    lines) -- followed by the bitmap bytes.
* HIRES.S lines 718-812, ``LayGen``:
    drawing starts at screen line YCO and *decrements* YCO after each row
    ("Next line up"), so rows are stored bottom-to-top.  Within a row,
    bytes are stored left-to-right.
* "01 POP Source/Source/HRTABLES.S" lines 46-56 and 255-261:
    the SHIFTn/CARRYn tables are indexed by "byte value w/hibit clr" and
    are addressed as ``SHIFTn-$80``, i.e. image bytes are expected to
    have bit 7 set.

Inferred from the data (consistent in all 16 files of "01 POP Source/Images"):

* Table byte 0 is the image count N.  (``setimage`` never reads it.)
* N+1 pointers follow; the last one is the end address of the last image.
* All pointers in the repository files are based at $6000, whatever
  address the table is loaded at in the game.

Apple II hardware fact (not from the repository): in hi-res mode each byte
supplies 7 dots, bit 0 being the leftmost; bit 7 selects the colour
palette / half-dot delay and is not a dot.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import List

DOTS_PER_BYTE = 7


class ImageTableError(ValueError):
    """Raised when a file does not have the expected image-table layout."""


@dataclass(frozen=True)
class Image:
    """One image of an image table.

    ``data`` holds the raw bitmap bytes exactly as stored: ``height`` rows of
    ``width_bytes`` bytes, bottom row first.
    """

    index: int          # 1-based image number, as used by the game
    address: int        # pointer value stored in the table (Apple II address)
    file_offset: int    # offset of the 2-byte header within the file
    width_bytes: int
    height: int
    data: bytes

    @property
    def width_dots(self) -> int:
        return self.width_bytes * DOTS_PER_BYTE

    def rows_top_down(self) -> List[bytes]:
        """Rows of raw bytes in display order (top row first)."""
        w = self.width_bytes
        rows = [self.data[i * w:(i + 1) * w] for i in range(self.height)]
        rows.reverse()
        return rows

    def dots(self) -> List[List[int]]:
        """Bitmap as rows (top first) of 0/1 dots, left to right.

        Bit 7 of every byte is dropped (it is not a dot).
        """
        out = []
        for row in self.rows_top_down():
            line = []
            for b in row:
                for bit in range(DOTS_PER_BYTE):
                    line.append((b >> bit) & 1)
            out.append(line)
        return out

    def hibit_clear_count(self) -> int:
        """Number of bitmap bytes whose bit 7 is clear (expected: 0)."""
        return sum(1 for b in self.data if b < 0x80)


@dataclass(frozen=True)
class ImageTable:
    count: int
    base_address: int       # address the pointers assume for file offset 0
    images: List[Image]
    used_length: int        # bytes covered by header + pointers + images
    trailing_bytes: int     # file bytes after the last image (slack)


def parse_image_table(data: bytes) -> ImageTable:
    """Parse an IMG.* image table.  Raises ImageTableError if malformed."""
    if len(data) < 3:
        raise ImageTableError("file too short for an image table")
    count = data[0]
    if count == 0:
        raise ImageTableError("image count is zero")
    ptr_end = 1 + 2 * (count + 1)
    if len(data) < ptr_end:
        raise ImageTableError("file too short for %d+1 pointers" % count)
    ptrs = [struct.unpack_from("<H", data, 1 + 2 * i)[0] for i in range(count + 1)]

    # The first image starts right after the pointer block.
    base = ptrs[0] - ptr_end
    if base < 0:
        raise ImageTableError("first pointer precedes the pointer block")

    images = []
    for i in range(count):
        start, end = ptrs[i], ptrs[i + 1]
        if end < start:
            raise ImageTableError("pointers not ascending at image %d" % (i + 1))
        off = start - base
        if off + 2 > len(data):
            raise ImageTableError("image %d header outside file" % (i + 1))
        width, height = data[off], data[off + 1]
        size = 2 + width * height
        if end - start != size:
            raise ImageTableError(
                "image %d: header says %dx%d (%d bytes) but pointers span %d bytes"
                % (i + 1, width, height, size, end - start))
        if off + size > len(data):
            raise ImageTableError("image %d data outside file" % (i + 1))
        images.append(Image(
            index=i + 1,
            address=start,
            file_offset=off,
            width_bytes=width,
            height=height,
            data=bytes(data[off + 2:off + size]),
        ))

    used = ptrs[count] - base
    return ImageTable(
        count=count,
        base_address=base,
        images=images,
        used_length=used,
        trailing_bytes=len(data) - used,
    )
