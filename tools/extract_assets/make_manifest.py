#!/usr/bin/env python3
"""Build assets/manifest.json: a hashed inventory of the original repository.

New code for the pop-study project.  The manifest records *facts about*
the original files (path, size, SHA-256, classification, parsed image-table
dimensions).  It contains none of the original code, art or level data.

Usage:
    python3 tools/extract_assets/make_manifest.py \
        --source /path/to/Prince-of-Persia-Apple-II --out assets/manifest.json

Classification is rule-based and each rule states its evidence level:
  confirmed  - verified by parsing the file or by source code that uses it
  inferred   - deduced from name/size only
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pop_imgtable import ImageTableError, parse_image_table  # noqa: E402

LEVEL_SIZE = 2304   # 720+720+256+256+96+256, see EQ.S lines 258-265
HIRES_PAGE = 8192   # one Apple II hi-res page ($2000 bytes)


def classify(rel, data):
    name = os.path.basename(rel)
    info = {}
    if name.endswith(".S"):
        kind, evidence = "source-merlin-6502", "confirmed"
        info["lines"] = data.count(b"\n")
    elif name.startswith("IMG."):
        try:
            t = parse_image_table(data)
            kind, evidence = "image-table", "confirmed"
            info.update(
                image_count=t.count,
                pointer_base="$%04X" % t.base_address,
                trailing_bytes=t.trailing_bytes,
                max_width_bytes=max(i.width_bytes for i in t.images),
                max_height=max(i.height for i in t.images),
            )
        except ImageTableError as e:
            kind, evidence = "image-table-unparsed", "inferred"
            info["parse_error"] = str(e)
    elif name.startswith("LEVEL") and len(data) == LEVEL_SIZE:
        kind, evidence = "level-blueprint", "confirmed"
    elif name.startswith("PAC."):
        kind, evidence = "packed-hires-picture", "inferred"
    elif len(data) in (HIRES_PAGE, HIRES_PAGE - 8):
        kind, evidence = "raw-hires-page", "inferred"
    elif name in ("README.md", "LICENSE", ".gitattributes"):
        kind, evidence = "repository-metadata", "confirmed"
    else:
        kind, evidence = "unclassified", "inferred"
    return kind, evidence, info


def git_head(path):
    try:
        return subprocess.run(["git", "-C", path, "rev-parse", "HEAD"],
                              capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def build(source):
    files = []
    for root, dirs, names in os.walk(source):
        dirs[:] = sorted(d for d in dirs if d != ".git")
        for n in sorted(names):
            full = os.path.join(root, n)
            rel = os.path.relpath(full, source).replace(os.sep, "/")
            with open(full, "rb") as f:
                data = f.read()
            kind, evidence, info = classify(rel, data)
            entry = {
                "path": rel,
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "kind": kind,
                "evidence": evidence,
            }
            entry.update(info)
            files.append(entry)

    by_hash = {}
    for e in files:
        by_hash.setdefault(e["sha256"], []).append(e["path"])
    for e in files:
        others = [p for p in by_hash[e["sha256"]] if p != e["path"]]
        if others:
            e["identical_to"] = others

    summary = {}
    for e in files:
        s = summary.setdefault(e["kind"], {"files": 0, "bytes": 0})
        s["files"] += 1
        s["bytes"] += e["size"]

    return {
        "schema": 1,
        "description": "Inventory of the original Prince of Persia Apple II "
                       "source release. Metadata only; no original content.",
        "original_repository": "https://github.com/jmechner/Prince-of-Persia-Apple-II",
        "original_commit": git_head(source),
        "file_count": len(files),
        "summary_by_kind": dict(sorted(summary.items())),
        "files": files,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--source", required=True)
    ap.add_argument("--out", default=os.path.join("assets", "manifest.json"))
    args = ap.parse_args(argv)
    if not os.path.isdir(args.source):
        ap.error("not a directory: %s" % args.source)
    manifest = build(args.source)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    print("wrote %s: %d files" % (args.out, manifest["file_count"]))
    for kind, s in manifest["summary_by_kind"].items():
        print("  %-24s %4d files %8d bytes" % (kind, s["files"], s["bytes"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
