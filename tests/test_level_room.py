"""Tests for the level parser, the fast blitters and the room builder.

Unit tests use synthetic levels and synthetic piece tables written for these
tests.  Integration tests need POP_SOURCE_DIR.
"""

import hashlib
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))

from pop_hires import (OP_AND, OP_EOR, OP_MASK, OP_ORA, OP_STA, CropNotModelled,  # noqa: E402
                       HiresPage, fastlay, fastmask, lay_gen, row_address)
from pop_level import (GMAXVAL, LEVEL_SIZE, OBJ, LevelError, obj_name,  # noqa: E402
                       parse_level)
from pop_room import BgData, RoomBuilder, RoomError, extract_table  # noqa: E402

# --- synthetic level ----------------------------------------------------------------


def make_level(blocks=None, specs=None, maps=None, kid=(1, 0, 0xFF), info0=25,
               linkloc=None, linkmap=None):
    """blocks/specs: {(screen, block): value}; maps: {screen: (l, r, a, b)}."""
    data = bytearray(LEVEL_SIZE)
    data[720:1440] = b"\xff" * 720                 # spec 255 everywhere
    for (s, b), v in (blocks or {}).items():
        data[(s - 1) * 30 + b] = v
    for (s, b), v in (specs or {}).items():
        data[720 + (s - 1) * 30 + b] = v
    for i, v in (linkloc or {}).items():
        data[1440 + i] = v
    for i, v in (linkmap or {}).items():
        data[1696 + i] = v
    for s, m in (maps or {}).items():
        data[1952 + (s - 1) * 4:1952 + s * 4] = bytes(m)
    data[2048] = info0
    data[2048 + 64:2048 + 67] = bytes(kid)
    return parse_level(bytes(data))


def test_level_size_and_sections():
    with pytest.raises(LevelError):
        parse_level(bytes(100))
    lv = make_level({(1, 0): OBJ["floor"] | 0x20, (24, 29): OBJ["gate"]},
                    {(1, 0): 7, (24, 29): 2}, {3: (1, 2, 0, 4)})
    assert lv.objid(1, 0) == OBJ["floor"]           # flag bits are masked off
    assert lv.state(1, 0) == 7
    assert lv.objid(24, 29) == OBJ["gate"]
    assert lv.neighbours(3) == {"left": 1, "right": 2, "above": 0, "below": 4}
    assert lv.kid_start == {"screen": 1, "block": 0, "face": 0xFF}
    assert lv.to_json()["screens"][0]["flags"][0] == 0x20
    with pytest.raises(LevelError):
        lv.objid(0, 0)
    with pytest.raises(LevelError):
        lv.objid(1, 30)
    assert obj_name(20) == "block" and obj_name(31) == "obj31"


def test_set_initials_converts_gadget_settings():
    lv = make_level(
        {(1, 0): OBJ["gate"], (1, 1): OBJ["gate"], (1, 2): OBJ["loose"],
         (1, 3): OBJ["flask"], (1, 4): OBJ["floor"], (24, 5): OBJ["gate"]},
        {(1, 0): 1, (1, 1): 2, (1, 2): 9, (1, 3): 3, (1, 4): 2, (24, 5): 1},
        info0=25)
    lv.set_initials()
    assert lv.state(1, 0) == GMAXVAL                # setting 1: gate up
    assert lv.state(1, 1) == 0                      # setting 2: gate down
    assert lv.state(1, 2) == 0                      # loose floor
    assert lv.state(1, 3) == 3 << 5                 # flask type into the top bits
    assert lv.state(1, 4) == 2                      # other pieces untouched
    assert lv.state(24, 5) == GMAXVAL               # INFO[0]-1 = 24 screens processed


def test_set_initials_stops_at_info0():
    lv = make_level({(7, 0): OBJ["gate"]}, {(7, 0): 1}, info0=7)
    lv.set_initials()                               # screens 6..1 only
    assert lv.state(7, 0) == 1
    bad = make_level({(1, 0): OBJ["gate"]}, {(1, 0): 9})
    with pytest.raises(LevelError, match="setting 9"):
        bad.set_initials()


