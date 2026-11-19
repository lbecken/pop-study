"""Tests for the 6502 assembler/simulator and the sound-effect tools.

Unit tests use small programs written for these tests.  Integration tests
need POP_SOURCE_DIR.
"""

import os
import struct
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_audio"))

from merlin_data import MerlinError, assemble, split_line  # noqa: E402
from mos6502 import (DECODE, FLAG_C, FLAG_N, FLAG_V, FLAG_Z, Cpu6502, CpuError,  # noqa: E402
                     assemble_6502, base_cycles)
from pop_sfx import CPU_HZ, render_pcm, tone_half_period, wav_bytes  # noqa: E402

# --- Merlin additions ----------------------------------------------------------------


def test_quoted_operands_and_character_constants():
    assert split_line(' lda #" " ;space') == ("", "lda", '#" "', "space")
    asm = assemble(""" org $1000
k = "p"-$60
 db "A",'A',k,"s"-$60
 asc "Hi @"
 asc 'ok'
""")
    assert asm.image == bytes([0xC1, 0x41, 0x90, 0x93, 0xC8, 0xE9, 0xA0, 0xC0, 0x6F, 0x6B])


def test_bitwise_operators_and_labelled_do():
    asm = assemble(""" org $1000
RdGrp = $84
Inc = $40
flag = 0
top db RdGrp.Inc,$0F!$05,$3C&$0F,flag!1
:skip do flag
 db 9
 fin
 dw :skip
""")
    assert asm.image == bytes([0xC4, 0x0A, 0x0C, 0x01, 0x04, 0x10])


def test_put_includes_and_stray_fin():
    files = {"eq": "base = $2000\n", "more": " db 7\n put inner\n", "inner": " db one\none = 1\n"}
    asm = assemble(" put eq\n org base\n put more\n db 8\n fin\n", files.__getitem__)
    assert asm.origin == 0x2000 and asm.image == bytes([7, 1, 8])
    assert asm.warnings == ["line 5: fin without do (ignored)"]
    with pytest.raises(MerlinError, match="no include resolver"):
        assemble(" put eq\n")
    with pytest.raises(MerlinError, match="else without do"):
        assemble(" org 0\n else\n")


# --- assembler -------------------------------------------------------------------------

def test_opcode_table_is_complete_and_unambiguous():
    assert len(DECODE) == 151
    assert DECODE[0xA9] == ("lda", "imm") and DECODE[0x2C] == ("bit", "abs")
    assert DECODE[0x6C] == ("jmp", "ind") and DECODE[0x91] == ("sta", "izy")
    assert base_cycles("bit", "abs") == 4 and base_cycles("sta", "abx") == 5
    assert base_cycles("inc", "abx") == 7 and base_cycles("rts", "imp") == 6


def test_addressing_modes_and_sizes():
    asm = assemble_6502("""
zp = $10
far = $1234
 org $0800
start lda #$05
 lda #>far
 lda #far
 lda zp
 lda zp,x
 lda far
 lda far,x
 lda far,y
 lda (zp),y
 lda (zp,x)
 ldx zp,y
 stx far
 asl
 asl zp
 jmp (far)
 jsr start
 rts
""")
    assert asm.image.hex() == ("a905" "a912" "a934" "a510" "b510" "ad3412" "bd3412" "b93412"
                               "b110" "a110" "b610" "8e3412" "0a" "0610" "6c3412" "200008"
                               "60")


def test_forward_references_are_absolute_and_branches_relative():
    asm = assemble_6502("""
 org $0800
 lda later
loop dex
 bne loop
 beq done
 blt loop
 bge loop
done rts
later = $20
""")
    # `later` is not yet known on its line, so it is assembled as absolute
    assert asm.image.hex() == "ad2000" "ca" "d0fd" "f004" "90f9" "b0f7" "60"
    with pytest.raises(MerlinError, match="branch out of range"):
        assemble_6502(" org $0800\n bne far\n ds 200\nfar rts\n")
    with pytest.raises(MerlinError, match="unsupported opcode"):
        assemble_6502(" org $0800\n stz $10\n")
    # two 65C02 instructions are accepted so IIGS-only code has its true size
    assert assemble_6502(" org $0800\n tsb $c036\n trb $10\n").image.hex() == "0c36c01410"
    with pytest.raises(MerlinError, match="no immediate mode"):
        assemble_6502(" org $0800\n sta #1\n")


