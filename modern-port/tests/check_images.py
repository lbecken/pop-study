#!/usr/bin/env python3
"""The port with replacement pictures: same game, other pictures, or refused.

New code for the pop-study project.  No original code runs here.

The port can take its image tables from a directory of new pictures
instead of the checkout of the original (`--images`,
docs/blender-to-sdl3.md).  There is no new artwork yet, so the directory
used here is a set of generated placeholders (frames of the right sizes,
tools/assets/make_placeholder_set.py).  What is checked:

  1. The route itself changes nothing: with a directory of copies of the
     original tables, every frame of every level is byte for byte what it
     is without the option.
  2. Other pictures do not change the game: with the placeholders, the
     whole game state after every frame of the demo and of levels 1-14
     under random keys is what it is with the original tables (in
     pop_worldrun, whose state the comparisons with the original use);
     and through pop_app the same frames leave the levels at the same
     moments, touch the same display switches and make the same sounds,
     while the pictures differ.
  3. What must be refused is refused: a directory without a
     PROVENANCE.txt or with an empty one, a missing table, and a
     character table with one image of another size, for the fixed tables
     and for each level's opponent set.

Usage:
    check_images.py <pop_headless> <pop_worldrun> <data dir> <original checkout> <work dir>
                    [seed] [--set <directory>]

With --set, part 2 alone is run on the given directory of replacement
tables, after tools/assets/check_imgset.py's check of it: the mechanical
test that a set of new pictures leaves the game as it is.
"""

import os
import random
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")
sys.path.insert(0, os.path.join(TOOLS, "extract_assets"))
sys.path.insert(0, os.path.join(TOOLS, "assets"))
sys.path.insert(0, HERE)

import check_app as ca  # noqa: E402
import check_imgset  # noqa: E402
import make_imgtable  # noqa: E402
import make_placeholder_set  # noqa: E402
import render_room as rr  # noqa: E402
from pop_imgtable import parse_image_table  # noqa: E402

IMAGES = os.path.join("01 POP Source", "Images")
FRAMES = 120


def headless(exe, data, source, level, inputs, work, images=None):
    """(status, per-frame token dicts, stderr) of a run of pop_headless."""
    script = os.path.join(work, "input.txt")
    with open(script, "w") as f:
        f.write("".join("%d %d %d %d\n" % i for i in inputs))
    argv = [exe, data, source, str(level), str(len(inputs)), "--input", script]
    if images:
        argv += ["--images", images]
    out = subprocess.run(argv, capture_output=True, text=True)
    frames = [dict(t.split("=") for t in line.split()[2:]) for line in out.stdout.splitlines()]
    return out.returncode, frames, out.stderr


