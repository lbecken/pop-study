"""MOS 6502: opcode table, a Merlin-syntax assembler and a cycle-counting CPU.

New code for the pop-study project; contains no original code or data.

Purpose: assemble small original routines exactly as written and run them,
so their behaviour and timing come from executing the original instructions
rather than from a hand-derived formula.

Scope and limits:
  * The 151 documented NMOS 6502 opcodes.  No undocumented opcodes, no
    65C02 additions.  Decimal mode is not modelled: ADC/SBC with the D flag
    set raise an error.
  * Cycle counts are the documented ones, including +1 for a taken branch,
    +1 more when a taken branch crosses a page, and +1 for indexed reads
    that cross a page.
  * Only the instructions exercised by the tests are verified.  Anything
    else should be checked before it is relied on.
  * No interrupts, no bus-accurate sub-cycle timing.
"""

from __future__ import annotations

import re
from typing import Callable, Dict, List, Optional, Tuple

from merlin_data import Assembly, MerlinError, Undefined, _Assembler

# --- opcode table ---------------------------------------------------------------

_GROUP = ("imm", "zp", "zpx", "abs", "abx", "aby", "izx", "izy")
OPCODES: Dict[str, Dict[str, int]] = {
    "adc": dict(zip(_GROUP, (0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71))),
    "and": dict(zip(_GROUP, (0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31))),
    "cmp": dict(zip(_GROUP, (0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1))),
    "eor": dict(zip(_GROUP, (0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51))),
    "lda": dict(zip(_GROUP, (0xA9, 0xA5, 0xB5, 0xAD, 0xBD, 0xB9, 0xA1, 0xB1))),
    "ora": dict(zip(_GROUP, (0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11))),
    "sbc": dict(zip(_GROUP, (0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1))),
    "sta": {"zp": 0x85, "zpx": 0x95, "abs": 0x8D, "abx": 0x9D, "aby": 0x99,
            "izx": 0x81, "izy": 0x91},
    "asl": {"acc": 0x0A, "zp": 0x06, "zpx": 0x16, "abs": 0x0E, "abx": 0x1E},
    "lsr": {"acc": 0x4A, "zp": 0x46, "zpx": 0x56, "abs": 0x4E, "abx": 0x5E},
    "rol": {"acc": 0x2A, "zp": 0x26, "zpx": 0x36, "abs": 0x2E, "abx": 0x3E},
    "ror": {"acc": 0x6A, "zp": 0x66, "zpx": 0x76, "abs": 0x6E, "abx": 0x7E},
    "inc": {"zp": 0xE6, "zpx": 0xF6, "abs": 0xEE, "abx": 0xFE},
    "dec": {"zp": 0xC6, "zpx": 0xD6, "abs": 0xCE, "abx": 0xDE},
    "bit": {"zp": 0x24, "abs": 0x2C},
    "cpx": {"imm": 0xE0, "zp": 0xE4, "abs": 0xEC},
    "cpy": {"imm": 0xC0, "zp": 0xC4, "abs": 0xCC},
    "ldx": {"imm": 0xA2, "zp": 0xA6, "zpy": 0xB6, "abs": 0xAE, "aby": 0xBE},
    "ldy": {"imm": 0xA0, "zp": 0xA4, "zpx": 0xB4, "abs": 0xAC, "abx": 0xBC},
    "stx": {"zp": 0x86, "zpy": 0x96, "abs": 0x8E},
    "sty": {"zp": 0x84, "zpx": 0x94, "abs": 0x8C},
    "jmp": {"abs": 0x4C, "ind": 0x6C},
    "jsr": {"abs": 0x20},
    "bcc": {"rel": 0x90}, "bcs": {"rel": 0xB0}, "beq": {"rel": 0xF0},
    "bmi": {"rel": 0x30}, "bne": {"rel": 0xD0}, "bpl": {"rel": 0x10},
    "bvc": {"rel": 0x50}, "bvs": {"rel": 0x70},
    "brk": {"imp": 0x00}, "clc": {"imp": 0x18}, "cld": {"imp": 0xD8},
    "cli": {"imp": 0x58}, "clv": {"imp": 0xB8}, "dex": {"imp": 0xCA},
    "dey": {"imp": 0x88}, "inx": {"imp": 0xE8}, "iny": {"imp": 0xC8},
    "nop": {"imp": 0xEA}, "pha": {"imp": 0x48}, "php": {"imp": 0x08},
    "pla": {"imp": 0x68}, "plp": {"imp": 0x28}, "rti": {"imp": 0x40},
    "rts": {"imp": 0x60}, "sec": {"imp": 0x38}, "sed": {"imp": 0xF8},
    "sei": {"imp": 0x78}, "tax": {"imp": 0xAA}, "tay": {"imp": 0xA8},
    "tsx": {"imp": 0xBA}, "txa": {"imp": 0x8A}, "txs": {"imp": 0x9A},
    "tya": {"imp": 0x98},
}
ALIASES = {"blt": "bcc", "bge": "bcs"}        # Merlin spellings
SIZE = {"imp": 1, "acc": 1, "imm": 2, "zp": 2, "zpx": 2, "zpy": 2, "izx": 2, "izy": 2,
        "rel": 2, "abs": 3, "abx": 3, "aby": 3, "ind": 3}
