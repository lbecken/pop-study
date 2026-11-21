"""Tests for tools/assets: the image-table encoder, the PNG reader, the
check of a replacement set and the placeholder generator.

Unit tests use made-up pictures only.  Tests against the original's tables
run when POP_SOURCE_DIR points at a checkout; otherwise they are skipped.
"""

import os
import random
import struct
import sys
import zlib

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "assets"))

import check_imgset  # noqa: E402
import make_imgtable as mk  # noqa: E402
import make_placeholder_set  # noqa: E402
from png_reader import PngError, read_png, read_png_dots  # noqa: E402
from png_writer import encode_grey_png, encode_rgb_png  # noqa: E402
from pop_imgtable import parse_image_table  # noqa: E402

SOURCE = os.environ.get("POP_SOURCE_DIR")
IMAGES = os.path.join("01 POP Source", "Images")
needs_source = pytest.mark.skipif(
    not SOURCE or not os.path.isdir(os.path.join(SOURCE or "", IMAGES)),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")


def random_dots(rng, width_bytes, height):
    return [[rng.randrange(2) for _ in range(width_bytes * 7)] for _ in range(height)]


# --- the encoder --------------------------------------------------------------------------------

def test_one_image_byte_for_byte():
    # two lines of one byte: the top one has its leftmost dot lit, the
    # bottom one its rightmost
    blob = mk.encode_image_table([[[1, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 1]]])
    assert blob == bytes([1,                 # one image
                          0x05, 0x60,        # it starts after the count and two pointers
                          0x09, 0x60,        # and ends four bytes later
                          1, 2,              # one byte wide, two lines high
                          0xC0,              # bottom line first: dot 6, and bit 7
                          0x81])             # top line: dot 0, and bit 7


def test_round_trip_of_random_tables():
    rng = random.Random(7)
    for _ in range(20):
        images = [random_dots(rng, rng.randint(1, 6), rng.randint(1, 50))
                  for _ in range(rng.randint(1, 40))]
        blob = mk.encode_image_table(images)
        table = parse_image_table(blob)
        assert table.count == len(images)
        assert table.base_address == 0x6000
        assert table.used_length == len(blob) and table.trailing_bytes == 0
        assert [im.dots() for im in table.images] == images
        assert all(im.hibit_clear_count() == 0 for im in table.images)
        assert mk.reencode(blob) == blob


def test_reencode_drops_what_follows_the_table():
    blob = mk.encode_image_table([[[1] * 7] * 3])
    assert mk.reencode(blob + b"leftover") == blob


@pytest.mark.parametrize("images, message", [
    ([], "1 to 255 images"),
    ([[[1] * 7]] * 256, "1 to 255 images"),
    ([[]], "lines high"),
    ([[[1] * 7] * 256], "lines high"),
    ([[[1] * 8]], "multiple of 7"),
    ([[[]]], "multiple of 7"),
    ([[[1] * 7, [1] * 14]], "differ in length"),
])
def test_what_cannot_be_a_table(images, message):
    with pytest.raises(mk.EncodeError, match=message):
        mk.encode_image_table(images)


def test_a_table_too_big_for_its_pointers():
    big = [[1] * 1785] * 255                         # 255 x 255 bytes
    with pytest.raises(mk.EncodeError, match="64K"):
        mk.encode_image_table([big])


# --- the PNG reader -----------------------------------------------------------------------------

def chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))