def runner_states(runner, data, commands):
    """Per frame: the runner's output without the checksums of the pages."""
    argv = [runner] + [os.path.join(data, n) for n in
                       ("framedef.bin", "seqtable.bin", "imgdims.bin", "autodata.bin")]
    out = subprocess.run(argv, input="\n".join(commands) + "\n", capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("pop_worldrun failed: " + out.stderr[-400:])
    states, pages = [], []
    for line in out.stdout.splitlines()[1:]:
        tokens = line.split()
        states.append(" ".join(t for t in tokens if not t.startswith(("pg1:", "pg2:"))))
        pages.append(tuple(t for t in tokens if t.startswith(("pg1:", "pg2:"))))
    return states, pages


def copy_set(src, dst, provenance="copies, for a test\n"):
    os.makedirs(dst)
    for name in os.listdir(src):
        if name.startswith("IMG."):
            shutil.copy(os.path.join(src, name), os.path.join(dst, name))
    if provenance is not None:
        with open(os.path.join(dst, "PROVENANCE.txt"), "w") as f:
            f.write(provenance)


def with_one_taller(path, number):
    """Rewrite a table with image `number` one line taller."""
    with open(path, "rb") as f:
        table = parse_image_table(f.read())
    images = [im.dots() for im in table.images]
    images[number - 1].append([1] * len(images[number - 1][0]))
    with open(path, "wb") as f:
        f.write(make_imgtable.encode_image_table(images))


def main(argv):
    if len(argv) < 6:
        print(__doc__)
        return 2
    exe, runner, data, source, work = argv[1:6]
    own_set = None
    if "--set" in argv:
        at = argv.index("--set")
        if at + 1 >= len(argv):
            print(__doc__)
            return 2
        own_set = argv[at + 1]
        argv = argv[:at] + argv[at + 2:]
    rng = random.Random(int(argv[6]) if len(argv) > 6 else 1989)
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)
    bg = rr.load_bgdata(source)
    games = [(0, [(0, 0, 0, 0)] * 420)] + [(n, ca.random_inputs(rng, FRAMES)) for n in range(1, 15)]

    placeholders = own_set or os.path.join(work, "placeholders")
    if own_set:
        failed, report = check_imgset.check_set(own_set, source)
        print("\n".join(report))
        if failed:
            return 1
    else:
        make_placeholder_set.make_set(source, placeholders)
        failed, report = check_imgset.check_set(placeholders, source)
        if failed or any("exactly the original's dots" in line for line in report):
            print("the placeholder set does not pass check_imgset.py:\n" + "\n".join(report))
            return 1

    # 1. the route changes nothing
    copies = os.path.join(work, "copies")
    copy_set(os.path.join(source, IMAGES), copies)
    plain = {}
    frames = 0
    for level, inputs in games:
        plain[level] = headless(exe, data, source, level, inputs, work)
        if own_set:
            continue
        same = headless(exe, data, source, level, inputs, work, copies)
        if plain[level][0] != 0 or same[:2] != plain[level][:2]:
            print("level %d: copies of the original tables read through --images give another "
                  "run than the checkout" % level)
            return 1
        frames += len(same[1])

    # 2. other pictures, the same game
    state_frames = 0
    for level, inputs in games:
        status, got, err = headless(exe, data, source, level, inputs, work, placeholders)
        want = plain[level][1]
        if status != 0 or len(got) != len(want):
            print("level %d with placeholders: status %d, %d frames against %d\n%s"
                  % (level, status, len(got), len(want), err[-300:]))
            return 1
        if "replacement pictures" not in err:
            print("pop_headless does not say that it shows replacement pictures")
            return 1
        for n, (a, b) in enumerate(zip(got, want)):
            if any(a[k] != b[k] for k in ("leave", "level", "sw", "sounds", "cycles")):
                print("level %d frame %d: with placeholders %s, with the original %s"
                      % (level, n, a, b))
                return 1
        if (got[0]["pg1"], got[0]["pg2"]) == (want[0]["pg1"], want[0]["pg2"]):
            print("level %d: the replacement tables draw the original's picture" % level)
            return 1
        new, new_pages = runner_states(runner, data, ca.runner_commands(source, data, bg, level,
                                                                       inputs, placeholders))
        old, old_pages = runner_states(runner, data, ca.runner_commands(source, data, bg, level,
                                                                       inputs))
        if len(new) != len(old):
            print("level %d: %d frames with placeholders, %d with the original"
                  % (level, len(new), len(old)))
            return 1
        for n, (a, b) in enumerate(zip(new, old)):
            if a != b:
                differing = [x.split(":")[0].split("=")[0] for x, y in zip(a.split(), b.split())
                             if x != y]
                print("level %d frame %d: the game state differs with placeholders: %s"
                      % (level, n, " ".join(differing[:12])))
                return 1
        if new_pages == old_pages:
            print("level %d: the runner draws the same pages with either set" % level)
            return 1
        state_frames += len(new)
    if own_set:
        print("%s: the game state after each of %d frames (the demo, levels 1-14 under "
              "random keys) is the same as with the original tables; the pictures are not"
              % (own_set, state_frames))
        return 0

    # 3. refusals
    refusals = []

    def refused(name, level, directory, message, starts_on=None):
        status, _, err = headless(exe, data, source, level, [(0, 0, 0, 0)] * 3, work, directory)
        if status == 0 or message not in err:
            print("%s: not refused on level %d (status %d): %s" % (name, level, status,
                                                                  err.strip()[-200:]))
            return False
        if starts_on is not None:
            status, _, err = headless(exe, data, source, starts_on, [(0, 0, 0, 0)] * 3, work,
                                      directory)
            if status != 0:
                print("%s: level %d, which does not use the table, does not start: %s"
                      % (name, starts_on, err.strip()[-200:]))
                return False
        refusals.append(name)
        return True

    unlabelled = os.path.join(work, "unlabelled")
    copy_set(placeholders, unlabelled, provenance=None)
    empty_label = os.path.join(work, "empty_label")
    copy_set(placeholders, empty_label, provenance="")
    if not refused("no PROVENANCE.txt", 1, unlabelled, "PROVENANCE.txt") \
            or not refused("an empty PROVENANCE.txt", 1, empty_label, "PROVENANCE.txt"):
        return 1
    for name, level, ok_level in (("IMG.CHTAB2", 1, None), ("IMG.BGTAB1.PAL", 4, 1),
                                  ("IMG.CHTAB4.SKEL", 3, 1)):
        directory = os.path.join(work, "without_" + name)
        copy_set(placeholders, directory)
        os.remove(os.path.join(directory, name))
        if not refused("without " + name, level, directory, "pop_headless:", ok_level):
            return 1
    # one image a line taller: the fixed tables, and each level's opponents
    for name, level, ok_level in (("IMG.CHTAB1", 1, None), ("IMG.CHTAB2", 1, None),
                                  ("IMG.CHTAB3", 1, None), ("IMG.CHTAB5", 1, None),
                                  ("IMG.CHTAB4.GD", 1, 3), ("IMG.CHTAB4.SKEL", 3, 1),
                                  ("IMG.CHTAB4.FAT", 6, 1), ("IMG.CHTAB4.SHAD", 12, 1),
                                  ("IMG.CHTAB4.VIZ", 13, 1)):
        directory = os.path.join(work, "taller_" + name)
        copy_set(placeholders, directory)
        with_one_taller(os.path.join(directory, name), 7)
        if not refused(name + " with an image of another size", level, directory,
                       "image sizes", ok_level):
            return 1
    # an opponent table of the right format but for another opponent
    directory = os.path.join(work, "swapped")
    copy_set(placeholders, directory)
    shutil.copy(os.path.join(placeholders, "IMG.CHTAB4.FAT"),
                os.path.join(directory, "IMG.CHTAB4.GD"))
    if not refused("the fat guard's sizes in the guards' table", 1, directory, "image sizes", 6):
        return 1

    print("copies of the original tables read through --images: %d frames of 15 games "
          "identical to the checkout's.  Placeholder pictures: the game state after each of "
          "%d frames (the demo, levels 1-14 under random keys) is the same as with the "
          "original tables, the pictures are not.  %d faulty directories refused (%s)"
          % (frames, state_frames, len(refusals), "; ".join(refusals)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
