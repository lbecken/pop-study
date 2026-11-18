"""A deliberately small assembler for Merlin *data* files.

New code for the pop-study project; contains no original code or data.

It understands just enough of the Merlin dialect to turn the original
FRAMEDEF.S and SEQTABLE.S (which contain only data directives) into the byte
image the game used, plus a symbol table and a per-line listing.  It is not a
6502 assembler: any mnemonic it does not know is an error, never a guess.

Supported:
    label = expr            equate
    org expr                set the location counter
    dum expr / dend         dummy section: labels and `ds` advance a private
                            counter and emit nothing
    ds expr                 reserve bytes (zero-filled outside `dum`)
    db / dfb expr,...       bytes (8-bit two's complement wraparound)
    dw / da expr,...        16-bit little-endian words
    hex 0A1B,2C             raw hex bytes
    do expr / else / fin    conditional assembly
    lup expr ... --^        repeat the enclosed lines expr times
    ]name = expr            variable: may be reassigned, evaluated in order
    lst, lstdo, tr, usr, xc ignored (listing / disk-write controls)

Labels start in column 0.  `:name` labels are local to the most recent
global label.  Comments are lines starting with `*` or `;`, and anything
after the operand field.

Expressions are evaluated strictly left to right with no operator
precedence, as Merlin does: `$c0+$20+6`, `-2`, `altset1-*`.  `*` in operand
position is the current location counter.  A leading `#` or `<` selects the
low byte of the value and `>` (or `#>`) the high byte.  Without a prefix a
`db` value must fit in a byte; this is stricter than Merlin, on purpose.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

IGNORED_OPS = {"lst", "lstdo", "tr", "usr", "xc"}
_LABEL_RE = re.compile(r"^(:[A-Za-z0-9_?.]+|\]?[A-Za-z_][A-Za-z0-9_?.]*)$")
_TERM_RE = re.compile(r"""
    \$(?P<hex>[0-9A-Fa-f]+)
  | %(?P<bin>[01]+)
  | (?P<dec>[0-9]+)
  | (?P<sym>:[A-Za-z0-9_?.]+|\]?[A-Za-z_][A-Za-z0-9_?.]*)
  | (?P<pc>\*)