# --- CPU -----------------------------------------------------------------------------------

def run(source, **regs):
    asm = assemble_6502(source)
    cpu = Cpu6502()
    cpu.load(asm.origin, asm.image)
    for k, v in regs.items():
        setattr(cpu, k, v)
    cycles = cpu.call(asm.symbols.get("start", asm.origin))
    return cpu, cycles, asm


def test_load_store_transfer_and_flags():
    cpu, cycles, _ = run("""
 org $0800
start lda #$80
 tax
 stx $10
 ldy $10
 iny
 sty $0300
 lda #0
 rts
""")
    assert (cpu.x, cpu.y, cpu.mem[0x10], cpu.mem[0x300]) == (0x80, 0x81, 0x80, 0x81)
    assert cpu.a == 0 and cpu.p & FLAG_Z and not cpu.p & FLAG_N
    assert cycles == 2 + 2 + 3 + 3 + 2 + 4 + 2 + 6


def test_arithmetic_compare_and_bit():
    cpu, _, _ = run(" org $0800\nstart clc\n lda #$50\n adc #$50\n rts\n")
    assert cpu.a == 0xA0 and cpu.p & FLAG_V and cpu.p & FLAG_N and not cpu.p & FLAG_C
    cpu, _, _ = run(" org $0800\nstart sec\n lda #$05\n sbc #$06\n rts\n")
    assert cpu.a == 0xFF and not cpu.p & FLAG_C
    cpu, _, _ = run(" org $0800\nstart lda #$00\n sec\n sbc #1\n eor #$ff\n clc\n adc #1\n rts\n")
    assert cpu.a == 1                                    # two's complement negate of -1
    cpu, _, _ = run(" org $0800\nstart ldy #5\n cpy #5\n rts\n")
    assert cpu.p & FLAG_C and cpu.p & FLAG_Z
    cpu, _, _ = run(" org $0800\nstart ldy #4\n cpy #5\n rts\n")
    assert not cpu.p & FLAG_C and cpu.p & FLAG_N
    cpu, _, _ = run(" org $0800\nv = $10\nstart lda #$c0\n sta v\n lda #$01\n bit v\n rts\n")
    assert cpu.p & FLAG_Z and cpu.p & FLAG_N and cpu.p & FLAG_V
    cpu, _, _ = run(" org $0800\nstart lda #$81\n asl\n rts\n")
    assert cpu.a == 0x02 and cpu.p & FLAG_C
    with pytest.raises(CpuError, match="decimal"):
        run(" org $0800\nstart sed\n adc #1\n rts\n")


def test_stack_subroutines_and_self_modifying_jump():
    cpu, cycles, asm = run("""
 org $0800
start lda #<target
 sta sm+1
 lda #>target
 sta sm+2
 lda #7
 pha
 jsr sub
 pla
 rts
sub
sm jmp $ffff
target ldx #9
 rts
""")
    assert cpu.a == 7 and cpu.x == 9 and cpu.sp == 0xFF


def test_branch_and_index_page_crossing_cost_a_cycle():
    same = " org $0810\nstart ldx #2\nloop dex\n bne loop\n rts\n"
    cross = " org $08fd\nstart ldx #2\nloop dex\n bne loop\n rts\n"   # loop at $08FF, branch at $0900
    _, a, _ = run(same)
    _, b, _ = run(cross)
    assert a == 2 + (2 + 3) + (2 + 2) + 6
    assert b == a + 1                                    # one taken branch crossed a page
    _, a, _ = run(" org $0800\nstart ldx #1\n lda $10fe,x\n rts\n")
    _, b, _ = run(" org $0800\nstart ldx #2\n lda $10fe,x\n rts\n")
    assert (a, b) == (2 + 4 + 6, 2 + 5 + 6)
    _, c, _ = run(" org $0800\nstart ldx #2\n sta $10fe,x\n rts\n")
    assert c == 2 + 5 + 6                                # stores never take the extra cycle


