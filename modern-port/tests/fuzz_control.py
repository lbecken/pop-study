#!/usr/bin/env python3
"""Differential test: C player-control and floor routines against the original.

New code for the pop-study project.

Every case builds a game state (a real level with a few pieces changed around
the character, a character record, joystick flags, an opponent), gives it to
the original routine running in the 6502 simulator (tools/oracle) and to the
C port (through pop_worldrun), and compares:

  * every tracked variable (character records, input flags, game variables,
    the FChar and edge variables),
  * the returned A (and X, Y, carry where the routine defines them),
  * the sounds queued, in order,
  * the level bytes changed,
  * the redraw buffers,
  * the calls made to subsystems that are not ported yet (slicers, spikes,
    potions, level exit, the input devices), in order.

The original's DIMCHAR reads image tables that live in main memory; here it
is replaced by a trap that answers from the same image files.  Cases in which
the original reaches memory the port does not model (an image number the
table does not hold, an index beyond a table) are counted and skipped, never
compared.

Usage:
    fuzz_control.py <pop_worldrun> <data dir> <original checkout> [cases] [seed]
"""

import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "oracle"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "extract_assets"))
sys.path.insert(0, HERE)

import fuzz_world as fw  # noqa: E402
from export_port_data import image_dims  # noqa: E402
from mos6502 import CpuError  # noqa: E402
from pop_oracle import BLUEPRINT, Oracle  # noqa: E402

EXTRA = ["jarabove", "alertguard", "JSTKX", "JSTKY", "btn", "clrF", "clrB", "clrU", "clrD",
         "clrbtn", "level", "stunned", "gotsword", "offguard", "EnemyAlert", "heroic",
         "refract", "gdtimer", "KidStrength", "OppStrength", "ChgKidStr", "ChgOppStr",
         "mergetimer", "rjumpflag", "droppedout", "savekidx", "lastpotion", "blockid",
         "bufindex", "timebomb", "FCharImage", "FCharY", "FCharFace", "FCharCU", "FCharCD",
         "FCharCL", "FCharCR", "FCharTable", "imwidth", "imheight", "leftej", "rightej",
         "topej", "leftblock", "rightblock", "topblock", "bottomblock", "CDLeftEj",
         "CDRightEj", "blockedge", "tempobjid", "height"]
BYTES = fw.BYTES + EXTRA
WORDS = fw.WORDS + ["FCharX"]
BUFFERS = (("redbuf", 30), ("wipebuf", 30), ("whitebuf", 30), ("topbuf", 10))
IN_MODULE = {"tempobjid": "COLL"}          # a COLL local, not in the shared equates

