#!/usr/bin/env python3
"""Extract frame definitions and animation sequences to JSON, with checks.

New code for the pop-study project.  Reads FRAMEDEF.S, SEQTABLE.S, SEQDATA.S
and the image tables from a local checkout of the original repository and
writes derived data to a git-ignored output directory.

Usage:
    python3 tools/extract_assets/extract_animdata.py \
        --source /path/to/Prince-of-Persia-Apple-II --out build/data

Output:
    <out>/frames.json       frame definitions (main, altset1, altset2), swords
    <out>/sequences.json    entry points, flat disassembly, per-entry traces
    <out>/animdata-report.txt  the cross-check report also printed to stdout

Exit status is 1 if a structural check fails (the data cannot be trusted),
0 otherwise.  Findings that are properties of the original data are listed
but do not fail the run.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from merlin_data import assemble, parse_equates  # noqa: E402
from pop_animdata import (INSTRUCTIONS, decode_frame_tables,  # noqa: E402
                          decode_sequence_table, trace)
from pop_imgtable import parse_image_table  # noqa: E402

SOURCE_SUBDIR = os.path.join("01 POP Source", "Source")
IMAGES_SUBDIR = os.path.join("01 POP Source", "Images")

# chtable index (0-based) -> image files that can occupy it.
# GAMEEQ.S 9-15, GRAFIX.S 184-187, MASTER.S 524-527.
CHTABLE_FILES = {
    0: ["IMG.CHTAB1"],
    1: ["IMG.CHTAB2"],
    2: ["IMG.CHTAB3"],
    3: ["IMG.CHTAB4.GD", "IMG.CHTAB4.FAT", "IMG.CHTAB4.SHAD",
        "IMG.CHTAB4.SKEL", "IMG.CHTAB4.VIZ"],
    4: ["IMG.CHTAB5"],
    5: ["IMG.CHTAB6.A", "IMG.CHTAB6.B"],
    6: ["IMG.CHTAB7"],
}
SWORD_CHTABLE = 2   # decodeswim, CTRLSUBS.S 1050-1055


def read_text(path):
    with open(path, "r", encoding="ascii") as f:
        return f.read()


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def load_image_counts(images_dir):
    counts = {}
    for names in CHTABLE_FILES.values():
        for name in names:
            path = os.path.join(images_dir, name)
            with open(path, "rb") as f:
                counts[name] = parse_image_table(f.read()).count
    return counts


def run(source, out_dir):
    src = os.path.join(source, SOURCE_SUBDIR)
    paths = {n: os.path.join(src, n) for n in ("FRAMEDEF.S", "SEQTABLE.S", "SEQDATA.S")}
    fasm = assemble(read_text(paths["FRAMEDEF.S"]))
    sasm = assemble(read_text(paths["SEQTABLE.S"]))
    seq_names = parse_equates(read_text(paths["SEQDATA.S"]))
    frames = decode_frame_tables(fasm)
    table = decode_sequence_table(sasm)
    image_counts = load_image_counts(os.path.join(source, IMAGES_SUBDIR))

    errors = list(table.problems)     # structural: fail the run
    findings = []                     # properties of the data: report only
    report = []

    # --- instruction codes in the table text must match ANIMCHAR's ------------
    for name, (code, _, _) in INSTRUCTIONS.items():
        if sasm.symbols.get(name) != code:
            errors.append("SEQTABLE.S defines %s = %r, expected %d"
                          % (name, sasm.symbols.get(name), code))
        if seq_names.get(name) != code:
            errors.append("SEQDATA.S defines %s = %r, expected %d"
                          % (name, seq_names.get(name), code))

    # --- entry points ----------------------------------------------------------
    name_by_number = {}
    for name, value in seq_names.items():
        if value > 0 and name not in ("fcheckmark", "fcentermark"):
            name_by_number.setdefault(value, name)
    # The label written in the entry table itself (several labels can share
    # one address, so the operand text is the authoritative name).
    entry_operand = {l.address: l.operand for l in sasm.lines
                     if l.op in ("dw", "da") and l.address < sasm.origin + 2 * len(table.entries)}
    entries_json, traces = [], {}
    for n, addr in enumerate(table.entries, 1):
        label = entry_operand.get(sasm.origin + 2 * (n - 1),
                                  table.labels.get(addr, ["?"])[0])
        t = trace(table, addr) if addr in table.by_address else None
        traces[n] = t
        entry = {"number": n, "address": "$%04X" % addr, "label": label,
                 "seqdata_name": name_by_number.get(n)}
        if t:
            loop = t.ticks[t.loop_to:]
            entry.update({
                "ticks_before_loop": t.loop_to,
                "loop_length": len(loop),
                "loop_frames": [k.frame for k in loop],
                "loop_dx": sum(k.dx for k in loop),
                "loop_dy": sum(k.dy for k in loop),
                "lead_in_dx": sum(k.dx for k in t.ticks[:t.loop_to]),
                "lead_in_dy": sum(k.dy for k in t.ticks[:t.loop_to]),
                "has_ifwtless": t.conditional,
                "ticks": [{
                    "start": "$%04X" % k.start,
                    "ops": [i.to_json(table.labels) for i in k.ops],
                    "frame": k.frame, "dx": k.dx, "dy": k.dy,
                } for k in t.ticks],
            })
        entries_json.append(entry)
        sd = name_by_number.get(n)
        if sd and sd != label:
            findings.append("entry %d: SEQDATA.S calls it %r, SEQTABLE.S label is %r"
                            % (n, sd, label))

    # --- which frames do sequences show? -----------------------------------------
    shown = {}
    for i in table.items:
        if i.op == "frame":
            shown.setdefault(i.args[0], []).append(i.address)
    main_by_no = {f.number: f for f in frames.main}
    alt1_by_no = {f.number: f for f in frames.altset1}
    alt2_by_no = {f.number: f for f in frames.altset2}
    for fr in sorted(shown):
        if fr == 0:
            continue
        defined = [s for s, d in (("main", main_by_no), ("altset1", alt1_by_no),
                                  ("altset2", alt2_by_no))
                   if fr in d and not d[fr].empty]
        if fr not in main_by_no:
            errors.append("sequence frame %d is outside the frame table" % fr)
        elif not defined:
            findings.append("frame %d is shown by a sequence (first at $%04X) but is "
                            "empty in every frame set" % (fr, shown[fr][0]))
    unused = [f.number for f in frames.main if not f.empty and f.number not in shown]

    # --- do referenced images exist? -------------------------------------------
    def check_images(set_name, flist):
        for f in flist:
            if f.empty or f.image == 0:
                continue
            files = CHTABLE_FILES.get(f.table)
            if not files:
                findings.append("%s frame %d uses chtable%d, for which the repository "
                                "has no image file" % (set_name, f.number, f.table + 1))
                continue
            missing = [n for n in files if f.image > image_counts[n]]
            if len(missing) == len(files):
                findings.append("%s frame %d: image %d not present in %s"
                                % (set_name, f.number, f.image, "/".join(files)))
            elif missing and f.table == 3:
                findings.append("%s frame %d: image %d missing from %s"
                                % (set_name, f.number, f.image, ", ".join(missing)))
            if f.sword and f.sword > len(frames.swords):
                findings.append("%s frame %d: sword frame %d beyond sword table"
                                % (set_name, f.number, f.sword))
    check_images("main", frames.main)
    check_images("altset1", frames.altset1)
    check_images("altset2", frames.altset2)
    sword_file = CHTABLE_FILES[SWORD_CHTABLE][0]
    for s in frames.swords:
        if s.image > image_counts[sword_file]:
            findings.append("sword frame %d: image %d not present in %s"
                            % (s.number, s.image, sword_file))

    # --- report -----------------------------------------------------------------
    used = lambda fl: sum(1 for f in fl if not f.empty)  # noqa: E731
    tables_used = sorted({f.table for f in frames.main if not f.empty})
    report.append("FRAMEDEF.S: $%04X-$%04X (%d bytes)" % (fasm.origin, fasm.end, len(fasm.image)))
    report.append("  main frames:    %d slots, %d defined, chtables used: %s"
                  % (len(frames.main), used(frames.main),
                     ", ".join(str(t + 1) for t in tables_used)))
    report.append("  altset1 frames: %d slots (150-%d), %d defined"
                  % (len(frames.altset1), 149 + len(frames.altset1), used(frames.altset1)))
    report.append("  altset2 frames: %d slots, %d defined"
                  % (len(frames.altset2), used(frames.altset2)))
    report.append("  sword frames:   %d defined" % len(frames.swords))
    for set_name, flist in (("main", frames.main), ("altset1", frames.altset1),
                            ("altset2", frames.altset2)):
        per = {}
        for f in flist:
            if not f.empty and f.image:
                per.setdefault(f.table, set()).add(f.image)
        for t in sorted(per):
            files = CHTABLE_FILES.get(t, [])
            report.append("  %-8s chtable%d: %3d distinct images, numbers %d-%d  (files: %s)"
                          % (set_name, t + 1, len(per[t]), min(per[t]), max(per[t]),
                             ", ".join("%s=%d" % (n, image_counts[n]) for n in files)))
    sw = sorted({x.image for x in frames.swords if x.image})
    report.append("  swords   chtable%d: %3d distinct images, numbers %d-%d  (files: %s=%d)"
                  % (SWORD_CHTABLE + 1, len(sw), sw[0], sw[-1], sword_file,
                     image_counts[sword_file]))
    report.append("SEQTABLE.S: $%04X-$%04X (%d bytes)" % (sasm.origin, table.end,
                                                         table.end - sasm.origin))
    report.append("  entry points:   %d" % len(table.entries))
    report.append("  instructions:   %d, frame bytes: %d, distinct frames shown: %d"
                  % (sum(1 for i in table.items if i.op != "frame"),
                     sum(1 for i in table.items if i.op == "frame"), len(shown)))
    counts = {}
    for i in table.items:
        if i.op != "frame":
            counts[i.op] = counts.get(i.op, 0) + 1
    report.append("  by instruction: " + ", ".join("%s %d" % kv for kv in sorted(
        counts.items(), key=lambda kv: -kv[1])))
    report.append("  defined main frames never shown by any sequence: %d%s"
                  % (len(unused), (" (%s)" % ", ".join(map(str, unused))) if unused else ""))
    report.append("")
    report.append("structural errors: %d" % len(errors))
    report.extend("  ERROR " + e for e in errors)
    report.append("findings: %d" % len(findings))
    report.extend("  " + f for f in findings)

    # --- write ------------------------------------------------------------------
    os.makedirs(out_dir, exist_ok=True)
    provenance = {
        "generated_by": "tools/extract_assets/extract_animdata.py",
        "note": "Derived from the original Prince of Persia Apple II source. "
                "Not redistributed by pop-study; regenerate from your own checkout.",
        "sources": {n: sha256(p) for n, p in sorted(paths.items())},
    }
    with open(os.path.join(out_dir, "frames.json"), "w") as f:
        json.dump({
            "provenance": provenance,
            "frame_size": 5,
            "main": [x.to_json() for x in frames.main],
            "altset1": [x.to_json() for x in frames.altset1],
            "altset2": [x.to_json() for x in frames.altset2],
            "swords": [x.to_json() for x in frames.swords],
        }, f, indent=1)
        f.write("\n")
    with open(os.path.join(out_dir, "sequences.json"), "w") as f:
        json.dump({
            "provenance": provenance,
            "origin": "$%04X" % table.origin,
            "end": "$%04X" % table.end,
            "instruction_codes": {k: v[0] for k, v in INSTRUCTIONS.items()},
            "entries": entries_json,
            "items": [i.to_json(table.labels) for i in table.items],
        }, f, indent=1)
        f.write("\n")
    text = "\n".join(report) + "\n"
    with open(os.path.join(out_dir, "animdata-report.txt"), "w") as f:
        f.write(text)
    return text, errors, findings


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--source", required=True)
    ap.add_argument("--out", default=os.path.join("build", "data"))
    args = ap.parse_args(argv)
    if not os.path.isdir(os.path.join(args.source, SOURCE_SUBDIR)):
        ap.error("not found: %s" % os.path.join(args.source, SOURCE_SUBDIR))
    text, errors, _ = run(args.source, args.out)
    sys.stdout.write(text)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
