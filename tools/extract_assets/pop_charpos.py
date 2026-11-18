"""Character position arithmetic: game coordinates -> blitter parameters.

New code for the pop-study project; contains no original code or data.
Each function is a literal port of one small original routine, keeping the
8-bit wraparound, so results can be compared with the 6502 code step by step.

Evidence (original repository @ 25243550, "01 POP Source/Source"):

    add_char_x   ADDCHARX     CTRLSUBS.S 353-363
    base_x       GETBASEX     CTRLSUBS.S 329-340
    setup_char   SETUPCHAR    CTRLSUBS.S 794-841
    cvtx         CVTX         GRAFIX.S 909-957, tables in TABLES.S
    placement    ADDCHAROBJ   GAMEBG.S 1050-1096; ADDMID GRAFIX.S 341-378;
                 MLayMask     HIRES.S 1364-1367 (XCO -= WIDTH when mirrored)

Coordinates:
    CharX, CharY   game units on a 140 x 192 grid whose left edge is x = 58
    FCharX, FCharY hi-res dots (280 x 192); FCharY is the image's bottom line
    CharFace       $FF = facing left (artwork as stored), $00 = facing right
"""

from __future__ import annotations

from dataclasses import dataclass

SCRN_LEFT = 58        # EQ.S 479
SCRN_TOP = 0          # EQ.S 481
FACE_LEFT = 0xFF
FACE_RIGHT = 0x00
BLOCK_WIDTH = 14      # TABLES.S BlockEdge: -12 + 14*n
FLOOR_Y = (55, 118, 181)   # TABLES.S FloorY for block rows 0, 1, 2


def add_char_x(char_x: int, a: int, face: int) -> int:
    """CharX + a when facing right, CharX - a when facing left (8-bit)."""
    a &= 0xFF
    if face & 0x80:                 # facing left
        a = (-a) & 0xFF
    return (a + char_x) & 0xFF


def base_x(char_x: int, face: int, fdx: int, fcheck: int) -> int:
    """X of the weight-bearing foot, in game units (8-bit)."""
    a = (-(fcheck & 0x1F)) & 0xFF
    a = (a + fdx) & 0xFF
    return add_char_x(char_x, a, face)


def setup_char(char_x: int, char_y: int, face: int,
               fdx: int, fdy: int, fcheck: int):
    """Return (FCharX, FCharY): FCharX as a signed 16-bit value, FCharY 8-bit."""
    lo = (add_char_x(char_x, fdx, face) - SCRN_LEFT) & 0xFF
    doubled = lo << 1                       # asl FCharX / rol FCharX+1
    lo, hi = doubled & 0xFF, doubled >> 8
    if hi != 0 and lo >= 0xF0:              # small negative value
        hi = 0xFF
    fchar_y = (fdy + char_y - SCRN_TOP) & 0xFF
    if ((fcheck ^ face) & 0x80) == 0:       # high bits match: odd dot
        lo += 1
        if lo > 0xFF:
            lo &= 0xFF
            hi = (hi + 1) & 0xFF
    x = (hi << 8) | lo
    if x >= 0x8000:
        x -= 0x10000
    return x, fchar_y


def cvtx(x: int):
    """280-wide dot coordinate -> (byte column, dot offset 0-6).

    The original uses tables for 0-255 and steps of 252 outside that range;
    the result equals floor division and modulus by 7 for any x.
    """
    return x // 7, x % 7


@dataclass(frozen=True)
class Placement:
    fchar_x: int
    fchar_y: int
    xco: int          # byte column passed to the blitter
    offset: int       # dot shift 0-6
    yco: int          # bottom line
    mirror: bool
    left_dot: int     # leftmost dot column of the image's byte box
    right_dot: int    # one past the rightmost dot column of the byte box
    top_line: int
    base_x: int       # game units
    base_dot: int     # the same in dots: 2 * (base_x - 58)


def placement(char_x: int, char_y: int, face: int, fdx: int, fdy: int,
              fcheck: int, width_bytes: int, height: int) -> Placement:
    fx, fy = setup_char(char_x, char_y, face, fdx, fdy, fcheck)
    xco, offset = cvtx(fx)
    mirror = (face & 0x80) == 0
    left = fx - 7 * width_bytes if mirror else fx
    bx = base_x(char_x, face, fdx, fcheck)
    return Placement(fx, fy, xco, offset, fy, mirror, left, left + 7 * width_bytes,
                     fy - height + 1, bx, 2 * (bx - SCRN_LEFT))
