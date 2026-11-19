"""Tests for merlin_data.py and pop_animdata.py.

Unit tests use small synthetic Merlin sources written for these tests.
Integration tests need POP_SOURCE_DIR (a checkout of the original
repository) and are skipped without it.
"""

import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))

from merlin_data import MerlinError, assemble, parse_equates, split_line  # noqa: E402
from pop_animdata import (AnimDataError, INSTRUCTIONS, decode_frame,  # noqa: E402
                          decode_frame_tables, decode_sequence_table, s8, trace)

# --- merlin_data ----------------------------------------------------------------


def test_split_line_fields():
    assert split_line("* comment") == ("", "", "", "* comment")
    assert split_line("") == ("", "", "", "")
    assert split_line("lab db 1,2 ;note here") == ("lab", "db", "1,2", "note here")
    assert split_line(" db chx,5") == ("", "db", "chx,5", "")
    assert split_line("alone") == ("alone", "", "", "")
    assert split_line(";col0 comment")[1] == ""
    assert split_line(" lst off") == ("", "lst", "off", "")
    assert split_line(":7 db $07,0,0,0,$40+10 ;run-10") == (
        ":7", "db", "$07,0,0,0,$40+10", "run-10")


def test_bytes_words_and_expressions():
    asm = assemble("""
org = $3000
neg = -5
 org org
start db 1,$10,%101,neg,$c0+$20+6,10-3,-4+1
 dw start,$1234
 hex 0aFF,10
""")
    assert asm.origin == 0x3000
    assert asm.image == bytes([1, 0x10, 5, 0xFB, 0xE6, 7, 0xFD,
                               0x00, 0x30, 0x34, 0x12, 0x0A, 0xFF, 0x10])
    assert asm.symbols["start"] == 0x3000


def test_expressions_are_left_to_right_without_precedence():
    # Merlin evaluates 2+3*4 as (2+3)*4
    asm = assemble(" org $1000\n db 2+3*4\n")
    assert asm.image == bytes([20])


def test_forward_references_and_local_labels():
    asm = assemble("""
 org $2000
first db 1
:loop db 2
 dw :loop,second
second db 3
:loop db 4
 dw :loop
""")
    assert asm.locals[("first", ":loop")] == 0x2001
    assert asm.locals[("second", ":loop")] == 0x2007
    assert asm.image == bytes([1, 2, 0x01, 0x20, 0x06, 0x20, 3, 4, 0x07, 0x20])
    assert asm.labels_at(0x2006) == ["second"]
    assert asm.labels_at(0x2007) == ["second:loop"]


def test_dummy_section_ds_and_pc():
    asm = assemble("""
bof = $2800
 dum bof
org ds 10
second ds 4
 dend
 org org
A db 1,2,3
 ds second-*
B db 9
 ds 1
""")
    assert asm.symbols["org"] == 0x2800
    assert asm.symbols["second"] == 0x280A
    assert asm.symbols["B"] == 0x280A
    assert asm.image == bytes([1, 2, 3] + [0] * 7 + [9, 0])


def test_conditional_assembly_and_ignored_ops():
    asm = assemble("""
 lst off
 tr on
 org $1000
 db 1
 do 0
 db 2
 else
 db 3
 fin
 do 1
 db 4
 fin
 usr $a9,15,$800,*-$1000
""")
    assert asm.image == bytes([1, 3, 4])


@pytest.mark.parametrize("source, message", [
    (" org $1000\n lda #1\n", "unsupported opcode"),
    (" org $1000\n db nowhere\n", "undefined symbol"),
    (" org $1000\n db 300\n", "does not fit a byte"),
    (" org $1000\na db 1\na db 2\n", "duplicate label"),
    (" db 1\n", "before any org"),
    (" org $1000\n db 1,,2\n", "bad operand list"),
])
def test_assembler_rejects_what_it_does_not_understand(source, message):
    with pytest.raises(MerlinError, match=message):
        assemble(source)


def test_open_do_at_end_of_file_is_a_warning():
    asm = assemble(" org $1000\n do 1\n db 1\n")
    assert asm.image == bytes([1])
    assert asm.warnings == ["do without fin at end of file (ignored)"]


def test_errors_carry_line_numbers():
    with pytest.raises(MerlinError, match="line 3"):
        assemble(" org $1000\n db 1\n bogus 2\n")


def test_parse_equates_skips_non_equates():
    eq = parse_equates(" tr on\nstartrun = 1\nstand = 2\ngoto = -1\nx = stand+1\n db 5\n")
    assert eq == {"startrun": 1, "stand": 2, "goto": -1, "x": 3}


# --- frame definitions --------------------------------------------------------------


def test_s8():
    assert [s8(b) for b in (0, 1, 127, 128, 255)] == [0, 1, 127, -128, -1]