def png(width, height, depth, ctype, lines, extra=b"", filters=None, interlace=0):
    """A PNG from already packed lines; `filters`: a filter type per line,
    applied here the way an encoder would."""
    bpp = max(1, {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype] * depth // 8)
    raw, prev = bytearray(), bytes(len(lines[0]))
    for y, line in enumerate(lines):
        kind = filters[y] if filters else 0
        out = bytearray()
        for i, v in enumerate(line):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 0:
                pred = 0
            elif kind == 1:
                pred = a
            elif kind == 2:
                pred = b
            elif kind == 3:
                pred = (a + b) >> 1
            else:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            out.append((v - pred) & 0xFF)
        raw.append(kind)
        raw += out
        prev = bytes(line)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, ctype, 0, 0, interlace))
            + extra + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def test_reads_what_the_projects_writer_writes():
    rows = [[0, 255, 127, 128], [255, 0, 0, 255]]
    assert read_png_dots(encode_grey_png(rows)) == [[0, 1, 0, 1], [1, 0, 0, 1]]
    rgb = [[(255, 255, 255), (0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]]
    # luminance: red and blue are dark, green and yellow light
    assert read_png_dots(encode_rgb_png(rgb)) == [[1, 0, 0, 1, 0, 1]]


def test_every_filter_type():
    rng = random.Random(3)
    width, height = 9, 12
    lines = [bytes(rng.randrange(256) for _ in range(width * 3)) for _ in range(height)]
    want = [[tuple(line[3 * x:3 * x + 3]) + (255,) for x in range(width)] for line in lines]
    for filters in ([0] * height, [1] * height, [2] * height, [3] * height, [4] * height,
                    [y % 5 for y in range(height)]):
        assert read_png(png(width, height, 8, 2, lines, filters=filters)) == (width, height, want)


def test_one_bit_grey():
    # 10 pixels a line: 1010101010, then 0000011111
    lines = [bytes([0b10101010, 0b10000000]), bytes([0b00000111, 0b11000000])]
    assert read_png_dots(png(10, 2, 1, 0, lines)) == [[1, 0] * 5, [0] * 5 + [1] * 5]


def test_two_and_four_bit_grey():
    assert read_png_dots(png(4, 1, 2, 0, [bytes([0b00011011])])) == [[0, 0, 1, 1]]
    assert read_png_dots(png(3, 1, 4, 0, [bytes([0x07, 0x80])])) == [[0, 0, 1]]


def test_palette_with_transparency():
    palette = chunk(b"PLTE", bytes([0, 0, 0, 255, 255, 255, 255, 255, 255]))
    transparency = chunk(b"tRNS", bytes([255, 255, 0]))    # colour 2: white but transparent
    lines = [bytes([0, 1, 2, 1])]
    assert read_png_dots(png(4, 1, 8, 3, lines, palette + transparency)) == [[0, 1, 0, 1]]
    packed = [bytes([0b00011001])]                          # the same, two bits a pixel
    assert read_png_dots(png(4, 1, 2, 3, packed, palette + transparency)) == [[0, 1, 0, 1]]


def test_alpha_decides_before_colour():
    grey_alpha = [bytes([255, 255, 255, 0, 0, 255, 255, 127, 255, 128])]
    assert read_png_dots(png(5, 1, 8, 4, grey_alpha)) == [[1, 0, 0, 0, 1]]
    rgba = [bytes([255, 255, 255, 255, 255, 255, 255, 10, 0, 0, 0, 255])]
    assert read_png_dots(png(3, 1, 8, 6, rgba)) == [[1, 0, 0]]


@pytest.mark.parametrize("data, message", [
    (b"GIF89a", "not a PNG"),
    (png(1, 1, 16, 0, [bytes(2)]), "16-bit"),
    (png(1, 1, 8, 0, [bytes(1)], interlace=1), "interlaced"),
    (png(1, 1, 8, 3, [bytes(1)]), "without a palette"),
    (png(2, 1, 8, 3, [bytes([0, 5])], chunk(b"PLTE", bytes(3))), "not in the palette"),
    (png(2, 2, 8, 0, [bytes(2)]), "wrong length"),
    (encode_grey_png([[0]])[:-12], "IEND"),
    (encode_grey_png([[0]])[:30], "truncated"),
])
def test_what_the_reader_refuses(data, message):
    with pytest.raises(PngError, match=message):
        read_png(data)


def test_a_damaged_file_is_noticed():
    data = bytearray(encode_grey_png([[0, 255, 0]] * 4))
    data[45] ^= 0x10                                        # inside the image data
    with pytest.raises(PngError, match="checksum"):
        read_png(bytes(data))


# --- pictures in a directory --------------------------------------------------------------------

def write_pictures(directory, images, names=None):
    os.makedirs(directory, exist_ok=True)
    for n, dots in enumerate(images):
        name = names[n] if names else "%03d.png" % (n + 1)
        with open(os.path.join(directory, name), "wb") as f:
            f.write(encode_grey_png([[255 if d else 0 for d in row] for row in dots]))


def test_directory_round_trip(tmp_path):
    rng = random.Random(11)
    images = [random_dots(rng, rng.randint(1, 5), rng.randint(1, 45)) for _ in range(12)]
    blob = mk.encode_image_table(images)
    table_file, pictures, back = tmp_path / "IMG.T", tmp_path / "pictures", tmp_path / "IMG.BACK"
    table_file.write_bytes(blob + b"slack after the table")
    assert mk.main(["decode", str(table_file), str(pictures)]) == 0
    assert sorted(os.listdir(pictures)) == ["%03d.png" % n for n in range(1, 13)]
    assert mk.main(["encode", str(pictures), str(back), "--like", str(table_file)]) == 0
    assert back.read_bytes() == blob


def test_numbers_must_run_without_gaps(tmp_path):
    write_pictures(str(tmp_path), [[[1] * 7]] * 3, ["001.png", "002.png", "004.png"])
    with pytest.raises(mk.EncodeError, match="missing: 3"):
        mk.encode_directory(str(tmp_path))


def test_two_files_for_one_number(tmp_path):
    write_pictures(str(tmp_path), [[[1] * 7]] * 2, ["1.png", "001.png"])
    with pytest.raises(mk.EncodeError, match="both image 1"):
        mk.encode_directory(str(tmp_path))


def test_an_empty_directory(tmp_path):
    with pytest.raises(mk.EncodeError, match="no pictures"):
        mk.encode_directory(str(tmp_path))


def test_like_holds_count_and_sizes(tmp_path, capsys):
    like = mk.encode_image_table([[[0] * 14] * 5, [[0] * 7] * 2])
    good, short, wide = str(tmp_path / "good"), str(tmp_path / "short"), str(tmp_path / "wide")
    write_pictures(good, [[[1] * 14] * 5, [[1] * 7] * 2])
    write_pictures(short, [[[1] * 14] * 5])
    write_pictures(wide, [[[1] * 14] * 5, [[1] * 14] * 2])
    assert parse_image_table(mk.encode_directory(good, like)).count == 2
    with pytest.raises(mk.EncodeError, match="1 pictures, but the table to match has 2"):
        mk.encode_directory(short, like)
    with pytest.raises(mk.EncodeError, match="image 2 is 14 x 2 dots, the table to match has 7 x 2"):
        mk.encode_directory(wide, like)
    # and the command says so instead of writing a table
    like_file, out = tmp_path / "like", tmp_path / "out"
    like_file.write_bytes(like)
    assert mk.main(["encode", wide, str(out), "--like", str(like_file)]) == 1
    assert not out.exists()
    assert "image 2" in capsys.readouterr().err


def test_a_picture_of_the_wrong_width_is_named(tmp_path):
    write_pictures(str(tmp_path), [[[1] * 10]])
    with pytest.raises(mk.EncodeError, match="multiple of 7"):
        mk.encode_directory(str(tmp_path))


def test_a_file_that_is_no_png_is_named(tmp_path):
    (tmp_path / "001.png").write_bytes(b"not a picture")
    with pytest.raises(mk.EncodeError, match="001.png: not a PNG"):
        mk.encode_directory(str(tmp_path))


# --- placeholders and the check of a set --------------------------------------------------------

def test_placeholder_is_a_frame():
    assert make_placeholder_set.placeholder(7, 4, False) == [
        [1, 1, 1, 1, 1, 1, 1],
        [1, 1, 0, 0, 0, 1, 1],
        [1, 1, 0, 0, 0, 1, 1],
        [1, 1, 1, 1, 1, 1, 1]]
    with_diagonal = make_placeholder_set.placeholder(14, 5, True)
    assert with_diagonal[2] == [1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1]
    assert make_placeholder_set.placeholder(7, 1, True) == [[1] * 7]


def test_lone_dots_are_counted():
    table = parse_image_table(mk.encode_image_table(
        [[[1, 0, 1, 1, 0, 0, 1], [0, 0, 0, 0, 0, 0, 0], [1, 1, 0, 1, 0, 1, 0]]]))
    assert check_imgset.lone_dots(table.images[0]) == 4


def test_check_table_findings():
    original = mk.encode_image_table([[[1, 1, 0, 0, 0, 1, 1]] * 3, [[0] * 14] * 2])
    same_sizes = mk.encode_image_table([[[0, 0, 0, 1, 0, 0, 0]] * 3, [[1] * 14] * 2])
    assert check_imgset.check_table(same_sizes, original) == (
        [], ["3 lit dots without a lit neighbour beside them"])
    errors, notes = check_imgset.check_table(original, original)
    assert errors == [] and notes == ["2 of 2 images have exactly the original's dots"]
    errors, _ = check_imgset.check_table(mk.encode_image_table([[[1] * 7] * 3]), original)
    assert errors == ["1 images, the original has 2"]
    errors, _ = check_imgset.check_table(
        mk.encode_image_table([[[1] * 7] * 4, [[1] * 14] * 2]), original)
    assert errors == ["image 1 is 1 bytes x 4 lines, the original 1 x 3"]
    errors, _ = check_imgset.check_table(b"\x01\x05", original)
    assert len(errors) == 1 and errors[0].startswith("not an image table")
    no_bit7 = bytearray(same_sizes)
    no_bit7[-1] &= 0x7F
    errors, _ = check_imgset.check_table(bytes(no_bit7), original)
    assert errors == ["1 image bytes without bit 7, which the blitter's tables need"]


def test_many_wrong_sizes_are_summed_up():
    original = mk.encode_image_table([[[0] * 7] * 2] * 8)
    other = mk.encode_image_table([[[0] * 7] * 3] * 8)
    errors, _ = check_imgset.check_table(other, original)
    assert len(errors) == 6 and errors[-1] == "... and 3 more images of another size"


# --- against the original's tables --------------------------------------------------------------

def original_tables():
    directory = os.path.join(SOURCE, IMAGES)
    return sorted(n for n in os.listdir(directory) if n.startswith("IMG."))


@needs_source
def test_every_original_table_round_trips():
    """The encoder's proof: dots alone give the original's bytes back."""
    names = original_tables()
    assert len(names) == 16
    for name in names:
        with open(os.path.join(SOURCE, IMAGES, name), "rb") as f:
            data = f.read()
        table = parse_image_table(data)
        assert mk.reencode(data) == data[:table.used_length], name


@needs_source
def test_original_table_through_png_files(tmp_path):
    for name in ("IMG.CHTAB1", "IMG.BGTAB2.PAL"):
        path = os.path.join(SOURCE, IMAGES, name)
        with open(path, "rb") as f:
            data = f.read()
        pictures, back = str(tmp_path / (name + ".d")), str(tmp_path / name)
        assert mk.main(["decode", path, pictures]) == 0
        assert mk.main(["encode", pictures, back, "--like", path]) == 0
        with open(back, "rb") as f:
            assert f.read() == data[:parse_image_table(data).used_length]


@needs_source
def test_placeholder_set_passes_the_check_and_copies_nothing(tmp_path):
    out = str(tmp_path / "set")
    made = make_placeholder_set.make_set(SOURCE, out)
    assert [m[0] for m in made] == original_tables()
    failed, report = check_imgset.check_set(out, SOURCE)
    assert failed == 0, report
    assert sum(1 for line in report if line.startswith("ok:")) == 16
    # no picture of the set is a picture of the original
    assert not any("exactly the original's dots" in line for line in report)
    assert check_imgset.main(["check_imgset.py", out, SOURCE]) == 0


@needs_source
def test_the_check_finds_what_is_wrong_with_a_set(tmp_path):
    out = str(tmp_path / "set")
    make_placeholder_set.make_set(SOURCE, out)
    os.remove(os.path.join(out, "PROVENANCE.txt"))
    os.remove(os.path.join(out, "IMG.CHTAB3"))
    os.remove(os.path.join(out, "IMG.CHTAB7"))                # not used in play
    with open(os.path.join(SOURCE, IMAGES, "IMG.CHTAB5"), "rb") as f:
        original = f.read()
    with open(os.path.join(out, "IMG.CHTAB5"), "wb") as f:    # the original's, unlabelled
        f.write(original)
    with open(os.path.join(out, "IMG.CHTAB2"), "wb") as f:    # another table's sizes
        f.write(mk.encode_image_table([[[1] * 7] * 9] * 70))
    with open(os.path.join(out, "notes.txt"), "w") as f:
        f.write("x")
    failed, report = check_imgset.check_set(out, SOURCE)
    text = "\n".join(report)
    assert "error: no PROVENANCE.txt" in text
    assert "error: IMG.CHTAB3 is missing" in text
    assert "note:  IMG.CHTAB7 is not there" in text
    assert "note:  IMG.CHTAB5: 45 of 45 images have exactly the original's dots" in text
    assert "error: IMG.CHTAB2: image 1 is 1 bytes x 9 lines" in text
    assert "note:  notes.txt is not a file the port reads" in text
    assert failed >= 3
    assert check_imgset.main(["check_imgset.py", out, SOURCE]) == 1
