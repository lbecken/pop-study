"""Tests for tools/extract_assets/pop_imgtable.py.

Unit tests use synthetic tables only (no original data in this repository).
Integration tests run against a local checkout of the original repository
when POP_SOURCE_DIR points at one; otherwise they are skipped.
"""

import os
import struct
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))

from png_writer import encode_grey_png, scale_rows  # noqa: E402
from pop_imgtable import ImageTableError, parse_image_table  # noqa: E402

BASE = 0x6000


def build_table(images, base=BASE, trailing=b""):
    """images: list of (width_bytes, height, data_bytes)."""
    n = len(images)
    addr = base + 1 + 2 * (n + 1)
    ptrs, body = [], b""
    for w, h, data in images:
        assert len(data) == w * h
        ptrs.append(addr)
        body += bytes([w, h]) + data
        addr += 2 + w * h
    ptrs.append(addr)
    return bytes([n]) + b"".join(struct.pack("<H", p) for p in ptrs) + body + trailing


def test_single_image_dimensions_and_offsets():
    data = build_table([(2, 3, bytes([0x81, 0x80, 0x82, 0x80, 0x84, 0x80]))])
    t = parse_image_table(data)
    assert t.count == 1
    assert t.base_address == BASE
    im = t.images[0]
    assert (im.index, im.width_bytes, im.height, im.width_dots) == (1, 2, 3, 14)
    assert im.address == BASE + 5
    assert im.file_offset == 5
    assert t.used_length == len(data)
    assert t.trailing_bytes == 0


def test_rows_are_stored_bottom_up():
    # stored order: bottom row first (LayGen draws from YCO upward)
    data = build_table([(1, 3, bytes([0x81, 0x82, 0x84]))])
    im = parse_image_table(data).images[0]
    assert im.rows_top_down() == [b"\x84", b"\x82", b"\x81"]


def test_bit0_is_leftmost_and_bit7_is_not_a_dot():
    data = build_table([(1, 1, bytes([0x80 | 0b0000101]))])
    im = parse_image_table(data).images[0]
    assert im.dots() == [[1, 0, 1, 0, 0, 0, 0]]
    data = build_table([(1, 1, bytes([0x80]))])
    assert parse_image_table(data).images[0].dots() == [[0] * 7]


def test_hibit_clear_count():
    data = build_table([(2, 1, bytes([0x01, 0x81]))])
    assert parse_image_table(data).images[0].hibit_clear_count() == 1


def test_multiple_images_and_trailing_slack():
    data = build_table(
        [(1, 1, b"\x80"), (2, 2, b"\x81\x82\x83\x84"), (1, 2, b"\xff\x80")],
        trailing=b"\x00" * 7)
    t = parse_image_table(data)
    assert [i.index for i in t.images] == [1, 2, 3]
    assert [(i.width_bytes, i.height) for i in t.images] == [(1, 1), (2, 2), (1, 2)]
    assert t.images[1].data == b"\x81\x82\x83\x84"
    assert t.trailing_bytes == 7


def test_other_base_address_is_detected():
    data = build_table([(1, 1, b"\x80")], base=0x8400)
    assert parse_image_table(data).base_address == 0x8400


@pytest.mark.parametrize("mutate, message", [
    (lambda d: d[:2], "too short"),
    (lambda d: b"\x00" + d[1:], "count is zero"),
    (lambda d: d[:6], "outside file"),
    (lambda d: d[:5] + bytes([d[5] + 1]) + d[6:], "pointers span"),
])
def test_malformed_tables_are_rejected(mutate, message):
    good = build_table([(1, 2, b"\x80\x81")])
    with pytest.raises(ImageTableError, match=message):
        parse_image_table(mutate(good))


def test_descending_pointers_are_rejected():
    good = bytearray(build_table([(1, 1, b"\x80"), (1, 1, b"\x80")]))
    struct.pack_into("<H", good, 3, 0x6000)  # second pointer below the first
    with pytest.raises(ImageTableError, match="not ascending"):
        parse_image_table(bytes(good))


def test_png_writer_signature_and_scale():
    png = encode_grey_png([[0, 255], [255, 0]])
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    assert struct.unpack(">II", png[16:24]) == (2, 2)
    assert scale_rows([[1, 2]], 2) == [[1, 1, 2, 2], [1, 1, 2, 2]]


# --- integration tests against the original checkout (optional) -------------

SOURCE = os.environ.get("POP_SOURCE_DIR")
IMAGES = os.path.join(SOURCE, "01 POP Source", "Images") if SOURCE else None
needs_source = pytest.mark.skipif(
    not (IMAGES and os.path.isdir(IMAGES)),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")

# (file, image count) as observed at original commit 25243550
EXPECTED_COUNTS = {
    "IMG.BGTAB1.DUN": 127, "IMG.BGTAB1.PAL": 127,
    "IMG.BGTAB2.DUN": 51, "IMG.BGTAB2.PAL": 50,
    "IMG.CHTAB1": 65, "IMG.CHTAB2": 70, "IMG.CHTAB3": 78,
    "IMG.CHTAB4.FAT": 32, "IMG.CHTAB4.GD": 32, "IMG.CHTAB4.SHAD": 32,
    "IMG.CHTAB4.SKEL": 32, "IMG.CHTAB4.VIZ": 32,
    "IMG.CHTAB5": 45, "IMG.CHTAB6.A": 103, "IMG.CHTAB6.B": 73,
    "IMG.CHTAB7": 8,
}


@needs_source
def test_all_original_tables_parse_with_expected_counts():
    found = {}
    for name in sorted(os.listdir(IMAGES)):
        with open(os.path.join(IMAGES, name), "rb") as f:
            t = parse_image_table(f.read())
        found[name] = t.count
        assert t.base_address == 0x6000, name
        assert all(i.hibit_clear_count() == 0 for i in t.images), name
    assert found == EXPECTED_COUNTS


@needs_source
def test_run_cycle_images_exist_in_chtab1():
    # FRAMEDEF.S lines 23-36: frames 1-14 (run-4..run-17) use Fimage $01-$0E
    # with Fsword 0, i.e. images 1-14 of chtable1.
    with open(os.path.join(IMAGES, "IMG.CHTAB1"), "rb") as f:
        t = parse_image_table(f.read())
    for im in t.images[:14]:
        assert 1 <= im.width_bytes <= 8 and 30 <= im.height <= 43, im.index
