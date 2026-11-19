"""Run original game routines: the assembled source in a 6502 simulator.

New code for the pop-study project; contains no original code or data.

`Oracle` assembles the game-side modules from the user's checkout, loads
them at their original addresses into one 64K memory, and lets a test call
any routine with chosen register and memory contents.  It is the reference
the C port is compared against.

What it is and is not:

  * The code that runs is the original source as assembled by this
    project's assembler (tools/extract_assets/mos6502.py).  The assembled
    bytes have not been compared with a disk image (docs/unknowns.md P1).
  * Memory is flat.  The game used auxiliary memory for its code and data
    and main memory for the hi-res pages and blitters; only the auxiliary
    side is loaded, and soft-switch writes are ignored.  Routines that
    draw (through the main-memory blitters) or touch the disk cannot be run.
  * GRAFIX.S is assembled without its last section, the Apple IIGS control
    panel routines, which are 65816 code.  Nothing before it moves.
"""

from __future__ import annotations

import os
import sys
from typing import Dict, Iterable, Optional, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "extract_assets"))
sys.path.insert(0, os.path.join(HERE, "..", "extract_audio"))

from merlin_data import Assembly  # noqa: E402
from mos6502 import Cpu6502, assemble_6502  # noqa: E402
from pop_sfx import include_resolver  # noqa: E402

SRC = os.path.join("01 POP Source", "Source")
CP525 = os.path.join("02 POP Disk Routines", "CP.525")

# Modules that live in auxiliary memory and its language card.
MODULES = ("GRAFIX", "TABLES", "FRAMEADV", "TOPCTRL", "FRAMEDEF", "SEQTABLE", "CTRL",
           "COLL", "GAMEBG", "AUTO", "CTRLSUBS", "SPECIALK", "VERSION", "SUBS", "SOUND",
           "MOVER", "MISC")
GRAFIX_CUT = "getparam"      # first label of the 65816 section
BLUEPRINT = 0xB700           # EQ.S 28
LEVEL_SIZE = 2304


class OracleError(RuntimeError):
    pass


def _truncate_grafix(text: str) -> str:
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.split(None, 1)[:1] == [GRAFIX_CUT] and not line[:1].isspace():
            cut = i
            while cut > 0 and lines[cut - 1].strip().lower() == "xc":
                cut -= 1
            return "\n".join(lines[:cut]) + "\n"
    raise OracleError("GRAFIX.S: label %s not found" % GRAFIX_CUT)


class Oracle:
    def __init__(self, source: str):
        src = os.path.join(source, SRC)
        resolve = include_resolver(src, os.path.join(source, CP525))
        self.modules: Dict[str, Assembly] = {}
        self.cpu = Cpu6502()
        used: Dict[int, str] = {}
        for name in MODULES:
            with open(os.path.join(src, name + ".S"), encoding="ascii") as f:
                text = f.read()
            if name == "GRAFIX":
                text = _truncate_grafix(text)
            asm = assemble_6502(text, resolve)
            self.modules[name] = asm
            for a in range(asm.origin, asm.end):
                if a in used:
                    raise OracleError("%s overlaps %s at $%04X" % (name, used[a], a))
                used[a] = name
            self.cpu.load(asm.origin, asm.image)
        self._image = bytes(self.cpu.mem)
        # Equates from EQ.S / GAMEEQ.S are the same in every module that
        # includes them; CTRL includes both.
        self.equ = self.modules["CTRL"].symbols

    # -- addresses ------------------------------------------------------------------

    def addr(self, name: str, module: Optional[str] = None) -> int:
        """Address of a symbol: a module's own label if `module` is given,
        otherwise a name from the shared equate files (zero-page variables,
        jump-table slots)."""
        table = self.modules[module].symbols if module else self.equ
        if name not in table:
            raise OracleError("unknown symbol %s%s" % (name, " in " + module if module else ""))
        return table[name] & 0xFFFF

    # -- memory -----------------------------------------------------------------------

    def reset(self) -> None:
        """Restore memory to the freshly loaded image and clear registers."""
        self.cpu.mem[:] = self._image
        self.cpu.a = self.cpu.x = self.cpu.y = 0
        self.cpu.sp = 0xFF
        self.cpu.p = 0x24

    def poke(self, name_or_addr, value, module: Optional[str] = None) -> None:
        a = name_or_addr if isinstance(name_or_addr, int) else self.addr(name_or_addr, module)
        if isinstance(value, int):
            self.cpu.mem[a] = value & 0xFF
        else:
            self.cpu.mem[a:a + len(value)] = bytes(value)

    def peek(self, name_or_addr, length: int = 0, module: Optional[str] = None):
        a = name_or_addr if isinstance(name_or_addr, int) else self.addr(name_or_addr, module)
        return self.cpu.mem[a] if not length else bytes(self.cpu.mem[a:a + length])

    def load_level(self, data: bytes) -> None:
        if len(data) != LEVEL_SIZE:
            raise OracleError("a level is %d bytes" % LEVEL_SIZE)
        self.poke(BLUEPRINT, data)

    def set_vars(self, values: Dict[str, int]) -> None:
        for name, v in values.items():
            self.poke(name, v)

    def get_vars(self, names: Iterable[str]) -> Dict[str, int]:
        return {n: self.peek(n) for n in names}

    # -- execution -------------------------------------------------------------------

    def call(self, name: str, a: int = 0, x: int = 0, y: int = 0,
             module: Optional[str] = None, max_cycles: int = 2_000_000
             ) -> Tuple[int, int, int, int]:
        """Call a routine; returns (A, X, Y, P) at its RTS.

        The N and Z flags are set from A first, as if the caller had just
        loaded it.  Some routines depend on that: GETLEFT and its siblings
        begin with `beq` without loading anything (CTRLSUBS.S 244-275).
        """
        cpu = self.cpu
        cpu.a, cpu.x, cpu.y = a & 0xFF, x & 0xFF, y & 0xFF
        cpu.sp = 0xFF
        cpu.p = (cpu.p & 0x7D) | (cpu.a & 0x80) | (0x02 if cpu.a == 0 else 0)
        cpu.call(self.addr(name, module), max_cycles)
        return cpu.a, cpu.x, cpu.y, cpu.p
