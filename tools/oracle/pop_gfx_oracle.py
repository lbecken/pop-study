"""Run the original drawing code: both memory banks of the game in a 6502
simulator.

New code for the pop-study project; contains no original code or data.

`pop_oracle.Oracle` loads the game-side modules into one flat memory, which
is enough for the game rules but not for anything that draws: the blitters
(HIRES.S), their tables (HRTABLES.S) and the hi-res pages are in main
memory, the game in auxiliary memory, and the two share addresses.
`GfxOracle` builds the 128K layout instead (docs/memory-map.md):

    auxiliary   the 17 game modules, the level, background image tables
                ($6000, $8400), character tables 4 and 7 ($9600, $9F00)
    main        HRTABLES ($E000), HIRES ($EE00), character tables 1, 2, 3
                and 5 ($6000, $8400, $0800, $A800), hi-res pages 1 and 2

and starts routines the way the game runs: ALTZP, RAMRD and RAMWRT on
(auxiliary).  Drawing routines are called through their GRAFIX entry
points, which copy the parameters to the main zero page and switch banks.

Limits: as for Oracle (the project's own assembler; no disk image to compare
with), plus those of pop_banked.BankedCpu.  MASTER and UNPACK are not loaded
(one entry of UNPACK is stood in for, see __init__).
"""

from __future__ import annotations

import os
import sys
from typing import Dict, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "extract_assets"))
sys.path.insert(0, os.path.join(HERE, "..", "extract_audio"))

from mos6502 import assemble_6502  # noqa: E402
from pop_banked import AUX, MAIN, BankedCpu  # noqa: E402
from pop_oracle import BLUEPRINT, CP525, SRC, Oracle  # noqa: E402
from pop_sfx import include_resolver  # noqa: E402

IMAGES = os.path.join("01 POP Source", "Images")
MAIN_MODULES = ("HRTABLES", "HIRES")
# image table -> (address, bank): GAMEEQ.S 9-18, GRAFIX.S 184-187, TOPCTRL.S 125
TABLES = {
    "chtable1": (0x6000, MAIN), "chtable2": (0x8400, MAIN), "chtable3": (0x0800, MAIN),
    "chtable4": (0x9600, AUX), "chtable5": (0xA800, MAIN), "chtable7": (0x9F00, AUX),
    "bgtable1": (0x6000, AUX), "bgtable2": (0x8400, AUX),
}
PAGE1, PAGE2, PAGE_SIZE = 0x2000, 0x4000, 0x2000
UNPACK_INVERTY = 0xEA09      # UNPACK.S 15: the jump-table slot of INVERTY
FILE_BASE = 0x6000           # the address the pointers in the image files assume


class GfxOracle:
    def __init__(self, source: str):
        self.source = source
        flat = Oracle(source)                        # assembles the game-side modules
        self.modules = dict(flat.modules)
        self.equ = flat.equ
        self.cpu = BankedCpu()
        for asm in flat.modules.values():
            self.cpu.load_bank(asm.origin, asm.image, AUX)
        src = os.path.join(source, SRC)
        resolve = include_resolver(src, os.path.join(source, CP525))
        for name in MAIN_MODULES:
            with open(os.path.join(src, name + ".S"), encoding="ascii") as f:
                asm = assemble_6502(f.read(), resolve)
            self.modules[name] = asm
            self.cpu.load_bank(asm.origin, asm.image, MAIN)
        # UNPACK.S (title-screen unpacking, main language card $EA00) uses
        # macros this project's assembler does not handle and is not loaded.
        # The game reaches one routine in it during play: INVERTY, through
        # the fourth slot of its jump table ($EA09).  UNPACK's INVERTY
        # (UNPACK.S 540-565) is the same code as HIRES's (HIRES.S 2098-2123;
        # a test compares the two texts), so a jump to the latter stands in.
        target = self.modules["HIRES"].symbols["INVERTY"]
        self.cpu.load_bank(UNPACK_INVERTY, bytes([0x4C, target & 0xFF, target >> 8]), MAIN)
        self.cpu.set_altzp(AUX)                      # the state the game runs in
        self.cpu.set_ramrd(AUX)
        self.cpu.ramwrt = AUX
        self._image = self.cpu.snapshot()

    # -- addresses --------------------------------------------------------------------

    def addr(self, name: str, module: Optional[str] = None) -> int:
        table = self.modules[module].symbols if module else self.equ
        if name not in table:
            raise KeyError("unknown symbol %s%s" % (name, " in " + module if module else ""))
        return table[name] & 0xFFFF

    # -- memory -----------------------------------------------------------------------

    def reset(self) -> None:
        self.cpu.restore(self._image)
        self.cpu.cycles = 0

    def keep(self) -> None:
        """Make the present memory (image tables loaded, say) what reset() returns to."""
        self._image = self.cpu.snapshot()

    def load_table(self, table: str, filename: str) -> None:
        """Load an image file where the game keeps that table.

        The image pointers in every repository file assume a load address
        of $6000 (docs/graphics-formats.md), and `setimage` uses them as
        they are (HIRES.S 264-277), so a table kept elsewhere must hold
        relocated pointers.  No game code does that; it is inferred to
        have been done when the disk was built (unknown G4).  Here the
        pointers are moved by the difference of the two addresses."""
        address, bank = TABLES[table]
        with open(os.path.join(self.source, IMAGES, filename), "rb") as f:
            data = bytearray(f.read())
        shift = address - FILE_BASE
        for i in range(data[0] + 1):                 # count + 1 pointers
            at = 1 + 2 * i
            p = (data[at] | (data[at + 1] << 8)) + shift
            data[at], data[at + 1] = p & 0xFF, (p >> 8) & 0xFF
        self.cpu.load_bank(address, bytes(data), bank)

    def load_level(self, data: bytes) -> None:
        self.cpu.load_bank(BLUEPRINT, data, AUX)

    def set_vars(self, values: Dict[str, int]) -> None:
        """Game variables (auxiliary memory)."""
        for name, v in values.items():
            self.cpu.poke(self.addr(name), AUX, v & 0xFF)

    def var(self, name: str) -> int:
        return self.cpu.peek(self.addr(name), AUX)

    def page(self, number: int = 1) -> bytes:
        """The 8192 bytes of a hi-res page (main memory)."""
        return self.cpu.peek(PAGE1 if number == 1 else PAGE2, MAIN, PAGE_SIZE)

    # -- execution --------------------------------------------------------------------

    def call(self, name: str, a: int = 0, x: int = 0, y: int = 0,
             module: Optional[str] = None, max_cycles: int = 20_000_000):
        cpu = self.cpu
        cpu.a, cpu.x, cpu.y = a & 0xFF, x & 0xFF, y & 0xFF
        cpu.sp = 0xFF
        cpu.p = (cpu.p & 0x7D) | (cpu.a & 0x80) | (0x02 if cpu.a == 0 else 0)
        cpu.call(self.addr(name, module), max_cycles)
        return cpu.a, cpu.x, cpu.y, cpu.p