# routine -> (module, label, results compared: any of "a", "x", "y", "c")
ROUTINES = {
    "clrall": ("CTRL", "clrall", "a"), "facejstk": ("SPECIALK", "FACEJSTK", ""),
    "decstr": ("MISC", "DECSTR", "a"), "getspikes": ("MOVER", "GETSPIKES", "a"),
    "getopdist": ("CTRLSUBS", "GETOPDIST", "ax"),
    "checkledge": ("CTRLSUBS", "CHECKLEDGE", "a"),
    "indexblock": ("CTRLSUBS", "INDEXBLOCK", "cy"),
    "markwipe": ("CTRLSUBS", "MARKWIPE", "c"), "markred": ("CTRLSUBS", "MARKRED", "c"),
    "removeobj": ("SUBS", "REMOVEOBJ", ""),
    "setupchar": ("CTRLSUBS", "SETUPCHAR", ""), "getedges": ("CTRLSUBS", "GETEDGES", ""),
    "getfwddist": ("COLL", "GETFWDDIST", "axy"),
    "playerctrl": ("CTRL", "PLAYERCTRL", ""), "userctrl": ("CTRL", "UserCtrl", ""), "genctrl": ("CTRL", "GENCTRL", ""),
    "guardctrl": ("CTRL", "GuardCtrl", ""), "fightctrl": ("CTRL", "FightCtrl", ""),
    "standing": ("CTRL", "standing", ""), "crouching": ("CTRL", "crouching", ""),
    "starting": ("CTRL", "starting", ""), "stjumpup": ("CTRL", "stjumpup", ""),
    "turning": ("CTRL", "turning", ""), "arunning": ("CTRL", "arunning", ""),
    "hanging": ("CTRL", "hanging", ""),
    "doblock": ("CTRL", "DoBlock", ""), "dostrike": ("CTRL", "DoStrike", ""),
    "doretreat": ("CTRL", "DoRetreat", ""), "doadvance": ("CTRL", "DoAdvance", ""),
    "dostartrun": ("CTRL", "DoStartrun", ""), "doturn": ("CTRL", "DoTurn", ""),
    "dostandjump": ("CTRL", "DoStandjump", ""), "docrouch": ("CTRL", "DoCrouch", ""),
    "doengarde": ("CTRL", "DoEngarde", ""), "dojumpup": ("CTRL", "DoJumpup", ""),
    "dojumpedge": ("CTRL", "DoJumpedge", ""), "dojumphang": ("CTRL", "DoJumphang", ""),
    "dorunjump": ("CTRL", "DoRunjump", ""), "dostepfwd": ("CTRL", "DoStepfwd", ""),
    "dojumphigh": ("CTRL", "DoJumphigh", ""),
    "trypickup": ("CTRL", "TryPickup", "a"), "pickitup": ("CTRL", "PickItUp", ""),
    "checkfloor": ("CTRL", "CHECKFLOOR", ""), "falling": ("CTRL", "falling", ""),
    "hitflr": ("CTRL", "hitflr", ""), "fallon": ("CTRL", "fallon", ""),
    "onground": ("CTRL", "onground", ""), "startfall": ("CTRL", "startfall", ""),
    "cdpatch": ("CTRL", "CDpatch", ""), "insideblock": ("CTRL", "InsideBlock", "a"),
    "checkimpale": ("CTRL", "CHECKIMPALE", ""), "doimpale": ("CTRL", "DOIMPALE", ""),
}

# Frames the handlers test for, so that every branch is reached often.
KEY_FRAMES = ([15] * 6 + [48, 50, 51, 52, 1, 2, 3, 67, 68, 69, 4, 7, 7, 9, 11, 11, 13, 14, 26,
              43, 44, 81, 83, 85, 87, 91, 99, 102, 104, 105, 106, 109, 109, 109, 185]
              + [150, 151, 152, 153, 157, 158, 158, 161, 162, 165, 166, 166, 167, 168, 170,
                 171, 171, 179])
PIECES = [0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 4, 4, 6, 7, 7, 9, 10, 10, 11, 11, 12, 12, 13, 14, 16,
          18, 20, 20, 22, 22]
FIGHT = ("fightctrl", "doblock", "dostrike", "doretreat", "doadvance", "genctrl", "userctrl",
         "playerctrl")
FLOOR_Y = {0xFF: 248, 0: 55, 1: 118, 2: 181, 3: 244}
TRISTATE = (0x00, 0x00, 0x01, 0xFF, 0xFF)


