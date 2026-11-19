"""The oracle (original code in the 6502 simulator) against the Python models.

These tests need POP_SOURCE_DIR.  They re-check earlier milestones against
the original routines themselves: the sequence interpreter, the position
arithmetic, the level parser and the geometry tables.
"""

import os
import random
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
for sub in (("tools", "extract_assets"), ("tools", "oracle"), ("modern-port", "tests")):
    sys.path.insert(0, os.path.join(HERE, "..", *sub))

SOURCE = os.environ.get("POP_SOURCE_DIR")
pytestmark = pytest.mark.skipif(
    not (SOURCE and os.path.isdir(os.path.join(SOURCE, "01 POP Source", "Source"))),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")


@pytest.fixture(scope="module")
def oracle():
    from pop_oracle import Oracle
    o = Oracle(SOURCE)
    with open(os.path.join(SOURCE, "01 POP Source", "Levels", "LEVEL1"), "rb") as f:
        o.level1 = f.read()
    o.load_level(o.level1)
    return o


def test_modules_load_without_overlap(oracle):
    assert len(oracle.modules) == 17
    assert oracle.modules["GRAFIX"].origin == 0x0400
    assert oracle.modules["GRAFIX"].end <= 0x0E00          # even without its IIGS tail
    assert oracle.modules["SPECIALK"].end <= 0xDFD8        # version string follows
    assert oracle.modules["GRAFIX"].warnings == ["do without fin at end of file (ignored)"]
    assert oracle.addr("CharX") == 0x41 and oracle.addr("soundtable") == 0xB6C0


def test_rdblock_agrees_with_the_level_parser(oracle):
    from pop_level import parse_level
    lv = parse_level(oracle.level1)
    for s in range(1, 25):
        for b in range(30):
            a, x, y, _ = oracle.call("rdblock", a=s, x=b % 10, y=b // 10)
            assert (a, x, y) == (lv.objid(s, b), lv.objid(s, b), b)
    # one column off the left edge of screen 1 is the last column of its left neighbour
    left = lv.neighbours(1)["left"]
    a, _, y, _ = oracle.call("rdblock", a=1, x=0xFF, y=0)
    assert (a, y) == (lv.objid(left, 9), 9)
    assert oracle.peek("tempscrn") == left and oracle.peek("tempblockx") == 9
    assert oracle.call("rdblock", a=0, x=3, y=1)[:2] == (20, 20)   # no screen: solid block


def test_facedx_does_the_opposite_of_its_comment(oracle):
    # CTRLSUBS.S 633-640: the comment says "DX if facing right"; the code
    # returns DX unchanged when facing LEFT and negates it when facing right.
    oracle.poke("CharFace", 0xFF)
    assert oracle.call("FACEDX", a=5, module="CTRLSUBS")[0] == 5
    oracle.poke("CharFace", 0x00)
    assert oracle.call("FACEDX", a=5, module="CTRLSUBS")[0] == 0xFB


def test_position_arithmetic_matches_the_original(oracle):
    from pop_charpos import add_char_x, base_x, setup_char
    rng = random.Random(3)
    for _ in range(400):
        cx, cy, face = rng.randrange(256), rng.randrange(256), rng.choice((0x00, 0xFF))
        fdx, fdy, fcheck, a = (rng.randrange(256) for _ in range(4))
        oracle.set_vars({"CharX": cx, "CharY": cy, "CharFace": face,
                         "Fdx": fdx, "Fdy": fdy, "Fcheck": fcheck})
        assert oracle.call("ADDCHARX", a=a, module="CTRLSUBS")[0] == add_char_x(cx, a, face)
        assert oracle.call("GETBASEX", module="CTRLSUBS")[0] == base_x(cx, face, fdx, fcheck)


def test_setupchar_matches_the_original(oracle):
    from pop_charpos import setup_char
    rng = random.Random(4)
    fx = oracle.addr("FCharX")
    for _ in range(400):
        cx, cy, face = rng.randrange(256), rng.randrange(256), rng.choice((0x00, 0xFF))
        posn = rng.randint(1, 240)
        oracle.set_vars({"CharX": cx, "CharY": cy, "CharFace": face, "CharPosn": posn,
                         "CharID": 0, "CharSword": 0})
        oracle.call("SETUPCHAR", module="CTRLSUBS")
        fdx, fdy, fcheck = oracle.peek("Fdx"), oracle.peek("Fdy"), oracle.peek("Fcheck")
        want_x, want_y = setup_char(cx, cy, face, fdx, fdy, fcheck)
        got_x = oracle.peek(fx) | (oracle.peek(fx + 1) << 8)
        assert got_x == want_x & 0xFFFF, (cx, face, posn)
        assert oracle.peek("FCharY") == want_y
        assert oracle.peek("FCharFace") == face


def test_frame_info_matches_the_decoded_tables(oracle):
    from merlin_data import assemble
    from pop_animdata import decode_frame_tables
    with open(os.path.join(SOURCE, "01 POP Source", "Source", "FRAMEDEF.S"),
              encoding="ascii") as f:
        frames = decode_frame_tables(assemble(f.read()))
    main = {f.number: f for f in frames.main}
    alt1 = {f.number: f for f in frames.altset1}
    alt2 = {f.number: f for f in frames.altset2}

    def info(posn, char_id):
        oracle.set_vars({"CharPosn": posn, "CharID": char_id})
        oracle.call("GETFRAMEINFO", module="CTRLSUBS")
        return bytes(oracle.peek(n) for n in ("Fimage", "Fsword", "Fdx", "Fdy", "Fcheck"))

    for posn in range(1, 241):
        assert info(posn, 0) == main[posn].raw                 # kid
        assert info(posn, 24) == main[posn].raw                # mouse
        shadow = alt1[posn] if 150 <= posn <= 189 else main[posn]
        assert info(posn, 1) == shadow.raw
        if 102 <= posn <= 106:                                 # guard falling frames
            guard = alt1[posn + 70]
        elif 150 <= posn <= 189:
            guard = alt1[posn]
        else:
            guard = main[posn]
        assert info(posn, 2) == guard.raw and info(posn, 4) == guard.raw
    for posn in range(1, 91):
        assert info(posn, 5) == alt2[posn].raw and info(posn, 6) == alt2[posn].raw


def test_sequence_interpreter_matches_the_original(oracle):
    """Every entry point, 80 ticks, both facings: the original ANIMCHAR
    against the state machine the C port is also compared with."""
    from compare_seq_traces import expected
    from merlin_data import assemble
    from pop_animdata import decode_sequence_table
    with open(os.path.join(SOURCE, "01 POP Source", "Source", "SEQTABLE.S"),
              encoding="ascii") as f:
        table = decode_sequence_table(assemble(f.read()))
    seq = oracle.addr("CharSeq")
    queue = oracle.addr("soundtable")
    ticks = 0
    for face in (0xFF, 0x00):
        for n, addr in enumerate(table.entries, 1):
            oracle.reset()
            oracle.load_level(oracle.level1)
            oracle.set_vars({"CharX": 128, "CharY": 100, "CharFace": face, "CharBlockY": 1,
                             "CharBlockX": 4, "CharScrn": 1, "CharLife": 0xFF, "level": 1,
                             "NextLevel": 1, "weightless": 0})
            oracle.call("jumpseq", a=n)
            assert oracle.peek(seq) | (oracle.peek(seq + 1) << 8) == addr
            for want in expected(table, addr, 80, face):
                oracle.set_vars({"jarabove": 0, "alertguard": 0})
                oracle.poke(queue, 0)
                oracle.call("animchar")
                frame, x, y, f, block_y, action, xv, yv, nxt, jar, alert, events = want
                got = (oracle.peek("CharPosn"), oracle.peek("CharX"), oracle.peek("CharY"),
                       oracle.peek("CharFace"), oracle.peek("CharBlockY"),
                       oracle.peek("CharAction"), oracle.peek("CharXVel"),
                       oracle.peek("CharYVel"),
                       oracle.peek(seq) | (oracle.peek(seq + 1) << 8),
                       oracle.peek("jarabove"), oracle.peek("alertguard"))
                assert got == (frame, x, y, f, block_y, action, xv, yv, nxt, jar, alert), n
                sounds = [oracle.peek(queue + 1 + i) for i in range(oracle.peek(queue))]
                want_sounds = [int(t) for t in events.replace("-", " ").replace("c", " ")
                               .replace("u", " ").replace("p", " ").replace("s", " ").split()]
                assert sounds == want_sounds, n
                ticks += 1
    assert ticks == 114 * 80 * 2


def test_geometry_tables_in_memory(oracle):
    from pop_charpos import BLOCK_WIDTH, FLOOR_Y, cvtx
    bt, ot = oracle.addr("ByteTable"), oracle.addr("OffsetTable")
    assert all((oracle.peek(bt + x), oracle.peek(ot + x)) == cvtx(x) for x in range(256))
    edge = oracle.addr("BlockEdge")
    assert oracle.peek(edge + 5) == 58
    assert all((oracle.peek(edge + i + 1) - oracle.peek(edge + i)) & 0xFF == BLOCK_WIDTH
               for i in range(19))
    assert tuple(oracle.peek(oracle.addr("FloorY") + 1 + i) for i in range(3)) == FLOOR_Y
