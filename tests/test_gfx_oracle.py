"""The original blitter (HIRES.S), run in the two-bank simulator, against the
Python model of milestone 3 (tools/extract_assets/pop_hires.py).

These tests need POP_SOURCE_DIR.  The blitter is called the way the game
calls it: from auxiliary memory, through the GRAFIX entry points that copy
the parameters to the main zero page and switch banks.  The hi-res page it
leaves in main memory must equal the model's, byte for byte.
"""

import os
import random
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
for sub in (("tools", "extract_assets"), ("tools", "oracle")):
    sys.path.insert(0, os.path.join(HERE, "..", *sub))

SOURCE = os.environ.get("POP_SOURCE_DIR")
pytestmark = pytest.mark.skipif(
    not (SOURCE and os.path.isdir(os.path.join(SOURCE, "01 POP Source", "Source"))),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")

import pop_hires as ph  # noqa: E402


@pytest.fixture(scope="module")
def gfx():
    from pop_gfx_oracle import GfxOracle
    o = GfxOracle(SOURCE)
    o.load_table("chtable1", "IMG.CHTAB1")
    o.load_table("chtable2", "IMG.CHTAB2")
    o.load_table("bgtable1", "IMG.BGTAB1.DUN")
    o.load_table("bgtable2", "IMG.BGTAB2.DUN")
    o.keep()
    return o


def image(o, table, number):
    """(width, height, data) of an image, read from the oracle's memory."""
    from pop_gfx_oracle import TABLES
    base, bank = TABLES[table]
    at = base + 2 * number - 1
    p = o.cpu.peek(at, bank) | (o.cpu.peek(at + 1, bank) << 8)
    w, h = o.cpu.peek(p, bank), o.cpu.peek(p + 1, bank)
    return w, h, o.cpu.peek(p + 2, bank, w * h)


def count(o, table):
    """Number of images in a table: its first byte."""
    from pop_gfx_oracle import TABLES
    base, bank = TABLES[table]
    return o.cpu.peek(base, bank)


def draw(o, entry, table, number, xco, yco, offset, opacity, background):
    """Fill page 1 with `background`, call a GRAFIX drawing entry, return the page."""
    from pop_banked import AUX, MAIN
    from pop_gfx_oracle import PAGE1, TABLES
    o.reset()
    o.cpu.poke(PAGE1, MAIN, background)
    base, bank = TABLES[table]
    o.set_vars({"PAGE": 0, "XCO": xco, "YCO": yco, "OFFSET": offset, "IMAGE": number,
                "OPACITY": opacity, "BANK": 3 if bank == AUX else 2,
                "LEFTCUT": 0, "RIGHTCUT": 40, "TOPCUT": 0, "BOTCUT": 192})
    t = o.addr("TABLE")
    o.cpu.poke(t, AUX, bytes([base & 0xFF, base >> 8]))
    o.call(entry)
    return o.page(1)


def noise(rng):
    return bytes(rng.randrange(256) for _ in range(ph.PAGE_SIZE))


def test_both_banks_are_laid_out_as_documented(gfx):
    from pop_banked import AUX, MAIN
    assert gfx.modules["HIRES"].origin == 0xEE00 and gfx.modules["HRTABLES"].origin == 0xE000
    assert gfx.modules["HIRES"].end <= 0xF880            # below MASTER
    # the same address holds different things in the two banks
    assert gfx.cpu.peek(0xEE00, MAIN, 3) != gfx.cpu.peek(0xEE00, AUX, 3)
    assert gfx.cpu.altzp == AUX and gfx.cpu.ramrd == AUX and gfx.cpu.ramwrt == AUX


def test_cls_fills_the_page_and_leaves_the_game_in_auxiliary_memory(gfx):
    from pop_banked import AUX
    gfx.reset()
    gfx.set_vars({"PAGE": 0})
    gfx.call("cls")
    assert gfx.page(1) == b"\x80" * ph.PAGE_SIZE
    assert gfx.page(2) == b"\x00" * ph.PAGE_SIZE
    assert gfx.cpu.altzp == AUX and gfx.cpu.ramrd == AUX and gfx.cpu.ramwrt == AUX


def test_mask_blitter_matches_the_model(gfx):
    rng = random.Random(1989)
    done = 0
    while done < 120:
        table = rng.choice(("chtable1", "chtable2"))
        number = rng.randint(1, count(gfx, table))
        w, h, data = image(gfx, table, number)
        mirror = rng.random() < 0.5
        offset = rng.randrange(7)
        left = rng.randint(0, 39 - w)                    # wholly on screen, carry byte too
        yco = rng.randint(h - 1, 191)
        xco = left + w if mirror else left
        background = noise(rng)
        got = draw(gfx, "lay", table, number, xco, yco, offset,
                   ph.OP_MASK | (0x80 if mirror else 0), background)
        page = ph.HiresPage()
        page.mem[:] = background
        ph.lay_mask(page, data, w, h, xco, offset, yco, mirror=mirror)
        assert got == bytes(page.mem), (table, number, xco, yco, offset, mirror)
        done += 1


def test_general_blitter_matches_the_model(gfx):
    rng = random.Random(1990)
    for _ in range(90):
        table = rng.choice(("chtable1", "chtable2"))
        number = rng.randint(1, count(gfx, table))
        w, h, data = image(gfx, table, number)
        offset, opacity = rng.randrange(7), rng.choice((ph.OP_AND, ph.OP_ORA, ph.OP_STA))
        xco, yco = rng.randint(0, 39 - w), rng.randint(h - 1, 191)
        background = noise(rng)
        got = draw(gfx, "lay", table, number, xco, yco, offset, opacity, background)
        page = ph.HiresPage()
        page.mem[:] = background
        ph.lay_gen(page, data, w, h, xco, offset, yco, opacity)
        assert got == bytes(page.mem), (table, number, xco, yco, offset, opacity)


def test_fast_blitters_match_the_model(gfx):
    rng = random.Random(1991)
    for _ in range(120):
        table = rng.choice(("bgtable1", "bgtable2"))
        number = rng.randint(1, count(gfx, table))
        w, h, data = image(gfx, table, number)
        xco, yco = rng.randint(0, 40 - w), rng.randint(0, 191)   # may stick out at the top
        background = noise(rng)
        page = ph.HiresPage()
        page.mem[:] = background
        if rng.random() < 0.3:
            got = draw(gfx, "fastmask", table, number, xco, yco, 0, 0, background)
            ph.fastmask(page, data, w, h, xco, yco)
        else:
            opacity = rng.choice((ph.OP_AND, ph.OP_ORA, ph.OP_STA, ph.OP_EOR))
            got = draw(gfx, "fastlay", table, number, xco, yco, 0, opacity, background)
            ph.fastlay(page, data, w, h, xco, yco, opacity)
        assert got == bytes(page.mem), (table, number, xco, yco)


# --- the full-screen redraw ------------------------------------------------------------

@pytest.mark.parametrize("level_no", range(15))
def test_full_redraw_of_every_screen_matches_the_room_model(gfx, level_no):
    """The original's own redraw (SETINITIALS, GETSCRNS, DoSure: SURE builds
    the image lists, DRAWALL hands them to the blitter) against the Python
    model of milestone 5 (pop_room.py), for every screen the level file
    declares: all 8192 bytes of the page."""
    from mos6502 import CpuError
    from pop_banked import AUX
    from pop_level import load_level
    from pop_oracle import BLUEPRINT
    from pop_room import RoomBuilder, bgset_file_suffix, draw_room
    import render_room as rr

    bg = rr.load_bgdata(SOURCE)
    path = os.path.join(SOURCE, "01 POP Source", "Levels", "LEVEL%d" % level_no)
    level = load_level(path)
    level.set_initials()
    tables = rr.load_tables(SOURCE, bg, level_no)
    set1, set2 = bg.level_bgset1[level_no], bg.level_bgset2[level_no]
    gfx.reset()
    gfx.load_table("bgtable1", "IMG.BGTAB1." + bgset_file_suffix(set1))
    gfx.load_table("bgtable2", "IMG.BGTAB2." + bgset_file_suffix(set2))
    with open(path, "rb") as f:
        gfx.load_level(f.read())
    gfx.set_vars({"level": level_no, "BGset1": set1, "BGset2": set2, "PAGE": 0})
    gfx.call("setinitials")
    screens = gfx.cpu.peek(BLUEPRINT + 2048, AUX) - 1
    assert screens >= 6
    for screen in range(1, screens + 1):
        gfx.set_vars({"VisScrn": screen, "PAGE": 0})
        try:
            gfx.call("getscrns")
            gfx.call("DoSure", module="TOPCTRL", max_cycles=80_000_000)
        except CpuError as error:                    # pragma: no cover
            pytest.fail("level %d screen %d: %s" % (level_no, screen, error))
        builder = RoomBuilder(bg, level, level_no, screen)
        builder.sure()
        page = draw_room(builder, tables[0][1], tables[1][1])
        got = gfx.page(1)
        differ = sum(a != b for a, b in zip(got, bytes(page.mem)))
        assert differ == 0, "level %d screen %d: %d bytes differ" % (level_no, screen, differ)


def test_the_two_inverty_routines_are_the_same_code():
    """The oracle stands HIRES's INVERTY in for UNPACK's (pop_gfx_oracle.py)."""
    def body(name, start, end):
        with open(os.path.join(SOURCE, "01 POP Source", "Source", name), encoding="ascii") as f:
            lines = f.read().splitlines()[start - 1:end]
        return [line for line in lines if line.strip()]
    unpack, hires = body("UNPACK.S", 540, 565), body("HIRES.S", 2098, 2123)
    assert unpack[0].split()[0] == "INVERTY" and unpack == hires