""", re.X)


class MerlinError(ValueError):
    def __init__(self, message: str, line_no: Optional[int] = None):
        self.line_no = line_no
        super().__init__(("line %d: " % line_no if line_no else "") + message)


class Undefined(Exception):
    """A symbol was not defined yet (legal in pass 1 for db/dw operands)."""


@dataclass
class Line:
    """One emitting source line."""
    line_no: int
    address: int
    data: bytes
    label: Optional[str]        # qualified label defined on this line, if any
    op: str
    operand: str
    comment: str


@dataclass
class Assembly:
    origin: int
    image: bytes                          # bytes from origin, gaps zero-filled
    symbols: Dict[str, int]               # global labels and equates
    locals: Dict[Tuple[str, str], int]    # (global scope, ":name") -> value
    lines: List[Line] = field(default_factory=list)        # lines that emit bytes
    label_lines: List[Line] = field(default_factory=list)  # lines defining an address label

    @property
    def end(self) -> int:
        return self.origin + len(self.image)

    def labels_at(self, address: int) -> List[str]:
        """Address labels (not equates) defined at this address, in file order."""
        return [l.label for l in self.label_lines if l.address == address]

    def byte(self, address: int) -> int:
        return self.image[address - self.origin]


def split_line(raw: str):
    """Return (label, op, operand, comment); all '' for blank/comment lines."""
    text = raw.rstrip("\r\n")
    if not text.strip() or text[0] in "*;":
        return "", "", "", text.strip()
    label = ""
    rest = text
    if not text[0].isspace():
        parts = text.split(None, 1)
        label = parts[0]
        rest = parts[1] if len(parts) > 1 else ""
    rest = rest.strip()
    if not rest or rest[0] == ";":
        return label, "", "", rest[1:].strip() if rest else ""
    parts = rest.split(None, 1)
    op = parts[0].lower()
    tail = parts[1].strip() if len(parts) > 1 else ""
    if tail.startswith(";"):
        return label, op, "", tail[1:].strip()
    parts = tail.split(None, 1)
    operand = parts[0] if parts else ""
    comment = parts[1].strip() if len(parts) > 1 else ""
    if comment.startswith(";"):
        comment = comment[1:].strip()
    return label, op, operand, comment


class _Assembler:
    def __init__(self, text: str):
        self.source = self._expand_lup(text.splitlines())
        self.symbols: Dict[str, int] = {}
        self.locals: Dict[Tuple[str, str], int] = {}

    @staticmethod
    def _expand_lup(raw_lines: List[str]) -> List[Tuple[int, str]]:
        """Unroll `lup n` ... `--^` blocks.  The count must be a literal."""
        out: List[Tuple[int, str]] = []
        i = 0
        while i < len(raw_lines):
            _, op, operand, _ = split_line(raw_lines[i])
            if op == "--^":
                raise MerlinError("--^ without lup", i + 1)
            if op != "lup":
                out.append((i + 1, raw_lines[i]))
                i += 1
                continue
            if not re.fullmatch(r"[0-9]+|\$[0-9A-Fa-f]+", operand):
                raise MerlinError("lup count must be a literal, got %r" % operand, i + 1)
            count = int(operand[1:], 16) if operand.startswith("$") else int(operand)
            body: List[Tuple[int, str]] = []
            j = i + 1
            while True:
                if j >= len(raw_lines):
                    raise MerlinError("lup without --^", i + 1)
                _, op2, _, _ = split_line(raw_lines[j])
                if op2 == "lup":
                    raise MerlinError("nested lup is not supported", j + 1)
                if op2 == "--^":
                    break
                body.append((j + 1, raw_lines[j]))
                j += 1
            out.extend(body * count)
            i = j + 1
        return out

    # -- expression evaluation ------------------------------------------------

    def lookup(self, name: str, scope: str) -> int:
        if name.startswith(":"):
            key = (scope, name)
            if key not in self.locals:
                raise Undefined(name)
            return self.locals[key]
        if name not in self.symbols:
            raise Undefined(name)
        return self.symbols[name]

    def evaluate(self, expr: str, pc: int, scope: str) -> int:
        s = expr.strip()
        if s.startswith("#"):
            # `#expr` = low byte, `#>expr` = high byte (as in `dfb #TABLE-$80`)
            s = s[1:]
            if s[:1] not in ("<", ">"):
                return self.evaluate(s, pc, scope) & 0xFF
        if s[:1] in ("<", ">"):
            value = self.evaluate(s[1:], pc, scope)
            return (value >> 8) & 0xFF if s[0] == ">" else value & 0xFF
        if not s:
            raise MerlinError("empty expression")
        pos, value, pending = 0, 0, "+"
        expect_term = True
        while pos < len(s):
            ch = s[pos]
            if expect_term:
                sign = 1
                while ch in "+-":
                    if ch == "-":
                        sign = -sign
                    pos += 1
                    if pos >= len(s):
                        raise MerlinError("dangling sign in %r" % expr)
                    ch = s[pos]
                m = _TERM_RE.match(s, pos)
                if not m:
                    raise MerlinError("cannot parse %r at %r" % (expr, s[pos:]))
                if m.group("hex") is not None:
                    term = int(m.group("hex"), 16)
                elif m.group("bin") is not None:
                    term = int(m.group("bin"), 2)
                elif m.group("dec") is not None:
                    term = int(m.group("dec"))
                elif m.group("pc") is not None:
                    term = pc
                else:
                    term = self.lookup(m.group("sym"), scope)
                term *= sign
                if pending == "+":
                    value += term
                elif pending == "-":
                    value -= term
                elif pending == "*":
                    value *= term
                else:
                    if term == 0:
                        raise MerlinError("division by zero in %r" % expr)
                    value = int(value / term)
                pos = m.end()
                expect_term = False
            else:
                if ch not in "+-*/":
                    raise MerlinError("unexpected %r in %r" % (ch, expr))
                pending = ch
                pos += 1
                expect_term = True
        if expect_term:
            raise MerlinError("expression ends with an operator: %r" % expr)
        return value

    # -- passes ---------------------------------------------------------------

    def run(self) -> Assembly:
        self._pass(final=False)
        return self._pass(final=True)

    def _pass(self, final: bool) -> Optional[Assembly]:
        pc = 0                 # real location counter
        dpc = 0                # dummy-section counter
        in_dum = False
        origin: Optional[int] = None
        chunks: List[Tuple[int, bytes]] = []
        lines: List[Line] = []
        label_lines: List[Line] = []
        scope = ""
        cond_stack: List[bool] = []

        for n, raw in self.source:
            try:
                label, op, operand, comment = split_line(raw)
                if op in ("do", "else", "fin"):
                    if op == "do":
                        active = all(cond_stack) and self.evaluate(operand, pc, scope) != 0
                        cond_stack.append(active)
                    elif op == "else":
                        if not cond_stack:
                            raise MerlinError("else without do")
                        cond_stack[-1] = not cond_stack[-1]
                    else:
                        if not cond_stack:
                            raise MerlinError("fin without do")
                        cond_stack.pop()
                    continue
                if cond_stack and not all(cond_stack):
                    continue
                if not label and not op:
                    continue
                if label and not _LABEL_RE.match(label):
                    raise MerlinError("bad label %r" % label)

                here = dpc if in_dum else pc

                if op == "=":
                    if not label:
                        raise MerlinError("equate without a label")
                    self._define(label, self.evaluate(operand, here, scope), scope, final)
                    if label[0] not in ":]":
                        scope = label
                    continue

                qualified = None
                if label:
                    self._define(label, here, scope, final)
                    if label.startswith(":"):
                        qualified = "%s%s" % (scope, label)
                    elif label.startswith("]"):
                        qualified = None
                    else:
                        scope = label
                        qualified = label

                data = b""
                if op == "":
                    pass
                elif op in IGNORED_OPS:
                    pass
                elif op == "org":
                    pc = self.evaluate(operand, here, scope)
                    if origin is None:
                        origin = pc
                    in_dum = False
                elif op == "dum":
                    dpc = self.evaluate(operand, here, scope)
                    in_dum = True
                elif op == "dend":
                    in_dum = False
                elif op == "ds":
                    count = self.evaluate(operand, here, scope)
                    if count < 0:
                        raise MerlinError("negative ds count %d" % count)
                    if in_dum:
                        dpc += count
                    else:
                        data = bytes(count)
                elif op in ("db", "dfb", "dw", "da"):
                    width = 1 if op in ("db", "dfb") else 2
                    items = [x for x in operand.split(",")]
                    if not operand or any(x == "" for x in items):
                        raise MerlinError("bad operand list %r" % operand)
                    out = bytearray()
                    for item in items:
                        try:
                            v = self.evaluate(item, here, scope)
                        except Undefined as u:
                            if final:
                                raise MerlinError("undefined symbol %s" % u)
                            v = 0
                        if width == 1:
                            if not -128 <= v <= 255:
                                raise MerlinError("value %d does not fit a byte" % v)
                            out.append(v & 0xFF)
                        else:
                            if not -32768 <= v <= 65535:
                                raise MerlinError("value %d does not fit a word" % v)
                            out += bytes(((v & 0xFF), (v >> 8) & 0xFF))
                    data = bytes(out)
                elif op == "hex":
                    digits = operand.replace(",", "")
                    if len(digits) % 2 or not re.fullmatch(r"[0-9A-Fa-f]*", digits):
                        raise MerlinError("bad hex data %r" % operand)
                    data = bytes.fromhex(digits)
                else:
                    raise MerlinError("unsupported opcode %r" % op)

                if in_dum and data:
                    raise MerlinError("data emitted inside a dummy section")

                line = Line(n, here, data, qualified, op, operand, comment)
                if qualified and not in_dum:
                    label_lines.append(line)
                if data:
                    if origin is None:
                        raise MerlinError("data before any org")
                    chunks.append((pc, data))
                    lines.append(line)
                    pc += len(data)
            except Undefined as u:
                raise MerlinError("undefined symbol %s" % u, n)
            except MerlinError as e:
                if e.line_no is None:
                    raise MerlinError(str(e), n)
                raise

        if cond_stack:
            raise MerlinError("do without fin at end of file")
        if not final:
            return None
        if origin is None:
            raise MerlinError("no org directive")
        low = min([origin] + [a for a, _ in chunks])
        high = max([origin] + [a + len(d) for a, d in chunks])
        image = bytearray(high - low)
        for a, d in chunks:
            image[a - low:a - low + len(d)] = d
        return Assembly(low, bytes(image), dict(self.symbols), dict(self.locals),
                        lines, label_lines)

    def _define(self, label: str, value: int, scope: str, final: bool):
        if label.startswith("]"):
            self.symbols[label] = value          # variables may be reassigned
        elif label.startswith(":"):
            key = (scope, label)
            if not final and key in self.locals:
                raise MerlinError("duplicate local label %s in scope %s" % (label, scope))
            self.locals[key] = value
        else:
            if not final and label in self.symbols:
                raise MerlinError("duplicate label %s" % label)
            self.symbols[label] = value


def assemble(text: str) -> Assembly:
    """Assemble Merlin data source text.  Raises MerlinError on anything odd."""
    return _Assembler(text).run()


def parse_equates(text: str) -> Dict[str, int]:
    """Collect `name = expr` equates from a file that may contain other things.

    Used for SEQDATA.S, which holds sequence names.  Equates whose value
    cannot be evaluated with the names seen so far are skipped.
    """
    a = _Assembler("")
    out: Dict[str, int] = {}
    for raw in text.splitlines():
        label, op, operand, _ = split_line(raw)
        if op != "=" or not label or label.startswith(":"):
            continue
        try:
            value = a.evaluate(operand, 0, "")
        except (Undefined, MerlinError):
            continue
        a.symbols[label] = value
        out[label] = value
    return out
