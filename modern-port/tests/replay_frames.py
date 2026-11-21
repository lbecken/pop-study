#!/usr/bin/env python3
"""End-to-end tests with the picture: whole frames, drawn, C port against
the original.

New code for the pop-study project.

replay_demo.py plays the demo level, and trace_kidstep.py random input on
the other levels, in both implementations with stand-ins for drawing.
Here the same two tests run with nothing of the drawing stood in for.  The original
runs in the two-bank simulator (tools/oracle/pop_gfx_oracle.py) with its
image tables loaded: MainLoop, the game logic, DoCleanCut and DoFast, the
display lists, the blitter in main memory, PageFlip.  The port runs the
same passes with its drawing code behind the hooks (pop_worldrun's V
line).  After every pass, besides everything replay_demo.py compares:

  - the 8,192 bytes of each of the two hi-res pages (as CRC-32);
  - what the monitor is shown: which page, hi-res or lo-res, graphics or
    text; the display soft switches touched during the pass, in order (a
    flash and the blank screen of a redraw come and go within one pass);
    the byte the text page was last filled with; and which page is being
    drawn on.

Still stood in for, the same on both sides: the keyboard (no key is
pressed), the vertical-blank wait, and playing the sounds.

Usage:
    replay_frames.py <pop_worldrun> <data dir> <original checkout> demo [max frames]
    replay_frames.py <pop_worldrun> <data dir> <original checkout> traces [traces] [seed]

"demo": the self-running demo of level 0 from its first frame to its end.
"traces": 60 frames of random keyboard input from random places on levels
1 to 14, as trace_kidstep.py makes them, each level with its own
background and opponent image tables.
"""

import os
import random
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fuzz_control as fc  # noqa: E402  (sets the import path for the oracle)
from pop_banked import AUX  # noqa: E402
from pop_gfx_oracle import GfxOracle  # noqa: E402
from pop_oracle import BLUEPRINT  # noqa: E402
import render_room as rr  # noqa: E402
from pop_room import bgset_file_suffix  # noqa: E402
from replay_demo import replay  # noqa: E402
from trace_kidstep import TraceHarness, main as trace_main  # noqa: E402

IMAGES = os.path.join("01 POP Source", "Images")
# the character tables that are in memory during play
CHTABLES = ((11, "chtable1", "IMG.CHTAB1"), (12, "chtable2", "IMG.CHTAB2"),
            (13, "chtable3", "IMG.CHTAB3"), (15, "chtable5", "IMG.CHTAB5"))
# The opponents' table by level: the set numbers are MISC.S 774; which file
# each set is on the disk is inferred from who lives on the levels (guards;
# the skeleton of level 3; the fat guard of 6; the shadow of 12; the vizier)
CHSET = (0, 0, 0, 1, 2, 2, 3, 2, 2, 2, 2, 2, 4, 5, 5)
CHSET_FILE = ("GD", "SKEL", "GD", "FAT", "SHAD", "VIZ")
# drawing routines the other tests stand in for, and which run here
REAL = ("dimchar", "inverty", "zeropeels", "sure", "fast", "PageFlip", "continuemsg",
        "printlevel", "timeleftmsg", "doflashon", "doflashoff", "lrcls", "lrclse", "copyscrn",
        "drawall")
# display soft switches: address -> (what, value, letter in the log)
SWITCHES = {0xC050: ("text", 0, "g"), 0xC051: ("text", 1, "t"), 0xC054: ("page2", 0, "1"),
            0xC055: ("page2", 1, "2"), 0xC056: ("hires", 0, "l"), 0xC057: ("hires", 1, "h")}


class BankedOracle(GfxOracle):
    """The two-bank oracle with the interface of the one-bank Oracle that
    the harness was written for: game memory is the auxiliary bank."""

    def _at(self, name_or_addr, module):
        return name_or_addr if isinstance(name_or_addr, int) else self.addr(name_or_addr, module)

    def poke(self, name_or_addr, value, module=None):
        self.cpu.poke(self._at(name_or_addr, module), AUX,
                      value & 0xFF if isinstance(value, int) else bytes(value))

    def peek(self, name_or_addr, length=0, module=None):
        return self.cpu.peek(self._at(name_or_addr, module), AUX, length)