def random_state(rng, level, routine):
    """A state, the blueprint pokes that go with it, and the buffer contents."""
    s = fw.random_state(rng)
    for n in EXTRA:
        s[n] = rng.randrange(256)
    s["FCharX"] = rng.randrange(0x10000)

    for rec in ("Char", "Op"):
        s[rec + "ID"] = rng.choice((0, 0, 0, 0, 1, 2, 4))
        s[rec + "Posn"] = rng.choice(KEY_FRAMES) if rng.random() < 0.7 else rng.randint(1, 240)
        s[rec + "Scrn"] = rng.randint(1, 24)
        row = rng.choice((0, 0, 1, 1, 2, 2, 0xFF, 3))
        s[rec + "BlockY"] = row
        s[rec + "Y"] = FLOOR_Y[row] if rng.random() < 0.6 else \
            (FLOOR_Y[row] + rng.randint(-40, 12)) & 0xFF
        s[rec + "X"] = rng.randint(50, 205)
        s[rec + "BlockX"] = (rng.randint(-1, 10)) & 0xFF
        s[rec + "Sword"] = rng.choice((0, 0, 2))
        s[rec + "Life"] = rng.choice((0xFF, 0xFF, 0xFF, 0x00, 0x05))
        s[rec + "Action"] = rng.choice((0, 0, 1, 1, 2, 3, 4, 4, 5, 6, 7))
        s[rec + "YVel"] = rng.choice((0, 3, 12, 21, 22, 27, 31, 32, 33, 40))
        s[rec + "XVel"] = rng.randint(-6, 6) & 0xFF
        s[rec + "Repeat"] = rng.choice((0, 0, 1, 5))
    if rng.random() < 0.7:                           # opponent on the same screen, near
        s["OpScrn"] = s["CharScrn"]
        s["OpX"] = (s["CharX"] + rng.choice((-140, -128, -127, -115, -114, -113, -100, -91,
                                             -90, -89, -60, -33, -32, -31, -14, -11, -10,
                                             -9, -6, -5, -4, -3, 0, 2, 9, 20, 31, 32, 33,
                                             60, 89, 90, 91, 113, 114, 115, 120, 127, 128,
                                             130))) & 0xFF
    for n in ("JSTKX", "JSTKY"):
        s[n] = rng.choice((0x00, 0x01, 0xFF))
    s["btn"] = rng.choice((0x00, 0x00, 0xFF, 0xFF, 0x01, 0x80))
    for n in ("clrF", "clrB", "clrU", "clrD", "clrbtn"):
        s[n] = rng.choice(TRISTATE)
    s["gotsword"] = rng.choice((0, 1, 1))
    s["offguard"] = rng.choice((0, 0, 1))
    s["EnemyAlert"] = rng.choice((0, 1, 2, 2))
    s["level"] = rng.choice((0, 1, 3, 12, 12, 12, 14))
    if s["level"] == 12 and rng.random() < 0.6:      # the phantom bridge
        s["CharScrn"] = rng.choice((2, 13))
        s["CharBlockY"], s["CharY"] = 0, 55
        s["mergetimer"] = rng.choice((0x00, 0xFF, 0x80))
    s["KidStrength"] = rng.choice((0, 1, 2, 3, 3, 10))
    s["stunned"] = rng.choice((0, 0, 0, 1, 7))
    s["OppStrength"] = rng.choice((0, 1, 3, 4))
    s["timebomb"] = 0                                # "must be 0!", CTRLSUBS.S 1036
    s["weightless"] = rng.choice((0, 0, 0, 5))
    s["blue"] = s["CharScrn"]
    s["tempblockx"] = rng.randint(-2, 12) & 0xFF
    s["tempblocky"] = rng.choice((0, 0, 1, 2, 3, 0xFF))
    s["blockid"] = rng.choice(PIECES)

    # Situations random states rarely produce.
    r = rng.random()
    if routine in FIGHT and r < 0.6:                 # sword drawn, opponent within reach
        s["CharPosn"] = rng.choice((150, 157, 158, 158, 161, 161, 165, 167, 168, 170, 171, 171))
        s["CharSword"], s["CharLife"], s["EnemyAlert"] = 2, 0xFF, 2
        s["CharAction"] = rng.choice((0, 1))
        s["OpScrn"] = s["CharScrn"]
        s["OpX"] = (s["CharX"] + rng.choice((-45, -31, -20, -6, 6, 20, 31, 45))) & 0xFF
        s["OpPosn"] = rng.choice((151, 152, 152, 153, 153, 158, 162, 168))
        s["OpFace"] = rng.choice((0x00, 0xFF))
    elif routine in ("standing", "genctrl", "userctrl", "playerctrl") and r < 0.6:
        s["CharPosn"], s["CharLife"], s["CharSword"] = 15, 0xFF, 0
        s["CharAction"] = rng.choice((0, 1))
        s["CharID"] = rng.choice((0, 0, 0, 1))
    elif routine == "checkimpale":
        s["CharPosn"] = rng.choice((5, 6, 7, 9, 12, 14, 14, 15, 26, 26, 43, 43, 44))
    elif routine in ("fallon", "checkfloor", "falling") and r < 0.5:
        # falling past a ledge with the button down, near the grab distance
        row = rng.choice((0, 1, 2))
        s.update(CharBlockY=row, CharLife=0xFF, btn=0xFF, CharAction=rng.choice((3, 4, 4)),
                 CharY=FLOOR_Y[row] - rng.choice((40, 27, 26, 25, 24, 12, 1, 0)),
                 CharYVel=rng.choice((3, 12, 30, 31, 32, 33)),
                 CharPosn=rng.choice((102, 103, 104, 105, 106)))
    elif routine in ("cdpatch", "startfall") and r < 0.5:
        s["rjumpflag"] = 44
        if routine == "startfall":
            s["CharPosn"] = 44
    if routine in FIGHT + ("standing", "doturn", "getopdist") and rng.random() < 0.3:
        # the opponent at exactly one of the distances the code compares with
        d = rng.choice((31, 32, 33, 89, 90, 91, -11, -10, -9, -7, -6, -5, -4, -3, 113, 114,
                        113, 114, 115))
        s["CharX"] = rng.randint(120, 140)
        s["OpScrn"] = s["CharScrn"]
        s["OpFace"] = s["CharFace"]
        if d >= 113:                                 # facing each other: 13 is added
            s["OpFace"] = s["CharFace"] ^ 0xFF
        s["OpX"] = s["CharX"] + (d if s["CharFace"] == 0 else -d)
        s["gotsword"], s["EnemyAlert"], s["offguard"] = 1, 2, 0
    if routine == "doblock" and rng.random() < 0.4:
        # blocking is decided by the opponent's distance (32) and frame
        d = rng.choice((31, 32, 33))
        s.update(CharPosn=rng.choice((158, 165, 168, 170, 171)), CharX=rng.randint(120, 140),
                 OpScrn=s["CharScrn"], OpFace=s["CharFace"], CharID=rng.choice((0, 1, 4)),
                 OpPosn=rng.choice((151, 152, 153, 162, 168)))
        s["OpX"] = s["CharX"] + (d if s["CharFace"] == 0 else -d)
    pieces = PIECES
    if routine in ("checkimpale", "hitflr", "falling", "checkfloor", "doimpale"):
        pieces = PIECES + [2] * 12                   # spikes
    elif routine in ("standing", "hanging", "dojumpup", "fallon"):
        pieces = PIECES + [4] * 6                    # gates

    # change a few pieces around the character
    pokes = {}
    scrn = s["CharScrn"]
    for _ in range(rng.choice((0, 2, 4, 6))):
        col = (s["CharBlockX"] + rng.randint(-2, 2)) % 10 if s["CharBlockX"] < 10 \
            else rng.randrange(10)
        rowi = rng.choice((0, 1, 2))
        if s["CharBlockY"] in (0, 1, 2) and rng.random() < 0.7:
            rowi = max(0, min(2, s["CharBlockY"] + rng.choice((-1, 0, 0, 0, 1))))
        i = (scrn - 1) * 30 + rowi * 10 + col
        pokes[i] = rng.choice(pieces) | rng.choice((0, 0, 0, 0x20, 0x40, 0xE0))
        pokes[720 + i] = rng.choice((0, 0, 1, 2, 4, 5, 20, 23, 24, 27, 28, 0x20, 0x78, 0x80,
                                     0xA0, 0xFF, rng.randrange(256)))
    if routine == "checkimpale" and r < 0.7:
        # spikes exactly where the character record says he is (the routine
        # reads CharBlockX without recomputing it)
        col, rowi = rng.randrange(10), rng.choice((0, 1, 2))
        s.update(CharBlockX=col, CharBlockY=rowi, _noreread=1)
        i = (scrn - 1) * 30 + rowi * 10 + col
        pokes.update({i: 2, 720 + i: rng.choice((0, 1, 2, 3, 4, 5, 6, 0x80, 0xFF))})
    if routine == "standing" and 0.6 <= r < 0.75:
        # "down" with the back to an edge, standing on a gate, facing left:
        # the nearly closed gate that cannot be climbed down from
        col, rowi = rng.randint(1, 8), rng.choice((0, 1, 2))
        s.update(CharPosn=15, CharFace=0xFF, CharLife=0xFF, CharSword=0, CharID=0,
                 CharAction=0, CharBlockY=rowi, CharY=FLOOR_Y[rowi], btn=0x00, clrD=0xFF,
                 clrF=0, clrB=0, clrU=0, JSTKX=0, CharX=58 + 14 * col + rng.randint(0, 13))
        i = (scrn - 1) * 30 + rowi * 10 + col
        pokes.update({i: 4, i - 1: 4, i + 1: 0, 720 + i: rng.choice((0, 8, 20, 24, 40, 188)),
                      720 + i - 1: rng.choice((0, 8, 20, 24, 40, 188))})
    if routine == "standing" and 0.75 <= r < 0.87:
        # "up" at the level exit: open far enough to walk in, or not quite
        col, rowi = rng.randint(1, 8), rng.choice((0, 1, 2))
        s.update(CharPosn=15, CharLife=0xFF, CharSword=0, CharID=0, CharAction=0,
                 CharBlockY=rowi, CharY=FLOOR_Y[rowi], clrU=0xFF, clrB=0, gotsword=0,
                 CharX=58 + 14 * col + rng.randint(0, 13))
        i = (scrn - 1) * 30 + rowi * 10 + col + rng.choice((-1, 0, 1))
        pokes.update({i: 16, 720 + i: rng.choice((0, 116, 119, 120, 123, 124, 200))})
    if routine == "checkledge" and r < 0.5:
        # a loose floor as the ledge, in each kind of state (the routine
        # tests the state with `bit` against the piece number)
        y = rng.randrange(30)
        s["_y"] = y
        s["_noreread"] = 1
        s["blockid"] = rng.choice((0, 0, 0, 0, 9, 12, 20))
        pokes.update({(scrn - 1) * 30 + y: 11,
                      720 + (scrn - 1) * 30 + y: rng.choice((0, 1, 2, 3, 4, 8, 16, 0x84, 0x80,
                                                             0x8B))})
    if routine == "hanging" and r < 0.3:
        # hanging under a gate that is open just enough, or not quite, to climb past
        col, rowi = rng.randrange(10), rng.choice((1, 2))
        s.update(CharBlockX=col, CharBlockY=rowi, CharPosn=rng.randint(87, 99), stunned=0,
                 JSTKY=0xFF, _noreread=1)
        i = (scrn - 1) * 30 + (rowi - 1) * 10 + col
        pokes.update({i: 4, 720 + i: rng.choice((0, 20, 23, 24, 27, 28, 188))})
    bufs = {}
    for name, size in BUFFERS:
        bufs[name] = bytes(rng.choice((0, 0, 0, 2, rng.randrange(256))) for _ in range(size))
    return s, pokes, bufs