def test_io_hook_and_runaway_guard():
    asm = assemble_6502("""
spkr = $c030
 org $0800
start ldx #3
loop bit spkr
 dex
 bne loop
 rts
""")
    cpu = Cpu6502()
    cpu.load(asm.origin, asm.image)
    hits = []
    cpu.on_read[0xC030] = lambda c: hits.append(c.cycles)
    cpu.call(asm.symbols["start"])
    assert hits == [6, 15, 24]                           # 2 + 4, then 4+2+3 apart
    cpu = Cpu6502()
    cpu.load(0x0800, bytes([0x4C, 0x00, 0x08]))          # jmp $0800
    with pytest.raises(CpuError, match="did not return"):
        cpu.call(0x0800, max_cycles=1000)
    cpu.load(0x0800, bytes([0x02]))
    with pytest.raises(CpuError, match="undocumented"):
        cpu.call(0x0800)


# --- tone arithmetic and rendering -----------------------------------------------------------

def test_tone_half_period_formula():
    assert tone_half_period(35) == 9 * 35 + 22
    assert tone_half_period(70) == 652
    assert tone_half_period(1000) == 3 * (9 * 232 + 10) + 12     # $03E8: 3 passes of 232
    assert tone_half_period(500) == 9 * 244 + 22                  # $01F4: 1 pass of 244
    assert tone_half_period(256) == tone_half_period(1)           # low byte 0 behaves as 1


def test_render_pcm_box_filter():
    rate = CPU_HZ // 100                                 # 100.008 cycles per sample
    flat = render_pcm([], 1000, rate, amplitude=1.0, lead_ms=0, tail_ms=0)
    assert flat == pytest.approx([-1.0] * len(flat))
    out = render_pcm([250, 600], 1000, rate, amplitude=1.0, lead_ms=0, tail_ms=0)
    # edges at 2.5 and 6.0 samples: sample 2 is half and half
    assert out[:7] == pytest.approx([-1.0, -1.0, 0.0, 1.0, 1.0, 1.0, -1.0], abs=2e-3)
    assert len(out) == 10
    up = render_pcm([0], 300, rate, amplitude=0.5, lead_ms=0, tail_ms=0, start_level=1)
    assert up[0] == pytest.approx(-0.5)


def test_wav_header():
    w = wav_bytes([0.0, 0.5, -0.5, 2.0], 8000)
    assert w[:4] == b"RIFF" and w[8:16] == b"WAVEfmt "
    assert struct.unpack("<IHHIIHH", w[16:36]) == (16, 1, 1, 8000, 16000, 2, 16)
    assert struct.unpack("<4h", w[44:52]) == (0, 16384, -16384, 32767)


# --- integration tests against the original checkout (optional) -------------------