DECODE: Dict[int, Tuple[str, str]] = {
    code: (mnem, mode) for mnem, modes in OPCODES.items() for mode, code in modes.items()}
assert len(DECODE) == 151

_STORE = ("sta", "stx", "sty")
_RMW = ("asl", "lsr", "rol", "ror", "inc", "dec")
_READ_CYCLES = {"imm": 2, "zp": 3, "zpx": 4, "zpy": 4, "abs": 4, "abx": 4, "aby": 4,
                "izx": 6, "izy": 5}
_STORE_CYCLES = {"zp": 3, "zpx": 4, "zpy": 4, "abs": 4, "abx": 5, "aby": 5,
                 "izx": 6, "izy": 6}
_RMW_CYCLES = {"acc": 2, "zp": 5, "zpx": 6, "abs": 6, "abx": 7}
_IMPLIED_CYCLES = {"brk": 7, "pha": 3, "php": 3, "pla": 4, "plp": 4, "rti": 6, "rts": 6}


def base_cycles(mnem: str, mode: str) -> int:
    if mode == "rel":
        return 2
    if mnem == "jmp":
        return 3 if mode == "abs" else 5
    if mnem == "jsr":
        return 6
    if mode == "imp":
        return _IMPLIED_CYCLES.get(mnem, 2)
    if mnem in _STORE:
        return _STORE_CYCLES[mode]
    if mnem in _RMW:
        return _RMW_CYCLES[mode]
    return _READ_CYCLES[mode]


# --- assembler ------------------------------------------------------------------

