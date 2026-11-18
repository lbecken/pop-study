"""Decoders for Prince of Persia (Apple II) frame definitions and sequences.

New code for the pop-study project; contains no original code or data.
Input is the byte image produced by merlin_data.assemble() from the
original FRAMEDEF.S / SEQTABLE.S.

Evidence (original repository @ 25243550, "01 POP Source/Source" unless
another path is given):

Frame definition, 5 bytes (Fimage, Fsword, Fdx, Fdy, Fcheck):
    layout        FRAMEDEF.S lines 8-21; GETFRAMEINFO, CTRLSUBS.S 1648-1674
    indexing      (frame-1)*5, getfindex, CTRLSUBS.S 941-964
    bit meanings  "04 Support/MakeDisk/S/COMMENTS.S" lines 346-400
    table number  decodeim, CTRLSUBS.S 1017-1039
    sword entry   3 bytes (image, dx, dy), getswordframe CTRLSUBS.S 974-1005,
                  SETUPSWORD 852-895; sword images come from chtable3
                  (decodeswim, CTRLSUBS.S 1050-1055)

Sequence byte-code:
    interpreter   ANIMCHAR, COLL.S lines 994-1156
    codes         SEQTABLE.S lines 10-24
    entry table   JUMPSEQ, CTRLSUBS.S 700-711: word (n-1) of the table
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

FRAME_SIZE = 5
SWORD_SIZE = 3

# Operand byte counts are taken from ANIMCHAR, not from the table text.
#   name: (code, operand bytes, operand kind)
INSTRUCTIONS: Dict[str, Tuple[int, int, str]] = {
    "goto":      (-1, 2, "address"),
    "aboutface": (-2, 0, ""),
    "up":        (-3, 0, ""),
    "down":      (-4, 0, ""),
    "chx":       (-5, 1, "signed"),
    "chy":       (-6, 1, "signed"),
    "act":       (-7, 1, "unsigned"),
    "setfall":   (-8, 2, "signed"),
    "ifwtless":  (-9, 2, "address"),
    "die":       (-10, 0, ""),
    "jaru":      (-11, 0, ""),
    "jard":      (-12, 0, ""),
    "effect":    (-13, 1, "unsigned"),
    "tap":       (-14, 1, "unsigned"),
    "nextlevel": (-15, 0, ""),
}
_BY_BYTE = {code & 0xFF: (name, n, kind) for name, (code, n, kind) in INSTRUCTIONS.items()}

MAX_OPS_PER_TICK = 64
MAX_TICKS = 2000


class AnimDataError(ValueError):
    pass


def s8(b: int) -> int:
    """Interpret a byte as signed 8-bit."""
    return b - 256 if b >= 128 else b


# --- frame definitions ---------------------------------------------------------

@dataclass(frozen=True)
class Frame:
    number: int
    raw: bytes
    image: int          # 0-127; 0 = no image
    table: int          # 0-7  (0 = chtable1 ... 6 = chtable7)
    sword: int          # 0-63 index into the sword table; 0 = none
    dx: int             # signed, 140-wide units, + = forward
    dy: int             # signed, + = down
    odd: bool           # Fcheck bit 7
    floor_check: bool   # Fcheck bit 6
    thin: bool          # Fcheck bit 5
    foot: int           # Fcheck bits 0-4
    name: str = ""      # trailing comment in the source, if any

    @property
    def empty(self) -> bool:
        return not any(self.raw)

    def to_json(self) -> dict:
        return {
            "frame": self.number,
            "name": self.name,
            "raw": self.raw.hex(),
            "empty": self.empty,
            "image": self.image,
            "table": self.table,
            "chtable": "chtable%d" % (self.table + 1),
            "sword": self.sword,
            "dx": self.dx,
            "dy": self.dy,
            "odd": self.odd,
            "floor_check": self.floor_check,
            "thin": self.thin,
            "foot": self.foot,
        }


def decode_frame(number: int, raw: bytes, name: str = "") -> Frame:
    if len(raw) != FRAME_SIZE:
        raise AnimDataError("frame %d: need %d bytes" % (number, FRAME_SIZE))
    fimage, fsword, fdx, fdy, fcheck = raw
    table = ((fimage & 0x80) >> 5) | ((fsword & 0xC0) >> 6)
    return Frame(
        number=number, raw=bytes(raw),
        image=fimage & 0x7F, table=table, sword=fsword & 0x3F,
        dx=s8(fdx), dy=s8(fdy),
        odd=bool(fcheck & 0x80), floor_check=bool(fcheck & 0x40),
        thin=bool(fcheck & 0x20), foot=fcheck & 0x1F, name=name,
    )


@dataclass(frozen=True)
class Sword:
    number: int
    image: int      # image number in chtable3; 0 = none
    dx: int
    dy: int
    name: str = ""

    def to_json(self) -> dict:
        return {"sword": self.number, "name": self.name, "image": self.image,
                "dx": self.dx, "dy": self.dy}


@dataclass
class FrameData:
    main: List[Frame]       # frames 1..240
    altset1: List[Frame]    # frames 150..189 for opponents
    altset2: List[Frame]    # frames 1..90 for princess-scene characters
    swords: List[Sword]     # sword frames 1..64


def decode_frame_tables(asm) -> FrameData:
    """asm: merlin_data.Assembly of FRAMEDEF.S."""
    sym = asm.symbols
    for need in ("Fdef", "altset1", "altset2", "swordtab"):
        if need not in sym:
            raise AnimDataError("FRAMEDEF symbol %s not found" % need)
    comments = {l.address: l.comment for l in asm.lines}

    def frames(start, end, first_number):
        if (end - start) % FRAME_SIZE:
            raise AnimDataError("frame area $%04X-$%04X not a multiple of 5" % (start, end))
        out = []
        for i in range((end - start) // FRAME_SIZE):
            a = start + i * FRAME_SIZE
            if a + FRAME_SIZE > asm.end:
                break
            raw = asm.image[a - asm.origin:a - asm.origin + FRAME_SIZE]
            out.append(decode_frame(first_number + i, raw, comments.get(a, "")))
        return out

    main = frames(sym["Fdef"], sym["altset1"], 1)
    alt1 = frames(sym["altset1"], sym["altset2"], 150)
    alt2 = frames(sym["altset2"], sym["swordtab"], 1)
    swords = []
    a, n = sym["swordtab"], 1
    last_data = max(l.address + len(l.data) for l in asm.lines if l.op != "ds")
    while a + SWORD_SIZE <= last_data:
        img, dx, dy = asm.image[a - asm.origin:a - asm.origin + SWORD_SIZE]
        swords.append(Sword(n, img, s8(dx), s8(dy), comments.get(a, "")))
        a += SWORD_SIZE
        n += 1
    return FrameData(main, alt1, alt2, swords)


# --- sequence table ------------------------------------------------------------

@dataclass(frozen=True)
class Item:
    """One decoded element of the sequence table."""
    address: int
    op: str                 # "frame" or an instruction name
    args: Tuple[int, ...]   # frame number, or decoded operands
    size: int

    def to_json(self, labels: Dict[int, List[str]]) -> dict:
        d = {"addr": "$%04X" % self.address, "op": self.op}
        if self.address in labels:
            d["labels"] = labels[self.address]
        if self.op == "frame":
            d["frame"] = self.args[0]
        elif self.op in ("goto", "ifwtless"):
            d["target"] = "$%04X" % self.args[0]
            if self.args[0] in labels:
                d["target_label"] = labels[self.args[0]][0]
        elif self.args:
            d["args"] = list(self.args)
        return d


@dataclass
class Tick:
    """What ANIMCHAR does in one call: instructions, then one frame number."""
    start: int
    ops: List[Item]
    frame: int

    @property
    def dx(self) -> int:
        return sum(i.args[0] for i in self.ops if i.op == "chx")

    @property
    def dy(self) -> int:
        return sum(i.args[0] for i in self.ops if i.op == "chy")


@dataclass
class Trace:
    ticks: List[Tick]
    loop_to: int            # index of the tick the sequence returns to
    conditional: bool       # an ifwtless was passed (not-taken path followed)


@dataclass
class SequenceTable:
    origin: int
    entries: List[int]                  # entry n (1-based) -> address
    items: List[Item]
    by_address: Dict[int, Item]
    labels: Dict[int, List[str]]        # address -> label names
    end: int
    problems: List[str] = field(default_factory=list)


def _read_item(image: bytes, origin: int, address: int) -> Item:
    off = address - origin
    b = image[off]
    if b not in _BY_BYTE:
        return Item(address, "frame", (b,), 1)
    name, n, kind = _BY_BYTE[b]
    if off + 1 + n > len(image):
        raise AnimDataError("instruction %s at $%04X runs past the table" % (name, address))
    ops = image[off + 1:off + 1 + n]
    if kind == "address":
        args: Tuple[int, ...] = (ops[0] | (ops[1] << 8),)
    elif kind == "signed":
        args = tuple(s8(x) for x in ops)
    else:
        args = tuple(ops)
    return Item(address, name, args, 1 + n)


def decode_sequence_table(asm) -> SequenceTable:
    """asm: merlin_data.Assembly of SEQTABLE.S.

    Works from the assembled bytes; the source text only supplies label
    names and the extent of the data.
    """
    image, origin = asm.image, asm.origin
    data_lines = [l for l in asm.lines if l.op in ("db", "dfb", "dw", "da", "hex")]
    if not data_lines:
        raise AnimDataError("no data in sequence table")
    end = max(l.address + len(l.data) for l in data_lines)

    # Entry table: 16-bit pointers up to the lowest address any of them names.
    entries: List[int] = []
    a = origin
    lowest = end
    while a < lowest:
        ptr = image[a - origin] | (image[a - origin + 1] << 8)
        entries.append(ptr)
        lowest = min(lowest, ptr)
        a += 2
    seq_start = a

    labels: Dict[int, List[str]] = {}
    for l in asm.label_lines:
        if seq_start <= l.address <= end:
            labels.setdefault(l.address, []).append(l.label)

    items: List[Item] = []
    a = seq_start
    while a < end:
        item = _read_item(image[:end - origin], origin, a)
        items.append(item)
        a += item.size
    by_address = {i.address: i for i in items}

    table = SequenceTable(origin, entries, items, by_address, labels, end)
    p = table.problems
    if a != end:
        p.append("linear decode overran the data: ended at $%04X, data ends $%04X" % (a, end))
    for n, ptr in enumerate(entries, 1):
        if ptr not in by_address:
            p.append("entry %d points to $%04X, not an instruction boundary" % (n, ptr))
    for addr, names in sorted(labels.items()):
        if addr not in by_address and addr != end:
            p.append("label %s at $%04X is inside an instruction" % (names[0], addr))
    for i in items:
        if i.op in ("goto", "ifwtless") and i.args[0] not in by_address:
            p.append("%s at $%04X targets $%04X, not an instruction boundary"
                     % (i.op, i.address, i.args[0]))
    return table


def trace(table: SequenceTable, start: int) -> Trace:
    """Follow a sequence the way ANIMCHAR would, with weightless = 0.

    Returns the ticks up to the point where the sequence re-enters a tick
    it has already produced, and the index of that tick.
    """
    seen: Dict[int, int] = {}
    ticks: List[Tick] = []
    conditional = False
    pc = start
    while len(ticks) < MAX_TICKS:
        if pc in seen:
            return Trace(ticks, seen[pc], conditional)
        seen[pc] = len(ticks)
        tick_start, ops = pc, []
        while True:
            if pc not in table.by_address:
                raise AnimDataError("trace from $%04X left the table at $%04X" % (start, pc))
            item = table.by_address[pc]
            if item.op == "frame":
                ticks.append(Tick(tick_start, ops, item.args[0]))
                pc += item.size
                break
            ops.append(item)
            if len(ops) > MAX_OPS_PER_TICK:
                raise AnimDataError("no frame within %d instructions from $%04X"
                                    % (MAX_OPS_PER_TICK, tick_start))
            if item.op == "goto":
                pc = item.args[0]
            else:
                if item.op == "ifwtless":
                    conditional = True
                pc += item.size
    raise AnimDataError("trace from $%04X did not loop within %d ticks" % (start, MAX_TICKS))
