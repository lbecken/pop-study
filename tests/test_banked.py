"""Tests for the two-bank 6502 (tools/oracle/pop_banked.py).

Small programs written for these tests; no original code or data.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "oracle"))

from mos6502 import assemble_6502  # noqa: E402
from pop_banked import AUX, MAIN, BankedCpu  # noqa: E402


def machine(source, bank=MAIN):
    """A machine with `source` assembled into the given bank."""
    asm = assemble_6502(source)
    cpu = BankedCpu()
    cpu.load_bank(asm.origin, asm.image, bank)
    return cpu, asm


def test_zero_page_stack_and_language_card_follow_altzp():
    # code in the $0300 area (not switched by ALTZP), main memory
    cpu, asm = machine("""
 org $0300
start lda #$11
 sta $10 ;main zero page
 sta $d000 ;main language card
 sta $c009 ;ALTZP on
 lda #$22
 sta $10 ;auxiliary zero page
 sta $d000
 sta $c008 ;ALTZP off
 rts
""")
    cpu.call(asm.symbols["start"])
    assert cpu.peek(0x10, MAIN) == 0x11 and cpu.peek(0x10, AUX) == 0x22
    assert cpu.peek(0xD000, MAIN) == 0x11 and cpu.peek(0xD000, AUX) == 0x22
    assert cpu.altzp == MAIN and cpu.mem[0x10] == 0x11


def test_a_call_across_language_cards_returns():
    # The pattern of GRAFIX.S 1523-1526: code in low auxiliary memory calls
    # a routine in the main language card; the return address lives on the
    # stack of whichever bank is selected when it is pushed and pulled.
    cpu, asm = machine("""
 org $0400
start sta $c008 ;main zero page, stack, language card
 jsr $ee00
 sta $c009
 lda $20 ;auxiliary zero page again
 rts
""", bank=AUX)
    cpu.load_bank(0xEE00, assemble_6502(" org $ee00\n lda #$5a\n sta $20\n rts\n").image, MAIN)
    cpu.poke(0x20, AUX, 0x33)
    cpu.set_ramrd(AUX)                               # the game runs with all three "on"
    cpu.ramwrt = AUX
    cpu.set_altzp(AUX)
    cpu.call(asm.symbols["start"])
    assert cpu.a == 0x33                             # read from the auxiliary zero page
    assert cpu.peek(0x20, MAIN) == 0x5A              # written by the main-memory routine
    assert cpu.altzp == AUX


def test_reading_one_bank_while_writing_the_other():
    # the pattern of HIRES.S copyscrnMA: read main, write auxiliary
    cpu, asm = machine("""
 org $d000
start sta $c002 ;read main
 sta $c005 ;write auxiliary
 ldx #3
loop lda $2000,x
 sta $2000,x
 dex
 bpl loop
 sta $c004 ;write main
 lda #$ee
 sta $2004
 rts
""")
    cpu.poke(0x2000, MAIN, bytes([1, 2, 3, 4]))
    cpu.poke(0x2000, AUX, bytes([9, 9, 9, 9, 9]))
    cpu.call(asm.symbols["start"])
    assert cpu.peek(0x2000, AUX, 5) == bytes([1, 2, 3, 4, 9])
    assert cpu.peek(0x2000, MAIN, 5) == bytes([1, 2, 3, 4, 0xEE])


def test_switching_reads_changes_what_code_and_data_are_seen():
    cpu, asm = machine("""
 org $d000
start sta $c003 ;read auxiliary
 lda $4000
 tax
 sta $c002 ;read main
 lda $4000
 rts
""")
    cpu.poke(0x4000, MAIN, 0x12)
    cpu.poke(0x4000, AUX, 0x34)
    cpu.call(asm.symbols["start"])
    assert (cpu.x, cpu.a) == (0x34, 0x12)
    assert cpu.ramrd == MAIN and cpu.ramwrt == MAIN


def test_snapshot_restores_both_banks_and_the_switches():
    cpu = BankedCpu()
    cpu.poke(0x1234, AUX, 7)
    cpu.poke(0xE000, MAIN, 8)
    saved = cpu.snapshot()
    cpu.set_altzp(AUX)
    cpu.set_ramrd(AUX)
    cpu.poke(0x1234, AUX, 0)
    cpu.poke(0xE000, MAIN, 0)
    cpu.restore(saved)
    assert cpu.peek(0x1234, AUX) == 7 and cpu.peek(0xE000, MAIN) == 8
    assert cpu.altzp == MAIN and cpu.ramrd == MAIN
