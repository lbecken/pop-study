"""Tests for the hi-res blitter model, position arithmetic and the Merlin
features added for TABLES.S / HRTABLES.S.

Unit tests are self-contained.  Integration tests need POP_SOURCE_DIR.
"""

import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))

from merlin_data import MerlinError, assemble  # noqa: E402
from png_writer import encode_rgb_png  # noqa: E402
from pop_charpos import (BLOCK_WIDTH, FACE_LEFT, FACE_RIGHT, FLOOR_Y,  # noqa: E402
                         SCRN_LEFT, add_char_x, base_x, cvtx, placement, setup_char)
from pop_hires import (AMASKS, BMASKS, CARRY, MASKTAB, MIRROR, SHIFT,  # noqa: E402
                       CropNotModelled, HiresPage, lay_mask, row_address)

# --- Merlin additions --------------------------------------------------------------


def test_lup_and_variables():
    asm = assemble("""
 org $1000
]b = 0
 lup 3
 db ]b,]b
]b = ]b+7
 --^
 db ]b
""")
    assert asm.image == bytes([0, 0, 7, 7, 14, 14, 21])
    assert asm.label_lines == []            # variables are not address labels


def test_variables_do_not_change_local_scope():
    asm = assemble(" org $1000\ntop db 1\n]v = 5\n:x db ]v\n dw :x\n")
    assert asm.locals[("top", ":x")] == 0x1001


def test_byte_selectors():
    asm = assemble(" org $E180\nT db 0\n db #T-$80,>T-$80,<T,#>T\n")
    assert asm.image == bytes([0, 0x00, 0xE1, 0x80, 0xE1])


@pytest.mark.parametrize("source, message", [
    (" org $1000\n lup 2\n db 1\n", "lup without"),
    (" org $1000\n db 1\n --^\n", "without lup"),
    (" org $1000\n lup n\n db 1\n --^\n", "must be a literal"),
    (" org $1000\n lup 2\n lup 2\n db 1\n --^\n --^\n", "nested lup"),
])
def test_lup_errors(source, message):
    with pytest.raises(MerlinError, match=message):
        assemble(source)


# --- hi-res tables -----------------------------------------------------------------


def test_shift_and_carry_reassemble_the_byte():
    for n in range(7):
        for b in range(128):
            assert SHIFT[n][b] & 0x80 and not CARRY[n][b] & 0x80
            wide = ((SHIFT[n][b] & 0x7F) | (CARRY[n][b] << 7))
            assert wide == b << n


def test_mirror_is_an_involution_on_seven_bits():
    for b in range(128):
        assert MIRROR[MIRROR[b] & 0x7F] & 0x7F == b
    assert MIRROR[0b0000001] == 0x80 | 0b1000000


def test_mask_clears_each_dot_and_its_neighbours_within_the_byte():
    assert MASKTAB[0] == 0xFF
    assert MASKTAB[0b0000001] == 0xFC       # dots 0,1 cleared; nothing wraps to the left
    assert MASKTAB[0b0001000] == 0xE3       # dots 2,3,4
    assert MASKTAB[0b1000000] == 0x9F       # dots 5,6; nothing wraps to the right
    assert MASKTAB[0x7F] == 0x80


def test_edge_masks():
    assert AMASKS == [0x80, 0x81, 0x83, 0x87, 0x8F, 0x9F, 0xBF]
    assert BMASKS == [0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0]


def test_row_address_interleave():
    assert [row_address(y) for y in (0, 1, 8, 64, 191)] == [
        0x2000, 0x2400, 0x2080, 0x2028, 0x3FD0]


# --- blitter -----------------------------------------------------------------------

IMG = bytes([0x80 | 0b0000001, 0x80 | 0b0100000,      # bottom row
             0x80 | 0b1111111, 0x80 | 0b0000000])     # top row; 2 bytes wide


def box(dots, x0, y0, w, h):
    return [row[x0:x0 + w] for row in dots[y0:y0 + h]]


def expected_dots(offset, mirror):
    rows = []
    for r in (1, 0):                                   # top row first
        bits = []
        for b in IMG[r * 2:r * 2 + 2]:
            bits += [(b >> i) & 1 for i in range(7)]
        if mirror:
            bits.reverse()
        rows.append([0] * offset + bits + [0] * (7 - offset))
    return rows


@pytest.mark.parametrize("offset", range(7))
def test_lay_on_blank_page_places_dots(offset):
    page = HiresPage()
    lay_mask(page, IMG, 2, 2, xco=10, offset=offset, yco=100)
    assert box(page.dots(), 70, 99, 21, 2) == expected_dots(offset, False)
    assert sum(map(sum, page.dots())) == sum(map(sum, expected_dots(offset, False)))


@pytest.mark.parametrize("offset", range(7))
def test_mirrored_lay_is_the_reflection_ending_at_xco(offset):
    page = HiresPage()
    lay_mask(page, IMG, 2, 2, xco=12, offset=offset, yco=100, mirror=True)
    # MLayMask: XCO -= WIDTH, so the box starts at column 10 like the test above
    assert box(page.dots(), 70, 99, 21, 2) == expected_dots(offset, True)