def test_links_and_reachable_screens():
    lv = make_level(maps={1: (0, 2, 0, 0), 2: (1, 0, 0, 3), 3: (0, 0, 2, 0), 9: (0, 0, 0, 0)},
                    linkloc={4: 0x25, 5: 0x80 | 0x47}, linkmap={4: 0x20 | 5, 5: 0x40 | 1})
    assert lv.screens_in_use() == [1, 2, 3]
    links = lv.links(4)
    assert links[0] == {"index": 4, "block": 5, "screen": 1 | (1 << 2), "timer": 5,
                        "last": False}
    assert links[1]["block"] == 7 and links[1]["screen"] == 2 | (2 << 2) and links[1]["last"]


# --- fast blitters ------------------------------------------------------------------

IMG = bytes([0x81, 0xC0,        # bottom row
             0xFF, 0x80])       # top row; 2 bytes wide, 2 lines


def test_page_is_memory_backed():
    page = HiresPage(0x80)
    page.rows[1][0] = 0xAA
    assert page.mem[row_address(1) - 0x2000] == 0xAA
    assert page.mem[0] == 0x80 and len(page.mem) == 0x2000


def test_fastlay_modes():
    for op, expect in ((OP_STA, [0x81, 0xC0]), (OP_ORA, [0x8B, 0xCA]),
                       (OP_AND, [0x00, 0x00]), (OP_EOR, [0x8B, 0xCA])):
        page = HiresPage(0x0A)
        fastlay(page, IMG, 2, 2, xco=5, yco=10, opacity=op)
        assert list(page.rows[10][5:7]) == expect, op
        assert page.rows[10][4] == 0x0A and page.rows[10][7] == 0x0A
        assert page.rows[11][5] == 0x0A and page.rows[8][5] == 0x0A
    page = HiresPage(0x0A)
    fastlay(page, IMG, 2, 2, 5, 10, OP_STA)
    assert list(page.rows[9][5:7]) == [0xFF, 0x80]             # second row is one line up
    with pytest.raises(ValueError):
        fastlay(page, IMG, 2, 2, 5, 10, OP_MASK)
    with pytest.raises(ValueError):
        fastlay(page, IMG, 2, 2, 5, 200, OP_STA)


def test_fastlay_clips_at_the_top_only():
    page = HiresPage(0x00)
    fastlay(page, IMG, 2, 2, xco=0, yco=0, opacity=OP_STA)      # taller than yco+1
    assert list(page.rows[0][0:2]) == [0x81, 0xC0]
    assert sum(page.mem) == 0x81 + 0xC0                          # nothing else written


def test_fastlay_past_column_39_goes_where_the_memory_layout_puts_it():
    page = HiresPage(0x00)
    fastlay(page, IMG[:2], 2, 1, xco=39, yco=0, opacity=OP_STA)
    assert page.rows[0][39] == 0x81
    assert page.overflow_writes == 1
    assert page.rows[64][0] == 0xC0                              # $2028 is line 64, column 0
    page = HiresPage(0x00)
    with pytest.raises(CropNotModelled):
        fastlay(page, IMG[:2], 2, 1, xco=120, yco=191, opacity=OP_STA)


def test_fastmask_uses_the_image_as_its_own_mask():
    page = HiresPage(0xFF)
    fastmask(page, bytes([0x80 | 0b0001000, 0x80]), 2, 1, xco=3, yco=7)
    assert page.rows[7][3] == 0xE3 and page.rows[7][4] == 0xFF


def test_lay_gen_shift_and_edges():
    page = HiresPage(0x7F)
    lay_gen(page, bytes([0x80 | 0b1000001]), 1, 1, xco=2, offset=3, yco=5, opacity=OP_STA)
    assert page.rows[5][2] == 0x80 | 0b0001000 | 0b0000111       # left 3 dots kept
    # dot 6 shifted by 3 lands on dot 2 of the next byte; its dots 3-6 are kept
    assert page.rows[5][3] == 0b1111000 | 0b0000100
    page = HiresPage(0x00)
    lay_gen(page, bytes([0x80 | 0b1000001]), 1, 1, 2, 3, 5, OP_ORA)
    assert page.rows[5][2] == 0x88 and page.rows[5][3] == 0x04
    with pytest.raises(CropNotModelled):
        lay_gen(page, bytes([0x80]), 1, 1, 39, 0, 5, OP_STA)


# --- room builder on synthetic piece tables ---------------------------------------------