def test_decode_frame_fields():
    f = decode_frame(5, bytes([0x05, 0x00, 0x00, 0x00, 0xC0 + 0x20 + 6]), "x")
    assert (f.image, f.table, f.sword, f.dx, f.dy) == (5, 0, 0, 0, 0)
    assert (f.odd, f.floor_check, f.thin, f.foot) == (True, True, True, 6)
    assert not f.empty and f.name == "x"


@pytest.mark.parametrize("fimage, fsword, table", [
    (0x01, 0x00, 0), (0x01, 0x40, 1), (0x01, 0x80, 2), (0x01, 0xC0, 3),
    (0x81, 0x00, 4), (0x81, 0x40, 5), (0x81, 0x80, 6), (0x81, 0xC0, 7),
])
def test_table_number_combines_fimage_bit7_and_fsword_bits67(fimage, fsword, table):
    # decodeim, CTRLSUBS.S 1017-1039
    f = decode_frame(1, bytes([fimage, fsword | 0x09, 0, 0, 0]))
    assert f.table == table and f.image == 1 and f.sword == 9


def test_signed_offsets_and_empty_frame():
    f = decode_frame(1, bytes([0x11, 0x40, 0xFE, 0xFD, 0x41]))
    assert (f.dx, f.dy) == (-2, -3)
    assert decode_frame(66, bytes(5)).empty
    with pytest.raises(AnimDataError):
        decode_frame(1, bytes(4))


FRAMEDEF_SYNTH = """
bof = $2800
 dum bof
org ds 10
altset1 ds 10
altset2 ds 5
swordtab ds 6
 dend
 org org
Fdef
:1 db $01,0,1,0,$c0+4 ;one
:2 db $02,$40+3,-1,2,$40 ;two
 ds altset1-*
ALTSET1
:150 db $0b,$c0+13,2,1,0 ;alt
 ds altset2-*
ALTSET2
:1 db $8a,$40,0,0,0 ;p
 ds swordtab-*
SWORDTAB
:1 db $1d,0,-9
:2 db $22,-9,-29 ;second
 ds 1
"""


def test_decode_frame_tables_layout():
    fd = decode_frame_tables(assemble(FRAMEDEF_SYNTH))
    assert [f.number for f in fd.main] == [1, 2]
    assert fd.main[0].name == "one" and fd.main[0].foot == 4
    assert (fd.main[1].table, fd.main[1].sword, fd.main[1].dx) == (1, 3, -1)
    assert [f.number for f in fd.altset1] == [150, 151]
    assert fd.altset1[0].table == 3 and fd.altset1[1].empty
    assert fd.altset2[0].table == 5 and fd.altset2[0].image == 10
    assert [(s.number, s.image, s.dx, s.dy) for s in fd.swords] == [
        (1, 0x1D, 0, -9), (2, 0x22, -9, -29)]
    assert fd.swords[1].name == "second"


# --- sequence table -----------------------------------------------------------------

SEQ_SYNTH = """
org = $3000
goto = -1
aboutface = -2
chx = -5
chy = -6
act = -7
setfall = -8
ifwtless = -9
tap = -14
 org org
:1 dw walk
:2 dw idle
:3 dw fall
idle
 db act,0
 db 15
 db goto
 dw idle
walk
 db act,1
 db 1,chx,3
cyc db 2,chx,-1
 db tap,1,3,chx,2,chy,-4
 db goto
 dw cyc
fall
 db setfall,-1,2
 db ifwtless
 dw idle
 db aboutface,7
 db goto
 dw idle
 lst
 ds 1
"""


def test_instruction_set_matches_codes():
    assert {k: v[0] for k, v in INSTRUCTIONS.items()} == {
        "goto": -1, "aboutface": -2, "up": -3, "down": -4, "chx": -5, "chy": -6,
        "act": -7, "setfall": -8, "ifwtless": -9, "die": -10, "jaru": -11,
        "jard": -12, "effect": -13, "tap": -14, "nextlevel": -15}


def test_sequence_table_decode_is_consistent():
    asm = assemble(SEQ_SYNTH)
    t = decode_sequence_table(asm)
    assert t.problems == []
    assert len(t.entries) == 3
    assert t.entries == [asm.symbols["walk"], asm.symbols["idle"], asm.symbols["fall"]]
    assert t.end == asm.end - 1                  # trailing `ds 1` excluded
    assert t.labels[asm.symbols["cyc"]] == ["cyc"]
    ops = [(i.op, i.args) for i in t.items]
    assert ops[:4] == [("act", (0,)), ("frame", (15,)), ("goto", (asm.symbols["idle"],)),
                       ("act", (1,))]
    assert ("setfall", (-1, 2)) in ops and ("chx", (-1,)) in ops


