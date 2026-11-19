#!/usr/bin/env python3
"""Compare the C sequence interpreter with the Python decoder.

New code for the pop-study project.

The two implementations share nothing: the C port (pop_seq.c) interprets the
table bytes directly; the Python side (tools/extract_assets/pop_animdata.py)
disassembles the table and traces it.  This script runs `pop_seqdump` for
every entry point and checks every tick against a state machine built here
on top of the Python trace.

Usage:
    compare_seq_traces.py <pop_seqdump> <seqtable.bin> <original checkout> [ticks]
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "extract_assets"))

from merlin_data import assemble  # noqa: E402
from pop_animdata import decode_sequence_table, trace  # noqa: E402

FOOTSTEP, SMACKWALL = 9, 13


def expected(table, entry_addr, ticks, face):
    """Yield the line pop_seqdump should print for each tick."""
    tr = trace(table, entry_addr)
    x, y, block_y, action, xv, yv = 128, 100, 1, 0, 0, 0
    i = 0
    for _ in range(ticks):
        k = tr.ticks[i]
        jar, alert, events = 0, 0, ""
        for op in k.ops:
            a = op.args
            if op.op == "chx":
                x = (x + (a[0] if face == 0 else -a[0])) & 0xFF
            elif op.op == "chy":
                y = (y + a[0]) & 0xFF
            elif op.op == "aboutface":
                face ^= 0xFF
            elif op.op == "up":
                block_y = (block_y - 1) & 0xFF
                events += "c"
            elif op.op == "down":
                block_y = (block_y + 1) & 0xFF
                events += "c"
            elif op.op == "act":
                action = a[0]
            elif op.op == "setfall":
                xv, yv = a[0] & 0xFF, a[1] & 0xFF
            elif op.op == "jaru":
                jar = 1
            elif op.op == "jard":
                jar = 0xFF
            elif op.op == "tap":
                if a[0] == 1:
                    events += "s%d" % FOOTSTEP
                elif a[0] == 2:
                    events += "s%d" % SMACKWALL
                if a[0] <= 2:
                    alert = 1
            elif op.op == "effect":
                if a[0] == 1:
                    events += "p"
            elif op.op == "nextlevel":
                events += "u"
            elif op.op in ("goto", "ifwtless", "die"):
                pass
            else:
                raise SystemExit("unhandled instruction %s" % op.op)
        i += 1
        if i >= len(tr.ticks):
            i = tr.loop_to
        next_start = tr.ticks[i].start
        yield (k.frame, x, y, face, block_y, action, xv, yv, next_start, jar, alert,
               events or "-")


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    dump, seqbin, source = argv[1:4]
    ticks = int(argv[4]) if len(argv) > 4 else 200
    with open(os.path.join(source, "01 POP Source", "Source", "SEQTABLE.S"),
              encoding="ascii") as f:
        table = decode_sequence_table(assemble(f.read()))

    compared = 0
    for face_name, face in (("left", 0xFF), ("right", 0x00)):
        out = subprocess.run([dump, seqbin, str(ticks), face_name],
                             capture_output=True, text=True)
        if out.returncode != 0:
            print("pop_seqdump failed:\n" + out.stdout[-2000:] + out.stderr)
            return 1
        lines = iter(out.stdout.splitlines())
        for n, addr in enumerate(table.entries, 1):
            for tick, want in enumerate(expected(table, addr, ticks, face), 1):
                got = next(lines, "").split()
                exp = [str(n), str(tick)] + [str(v) for v in want]
                if got != exp:
                    print("MISMATCH facing %s, entry %d, tick %d" % (face_name, n, tick))
                    print("  C     : " + " ".join(got))
                    print("  Python: " + " ".join(exp))
                    return 1
                compared += 1
        if next(lines, None) is not None:
            print("pop_seqdump printed more lines than expected")
            return 1
    print("%d ticks compared over %d entries x 2 facings: identical"
          % (compared, len(table.entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