def synthetic_bgdata():
    """Piece tables with the original names and made-up values:
    piece image numbers are 1 + objid in decade blocks, so tests can read them."""
    def row(name, f):
        return "%s db %s\n" % (name, ",".join(str(f(i)) for i in range(30)))
    t = ""
    t += row("maska", lambda i: 3 if i else 0)
    t += row("piecea", lambda i: 40 + i if i in (1, 3, 11) else 0)
    t += row("pieceay", lambda i: 1 if i == 3 else 0)
    t += row("maskb", lambda i: 4 if i in (1, 20) else 0)
    t += row("pieceb", lambda i: 60 + i if i in (3, 4) else 0)
    t += row("pieceby", lambda i: 2 if i == 20 else 0)
    t += row("bstripe", lambda i: 71 if i == 3 else 0)
    t += row("piecec", lambda i: 80 + i if i in (1, 3) else 0)
    t += row("pieced", lambda i: 90 + i if i in (1, 3, 11) else 0)
    t += row("fronti", lambda i: 100 + i if i in (3, 20) else 0)
    t += row("fronty", lambda i: 255 if i == 3 else 0)
    t += row("frontx", lambda i: 1 if i == 3 else 0)
    t += """
gatebotSTA = 10
gatebotORA = 11
gateB1 = 12
gatecmask = 13
gate8c db 20,21,22,23,24,25,26,27
gate8b db 30,31,32,33,34,35,36,37
stairs = 14
door = 15
doormask = 16
toprepair = 17
spikea db 0,5,5,5,5,5,5,5,5,0
spikeb db 0,6,6,6,6,6,6,6,6,0
spikeExt = 5
slicerseq db 4,3,1,2,5,4,4
slicerRet = 6
slicertop db 0,7,7,7,7
slicerbot db 8,8,8,8,8
slicerbot2 db 9,9,9,8,8
slicergap db 0,38,46,53,55
slicerfrnt db 18,18,18,18,18
looseb = 19
loosea db 51,52,51,52,52,51,51,51,52,52,52
looseby db 0,1,0,255,255,0,0,0,255,255,255
loosed db 101,102,101,102,102,101,101,101,102,102,102
Ffalling = 10
specialflask = 38
swordgleam1 = 39
swordgleam0 = 40
panelb0 = 99
panelc0 = 98
numpans = 3
panelb db 99,97,96
panelc db 98,95,94
archpanel = 93
numbpans = 3
spaceb db 0,76,77,78
spaceby db 0,236,236,0
floorb db 72,73,74,74
floorby db 0,0,0,0
numblox = 2
blockb db 110,111
blockc db 112,112
blockd db 113,113
blockfr db 114,114
gmaxval = 47*4
"""
    gamebg = ("torchflame db 120,121,122,123,124,125,126,127,128,120,122,124\n"
              " db 126,125,123,121,128,127\n"
              "\nbubble db 50,51,52,53,52,51,53,52,51\n")
    misc = ("bgset1 db 0,1\nbgset2 db 0,1\nchset db 0,0\n")
    return BgData(t, gamebg, misc)


def entries(lst, **want):
    return [e for e in lst if all(getattr(e, k) == v for k, v in want.items())]


def test_extract_table_from_mixed_file():
    text = " lda #1\nfoo hex 01,02\n db 3\n\nbar db 9\n rts\n"
    assert extract_table(text, "foo") == bytes([1, 2, 3])
    assert extract_table(text, "bar") == bytes([9])
    with pytest.raises(RoomError):
        extract_table(text, "nope")


def test_empty_screen_draws_the_row_above_and_the_left_wall():
    bg = synthetic_bgdata()
    b = RoomBuilder(bg, make_level(), 0, 1)
    b.sure()
    # row above is treated as solid floor: ten D-sections on line 2, stored
    d = entries(b.back, y=2, opacity=OP_STA)
    assert [e.x for e in d] == [0, 4, 8, 12, 16, 20, 24, 28, 32, 36]
    assert all(e.image == 91 for e in d)
    # no screen to the left: a solid block's B-section in column 0 of each row
    wall = entries(b.back, image=110)
    assert [(e.x, e.y) for e in wall] == [(0, 188 + 2), (0, 125 + 2), (0, 62 + 2)]
    # no screen below: the bottom row sees floor below it, so C-sections are drawn
    c = entries(b.back, image=81, y=191)
    assert [e.x for e in c] == [4, 8, 12, 16, 20, 24, 28, 32, 36]
    # the nine B-sections of the row above fall on line 255 and are dropped
    assert b.rejected == 9
    assert b.fore == [] and b.mid == []