def test_trace_follows_animchar_tick_semantics():
    asm = assemble(SEQ_SYNTH)
    t = decode_sequence_table(asm)
    tr = trace(t, t.entries[0])                  # walk
    # tick 1: act 1, frame 1.  Operands written after a frame number run on
    # the next tick: chx 3 belongs to the tick that shows frame 2.
    assert [k.frame for k in tr.ticks] == [1, 2, 3, 2]
    assert [k.dx for k in tr.ticks] == [0, 3, -1, 2]
    assert [k.dy for k in tr.ticks] == [0, 0, 0, -4]
    assert tr.loop_to == 2                       # loop is ticks [3, 2]
    assert sum(k.dx for k in tr.ticks[tr.loop_to:]) == 1
    assert [i.op for i in tr.ticks[2].ops] == ["chx", "tap"]
    assert not tr.conditional


def test_trace_takes_not_weightless_path_and_flags_it():
    asm = assemble(SEQ_SYNTH)
    t = decode_sequence_table(asm)
    tr = trace(t, t.entries[2])                  # fall
    assert tr.conditional
    assert [k.frame for k in tr.ticks] == [7, 15, 15]
    assert [i.op for i in tr.ticks[0].ops] == ["setfall", "ifwtless", "aboutface"]


def test_misaligned_goto_is_reported():
    # cyc is `2,chx,-1`: cyc+2 is the operand byte of chx, not an instruction
    bad = SEQ_SYNTH.replace(" dw cyc\n", " dw cyc+2\n")
    t = decode_sequence_table(assemble(bad))
    assert any("not an instruction boundary" in p for p in t.problems)


def test_trace_rejects_frameless_loop():
    src = """
goto = -1
 org $3000
:1 dw spin
spin db goto
 dw spin
"""
    t = decode_sequence_table(assemble(src))
    with pytest.raises(AnimDataError, match="no frame within"):
        trace(t, t.entries[0])


# --- integration tests against the original checkout (optional) -------------------

SOURCE = os.environ.get("POP_SOURCE_DIR")
SRC = os.path.join(SOURCE, "01 POP Source", "Source") if SOURCE else None
needs_source = pytest.mark.skipif(
    not (SRC and os.path.isdir(SRC)),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")


def _asm(name):
    with open(os.path.join(SRC, name), encoding="ascii") as f:
        return assemble(f.read())


@needs_source
def test_original_framedef_layout():
    asm = _asm("FRAMEDEF.S")
    # FRAMEDEF.S 8-15: 1200 + 200 + 450 + 192 bytes from $2800; the sequence
    # table starts at $3000 (GAMEEQ.S 22), so the frame data must end below it.
    assert asm.origin == 0x2800 and asm.end <= 0x3000
    fd = decode_frame_tables(asm)
    assert (len(fd.main), len(fd.altset1), len(fd.altset2)) == (240, 40, 90)
    stand = fd.main[14]
    assert (stand.number, stand.image, stand.table, stand.sword) == (15, 15, 0, 9)
    assert (stand.dx, stand.dy, stand.floor_check, stand.foot) == (0, 0, True, 3)
    run = fd.main[:14]
    assert [f.image for f in run] == list(range(1, 15))
    assert all(f.table == 0 and f.sword == 0 and f.floor_check for f in run)
    assert all(f.table == 3 for f in fd.altset1 if not f.empty)


@needs_source
def test_original_sequence_table_structure():
    asm = _asm("SEQTABLE.S")
    assert asm.origin == 0x3000 and asm.end <= 0x3A00      # ctrl org, GAMEEQ.S 23
    t = decode_sequence_table(asm)
    assert t.problems == []
    assert len(t.entries) == 114
    for addr in t.entries:
        trace(t, addr)                                      # every entry terminates in a loop


@needs_source
def test_original_run_cycle():
    asm = _asm("SEQTABLE.S")
    t = decode_sequence_table(asm)
    with open(os.path.join(SRC, "SEQDATA.S"), encoding="ascii") as f:
        names = parse_equates(f.read())
    tr = trace(t, t.entries[names["startrun"] - 1])
    frames = [k.frame for k in tr.ticks]
    assert frames == [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 7]
    assert [k.dx for k in tr.ticks] == [0, 0, 0, 0, 8, 3, 3, 5, 1, 2, 4, 5, 2, 3, 4]
    loop = tr.ticks[tr.loop_to:]
    assert len(loop) == 8 and sum(k.dx for k in loop) == 26
    assert sum(k.dx for k in tr.ticks[:tr.loop_to]) == 14
    taps = [k.frame for k in tr.ticks if any(i.op == "tap" for i in k.ops)]
    assert taps == [9, 13]
    # `running` joins the same cycle without the start-up
    tr2 = trace(t, t.entries[names["running"] - 1])
    assert [k.frame for k in tr2.ticks[tr2.loop_to:]] == [k.frame for k in loop]


@needs_source
def test_original_frames_shown_are_defined_and_images_exist():
    from extract_animdata import run
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        text, errors, findings = run(SOURCE, tmp)
        assert errors == []
        assert not [f for f in findings if "not present" in f or "empty in every" in f]
        assert sorted(os.listdir(tmp)) == ["animdata-report.txt", "frames.json",
                                           "sequences.json"]
