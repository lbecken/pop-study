"""Byte-level model of the Apple II hi-res page and of the game's mask blitter.

New code for the pop-study project; contains no original code or data.

It models only what milestone 3 needs: drawing a character image that lies
entirely on screen, in "mask" mode, either normally or mirrored.  Cropping
(the CROP routine), the peel/save-under buffers and the other opacity modes
are not modelled; an image that would need cropping raises CropNotModelled.

Evidence (original repository @ 25243550, "01 POP Source/Source"):

    LayMask          HIRES.S 833-998      mask-then-OR, left to right
    MLayMask         HIRES.S 1356-1533    same, image read right to left and
                                          each byte mirrored; XCO -= WIDTH first
    kid drawn with   GAMEBG.S 432-437     OPACITY = mask, via addmid
    mirror flag      GRAFIX.S 362-366     set when FCharFace is "right"
    tables           HRTABLES.S           YLO/YHI, SHIFTn/CARRYn, MIRROR,
                                          MASKTAB, AMASKS/BMASKS

The lookup tables are generated here from closed-form rules.  An integration
test checks them byte for byte against the assembled HRTABLES.S, so the rules
are *confirmed* descriptions of the original tables, not approximations.
"""

from __future__ import annotations

from typing import List, Sequence

COLUMNS = 40          # bytes per hi-res row
ROWS = 192
DOTS_PER_BYTE = 7
WIDTH_DOTS = COLUMNS * DOTS_PER_BYTE   # 280


class CropNotModelled(NotImplementedError):
    """The image is partly off screen; the original would crop it."""


# --- tables (index = byte value with bit 7 clear, 0-127) -----------------------

def shift_table(n: int) -> List[int]:
    """SHIFTn: byte shifted right on screen by n dots, bit 7 set."""
    return [((b << n) & 0x7F) | 0x80 for b in range(128)]


def carry_table(n: int) -> List[int]:
    """CARRYn: the dots that spill into the next byte, bit 7 clear."""
    return [b >> (7 - n) for b in range(128)]


def mirror_table() -> List[int]:
    """MIRROR: the 7 dots in reverse order, bit 7 set."""
    return [int("{:07b}".format(b)[::-1], 2) | 0x80 for b in range(128)]


def mask_table() -> List[int]:
    """MASKTAB: screen dots to keep; each image dot clears itself and its
    two neighbours within the same byte.  Bit 7 set."""
    return [0x80 | (~(b | (b << 1) | (b >> 1)) & 0x7F) for b in range(128)]


AMASKS = [0x80 | ((1 << n) - 1) for n in range(7)]            # dots left of the image
BMASKS = [0x80 | (0x7F & ~((1 << n) - 1)) for n in range(7)]  # dots right of the carry

SHIFT = [shift_table(n) for n in range(7)]
CARRY = [carry_table(n) for n in range(7)]
MIRROR = mirror_table()
MASKTAB = mask_table()


def row_address(y: int) -> int:
    """YLO/YHI: address of the first byte of screen line y on page 1."""
    return 0x2000 + ((y & 7) << 10) + (((y >> 3) & 7) << 7) + (y >> 6) * 0x28


# --- page ---------------------------------------------------------------------------

class HiresPage:
    """192 rows of 40 bytes, addressed by (row, column) rather than by the
    interleaved Apple II memory layout."""

    def __init__(self, fill: int = 0x00):
        self.rows = [bytearray([fill]) * COLUMNS for _ in range(ROWS)]

    def dots(self) -> List[List[int]]:
        """280x192 dots, 0/1, bit 0 of each byte leftmost, bit 7 ignored."""
        return [[(b >> bit) & 1 for b in row for bit in range(DOTS_PER_BYTE)]
                for row in self.rows]


def lay_mask(page: HiresPage, data: bytes, width: int, height: int,
             xco: int, offset: int, yco: int, mirror: bool = False) -> None:
    """Draw one image in mask mode, as LayMask / MLayMask do when no cropping
    is needed.

    data    width*height bytes, bottom row first, every byte with bit 7 set
    xco     byte column: left edge of the image, or for mirror=True the column
            just right of it (MLayMask subtracts WIDTH)
    offset  0-6 dots of right shift
    yco     screen line of the image's bottom row
    """
    if not 0 <= offset <= 6:
        raise ValueError("offset must be 0-6")
    if len(data) != width * height:
        raise ValueError("data length does not match width*height")
    left = xco - width if mirror else xco
    # The routine always touches one extra byte to the right (the carry byte).
    if left < 0 or left + width > COLUMNS - 1 or yco > ROWS - 1 or yco - height + 1 < 0:
        raise CropNotModelled("image at columns %d..%d, lines %d..%d"
                              % (left, left + width, yco - height + 1, yco))
    shift, carry = SHIFT[offset], CARRY[offset]
    for r in range(height):
        row = page.rows[yco - r]
        line = data[r * width:(r + 1) * width]
        order = reversed(line) if mirror else line
        mask_carry = AMASKS[offset]
        image_carry = AMASKS[offset] & row[left]
        col = left
        for b in order:
            if b < 0x80:
                raise ValueError("image byte without bit 7")
            i = b & 0x7F
            if mirror:
                i = MIRROR[i] & 0x7F
            image_byte = shift[i] | image_carry
            image_carry = carry[i]
            m = MASKTAB[i] & 0x7F
            row[col] = ((shift[m] | mask_carry) & row[col]) | image_byte
            mask_carry = carry[m]
            col += 1
        row[col] = ((BMASKS[offset] | mask_carry) & row[col]) | image_carry


def crop_dots(dots: Sequence[Sequence[int]], x0: int, y0: int, w: int, h: int):
    return [list(r[x0:x0 + w]) for r in dots[y0:y0 + h]]