def test_rows_are_built_bottom_to_top_and_left_to_right():
    bg = synthetic_bgdata()
    lv = make_level({(1, 3): OBJ["floor"], (1, 13): OBJ["floor"], (1, 23): OBJ["floor"],
                     (1, 24): OBJ["floor"]})
    b = RoomBuilder(bg, lv, 0, 1)
    b.sure()
    d = [(e.x, e.y) for e in entries(b.back, image=91) if e.y != 2]
    assert d == [(12, 191), (16, 191), (12, 128), (12, 65)]


def test_floor_block_sections_and_neighbour_rules():
    bg = synthetic_bgdata()
    lv = make_level({(1, 23): OBJ["floor"], (1, 13): OBJ["floor"]}, {(1, 23): 1, (1, 13): 9})
    b = RoomBuilder(bg, lv, 0, 1)
    b.sure()
    # the floor block itself: D stored at the block bottom, A or'd 3 lines higher
    assert entries(b.back, image=91, x=12, y=191, opacity=OP_STA)
    assert entries(b.back, image=41, x=12, y=188, opacity=OP_ORA)
    # its B-section is drawn by the block to its right, chosen by its state
    assert entries(b.back, image=73, x=16, y=188, opacity=OP_ORA)
    # an out-of-range state falls back to entry 0 for floor
    assert entries(b.back, image=72, x=16, y=125)
    # its C-section is drawn by the empty block above and to the right
    assert entries(b.back, image=81, x=16, y=128, opacity=OP_ORA)
    # ... together with the B-mask of the piece left of that block (floor)
    assert entries(b.back, image=4, x=16, y=128, opacity=OP_AND)


def test_posts_foreground_depends_on_the_background_set():
    bg = synthetic_bgdata()
    lv = make_level({(1, 25): OBJ["posts"]})
    dungeon = RoomBuilder(bg, lv, 0, 1)
    dungeon.sure()
    assert [(e.x, e.y, e.opacity) for e in dungeon.fore] == [(21, 187, OP_STA)]
    assert not entries(dungeon.back, image=71)
    palace = RoomBuilder(bg, lv, 1, 1)
    palace.sure()
    assert [e.opacity for e in palace.fore] == [OP_MASK, OP_ORA]       # mask, then or
    stripe = entries(palace.back, image=71)                            # palace-only stripe
    assert [(e.x, e.y) for e in stripe] == [(24, 188 - 32)]


def test_block_hides_b_and_uses_plain_foreground():
    bg = synthetic_bgdata()
    lv = make_level({(1, 24): OBJ["floor"], (1, 25): OBJ["block"]}, {(1, 25): 1})
    b = RoomBuilder(bg, lv, 0, 1)
    b.sure()
    assert not entries(b.back, x=20, y=188, image=72)      # floor's B hidden by the block
    assert entries(b.back, image=113, x=20, y=191, opacity=OP_STA)
    assert [(e.image, e.opacity) for e in b.fore] == [(114, OP_STA)]
    assert entries(b.back, image=111, x=24, y=190)         # block's own B, by state 1


def test_gate_states_choose_the_bar_pieces():
    bg = synthetic_bgdata()
    for setting, bottom in ((2, 10), (1, 10)):
        lv = make_level({(1, 24): OBJ["gate"]}, {(1, 24): setting})
        lv.set_initials()
        b = RoomBuilder(bg, lv, 0, 1)
        b.sure()
        gate = [e for e in b.back if e.source in ("drawgateb", "restorebot")]
        assert gate and all(e.x == 20 for e in gate)
        if setting == 2:               # closed: bottom piece crosses the floor line
            assert gate[0].source == "restorebot"
            assert entries(gate, image=11, y=188 - 1 - 2, opacity=OP_ORA)
            bars = entries(gate, image=12)                 # five full 8-line sections
            assert [e.y for e in bars] == [175, 167, 159, 151, 143]
            # the rest of the 62-line opening takes the 7-line top piece
            assert entries(gate, image=36, y=135, opacity=OP_STA)
        else:                          # open: bottom piece 48 lines up, stored
            assert entries(gate, image=bottom, y=188 - 48, opacity=OP_STA)
            assert len(entries(gate, image=12)) == 0