class _Asm6502(_Assembler):
    """Merlin data directives plus 6502 instructions.

    Addressing follows Merlin: `#v` immediate (low byte; `#>v` high byte),
    `v` zero page if v is already known and below $100 else absolute,
    `v,x` `v,y` `(v),y` `(v,x)` `(v)`, no operand for implied and for
    accumulator shifts.  A forward reference is assembled as absolute.
    """

    def __init__(self, text, include=None):
        super().__init__(text, include)
        self._modes: List[str] = []       # decided in pass 1, reused in pass 2
        self._cursor = 0
        self._final_seen = False

    def assemble_instruction(self, op, operand, pc, scope, final, line_no):
        mnem = ALIASES.get(op, op)
        if mnem not in OPCODES:
            raise MerlinError("unsupported opcode %r" % op)
        if final and not self._final_seen:
            self._final_seen = True
            self._cursor = 0
        modes = OPCODES[mnem]
        text = operand.strip()
        low = text.lower()

        def value(expr):
            """(value, known).  Unknown symbols are legal in pass 1 only."""
            try:
                return self.evaluate(expr, pc, scope), True
            except Undefined as u:
                if final:
                    raise MerlinError("undefined symbol %s" % u)
                return 0, False

        if not text or (low == "a" and "acc" in modes):
            mode = "imp" if "imp" in modes else "acc"
            if mode not in modes:
                raise MerlinError("%s needs an operand" % op)
            return bytes([modes[mode]])
        if "rel" in modes:
            target, known = value(text)
            off = target - (pc + 2)
            if final and not -128 <= off <= 127:
                raise MerlinError("branch out of range (%d)" % off)
            return bytes([modes["rel"], off & 0xFF if known else 0])
        if text.startswith("#"):
            v, _ = value(text)
            if "imm" not in modes:
                raise MerlinError("%s has no immediate mode" % op)
            return bytes([modes["imm"], v & 0xFF])

        m = re.fullmatch(r"\((.+)\),[yY]", text)
        if m:
            v, _ = value(m.group(1))
            return self._emit(op, modes, "izy", v, final)
        m = re.fullmatch(r"\((.+),[xX]\)", text)
        if m:
            v, _ = value(m.group(1))
            return self._emit(op, modes, "izx", v, final)
        m = re.fullmatch(r"\((.+)\)", text)
        if m:
            v, _ = value(m.group(1))
            return self._emit(op, modes, "ind", v, final)

        index = ""
        expr = text
        if low.endswith(",x") or low.endswith(",y"):
            index, expr = low[-1], text[:-2]
        v, known = value(expr)
        zp_mode, abs_mode = "zp" + index, "ab" + index if index else "abs"
        if final:
            mode = self._modes[self._cursor]
            self._cursor += 1
        else:
            if known and 0 <= v < 0x100 and zp_mode in modes:
                mode = zp_mode
            elif abs_mode in modes:
                mode = abs_mode
            elif zp_mode in modes:
                mode = zp_mode
            else:
                raise MerlinError("%s does not take operand %r" % (op, operand))
            self._modes.append(mode)
        return self._emit(op, modes, mode, v, final)

    @staticmethod
    def _emit(op, modes, mode, v, final):
        if mode not in modes:
            raise MerlinError("%s has no %s addressing mode" % (op, mode))
        if SIZE[mode] == 2:
            if final and not 0 <= v < 0x100:
                raise MerlinError("operand $%X does not fit zero page" % v)
            return bytes([modes[mode], v & 0xFF])
        return bytes([modes[mode], v & 0xFF, (v >> 8) & 0xFF])


def assemble_6502(text: str, include=None) -> Assembly:
    """Assemble Merlin source containing 6502 instructions and data."""
    return _Asm6502(text, include).run()


# --- CPU --------------------------------------------------------------------------

FLAG_C, FLAG_Z, FLAG_I, FLAG_D, FLAG_B, FLAG_V, FLAG_N = 0x01, 0x02, 0x04, 0x08, 0x10, 0x40, 0x80


class CpuError(RuntimeError):
    pass


