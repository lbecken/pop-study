"""A 6502 with the Apple IIe's two 64K memory banks.

New code for the pop-study project; contains no original code or data.

The game keeps its code and data in auxiliary memory and its blitters and
hi-res pages in main memory, and crosses between them with the soft
switches (MASTER.S 171-180, HIRES.S 84-146).  `BankedCpu` models the three
switch pairs the game uses:

    $C008 / $C009   ALTZP off / on    zero page, stack and $D000-$FFFF
    $C002 / $C003   RAMRD off / on    reads of   $0200-$BFFF
    $C004 / $C005   RAMWRT off / on   writes to  $0200-$BFFF

"off" selects main memory, "on" auxiliary memory.  The switches act when
written to (the game always uses `sta`).

Not modelled: the two $D000-$DFFF banks of each language card and the
ROM/RAM switches at $C080-$C08F (the game runs with RAM selected throughout),
80STORE and the display switches, and the I/O space itself.

How it works: `mem` always holds what the processor would read, so the
simulator's instruction fetch and addressing need no change.  The bank that
is not being read is kept in shadow arrays; a switch swaps the region in.
Only when RAMRD and RAMWRT differ do writes to $0200-$BFFF go to the shadow.
"""

from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "extract_assets"))

from mos6502 import Cpu6502  # noqa: E402

LOW_END = 0x0200             # zero page and stack: $0000-$01FF
RAM_END = 0xC000             # $0200-$BFFF
LC_START = 0xD000            # language card: $D000-$FFFF
MAIN, AUX = False, True


class BankedCpu(Cpu6502):
    def __init__(self):
        super().__init__()
        self._low = bytearray(LOW_END)               # the bank ALTZP does not select
        self._lc = bytearray(0x10000 - LC_START)
        self._ram = bytearray(RAM_END - LOW_END)     # the bank RAMRD does not select
        self.altzp = MAIN
        self.ramrd = MAIN
        self.ramwrt = MAIN

    # -- switches -----------------------------------------------------------------

    def set_altzp(self, bank: bool) -> None:
        if bank != self.altzp:
            self.mem[:LOW_END], self._low[:] = self._low, self.mem[:LOW_END]
            self.mem[LC_START:], self._lc[:] = self._lc, self.mem[LC_START:]
            self.altzp = bank

    def set_ramrd(self, bank: bool) -> None:
        if bank != self.ramrd:
            self.mem[LOW_END:RAM_END], self._ram[:] = self._ram, self.mem[LOW_END:RAM_END]
            self.ramrd = bank

    def _switch(self, a: int) -> None:
        if a == 0xC002:
            self.set_ramrd(MAIN)
        elif a == 0xC003:
            self.set_ramrd(AUX)
        elif a == 0xC004:
            self.ramwrt = MAIN
        elif a == 0xC005:
            self.ramwrt = AUX
        elif a == 0xC008:
            self.set_altzp(MAIN)
        elif a == 0xC009:
            self.set_altzp(AUX)

    def _wr(self, a: int, v: int) -> None:
        a &= 0xFFFF
        if 0xC000 <= a <= 0xC0FF:
            self._switch(a)
        elif LOW_END <= a < RAM_END and self.ramwrt != self.ramrd:
            self._ram[a - LOW_END] = v & 0xFF
        else:
            self.mem[a] = v & 0xFF
        if a in self.on_write:
            self._pending.append(self.on_write[a])

    # -- access from outside, by bank ---------------------------------------------------

    def _where(self, a: int, bank: bool):
        """(array, index) holding address `a` of the given bank."""
        if a < LOW_END:
            return (self.mem, a) if bank == self.altzp else (self._low, a)
        if a >= LC_START:
            return (self.mem, a) if bank == self.altzp else (self._lc, a - LC_START)
        if a < RAM_END:
            return (self.mem, a) if bank == self.ramrd else (self._ram, a - LOW_END)
        return self.mem, a                           # $C000-$CFFF: not banked here

    def peek(self, a: int, bank: bool, length: int = 0):
        if not length:
            array, i = self._where(a, bank)
            return array[i]
        return bytes(self.peek(a + k, bank) for k in range(length))

    def poke(self, a: int, bank: bool, value) -> None:
        if isinstance(value, int):
            value = bytes([value])
        for k, v in enumerate(value):
            array, i = self._where(a + k, bank)
            array[i] = v

    def load_bank(self, address: int, data: bytes, bank: bool) -> None:
        self.poke(address, bank, data)

    def snapshot(self):
        return (bytes(self.mem), bytes(self._low), bytes(self._lc), bytes(self._ram),
                self.altzp, self.ramrd, self.ramwrt)

    def restore(self, state) -> None:
        mem, low, lc, ram, self.altzp, self.ramrd, self.ramwrt = state
        self.mem[:] = mem
        self._low[:] = low
        self._lc[:] = lc
        self._ram[:] = ram