SOURCE = os.environ.get("POP_SOURCE_DIR")
SRC = os.path.join(SOURCE, "01 POP Source", "Source") if SOURCE else None
needs_source = pytest.mark.skipif(
    not (SRC and os.path.isdir(SRC)),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")

# module: (origin, address the next module starts at), from EQ.S / GAMEEQ.S
MODULES = {
    "TABLES.S": (0x0E00, 0x1290), "FRAMEADV.S": (0x1290, 0x2000),
    "TOPCTRL.S": (0x2000, 0x2800), "FRAMEDEF.S": (0x2800, 0x3000),
    "SEQTABLE.S": (0x3000, 0x3A00), "CTRL.S": (0x3A00, 0x4500),
    "COLL.S": (0x4500, 0x4C00), "GAMEBG.S": (0x4C00, 0x5400),
    "AUTO.S": (0x5400, 0x5E00), "CTRLSUBS.S": (0xD000, 0xD900),
    "VERSION.S": (0xDFD8, 0xE000), "SUBS.S": (0xE000, 0xEA00),
    "SOUND.S": (0xEA00, 0xEE00), "MOVER.S": (0xEE00, 0xF900),
    "HRTABLES.S": (0xE000, 0xEA00), "HIRES.S": (0xEE00, 0xF880),
    "MASTER.S": (0xF880, 0xFFFA),
}


@needs_source
def test_original_modules_assemble_and_fit_their_slots():
    from pop_sfx import include_resolver
    resolve = include_resolver(SRC, os.path.join(SOURCE, "02 POP Disk Routines", "CP.525"))
    slack = {}
    for name, (origin, limit) in MODULES.items():
        with open(os.path.join(SRC, name), encoding="ascii") as f:
            asm = assemble_6502(f.read(), resolve)
        assert asm.origin == origin, name
        assert asm.end <= limit, name
        slack[name] = limit - asm.end
    # several modules were packed to within a few bytes of the next one
    assert slack["CTRL.S"] <= 2 and slack["CTRLSUBS.S"] <= 2 and slack["TOPCTRL.S"] <= 8


@needs_source
def test_sound_module_layout():
    from pop_sfx import SoundModule
    mod = SoundModule(SRC)
    assert (mod.asm.origin, mod.asm.end) == (0xEA00, 0xEB2F)
    assert mod.count == 20
    assert mod.sym["soundon"] == 0x0203 and mod.sym["soundtable"] == 0xB6C0
    # the whole tone loop lies in one page, so no branch pays a page-cross cycle
    tone, pitch = mod.sym["tone"], mod.asm.locals[("tone", ":pitch")]
    assert tone >> 8 == (pitch - 1) >> 8 == 0xEB
    assert mod.names[9] == "Footstep" and mod.names[19] == "JawsClash"


@needs_source
def test_effects_measured_from_the_original_code():
    from pop_sfx import SoundModule
    mod = SoundModule(SRC)
    fx = {e.name: e for e in mod.all_effects()}
    assert len(fx) == 20

    def steady(name):
        return [(s["half_period"], s["toggles"] + 1) for s in fx[name].segments()
                if s["toggles"] > 1]

    assert steady("Footstep") == [(337, 3)]
    assert steady("PlateDown") == [(652, 4)] and steady("PlateUp") == [(832, 4)]
    assert steady("LoweringGate") == [(85, 8)]
    assert steady("JawsClash") == [(112, 50)]
    assert steady("Splat") == [(6306, 3)]
    assert len(fx["RaisingGate"].toggles) == 2 and fx["RaisingGate"].intervals == [202]
    assert steady("FlashMsg") == [(2218, 15), (922, 25), (2218, 15), (922, 25)]
    # every steady tone agrees with the closed-form loop count
    pitches = {tone_half_period(p) for p in range(1, 1100)}
    for e in fx.values():
        for s in e.segments():
            if s["toggles"] > 1:
                assert s["half_period"] in pitches, e.name
    # effects that are the same sound under different names
    same = lambda a, b: fx[a].intervals == fx[b].intervals  # noqa: E731
    assert same("Splat", "MirrorCrack") and same("Splat", "LooseCrash")
    assert same("Splat", "SmackWall") and same("Splat", "Impaled") and same("Splat", "GateSlam")
    assert same("PlateDown", "GateDown") and same("GotKey", "FlashMsg")
    assert same("SpecialKey1", "SwordClash1") and same("SpecialKey1", "SwordClash2")
    assert len({tuple(e.intervals) for e in fx.values()}) == 11
    # the two-tone message sound outlasts a nominal game tick
    assert fx["FlashMsg"].total_cycles > CPU_HZ * 60 / 725 > fx["Splat"].total_cycles


@needs_source
def test_queue_behaviour_of_the_original_code():
    from pop_sfx import SoundModule
    mod = SoundModule(SRC)
    foot, plate = mod.names_to_number("Footstep"), mod.names_to_number("PlateDown")
    toggles, _ = mod.play_queue([foot, plate])
    gaps = [b - a for a, b in zip(toggles, toggles[1:])]
    assert gaps[:3] == [652, 652, 652] and gaps[-2:] == [337, 337]   # last added plays first
    assert mod.play_queue([foot], sound_on=False)[0] == []
    assert mod.play_queue([])[0] == []
    assert mod.play_queue([20])[0] == [] and mod.play_queue([127])[0] == []  # beyond the table
    q = bytearray(33)
    for n in range(40):
        mod.add_sound(q, n)
    assert q[0] == 32 and list(q[1:33]) == list(range(32))            # 33rd and later dropped
