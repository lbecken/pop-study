#!/usr/bin/env python3
"""Extract the sound effects: event data, a table and WAV previews.

New code for the pop-study project.  Reads a local checkout of the original
repository, runs its sound module in a 6502 simulator, and writes derived
files to a git-ignored directory.

Usage:
    python3 tools/extract_audio/extract_sfx.py \
        --source /path/to/Prince-of-Persia-Apple-II --out build/audio

Output:
    sfx.json            per effect: speaker toggle times in CPU cycles,
                        frequency segments, length, share of a game tick
    sfx.csv             one line per effect
    sfx_NN_name.wav     44.1 kHz previews of the 1-bit speaker signal
    sfx_all.wav         all effects in sequence, 250 ms apart
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pop_sfx import (CPU_HZ, NOMINAL_TICKS_PER_MINUTE, TICK_CYCLES, SoundModule,  # noqa: E402
                     render_pcm, tone_half_period, wav_bytes)

SRC = os.path.join("01 POP Source", "Source")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--source", required=True)
    ap.add_argument("--out", default=os.path.join("build", "audio"))
    ap.add_argument("--rate", type=int, default=44100)
    args = ap.parse_args(argv)

    mod = SoundModule(os.path.join(args.source, SRC))
    effects = mod.all_effects()
    os.makedirs(args.out, exist_ok=True)

    with open(os.path.join(args.out, "sfx.json"), "w") as f:
        json.dump({
            "note": "Derived by executing the original SOUND.S; not redistributed "
                    "by pop-study.",
            "cpu_hz": CPU_HZ,
            "nominal_ticks_per_minute": NOMINAL_TICKS_PER_MINUTE,
            "nominal_tick_cycles": round(TICK_CYCLES, 1),
            "module": {"origin": "$%04X" % mod.asm.origin, "size": len(mod.asm.image),
                       "tone": "$%04X" % mod.sym["tone"]},
            "effects": [e.to_json() for e in effects],
        }, f, indent=1)
        f.write("\n")

    lines = ["number,name,routine,speaker_accesses,segments,total_cycles,total_ms,tick_share"]
    print("%2s %-13s %-15s %4s  %-34s %8s %8s %6s" % (
        "#", "name", "routine", "acc", "half-period x intervals (Hz)", "cycles", "ms", "tick"))
    everything, gap = [], [0.0] * int(0.25 * args.rate)
    for e in effects:
        segs = e.segments()
        seg_text = " + ".join("%dx%d (%.0f)" % (s["half_period"], s["toggles"], s["hz"])
                              for s in segs)
        ms = 1000 * e.total_cycles / CPU_HZ
        share = e.total_cycles / TICK_CYCLES
        print("%2d %-13s %-15s %4d  %-34s %8d %8.2f %5.1f%%" % (
            e.number, e.name, e.routine, len(e.toggles), seg_text, e.total_cycles, ms,
            100 * share))
        lines.append("%d,%s,%s,%d,%s,%d,%.3f,%.4f" % (
            e.number, e.name, e.routine, len(e.toggles),
            " + ".join("%dx%d" % (s["half_period"], s["toggles"]) for s in segs),
            e.total_cycles, ms, share))
        # Closed-form check of the loop count against what the simulator
        # measured.  Single odd intervals are the hand-over between two tones
        # (return, reload, call), not part of either tone.
        for s in segs:
            if s["toggles"] < 2:
                continue
            if not any(tone_half_period(p) == s["half_period"] for p in range(1, 2048)):
                raise SystemExit("effect %d: half period %d matches no pitch value"
                                 % (e.number, s["half_period"]))
        pcm = render_pcm(e.toggles, e.total_cycles, args.rate)
        with open(os.path.join(args.out, "sfx_%02d_%s.wav" % (e.number, e.name)), "wb") as f:
            f.write(wav_bytes(pcm, args.rate))
        everything += pcm + gap
    with open(os.path.join(args.out, "sfx.csv"), "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(args.out, "sfx_all.wav"), "wb") as f:
        f.write(wav_bytes(everything, args.rate))
    print("nominal game tick: %.0f cycles (%.1f ms)" % (TICK_CYCLES, 1000 * TICK_CYCLES / CPU_HZ))
    return 0


if __name__ == "__main__":
    sys.exit(main())