def test_pressplate_and_torch_and_flask():
    bg = synthetic_bgdata()
    lv = make_level({(1, 20): OBJ["torch"], (1, 22): OBJ["torch"], (1, 25): OBJ["flask"],
                     (1, 27): OBJ["upressplate"]},
                    {(1, 20): 0, (1, 22): 3, (1, 25): 0x40 | 2, (1, 27): 6},
                    linkmap={6: 2})
    b = RoomBuilder(bg, lv, 0, 1)
    b.sure()
    flames = [e for e in b.back if e.source == "drawtorchb"]
    # torch in column 0: flame drawn by column 1; frame chosen by the torch's state
    assert [(e.x, e.y, e.image) for e in flames] == [(5, 188 - 43, 120), (13, 188 - 43, 123)]
    assert len(b.mid) == 1                                   # the flask's bubbles
    m = b.mid[0]
    assert (m.x, m.y, m.offset, m.image) == (22, 188 - 4 - 14, 2, 52)
    assert entries(b.fore, image=38)                         # tall flask: special front
    # a depressed "up" pressure plate is drawn as plain floor with state 0
    b.calcblue(1)
    assert b.getobjid1(27) == OBJ["floor"] and b.state == 0


def test_null_blueprint_read_is_an_error_not_a_guess():
    b = RoomBuilder(synthetic_bgdata(), make_level(), 0, 1)
    with pytest.raises(RoomError):
        b.getobjid1(0)


# --- integration tests against the original checkout (optional) -------------------

SOURCE = os.environ.get("POP_SOURCE_DIR")
needs_source = pytest.mark.skipif(
    not (SOURCE and os.path.isdir(os.path.join(SOURCE, "01 POP Source", "Levels"))),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")


@needs_source
def test_original_levels_parse():
    from pop_level import load_level
    for n in range(15):
        lv = load_level(os.path.join(SOURCE, "01 POP Source", "Levels", "LEVEL%d" % n))
        assert 1 <= lv.kid_start["screen"] <= 24 and lv.kid_start["block"] < 30
        assert lv.kid_start["face"] in (0x00, 0xFF)
        processed = range(1, lv.info[0])            # screens SETINITIALS touches
        assert set(lv.screens_in_use()) <= set(processed)
        for s in range(1, 25):
            for blk in range(30):
                o = lv.objid(s, blk)
                assert o < 30
                if o == OBJ["gate"] and s in processed:
                    assert lv.state(s, blk) in (1, 2)
                if o in (OBJ["pressplate"], OBJ["upressplate"]):
                    assert lv.linkmap[lv.state(s, blk)] & 0x1F == 0   # all start "up"
        lv.set_initials()
    l1 = load_level(os.path.join(SOURCE, "01 POP Source", "Levels", "LEVEL1"))
    assert l1.kid_start == {"screen": 1, "block": 0, "face": 0xFF}
    assert l1.neighbours(1) == {"left": 5, "right": 0, "above": 0, "below": 2}


@needs_source
def test_original_piece_tables_have_one_entry_per_piece():
    import render_room
    bg = render_room.load_bgdata(SOURCE)
    starts = sorted(line.address for line in bg._asm.label_lines)
    for name in ("maska", "piecea", "pieceay", "maskb", "pieceb", "pieceby", "bstripe",
                 "piecec", "pieced", "fronti", "fronty", "frontx"):
        nxt = min(a for a in starts if a > bg.sym[name])
        assert nxt - bg.sym[name] == 30, name
    assert list(bg.level_bgset1) == [0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 1, 1, 2, 2, 1]
    assert len(bg.torchflame) == 18 and len(bg.bubble) == 9


@needs_source
def test_every_original_screen_builds_and_draws():
    import render_room
    from pop_level import load_level
    bg = render_room.load_bgdata(SOURCE)
    screens = 0
    biggest = {"back": 0, "fore": 0, "mid": 0}
    for n in range(15):
        lv = load_level(os.path.join(SOURCE, "01 POP Source", "Levels", "LEVEL%d" % n))
        lv.set_initials()
        tables = render_room.load_tables(SOURCE, bg, n)
        for s in lv.screens_in_use():
            builder, page = render_room.build_screen(bg, lv, n, s, tables)
            screens += 1
            assert page.overflow_writes == 0            # nothing drawn past column 39
            assert builder.stale_reads == []
            for k in biggest:
                biggest[k] = max(biggest[k], len(getattr(builder, k)))
            if (n, s) == (1, 1):
                assert (len(builder.back), len(builder.mid), len(builder.fore),
                        builder.rejected) == (74, 0, 16, 9)
                # regression lock on the rendered page, not an external reference
                assert hashlib.sha256(bytes(page.mem)).hexdigest().startswith(
                    "503226497a1b8f21")
    assert screens == 299
    assert biggest == {"back": 164, "fore": 30, "mid": 2}   # limits are 199, 99, 45