def random_args(rng, routine, s):
    a, x, y = rng.randrange(256), rng.randrange(256), rng.randrange(30)
    if routine in ("checkledge", "pickitup"):
        a = rng.choice(PIECES + [10, 22, 22])
    if routine == "dostrike":
        x = s["CharPosn"] if rng.random() < 0.5 else rng.choice(KEY_FRAMES)
    if routine == "decstr":
        a = rng.choice((0, 1, 1, 2, 3, 100))
    return a, x, y


class Harness:
    def __init__(self, source):
        self.oracle = Oracle(source)
        self.addr = {n: self.oracle.addr(n, IN_MODULE.get(n)) for n in BYTES + WORDS}
        self.bufaddr = {n: self.oracle.addr(n) for n, _ in BUFFERS}
        self.soundtable = self.oracle.addr("soundtable")
        self.dims = image_dims(source)
        self.hooks = []
        self.stray = False
        cpu = self.oracle.cpu
        cpu.traps[self.oracle.addr("dimchar")] = self.trap_dimchar
        cpu.traps[self.oracle.addr("addslicers")] = lambda c: self.hooks.append("s")
        cpu.traps[self.oracle.addr("jamspikes")] = self.trap_jamspikes
        cpu.traps[self.oracle.addr("potioneffect")] = lambda c: self.hooks.append("p")
        cpu.traps[self.oracle.addr("GoneUpstairs", "COLL")] = lambda c: self.hooks.append("u")
        # PLAYERCTRL's input step (LoadSelect, getselect, clrjstk), SaveSelect
        # and the demo's own control: one mark each, nothing done
        cpu.traps[self.oracle.addr("LoadSelect")] = lambda c: self.hooks.append("i")
        cpu.traps[self.oracle.addr("getselect")] = lambda c: None
        cpu.traps[self.oracle.addr("clrjstk")] = lambda c: None
        cpu.traps[self.oracle.addr("SaveSelect")] = lambda c: self.hooks.append("o")
        cpu.traps[self.oracle.addr("DemoCtrl", "CTRL")] = lambda c: self.hooks.append("d")

    def trap_dimchar(self, cpu):
        """DIMCHAR: In A = image #, X = table #; out A = width, X = height."""
        image, table = cpu.a, cpu.x
        if table >= len(self.dims) or not 1 <= image <= len(self.dims[table]):
            self.stray = True                        # the original would read stray memory
            cpu.a = cpu.x = 0
            return
        cpu.a, cpu.x = self.dims[table][image - 1]

    def trap_jamspikes(self, cpu):
        """JAMSPIKES (MOVER.S 354-358) minus the moving-object list."""
        o = self.oracle
        ptr = o.peek("BlueSpec") | (o.peek(o.addr("BlueSpec") + 1) << 8)
        cpu.mem[(ptr + cpu.y) & 0xFFFF] = 0xFF
        self.hooks.append("j")

    def run(self, level, s, bufs, steps, carry):
        """Run `steps` [(routine, a, x, y)] on state s; return the outcome of
        the last one, or "stray" / "error" if the original left the modelled
        memory."""
        o = self.oracle
        o.reset()
        o.load_level(level)
        for n in BYTES:
            o.poke(self.addr[n], s[n])
        for w in WORDS:
            o.poke(self.addr[w], s[w] & 0xFF)
            o.poke(self.addr[w] + 1, s[w] >> 8)
        base = BLUEPRINT + (s["blue"] - 1) * 30
        for name, v in (("BlueType", base), ("BlueSpec", base + 720)):
            a = o.addr(name)
            o.poke(a, v & 0xFF)
            o.poke(a + 1, v >> 8)
        for name, _ in BUFFERS:
            o.poke(self.bufaddr[name], bufs[name])
        self.stray = False
        for routine, a, x, y in steps:
            module, label, _ = ROUTINES.get(routine, (None, None, None))
            if module is None:                       # a world-query routine as a first step
                module, label = fw.ROUTINES[routine][0], routine.upper()
            o.poke(self.soundtable, 0)
            self.hooks = []
            o.load_level(level)
            o.cpu.p = (o.cpu.p & 0xFE) | (carry & 1)
            try:
                ra, rx, ry, rp = o.call(label, a, x, y, module=module, max_cycles=400_000)
            except CpuError:
                return "error"                       # ran into data or never returned
            if self.stray:
                return "stray"
        out = {n: o.peek(self.addr[n]) for n in BYTES}
        for w in WORDS:
            out[w] = o.peek(self.addr[w]) | (o.peek(self.addr[w] + 1) << 8)
        a = o.addr("BlueType")
        ptr = o.peek(a) | (o.peek(a + 1) << 8)
        out["blue"] = (ptr - BLUEPRINT) // 30 + 1 if (ptr - BLUEPRINT) % 30 == 0 else -1
        count = o.peek(self.soundtable)
        now = o.peek(BLUEPRINT, len(level))
        return {
            "vars": out, "a": ra, "x": rx, "y": ry, "c": rp & 1,
            "snd": bytes(o.peek(self.soundtable + 1, count)).hex() if count else "-",
            "hooks": "".join(self.hooks) or "-",
            "lvl": ",".join("%d.%d" % (i, now[i]) for i in range(len(level))
                            if now[i] != level[i]) or "-",
            "bufs": {n: bytes(o.peek(self.bufaddr[n], size)).hex() for n, size in BUFFERS},
        }


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    runner, data, source = argv[1:4]
    per_routine = int(argv[4]) if len(argv) > 4 else 400
    seed = int(argv[5]) if len(argv) > 5 else 1989
    only = argv[6].split(",") if len(argv) > 6 else None

    h = Harness(source)
    levels = []
    for n in range(15):
        with open(os.path.join(source, "01 POP Source", "Levels", "LEVEL%d" % n), "rb") as f:
            levels.append(f.read())

    commands, expected = [], []
    names = sorted(only or ROUTINES)
    level = None
    for round_no in range(per_routine):
        if round_no % 10 == 0:                       # a fresh level now and then
            rng = random.Random("%d/%d" % (seed, round_no))
            level = bytearray(levels[rng.choice((1, 3, 4, 6, 7, 12, 12, 13, 14,
                                                 rng.randrange(15)))])
            commands.append("L " + level.hex())
        for routine in names:
            # each case has its own random stream, so that changing how one
            # routine's cases are made leaves the others' cases as they were
            rng = random.Random("%d/%d/%s" % (seed, round_no, routine))
            s, pokes, bufs = random_state(rng, level, routine)
            a, x, y = random_args(rng, routine, s)
            carry = rng.randrange(2)
            if routine in ("markwipe", "markred"):
                x = carry                            # the runner takes the carry in x
                if carry and routine == "markred" and rng.random() < 0.5:
                    y = rng.randrange(256)           # with carry set only y < 10 is used
            noreread = s.pop("_noreread", 0)
            fixed_y = s.pop("_y", None)
            if fixed_y is not None:                  # the loose-floor ledge case
                a, y = 11, fixed_y
            for i, v in pokes.items():
                level[i] = v
            steps = [(routine, a, x, y)]
            if routine not in ("setupchar", "getedges", "markwipe", "markred", "indexblock",
                               "decstr", "getopdist", "clrall", "facejstk") \
                    and not noreread and rng.random() < 0.8:
                steps.insert(0, ("rereadblocks", 0, 0, 0))   # a consistent CharBlockX
            if routine == "getedges":
                steps.insert(0, ("setupchar", 0, 0, 0))
            if routine in ("getspikes", "checkledge", "pickitup", "removeobj", "doimpale") \
                    and fixed_y is None and rng.random() < 0.8:
                # these take "rdblock results": read a block first
                steps.insert(len(steps) - 1, (rng.choice(("getunderft", "getinfront",
                                                          "getabove")), 0, 0, 0))
                steps[-1] = (routine, a, x, None)
            # run the original; a None y means "Y as the previous step left it"
            outcome = None
            fixed = list(steps)
            if fixed[-1][3] is None:
                # run the first steps to learn Y, then the whole chain again
                pre = h.run(bytes(level), s, bufs, fixed[:-1], carry)
                if isinstance(pre, str):
                    fixed, outcome = None, "error"
                else:
                    fixed[-1] = (routine, a, x, pre["y"])
            if fixed is not None:
                outcome = h.run(bytes(level), s, bufs, fixed, carry)
            if pokes:
                commands.append("P " + " ".join("%d=%d" % kv for kv in pokes.items()))
            commands.append("S " + " ".join("%s=%d" % kv for kv in s.items()))
            for name, _ in BUFFERS:
                commands.append("H %s %s" % (name, bufs[name].hex()))
            for st in (fixed or [(routine, a, x, 0)]):
                commands.append("R %s %d %d %d" % st)
                expected.append(None)
            expected[-1] = (routine, fixed, s, outcome)

    argv_c = [runner, os.path.join(data, "framedef.bin"), os.path.join(data, "seqtable.bin"),
              os.path.join(data, "imgdims.bin")]
    out = subprocess.run(argv_c, input="\n".join(commands) + "\n", capture_output=True,
                         text=True)
    if out.returncode != 0:
        print("pop_worldrun failed: " + out.stderr[-500:])
        return 1
    lines = out.stdout.splitlines()
    if len(lines) != len(expected):
        print("expected %d result lines, got %d" % (len(expected), len(lines)))
        return 1

    failures = 0
    compared = dict.fromkeys(names, 0)
    stray = dict.fromkeys(names, 0)
    changed = dict.fromkeys(names, 0)
    for line, exp in zip(lines, expected):
        if exp is None:
            continue
        routine, steps, before, want = exp
        got, extra = {}, {}
        for tok in line.split():
            if "=" in tok:
                k, v = tok.split("=")
                got[k] = int(v)
            else:
                k, v = tok.split(":")
                extra[k] = v
        if isinstance(want, str) or got["unmodelled"]:
            # Outside the tables on either side: nothing to compare.  The one
            # thing that can be checked is that the port noticed too when the
            # original asked for an image that does not exist.
            if want == "stray" and not got["unmodelled"]:
                failures += 1
                if failures <= 8:
                    print("MISMATCH %s: the original asked for a missing image, "
                          "the port did not" % routine)
                    print("   steps: %r" % (steps,))
            stray[routine] += 1
            continue
        compared[routine] += 1
        problems = []
        results = ROUTINES[routine][2]
        if "a" in results and got["a"] != want["a"]:
            problems.append("A: C %d, original %d" % (got["a"], want["a"]))
        if "c" in results and got["a"] != want["c"]:
            problems.append("carry: C %d, original %d" % (got["a"], want["c"]))
        if "x" in results and got["X"] != want["x"]:
            problems.append("X: C %d, original %d" % (got["X"], want["x"]))
        if "y" in results and got["Y"] != want["y"]:
            problems.append("Y: C %d, original %d" % (got["Y"], want["y"]))
        if got["blue"] != want["vars"]["blue"]:
            problems.append("blueprint screen: C %d, original %d"
                            % (got["blue"], want["vars"]["blue"]))
        for n in BYTES + WORDS:
            if got[n] != want["vars"][n]:
                problems.append("%s: C %d, original %d (was %d)"
                                % (n, got[n], want["vars"][n], before[n]))
        for key in ("snd", "hooks", "lvl"):
            if extra[key] != want[key]:
                problems.append("%s: C %s, original %s" % (key, extra[key], want[key]))
        for n, _ in BUFFERS:
            if extra[n] != want["bufs"][n]:
                problems.append("%s: C %s, original %s" % (n, extra[n], want["bufs"][n]))
        if got["CharSeq"] != before["CharSeq"] or got["CharX"] != before["CharX"] \
                or extra["lvl"] != "-" or extra["snd"] != "-" or extra["hooks"] != "-":
            changed[routine] += 1
        if problems:
            failures += 1
            if failures <= 8:
                print("MISMATCH %s" % routine)
                for p in problems[:10]:
                    print("   " + p)
                print("   steps: %r" % (steps,))
                print("   state: " + " ".join("%s=%d" % (k, before[k]) for k in sorted(before)))
    total = sum(compared.values())
    thin = [n for n in names if compared[n] * 2 < per_routine]
    if os.environ.get("POP_FUZZ_VERBOSE"):
        for n in names:
            print("  %-12s compared %4d  acted %4d  skipped %4d"
                  % (n, compared[n], changed[n], stray[n]))
    if failures:
        print("%d of %d compared cases differ" % (failures, total))
        return 1
    if thin:
        print("too few comparable cases for: " + ", ".join(thin))
        return 1
    print("%d cases over %d routines: C port and original code agree "
          "(%d more skipped: the original read outside the modelled tables)"
          % (total, len(names), sum(stray.values())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