class FrameHarness(TraceHarness):
    budget = 200_000_000         # a full redraw is some millions of cycles
    extra = ("pg1", "pg2", "scr")
    # Not compared: COLL's `tempobjid` is zero-page cell $F2 of a scratch
    # area that every module uses for its own locals, and the drawing code
    # leaves its `xsave` there (FRAMEADV.S 36).  The port has two variables.
    # Were the value ever carried from one module to the other, the game
    # states would part company in a later frame.
    # Nor "script": the tests' made-up movement script is kept at $9000,
    # which the single-routine tests have free and an image table needs here.
    ignore = ("tempobjid", "script")

    def __init__(self, source, data):
        oracle = BankedOracle(source)
        super().__init__(source, oracle=oracle)
        self.source, self.data = source, data
        self.bg = rr.load_bgdata(source)
        self.tables = []
        self.started = 0
        self.pictures = set()
        self.cycles = []             # 6502 cycles of each pass of MainLoop
        cpu = oracle.cpu
        for name in REAL:
            del cpu.traps[oracle.addr(name)]
        # A trap is an address.  Main memory has code too (the blitter; the
        # tables next to it are data): make sure no trapped game address is
        # also an address of that.
        blitter = oracle.modules["HIRES"]
        clash = [a for a in cpu.traps if blitter.origin <= a < blitter.end]
        assert not clash, "trapped addresses inside the main-memory blitter: %r" % clash
        self.screen = {"text": 0, "page2": 0, "hires": 0}
        self.touched = []

        def switch(what, value, letter):
            self.screen[what] = value
            self.touched.append(letter)
        for address, (what, value, letter) in SWITCHES.items():
            cpu.on_read[address] = lambda c, args=(what, value, letter): switch(*args)

    def start_trace(self, level_no, s):
        """The image tables of a level, for both sides; the level's own
        background set in the start state."""
        set1, set2 = self.bg.level_bgset1[level_no], self.bg.level_bgset2[level_no]
        s["BGset1"] = set1
        if s:
            # Messages on the screen for the first frames of some traces: the
            # level's number, or the time left at some point of the hour.
            # (From a random stream of its own, so that the traces are
            # otherwise those of trace_kidstep.py.)
            self.started += 1
            rng = random.Random("frames/%d" % self.started)
            kind = rng.random()
            if kind < 0.2:                           # "LEVEL n"
                s.update(message=1, msgtimer=rng.choice((8, 20, 35)))
            elif kind < 0.6:                         # the time left
                if rng.random() < 0.5:               # in the last two minutes: seconds
                    count = 43505 - rng.randint(60, 1440)
                else:                                # a minute ends while it is shown
                    count = 725 * rng.randint(1, 58) - rng.randint(2, 12)
                s.update(message=3, msgtimer=rng.choice((20, 25, 35)), FrameCount=count & 0xFF,
                         FrameCountH=count >> 8)
        self.tables = [(1, "bgtable1", "IMG.BGTAB1." + bgset_file_suffix(set1)),
                       (2, "bgtable2", "IMG.BGTAB2." + bgset_file_suffix(set2)),
                       (14, "chtable4", "IMG.CHTAB4." + CHSET_FILE[CHSET[level_no]])]
        self.tables += list(CHTABLES)
        return ["V " + os.path.join(self.data, "bgdata.bin")] + \
               ["T %d %s" % (number, os.path.join(self.source, IMAGES, filename))
                for number, _, filename in self.tables]

    def begin(self, level, s, bufs):
        super().begin(level, s, bufs)                # (resets memory)
        for _, table, filename in self.tables:
            self.oracle.load_table(table, filename)
        self.screen.update(text=0, page2=0, hires=0)
        del self.touched[:]

    def frame(self):
        before = self.oracle.cpu.cycles
        out = super().frame()
        if not isinstance(out, str):         # (cycles, whether a whole screen was drawn)
            self.cycles.append((self.oracle.cpu.cycles - before, "t" in out["scr"]))
        return out

    def more(self, out):
        o = self.oracle
        out["pg1"] = "%08x" % zlib.crc32(o.page(1))
        out["pg2"] = "%08x" % zlib.crc32(o.page(2))
        out["scr"] = "%d%d%d.%02x.%02x.%s" % (
            self.screen["page2"] + 1, self.screen["hires"], self.screen["text"],
            o.peek("scrncolor"), o.peek("PAGE"), "".join(self.touched) or "-")
        del self.touched[:]
        self.pictures.add((out["pg1"], out["pg2"]))


def main(argv):
    if len(argv) < 5 or argv[4] not in ("demo", "traces"):
        print(__doc__)
        return 2
    runner, data, source, mode = argv[1:5]
    h = FrameHarness(source, data)
    if mode == "traces":
        status = trace_main(argv[:4] + argv[5:], harness=h,
                            what="C port and original code agree on the game state, on every "
                                 "byte of both hi-res pages and on what the monitor is shown")
        if status == 0 and h.pictures:
            print("   %d different pairs of pages in the original's run" % len(h.pictures))
        return status
    max_frames = int(argv[5]) if len(argv) > 5 else 600
    result = replay(h, runner, data, source, max_frames, h.start_trace(0, {}))
    if result is None:
        return 1
    frames, seen, story, ended = result
    print("demo replayed with its drawing: %d frames, C port and original code agree after "
          "every one on the game state, on every byte of both hi-res pages and on what the "
          "monitor is shown" % frames)
    print("   in the original's run: %s; screens %s; %s; ended by %s"
          % ("%d different pairs of pages" % len(h.pictures) if h.pictures else "(kept results)",
             seen["screens"], " ".join(story) or "nobody dies", ended))
    if h.cycles:
        # How long the original takes over a frame: game logic and drawing,
        # by the simulator's cycle count.  Not in it: playing the frame's
        # sounds and the waits for vertical blank (stood in for).
        clock = 1020484                      # cycles a second (Apple IIe, NTSC)
        plain = sorted(c for c, cut in h.cycles[:-1] if not cut)     # (the last pass leaves)
        cuts = sorted(c for c, cut in h.cycles if cut)
        quart = [plain[len(plain) * k // 4] for k in (1, 2, 3)]
        print("   the original's time for a pass of MainLoop, by the simulator's cycle count "
              "(sound and the wait for vertical blank not included):")
        print("   %d ordinary passes: %d to %d cycles, quartiles %d / %d / %d, mean %.0f = %.1f ms "
              "= %.1f frames a second" % (len(plain), plain[0], plain[-1], quart[0], quart[1],
                                          quart[2], sum(plain) / len(plain),
                                          1000 * sum(plain) / len(plain) / clock,
                                          clock * len(plain) / sum(plain)))
        if cuts:
            print("   %d passes that drew a whole screen: %d to %d cycles (%.2f to %.2f s)"
                  % (len(cuts), cuts[0], cuts[-1], cuts[0] / clock, cuts[-1] / clock))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