def test_image_bytes_get_bit7_and_carry_byte_keeps_its_own():
    page = HiresPage(0x00)
    lay_mask(page, IMG, 2, 2, xco=10, offset=3, yco=100)
    row = page.rows[100]
    assert row[10] & 0x80 and row[11] & 0x80           # SHIFTn sets bit 7
    assert not row[12] & 0x80                          # carry byte: screen's bit 7 kept
    assert row[9] == 0x00 and row[13] == 0x00


def test_mask_clears_a_one_dot_halo_but_not_across_bytes():
    page = HiresPage(0x7F)                              # every dot lit
    one = bytes([0x80 | 0b0001000])                     # single dot, bit 3
    lay_mask(page, one, 1, 1, xco=5, offset=0, yco=50)
    d = page.dots()[50]
    assert d[35:42] == [1, 1, 0, 1, 0, 1, 1]            # dot kept, neighbours cleared
    assert d[34] == 1 and d[42] == 1
    assert page.dots()[49][35:42] == [1] * 7            # no vertical halo

    page = HiresPage(0x7F)
    edge = bytes([0x80 | 0b0000001])                    # dot 0: halo cannot reach byte 4
    lay_mask(page, edge, 1, 1, xco=5, offset=0, yco=50)
    d = page.dots()[50]
    assert d[34] == 1 and d[35:38] == [1, 0, 1]

    page = HiresPage(0x7F)                              # shifted: mask shifts with the image
    lay_mask(page, edge, 1, 1, xco=5, offset=2, yco=50)
    d = page.dots()[50]
    assert d[35:40] == [1, 1, 1, 0, 1]


def test_offscreen_images_are_refused_not_guessed():
    page = HiresPage()
    with pytest.raises(CropNotModelled):
        lay_mask(page, IMG, 2, 2, xco=38, offset=0, yco=100)   # carry byte would be col 40
    with pytest.raises(CropNotModelled):
        lay_mask(page, IMG, 2, 2, xco=1, offset=0, yco=100, mirror=True)
    with pytest.raises(CropNotModelled):
        lay_mask(page, IMG, 2, 2, xco=10, offset=0, yco=0)
    with pytest.raises(ValueError):
        lay_mask(page, bytes([0x01, 0x80, 0x80, 0x80]), 2, 2, 10, 0, 100)


# --- position arithmetic -------------------------------------------------------------


def test_add_char_x_direction_and_wraparound():
    assert add_char_x(100, 5, FACE_RIGHT) == 105
    assert add_char_x(100, 5, FACE_LEFT) == 95
    assert add_char_x(100, -3 & 0xFF, FACE_LEFT) == 103
    assert add_char_x(2, 5, FACE_LEFT) == 253          # 8-bit wrap, as on the 6502
    assert add_char_x(254, 5, FACE_RIGHT) == 3


def test_base_x_is_foot_pixels_in_from_the_image_edge():
    # facing left: image left edge is CharX - Fdx; base = edge + foot
    assert base_x(168, FACE_LEFT, 3, 0x40 | 7) == 168 - 3 + 7
    # facing right: image right edge is CharX + Fdx; base = edge - foot
    assert base_x(87, FACE_RIGHT, 3, 0x40 | 7) == 87 + 3 - 7


def test_setup_char_doubles_and_adds_the_odd_dot():
    # Fcheck bit 7 set, facing left ($FF): high bits match -> +1
    assert setup_char(168, 181, FACE_LEFT, 0, 0, 0xC0) == (2 * (168 - 58) + 1, 181)
    assert setup_char(168, 181, FACE_LEFT, 0, 0, 0x40) == (2 * (168 - 58), 181)
    # facing right the sense is inverted
    assert setup_char(87, 181, FACE_RIGHT, 0, 0, 0x40)[0] == 2 * (87 - 58) + 1
    assert setup_char(87, 181, FACE_RIGHT, 0, 0, 0xC0)[0] == 2 * (87 - 58)
    assert setup_char(168, 181, FACE_LEFT, 4, 0xFD, 0x40) == (2 * (164 - 58), 178)


def test_setup_char_off_screen_ranges():
    # 8-bit (CharX - 58) >= 128 doubles past 255: treated as a large positive x ...
    assert setup_char(58 + 130, 0, FACE_LEFT, 0, 0, 0x40)[0] == 260
    # ... unless the low byte is >= $F0, which is read as a small negative x
    assert setup_char(58 - 3, 0, FACE_LEFT, 0, 0, 0x40)[0] == -6
    assert setup_char(58 - 8, 0, FACE_LEFT, 0, 0, 0x40)[0] == -16
    # just outside that window the wrap is not corrected (a pitfall to keep)
    assert setup_char(58 - 9, 0, FACE_LEFT, 0, 0, 0x40)[0] == 494


def test_cvtx():
    assert cvtx(0) == (0, 0)
    assert cvtx(219) == (31, 2)
    assert cvtx(300) == (42, 6)
    assert cvtx(-1) == (-1, 6)
    assert cvtx(-8) == (-2, 6)


