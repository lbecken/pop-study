"""Parser for Prince of Persia (Apple II) level blueprints (LEVEL0..LEVEL14).

New code for the pop-study project; contains no original code or data.

Layout, 2304 bytes ($900), as loaded at $B700 (original repository @ 25243550):

    offset  size  name       source
    0       720   BLUETYPE   EQ.S 258-265; 24 screens x 30 blocks
    720     720   BLUESPEC
    1440    256   LINKLOC
    1696    256   LINKMAP
    1952    96    MAP        24 screens x (left, right, above, below)
    2048    256   INFO       EQ.S 272-287

The author's description is in "04 Support/MakeDisk/S/COMMENTS.S" lines
237-338.  Code that confirms it: CALCBLUE (GRAFIX.S 1757-1778) for the
per-screen addressing, getobjid1 (FRAMEADV.S 2047-2098) for the type/spec
split and the pressure-plate rule, GETINITOBJ (FRAMEADV.S 2217-2247) and
SETINITIALS (SUBS.S 1441-1462) for initial states.

Blocks are numbered 0-29 left to right, top to bottom (10 per row).
Screens are numbered 1-24; 0 means "no screen".
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional

LEVEL_SIZE = 2304
SCREENS = 24
BLOCKS = 30
COLS = 10
ROWS = 3

# EQ.S 484-486
SECMASK = 0xC0
REQMASK = 0x20
IDMASK = 0x1F

# MOVEDATA.S 8-37 (objnames)
OBJ_NAMES = [
    "space", "floor", "spikes", "posts", "gate", "dpressplate", "pressplate",
    "panelwif", "pillarbottom", "pillartop", "flask", "loose", "panelwof",
    "mirror", "rubble", "upressplate", "exit", "exit2", "slicer", "torch",
    "block", "bones", "sword", "window", "window2", "archbot", "archtop1",
    "archtop2", "archtop3", "archtop4",
]
OBJ = {name: i for i, name in enumerate(OBJ_NAMES)}

# FRAMEADV.S 22-23 `initsettings db gmaxval,gminval`; BGDATA.S 194-195
GMAXVAL = 47 * 4
GMINVAL = 0
INIT_SETTINGS = (GMAXVAL, GMINVAL)

GUARD_SLOTS = 24


class LevelError(ValueError):
    pass


def obj_name(objid: int) -> str:
    return OBJ_NAMES[objid] if objid < len(OBJ_NAMES) else "obj%d" % objid


@dataclass
class Level:
    """A level blueprint.  `type` and `spec` are mutable working copies, as
    the game's own buffer at $B700 is."""
    type: bytearray      # 720
    spec: bytearray      # 720
    linkloc: bytes       # 256
    linkmap: bytearray   # 256
    map: bytes           # 96
    info: bytes          # 256

    # -- addressing -----------------------------------------------------------

    @staticmethod
    def index(screen: int, block: int) -> int:
        if not 1 <= screen <= SCREENS or not 0 <= block < BLOCKS:
            raise LevelError("screen %d block %d out of range" % (screen, block))
        return (screen - 1) * BLOCKS + block

    def objid(self, screen: int, block: int) -> int:
        return self.type[self.index(screen, block)] & IDMASK

    def state(self, screen: int, block: int) -> int:
        return self.spec[self.index(screen, block)]

    def neighbours(self, screen: int) -> Dict[str, int]:
        """MAP entry: screen numbers to the left, right, above, below (0 = none)."""
        if not 1 <= screen <= SCREENS:
            raise LevelError("screen %d out of range" % screen)
        l, r, a, b = self.map[(screen - 1) * 4:(screen - 1) * 4 + 4]
        return {"left": l, "right": r, "above": a, "below": b}

    # -- INFO -----------------------------------------------------------------

    @property
    def screen_count_plus_one(self) -> int:
        """INFO byte 0, used by SETINITIALS as "number of screens +1"."""
        return self.info[0]

    @property
    def kid_start(self) -> Dict[str, int]:
        return {"screen": self.info[64], "block": self.info[65], "face": self.info[66]}

    @property
    def sword_start(self) -> Dict[str, int]:
        return {"screen": self.info[68], "block": self.info[69]}

    def guard_start(self, screen: int) -> Dict[str, int]:
        """Starting data of the guard on a screen (GdStart* arrays, one slot
        per screen).  Which block value means "no guard" is not yet verified."""
        i = screen - 1
        base = 71
        g = lambda n: self.info[base + n * GUARD_SLOTS + i]  # noqa: E731
        return {"block": g(0), "face": g(1), "x": g(2), "seq_lo": g(3),
                "prog": g(4), "seq_hi": g(5)}

    # -- links ----------------------------------------------------------------

    def links(self, first: int) -> List[Dict[str, int]]:
        """Decode the pressure-plate link list starting at index `first`
        (COMMENTS.S 293-309)."""
        out = []
        i = first
        while i < 256:
            loc, mp = self.linkloc[i], self.linkmap[i]
            if loc == 0xFF:
                break
            out.append({
                "index": i,
                "block": loc & 0x1F,
                "screen": ((loc >> 5) & 0x03) | ((mp >> 5) << 2),
                "timer": mp & 0x1F,
                "last": bool(loc & 0x80),
            })
            if loc & 0x80:
                break
            i += 1
        return out

    # -- game-start state -----------------------------------------------------

    def set_initials(self) -> None:
        """SETINITIALS: replace the editor's settings in BLUESPEC with the
        states the game uses, for screens INFO[0]-1 down to 1."""
        top = (self.info[0] - 1) & 0xFF
        for screen in range(top, 0, -1):
            if screen > SCREENS:
                raise LevelError("INFO[0] = %d names a screen beyond 24" % self.info[0])
            for block in range(BLOCKS - 1, -1, -1):
                i = self.index(screen, block)
                objid = self.type[i] & IDMASK
                if objid == OBJ["gate"]:
                    setting = self.spec[i]
                    if not 1 <= setting <= len(INIT_SETTINGS):
                        raise LevelError("gate at screen %d block %d has setting %d"
                                         % (screen, block, setting))
                    self.spec[i] = INIT_SETTINGS[setting - 1]
                elif objid == OBJ["loose"]:
                    self.spec[i] = 0
                elif objid == OBJ["flask"]:
                    self.spec[i] = (self.spec[i] << 5) & 0xFF

    # -- export ---------------------------------------------------------------

    def screens_in_use(self) -> List[int]:
        """Screens reachable through MAP from the kid's start screen."""
        start = self.kid_start["screen"]
        if not 1 <= start <= SCREENS:
            return []
        seen, todo = [], [start]
        while todo:
            s = todo.pop()
            if s in seen or not 1 <= s <= SCREENS:
                continue
            seen.append(s)
            todo.extend(self.neighbours(s).values())
        return sorted(seen)

    def to_json(self) -> dict:
        screens = []
        for s in range(1, SCREENS + 1):
            base = (s - 1) * BLOCKS
            t = self.type[base:base + BLOCKS]
            screens.append({
                "screen": s,
                "neighbours": self.neighbours(s),
                "objid": [b & IDMASK for b in t],
                "flags": [b & (SECMASK | REQMASK) for b in t],
                "spec": list(self.spec[base:base + BLOCKS]),
                "rows": [" ".join(obj_name(b & IDMASK) for b in t[r * COLS:(r + 1) * COLS])
                         for r in range(ROWS)],
            })
        return {
            "info0": self.info[0],
            "kid_start": self.kid_start,
            "sword_start": self.sword_start,
            "screens_in_use": self.screens_in_use(),
            "screens": screens,
            "linkloc": list(self.linkloc),
            "linkmap": list(self.linkmap),
        }


def parse_level(data: bytes) -> Level:
    if len(data) != LEVEL_SIZE:
        raise LevelError("a level blueprint is %d bytes, got %d" % (LEVEL_SIZE, len(data)))
    return Level(
        type=bytearray(data[0:720]),
        spec=bytearray(data[720:1440]),
        linkloc=bytes(data[1440:1696]),
        linkmap=bytearray(data[1696:1952]),
        map=bytes(data[1952:2048]),
        info=bytes(data[2048:2304]),
    )


def load_level(path: str) -> Level:
    with open(path, "rb") as f:
        return parse_level(f.read())