class Cpu6502:
    """A small NMOS 6502 with a cycle counter.

    `on_read[address]` / `on_write[address]` are optional callbacks
    `f(cpu)` invoked when that address is accessed; `cpu.cycles` at that
    moment is the cycle count at the end of the accessing instruction.
    """

    def __init__(self):
        self.mem = bytearray(0x10000)
        self.a = self.x = self.y = 0
        self.sp = 0xFF
        self.pc = 0
        self.p = 0x20 | FLAG_I
        self.cycles = 0
        self.on_read: Dict[int, Callable] = {}
        self.on_write: Dict[int, Callable] = {}

    # -- memory -----------------------------------------------------------------

    def load(self, address: int, data: bytes) -> None:
        self.mem[address:address + len(data)] = data

    def _rd(self, a: int) -> int:
        a &= 0xFFFF
        if a in self.on_read:
            self._pending.append(self.on_read[a])
        return self.mem[a]

    def _wr(self, a: int, v: int) -> None:
        a &= 0xFFFF
        self.mem[a] = v & 0xFF
        if a in self.on_write:
            self._pending.append(self.on_write[a])

    def _push(self, v: int) -> None:
        self.mem[0x100 + self.sp] = v & 0xFF
        self.sp = (self.sp - 1) & 0xFF

    def _pull(self) -> int:
        self.sp = (self.sp + 1) & 0xFF
        return self.mem[0x100 + self.sp]

    def _nz(self, v: int) -> int:
        v &= 0xFF
        self.p = (self.p & ~(FLAG_N | FLAG_Z)) | (v & 0x80) | (FLAG_Z if v == 0 else 0)
        return v

    def _flag(self, flag: int, on) -> None:
        self.p = (self.p | flag) if on else (self.p & ~flag)

    # -- execution --------------------------------------------------------------

    def step(self) -> int:
        """Execute one instruction; return the cycles it took."""
        self._pending: List[Callable] = []
        opcode = self.mem[self.pc]
        if opcode not in DECODE:
            raise CpuError("undocumented opcode $%02X at $%04X" % (opcode, self.pc))
        mnem, mode = DECODE[opcode]
        pc = self.pc
        lo = self.mem[(pc + 1) & 0xFFFF]
        hi = self.mem[(pc + 2) & 0xFFFF]
        self.pc = (pc + SIZE[mode]) & 0xFFFF
        cycles = base_cycles(mnem, mode)
        addr: Optional[int] = None
        crossed = False

        if mode in ("zp",):
            addr = lo
        elif mode == "zpx":
            addr = (lo + self.x) & 0xFF
        elif mode == "zpy":
            addr = (lo + self.y) & 0xFF
        elif mode == "abs":
            addr = lo | (hi << 8)
        elif mode in ("abx", "aby"):
            base = lo | (hi << 8)
            addr = (base + (self.x if mode == "abx" else self.y)) & 0xFFFF
            crossed = (addr & 0xFF00) != (base & 0xFF00)
        elif mode == "izx":
            z = (lo + self.x) & 0xFF
            addr = self.mem[z] | (self.mem[(z + 1) & 0xFF] << 8)
        elif mode == "izy":
            base = self.mem[lo] | (self.mem[(lo + 1) & 0xFF] << 8)
            addr = (base + self.y) & 0xFFFF
            crossed = (addr & 0xFF00) != (base & 0xFF00)
        elif mode == "ind":
            ptr = lo | (hi << 8)             # NMOS: high byte fetched without carry
            addr = self.mem[ptr] | (self.mem[(ptr & 0xFF00) | ((ptr + 1) & 0xFF)] << 8)

        is_read = mnem not in _STORE and mnem not in _RMW
        if crossed and is_read and mode in ("abx", "aby", "izy"):
            cycles += 1

        def operand() -> int:
            if mode == "imm":
                return lo
            return self._rd(addr)

        if mnem == "lda":
            self.a = self._nz(operand())
        elif mnem == "ldx":
            self.x = self._nz(operand())
        elif mnem == "ldy":
            self.y = self._nz(operand())
        elif mnem == "sta":
            self._wr(addr, self.a)
        elif mnem == "stx":
            self._wr(addr, self.x)
        elif mnem == "sty":
            self._wr(addr, self.y)
        elif mnem in ("and", "ora", "eor"):
            v = operand()
            self.a = self._nz(self.a & v if mnem == "and" else
                              self.a | v if mnem == "ora" else self.a ^ v)
        elif mnem in ("adc", "sbc"):
            if self.p & FLAG_D:
                raise CpuError("decimal mode is not modelled ($%04X)" % pc)
            v = operand()
            if mnem == "sbc":
                v ^= 0xFF
            total = self.a + v + (self.p & FLAG_C)
            self._flag(FLAG_C, total > 0xFF)
            self._flag(FLAG_V, (~(self.a ^ v) & (self.a ^ total)) & 0x80)
            self.a = self._nz(total)
        elif mnem in ("cmp", "cpx", "cpy"):
            reg = self.a if mnem == "cmp" else self.x if mnem == "cpx" else self.y
            v = operand()
            self._flag(FLAG_C, reg >= v)
            self._nz(reg - v)
        elif mnem == "bit":
            v = operand()
            self._flag(FLAG_Z, (self.a & v) == 0)
            self.p = (self.p & ~(FLAG_N | FLAG_V)) | (v & (FLAG_N | FLAG_V))
        elif mnem in _RMW:
            v = self.a if mode == "acc" else self._rd(addr)
            if mnem == "asl":
                self._flag(FLAG_C, v & 0x80)
                v = (v << 1) & 0xFF
            elif mnem == "lsr":
                self._flag(FLAG_C, v & 0x01)
                v >>= 1
            elif mnem == "rol":
                c = self.p & FLAG_C
                self._flag(FLAG_C, v & 0x80)
                v = ((v << 1) | c) & 0xFF
            elif mnem == "ror":
                c = self.p & FLAG_C
                self._flag(FLAG_C, v & 0x01)
                v = (v >> 1) | (c << 7)
            elif mnem == "inc":
                v = (v + 1) & 0xFF
            else:
                v = (v - 1) & 0xFF
            self._nz(v)
            if mode == "acc":
                self.a = v
            else:
                self._wr(addr, v)
        elif mode == "rel":
            taken = {
                "bcc": not self.p & FLAG_C, "bcs": bool(self.p & FLAG_C),
                "bne": not self.p & FLAG_Z, "beq": bool(self.p & FLAG_Z),
                "bpl": not self.p & FLAG_N, "bmi": bool(self.p & FLAG_N),
                "bvc": not self.p & FLAG_V, "bvs": bool(self.p & FLAG_V),
            }[mnem]
            if taken:
                target = (self.pc + (lo - 256 if lo >= 0x80 else lo)) & 0xFFFF
                cycles += 1 + ((target & 0xFF00) != (self.pc & 0xFF00))
                self.pc = target
        elif mnem == "jmp":
            self.pc = addr
        elif mnem == "jsr":
            ret = (self.pc - 1) & 0xFFFF
            self._push(ret >> 8)
            self._push(ret)
            self.pc = addr
        elif mnem == "rts":
            l = self._pull()
            self.pc = ((l | (self._pull() << 8)) + 1) & 0xFFFF
        elif mnem == "rti":
            self.p = (self._pull() & ~FLAG_B) | 0x20
            l = self._pull()
            self.pc = l | (self._pull() << 8)
        elif mnem == "brk":
            raise CpuError("BRK at $%04X" % pc)
        elif mnem == "pha":
            self._push(self.a)
        elif mnem == "php":
            self._push(self.p | FLAG_B | 0x20)
        elif mnem == "pla":
            self.a = self._nz(self._pull())
        elif mnem == "plp":
            self.p = (self._pull() & ~FLAG_B) | 0x20
        elif mnem in ("inx", "iny", "dex", "dey"):
            d = 1 if mnem[0] == "i" else -1
            if mnem[2] == "x":
                self.x = self._nz(self.x + d)
            else:
                self.y = self._nz(self.y + d)
        elif mnem == "tax":
            self.x = self._nz(self.a)
        elif mnem == "tay":
            self.y = self._nz(self.a)
        elif mnem == "txa":
            self.a = self._nz(self.x)
        elif mnem == "tya":
            self.a = self._nz(self.y)
        elif mnem == "tsx":
            self.x = self._nz(self.sp)
        elif mnem == "txs":
            self.sp = self.x
        elif mnem in ("clc", "sec", "cli", "sei", "cld", "sed", "clv"):
            flag = {"c": FLAG_C, "i": FLAG_I, "d": FLAG_D, "v": FLAG_V}[mnem[2]]
            self._flag(flag, mnem[0] == "s")
        elif mnem == "nop":
            pass
        else:                                  # pragma: no cover
            raise CpuError("unhandled %s" % mnem)

        self.cycles += cycles
        for callback in self._pending:
            callback(self)
        return cycles

    def call(self, address: int, max_cycles: int = 50_000_000) -> int:
        """JSR to `address` and run until the matching RTS.  Returns the
        cycles spent inside the routine, including its final RTS."""
        sentinel = 0xFFF0
        ret = (sentinel - 1) & 0xFFFF
        self._push(ret >> 8)
        self._push(ret)
        self.pc = address
        start = self.cycles
        while self.pc != sentinel:
            self.step()
            if self.cycles - start > max_cycles:
                raise CpuError("routine at $%04X did not return within %d cycles"
                               % (address, max_cycles))
        return self.cycles - start