def test_placement_left_and_right():
    p = placement(168, 181, FACE_LEFT, 0, 0, 0x40 | 3, width_bytes=2, height=41)
    assert (p.fchar_x, p.xco, p.offset, p.mirror) == (220, 31, 3, False)
    assert (p.left_dot, p.right_dot, p.top_line, p.yco) == (220, 234, 141, 181)
    assert p.base_x == 171 and p.base_dot == 226
    q = placement(87, 181, FACE_RIGHT, 0, 0, 0x40 | 3, width_bytes=2, height=41)
    assert q.mirror and (q.left_dot, q.right_dot) == (q.fchar_x - 14, q.fchar_x)
    assert q.base_x == 84


def test_rgb_png():
    png = encode_rgb_png([[(0, 0, 0), (255, 0, 0)]])
    assert png[:8] == b"\x89PNG\r\n\x1a\n" and png[25] == 2      # colour type 2


# --- integration tests against the original checkout (optional) -------------------

SOURCE = os.environ.get("POP_SOURCE_DIR")
SRC = os.path.join(SOURCE, "01 POP Source", "Source") if SOURCE else None
needs_source = pytest.mark.skipif(
    not (SRC and os.path.isdir(SRC)),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")


def _asm(name):
    with open(os.path.join(SRC, name), encoding="ascii") as f:
        return assemble(f.read())


def _table(asm, name, n):
    a = asm.symbols[name] - asm.origin
    return list(asm.image[a:a + n])


@needs_source
def test_generated_tables_equal_original_hrtables():
    h = _asm("HRTABLES.S")
    assert h.origin == 0xE000 and h.end <= 0xEA00          # unpack org, EQ.S 15
    for n in range(7):
        assert _table(h, "SHIFT%d" % n, 128) == SHIFT[n]
        assert _table(h, "CARRY%d" % n, 128) == CARRY[n]
    assert _table(h, "MIRROR", 128) == MIRROR
    assert _table(h, "MASKTAB", 128) == MASKTAB
    assert _table(h, "AMASKS", 7) == AMASKS
    assert _table(h, "BMASKS", 7) == BMASKS
    ylo, yhi = _table(h, "YLO", 192), _table(h, "YHI", 192)
    assert [ylo[y] | (yhi[y] << 8) for y in range(192)] == [row_address(y) for y in range(192)]
    # SHIFTL/SHIFTH hold SHIFTn-$80, so image bytes (bit 7 set) index directly
    lo, hi = _table(h, "SHIFTL", 7), _table(h, "SHIFTH", 7)
    assert [lo[n] | (hi[n] << 8) for n in range(7)] == [
        h.symbols["SHIFT%d" % n] - 0x80 for n in range(7)]


@needs_source
def test_geometry_constants_equal_original_tables():
    t = _asm("TABLES.S")
    assert t.origin == 0x0E00 and t.end <= 0x1290          # frameadv org, EQ.S 23
    bt, ot = _table(t, "ByteTable", 256), _table(t, "OffsetTable", 256)
    assert all((bt[x], ot[x]) == cvtx(x) for x in range(256))
    edge = _table(t, "BlockEdge", 20)
    assert edge[5] == SCRN_LEFT                             # block 0 starts at the screen edge
    assert all((edge[i + 1] - edge[i]) & 0xFF == BLOCK_WIDTH for i in range(19))
    assert tuple(_table(t, "FloorY", 5)[1:4]) == FLOOR_Y
    assert _table(t, "Mult7", 16) == [7 * i for i in range(16)]


@needs_source
def test_run_cycle_render_properties():
    import render_run_cycle as r
    frames, table, names, images = r.load(SOURCE)
    left = r.simulate(frames, table, names, images, "startrun", 23, 168, 2, FACE_LEFT)
    right = r.simulate(frames, table, names, images, "startrun", 23, 255 - 168, 2, FACE_RIGHT)
    assert [s["frame"] for s in left[:16]] == [15, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                               12, 13, 14, 7]
    assert left[15]["char_x"] == 168 - 14 - 26 and left[23]["char_x"] == 168 - 14 - 52

    # the supporting foot stays within 2 game units for four ticks, then
    # switches to the other foot one step further on
    stances = [[s["base_x"] for s in left[a:a + 4]] for a in (8, 12, 16, 20)]
    assert stances == [[154, 152, 153, 153], [140, 138, 139, 138],
                       [128, 126, 127, 127], [114, 112, 113, 112]]
    assert all(max(x) - min(x) <= 2 for x in stances)
    assert stances[0][0] - stances[2][0] == 26               # one full cycle

    # every frame's feet reach the floor line: bottom image row is CharY
    assert all(s["fchar_y"] == FLOOR_Y[2] for s in left)

    # facing right is an exact dot mirror of facing left (axis between dots 139)
    for a, b in zip(left, right):
        da, db = r.draw_page(a).dots(), r.draw_page(b).dots()
        assert all(da[y][x] == db[y][278 - x] for y in range(130, 192) for x in range(279))
        assert a["base_x"] + b["base_x"] == 255
