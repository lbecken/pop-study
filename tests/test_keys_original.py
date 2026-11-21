"""Facts about the original's key handling that the port and its notes rely
on (docs/port/keys.md), read off the assembled original.

All of these need a checkout of the original (POP_SOURCE_DIR); without one
they are skipped.
"""

import os
import re
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "oracle"))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_audio"))

SOURCE = os.environ.get("POP_SOURCE_DIR")
CODE = os.path.join("01 POP Source", "Source")
pytestmark = pytest.mark.skipif(
    not SOURCE or not os.path.isdir(os.path.join(SOURCE or "", CODE)),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")


@pytest.fixture(scope="module")
def oracle():
    from pop_oracle import Oracle
    return Oracle(SOURCE)


def routine_bytes(oracle, module, label, length):
    m = oracle.modules[module]
    at = m.symbols[label] - m.origin
    return bytes(m.image[at:at + length])


def test_the_beep(oracle):
    """GTONE: 50 speaker accesses 157 cycles apart, and the port's numbers."""
    from pop_sfx import tone_half_period
    accesses = []
    oracle.cpu.on_read[0xC030] = lambda c: accesses.append(c.cycles)
    start = oracle.cpu.cycles
    try:
        oracle.call("GTONE", module="GRAFIX")
    finally:
        del oracle.cpu.on_read[0xC030]
    total = oracle.cpu.cycles - start
    gaps = {b - a for a, b in zip(accesses, accesses[1:])}
    assert len(accesses) == 50
    assert gaps == {157} == {tone_half_period(15)}
    lead, tail = accesses[0] - start, start + total - accesses[-1]
    assert (lead, tail, total) == (21, 158, 7872)
    with open(os.path.join(HERE, "..", "modern-port", "src", "app", "pop_app.c")) as f:
        text = f.read()
    numbers = {name: int(value) for name, value in
               re.findall(r"(GTONE_[A-Z]+) = (\d+)", text)}
    assert numbers == {"GTONE_PITCH": 15, "GTONE_DURATION": 50, "GTONE_LEAD": lead,
                       "GTONE_TAIL": tail}


def test_development_mode_is_off_at_startup(oracle):
    """INITSYSTEM: `lda #FinalDisk!1; sta develment` with TOPCTRL's FinalDisk = 1."""
    develment = oracle.addr("develment")
    code = routine_bytes(oracle, "TOPCTRL", "INITSYSTEM", 24)
    assert bytes((0xA9, 0x00, 0x8D, develment & 0xFF, develment >> 8)) in code


def test_build_switches_disagree():
    values = {}
    for name in ("SPECIALK.S", "TOPCTRL.S", "MASTER.S"):
        with open(os.path.join(SOURCE, CODE, name), encoding="ascii", errors="replace") as f:
            values[name] = int(re.search(r"^FinalDisk = (\d)", f.read(), re.M).group(1))
    assert values == {"SPECIALK.S": 0, "TOPCTRL.S": 1, "MASTER.S": 1}


def test_code_words_are_assembled(oracle):
    """Stored backwards, high bits set, each ended by a zero."""
    def stored(word):
        return bytes(ord(c) | 0x80 for c in reversed(word)) + b"\x00"
    for label, word in (("C_skip", "SKIP"), ("C_devel", "POP"), ("C_zap2", "ZAP"),
                        ("C_boost", "BOOST"), ("C_restore", "R"), ("C_zap1", "Z"),
                        ("C_tina", "TINA")):
        assert routine_bytes(oracle, "SPECIALK", label, len(word) + 1) == stored(word)


def test_switched_buttons_are_not_switched(oracle):
    """BREAD: on either branch $C061 is loaded with LDA and $C062 with LDX,
    and A goes to BTN0, X to BTN1."""
    code = routine_bytes(oracle, "GRAFIX", "BREAD", 26)
    jbtns, btn0, btn1 = (oracle.addr(n) for n in ("jbtns", "BTN0", "BTN1"))
    assert code[:3] == bytes((0xAD, jbtns & 0xFF, jbtns >> 8))          # lda jbtns
    assert code[3] == 0xD0                                               # bne :1
    straight, switched = code[5:11], code[5 + code[4]:5 + code[4] + 6]
    lda, ldx = bytes((0xAD, 0x61, 0xC0)), bytes((0xAE, 0x62, 0xC0))
    assert straight == lda + ldx
    assert switched == ldx + lda
    assert btn0 < 0x100 and btn1 < 0x100
    assert code[11:15] == bytes((0x85, btn0, 0x86, btn1))                # sta BTN0; stx BTN1


def test_one_saved_field_is_never_used():
    uses = []
    for root, _, files in os.walk(SOURCE):
        for name in files:
            if name.upper().endswith(".S"):
                with open(os.path.join(root, name), encoding="ascii", errors="replace") as f:
                    uses += [line.strip() for line in f if "SavMaxed" in line]
    assert uses and all(line == "SavMaxed ds 1" for line in uses)
