#!/usr/bin/env python3
"""Compare the C effect renderer with the Python one, sample by sample.

New code for the pop-study project.  The Python side re-runs the original
sound module in the 6502 simulator; the C side reads the generated sfx.bin.
So this checks the exported data and the C renderer together.

Usage:
    compare_sfx.py <pop_sfxdump> <sfx.bin> <original checkout> [rate]
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "extract_audio"))

from pop_sfx import SoundModule, render_pcm  # noqa: E402

TOLERANCE = 2e-6


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    dump, sfxbin, source = argv[1:4]
    rate = int(argv[4]) if len(argv) > 4 else 44100
    mod = SoundModule(os.path.join(source, "01 POP Source", "Source"))
    out = subprocess.run([dump, sfxbin, str(rate)], capture_output=True, text=True)
    if out.returncode != 0:
        print("pop_sfxdump failed:\n" + out.stderr)
        return 1
    lines = iter(out.stdout.splitlines())
    total = 0
    worst = 0.0
    for e in mod.all_effects():
        head = next(lines).split()
        want = render_pcm(e.toggles, e.total_cycles, rate)
        if head[:2] != ["effect", str(e.number)] or int(head[2]) != len(want):
            print("effect %d: header %r, expected %d samples" % (e.number, head, len(want)))
            return 1
        end = 1 if len(e.toggles) % 2 else -1
        if int(head[3]) != end:
            print("effect %d: end level %s, expected %d" % (e.number, head[3], end))
            return 1
        for i, w in enumerate(want):
            got = float(next(lines))
            worst = max(worst, abs(got - w))
            if abs(got - w) > TOLERANCE:
                print("effect %d sample %d: C %.7f, Python %.7f" % (e.number, i, got, w))
                return 1
        total += len(want)
    if next(lines, None) is not None:
        print("pop_sfxdump printed more than expected")
        return 1
    print("%d samples over %d effects at %d Hz: identical within %.0e (worst %.1e)"
          % (total, mod.count, rate, TOLERANCE, worst))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
