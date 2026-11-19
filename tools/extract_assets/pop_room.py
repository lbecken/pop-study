"""Model of the game's full-screen redraw: level blueprint -> image lists.

New code for the pop-study project; contains no original code or data.
The piece tables it uses are read at run time from the user's checkout
(BGDATA.S, GAMEBG.S, MISC.S); nothing original is stored here.

What is modelled (original repository @ 25243550, "01 POP Source/Source"):

    SURE and everything it calls       FRAMEADV.S 49-179, 346-374, 561-700,
                                       754-1246, 1365-1927, 2041-2109
    setupflame, setupflask             GAMEBG.S 675-750
    ADDBACK, ADDFORE, ADDMIDEZO        GRAFIX.S 203-246, 390-427
    DRAWBACK, DRAWMID, DRAWFORE        GRAFIX.S 571-626, 679-752

The routines are ported one for one and keep the original names.  Like the
6502 code they communicate through a handful of persistent variables
(XCO, YCO, IMAGE, OPACITY, PRECED, spreced, state ...), and some paths
reuse whatever value the previous routine left behind; keeping the
variables persistent reproduces that.  All arithmetic is 8-bit.

Not modelled: characters and other objects, the kid-dependent gate
foreground (DrawGateBF?), strength meters, messages, and the partial
redraw path (FAST).  This is the screen as SURE builds it with nobody in it.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

from merlin_data import assemble, split_line
from pop_hires import (OP_AND, OP_MASK, OP_ORA, OP_STA, HiresPage, fastlay,
                       fastmask, lay_gen)
from pop_level import OBJ, Level

BLOCK_BOT = (2, 65, 128, 191, 254)      # TABLES.S BlockBot, index = row + 1
MAXBACK, MAXFORE, MAXMID = 200, 100, 46  # EQ.S 294-298
USE_LAY = 1                              # GAMEEQ.S 662
TORCH_LAST = 17                          # MOVEDATA.S 42
BUBB_LAST = 8                            # MOVEDATA.S 43
LEVEL_WITH_OPEN_VOID = 12                # FRAMEADV.S 693-700

_DATA_OPS = ("db", "dfb", "hex", "dw", "da")


class RoomError(RuntimeError):
    pass


def extract_table(text: str, label: str) -> bytes:
    """Assemble one labelled data table out of a file that is otherwise code."""
    lines = text.splitlines()
    for i, raw in enumerate(lines):
        lab, op, _, _ = split_line(raw)
        if lab == label and op in _DATA_OPS:
            chunk = [raw]
            for nxt in lines[i + 1:]:
                lab2, op2, _, _ = split_line(nxt)
                if lab2 or op2 not in _DATA_OPS:
                    if not lab2 and not op2:      # blank or comment: keep looking
                        if nxt.strip() == "":
                            break
                        continue
                    break
                chunk.append(nxt)
            return assemble(" org 0\n" + "\n".join(chunk) + "\n").image
    raise RoomError("table %s not found" % label)


class BgData:
    """The piece tables of BGDATA.S plus the few tables kept elsewhere."""

    def __init__(self, bgdata_text: str, gamebg_text: str, misc_text: str):
        self._asm = assemble(" org 0\n" + bgdata_text)
        self.sym = self._asm.symbols
        self.torchflame = extract_table(gamebg_text, "torchflame")
        self.bubble = extract_table(gamebg_text, "bubble")
        self.level_bgset1 = extract_table(misc_text, "bgset1")
        self.level_bgset2 = extract_table(misc_text, "bgset2")
        self.level_chset = extract_table(misc_text, "chset")

    def tab(self, name: str, index: int) -> int:
        """table[index], reading whatever byte follows if index runs past the
        end, as indexed addressing would; an index outside BGDATA is an error."""
        a = self.sym[name] + index
        if not 0 <= a < len(self._asm.image):
            raise RoomError("%s[%d] is outside BGDATA" % (name, index))
        return self._asm.image[a]

    def const(self, name: str) -> int:
        return self.sym[name] & 0xFF


@dataclass(frozen=True)
class ListEntry:
    x: int
    y: int
    image: int          # coded: bit 7 = bgtable2
    opacity: int
    offset: int = 0
    source: str = ""    # routine that added it (for inspection only)


def u8(v: int) -> int:
    return v & 0xFF


class RoomBuilder:
    """Runs SURE for one screen and collects the image lists."""

    def __init__(self, bg: BgData, level: Level, level_no: int, screen: int):
        self.bg = bg
        self.level = level
        self.level_no = level_no
        self.SCRNUM = screen
        self.BGset1 = bg.level_bgset1[level_no]
        n = level.neighbours(screen) if screen else {"left": 0, "right": 0,
                                                     "above": 0, "below": 0}
        self.scrnLeft, self.scrnRight = n["left"], n["right"]
        self.scrnAbove, self.scrnBelow = n["above"], n["below"]
        self.scrnBelowL = level.neighbours(self.scrnBelow)["left"] if self.scrnBelow else 0
        self.KidStartScrn = level.kid_start["screen"]

        self.back: List[ListEntry] = []
        self.fore: List[ListEntry] = []
        self.mid: List[ListEntry] = []
        self.rejected = 0            # ADDBACK/ADDFORE calls dropped for YCO >= 192
        self.stale_reads: List[str] = []

        # persistent "zero page"
        self.XCO = self.YCO = self.IMAGE = self.OPACITY = self.OFFSET = 0
        self.PRECED = self.spreced = self.objid = self.state = 0
        self.PREV, self.sprev = [0, 0, 0], [0, 0, 0]
        self.BELOW, self.SBELOW = [0] * 10, [0] * 10
        self.rowno = self.colno = self.yindex = 0
        self.Ay = self.Dy = self.blockxco = 0
        self.blockthr = self.gateposn = self.gatebot = self.xsave = 0
        self.doortop = 0
        self._blue = 0               # screen whose blueprint BlueType/BlueSpec address
        self._source = ""

    # --- blueprint access --------------------------------------------------------

    def calcblue(self, screen: int) -> None:
        self._blue = screen          # 0: the original points at menu data / garbage

    def getobjid1(self, y: int) -> int:
        """FRAMEADV.S 2047-2098.  Sets self.state; returns the object id."""
        if not self._blue:
            raise RoomError("blueprint read with no screen selected")
        self.state = self.level.state(self._blue, y)
        objid = self.level.objid(self._blue, y)
        if objid == OBJ["pressplate"]:
            if self.level.linkmap[self.state] & 0x1F >= 2:
                return OBJ["dpressplate"]
            return OBJ["pressplate"]
        if objid == OBJ["upressplate"]:
            if self.level.linkmap[self.state] & 0x1F >= 2:
                self.state = 0
                return OBJ["floor"]
            return OBJ["upressplate"]
        return objid

    def getobjid(self, y: int) -> int:
        if self.SCRNUM == 0:
            return OBJ["space"]      # GOnull: state is left as it was
        return self.getobjid1(y)

    # --- image lists -------------------------------------------------------------

    def addback(self) -> None:       # GRAFIX.S 203-219
        if len(self.back) + 1 >= MAXBACK:
            raise RoomError("background list full")
        if self.YCO >= 192:
            self.rejected += 1
            return
        self.back.append(ListEntry(self.XCO, self.YCO, self.IMAGE, self.OPACITY,
                                   0, self._source))

    def addfore(self) -> None:       # GRAFIX.S 235-246
        if len(self.fore) + 1 >= MAXFORE:
            raise RoomError("foreground list full")
        if self.YCO >= 192:
            self.rejected += 1
            return
        self.fore.append(ListEntry(self.XCO, self.YCO, self.IMAGE, self.OPACITY,
                                   0, self._source))

    def addmidezo(self) -> None:     # GRAFIX.S 392-427 with midTYP = UseLay
        if len(self.mid) + 1 >= MAXMID:
            raise RoomError("mid list full")
        self.mid.append(ListEntry(self.XCO, self.YCO, self.IMAGE, self.OPACITY,
                                  self.OFFSET, self._source))

    add = addback                    # SURE calls setback first (FRAMEADV.S 53)

    def maddfore(self) -> None:      # FRAMEADV.S 1176-1181
        self.OPACITY = OP_MASK
        self.addfore()
        self.OPACITY = OP_ORA
        self.addfore()

    # --- SURE --------------------------------------------------------------------

    def sure(self) -> None:          # FRAMEADV.S 49-179
        self.getprev()
        self.calcblue(self.SCRNUM)
        for row in (2, 1, 0):
            self.rowno = row
            self.Dy = BLOCK_BOT[row + 1]
            self.Ay = u8(self.Dy - 3)
            self.yindex = row * 10
            self.PRECED, self.spreced = self.PREV[row], self.sprev[row]
            self.getbelow()
            for col in range(10):
                self.colno = col
                self.XCO = self.blockxco = col * 4
                self.objid = self.getobjid(self.yindex)
                self.RedBlockSure()
                self.PRECED, self.spreced = self.objid, self.state
                self.yindex += 1

        # bottom row of the screen above: D-sections only
        self.rowno = 2
        self.Dy, self.Ay = 2, 0xFF
        self.yindex = 20
        self.PRECED = 0              # spreced keeps its last value
        saved = (self.scrnBelow, self.scrnBelowL)
        self.scrnBelow, self.scrnBelowL = self.SCRNUM, self.scrnLeft
        self.getbelow()
        self.calcblue(self.scrnAbove)
        for col in range(10):
            self.colno = col
            self.XCO = self.blockxco = col * 4
            if self.scrnAbove == 0:
                self.objid = OBJ["floor"]        # state keeps its last value
            else:
                self.objid = self.getobjid1(self.yindex)
            self.RedDSure()
            self.PRECED, self.spreced = self.objid, self.state
            self.yindex += 1
        self.scrnBelow, self.scrnBelowL = saved

    def getprev(self) -> None:       # FRAMEADV.S 561-602
        if self.scrnLeft == 0:
            self.PREV = [OBJ["block"]] * 3
            self.sprev = [0, 0, 0]
            return
        self.calcblue(self.scrnLeft)
        for i, y in enumerate((9, 19, 29)):
            self.PREV[i] = self.getobjid1(y)
            self.sprev[i] = self.state

    def getbelow(self) -> None:      # FRAMEADV.S 616-700
        x = self.rowno
        if x < 2:
            self.BELOW[0], self.SBELOW[0] = self.PREV[x + 1], self.sprev[x + 1]
            y = self.yindex + 10
            for i in range(1, 10):
                self.BELOW[i] = self.getobjid(y)
                self.SBELOW[i] = self.state
                y += 1
            return
        if self.scrnBelow == 0:
            for i in range(1, 10):
                self.BELOW[i] = 1    # floor; SBELOW keeps its old contents
        else:
            self.calcblue(self.scrnBelow)
            for y in range(8, -1, -1):
                self.BELOW[y + 1] = self.getobjid(y)
                self.SBELOW[y + 1] = self.state
        if self.scrnBelowL == 0:
            self.BELOW[0] = (OBJ["space"] if self.level_no == LEVEL_WITH_OPEN_VOID
                             else OBJ["block"])
        else:
            self.calcblue(self.scrnBelowL)
            self.BELOW[0] = self.getobjid(9)
            self.SBELOW[0] = self.state
        self.calcblue(self.SCRNUM)

    def RedBlockSure(self) -> None:  # FRAMEADV.S 346-359
        self.drawc()
        self.drawmc()
        self.drawb()
        self.drawmb()
        self.drawd()
        self.drawmd()
        self.drawa()
        self.drawma()
        self.drawfrnt()

    def RedDSure(self) -> None:      # FRAMEADV.S 368-374
        self.drawc()
        self.drawmc()
        self.drawb()
        self.drawd()
        self.drawmd()
        self.drawfrnt()

    # --- sections ----------------------------------------------------------------

    def _src(self, name: str) -> None:
        self._source = name

    def checkc(self) -> bool:        # FRAMEADV.S 917-928
        o = self.objid
        return (o == OBJ["space"] or o == OBJ["pillartop"] or o == OBJ["panelwof"]
                or o >= OBJ["archtop1"])

    def drawc(self) -> None:         # 906-910
        if not self.checkc():
            return
        self.dodrawc()
        self.domaskb()

    def dodrawc(self) -> None:       # 935-973
        self._src("dodrawc")
        bg = self.bg
        x = self.BELOW[self.colno]
        if x == OBJ["block"]:
            y = self.SBELOW[self.colno]
            if y >= bg.const("numblox"):
                y = 0
            img = bg.tab("blockc", y)
            if img == 0:
                return
        else:
            img = bg.tab("piecec", x)
            if img == 0:
                return
            if img == bg.const("panelc0"):
                y = self.SBELOW[self.colno]
                if y >= bg.const("numpans"):
                    return
                img = bg.tab("panelc", y)
                if img == 0:
                    return
        self.IMAGE = img
        self.XCO = self.blockxco
        self.YCO = self.Dy
        self.OPACITY = OP_ORA
        self.add()

    def domaskb(self) -> None:       # 980-990
        self._src("domaskb")
        m = self.bg.tab("maskb", self.PRECED)
        if m == 0:
            return
        self.IMAGE = m
        self.YCO = self.Dy
        self.OPACITY = OP_AND
        self.add()                   # XCO is whatever it was

    def drawmc(self) -> None:        # 880-899
        if self.objid not in (OBJ["space"], OBJ["panelwof"], OBJ["pillartop"]):
            return
        if self.BELOW[self.colno] == OBJ["gate"]:
            self.drawgatec()

    def drawb(self) -> None:         # 997-1085
        self._src("drawb")
        bg = self.bg
        if self.objid == OBJ["block"]:
            return
        x = self.PRECED
        if x == OBJ["space"]:
            y = self.spreced
            if y >= bg.const("numbpans") + 1:
                return
            img = bg.tab("spaceb", y)
            if img == 0:
                return
            self.IMAGE = img
            self._drawb_cont(bg.tab("spaceby", y))
            return
        if x == OBJ["floor"]:
            y = self.spreced
            if y >= bg.const("numbpans") + 1:
                y = 0
            img = bg.tab("floorb", y)
            if img == 0:
                return
            self.IMAGE = img
            self._drawb_cont(bg.tab("floorby", y))
            return
        if x == OBJ["block"]:
            y = self.spreced
            if y >= bg.const("numblox"):
                y = 0
            img = bg.tab("blockb", y)
            if img == 0:
                raise RoomError("blockb entry is zero: original falls into :floor")
            self.IMAGE = img
            self._drawb_cont(bg.tab("pieceby", x))
            return
        img = bg.tab("pieceb", x)
        if img == bg.const("panelb0"):
            y = self.spreced
            if y >= bg.const("numpans"):
                return
            img = bg.tab("panelb", y)
            if img == 0:
                return
            self.IMAGE = img
            self._drawb_cont(bg.tab("pieceby", x))
            return                   # no stripe after a panel
        if img != 0:
            self.IMAGE = img
            self._drawb_cont(bg.tab("pieceby", x))
        # stripe (palace background set only)
        if self.BGset1 != 1:
            return
        s = bg.tab("bstripe", self.PRECED)
        if s == 0:
            return
        self._src("drawb:stripe")
        self.IMAGE = s
        self._drawb_cont2(u8(self.Ay - 32))

    def _drawb_cont(self, dy: int) -> None:      # :cont
        self._drawb_cont2(u8(dy + self.Ay))

    def _drawb_cont2(self, y: int) -> None:      # :cont2
        self.YCO = y
        self.XCO = self.blockxco
        self.OPACITY = OP_ORA
        self.add()

    def drawmb(self) -> None:        # 849-873
        p = self.PRECED
        if p == OBJ["gate"]:
            self.drawgateb()
        elif p == OBJ["spikes"]:
            self.drawspikeb()
        elif p == OBJ["loose"]:
            self.drawlooseb()
        elif p == OBJ["torch"]:
            self.drawtorchb()
        elif p == OBJ["exit"]:
            self.drawexitb()

    def drawd(self) -> None:         # 1096-1123
        self._src("drawd")
        bg = self.bg
        self.OPACITY = OP_STA
        x = self.objid
        if x == OBJ["block"]:
            y = self.state
            if y >= bg.const("numblox"):
                y = 0
            img = bg.tab("blockd", y)
            if img == 0:
                raise RoomError("blockd entry is zero: original falls through")
        else:
            if x == OBJ["panelwof"]:
                self.OPACITY = OP_ORA
            img = bg.tab("pieced", x)
            if img == 0:
                return
        self.IMAGE = img
        self.XCO = self.blockxco
        self.YCO = self.Dy
        self.add()

    def drawmd(self) -> None:        # 1239-1246
        if self.objid == OBJ["loose"]:
            self.drawloosed()

    def drawa(self) -> None:         # 1134-1155
        p = self.PRECED
        if p == OBJ["archtop1"]:
            if self.objid == OBJ["panelwof"]:
                self._src("drawa:archpanel")
                self.adda1(self.bg.const("archpanel"), self.objid)
                return
            self.adda()
            return
        if p in (OBJ["panelwif"], OBJ["panelwof"], OBJ["pillartop"], OBJ["block"]):
            self.addamask()
        self.adda()

    def addamask(self) -> None:      # 1183-1193
        self._src("addamask")
        m = self.bg.tab("maska", self.objid)
        if m == 0:
            return
        self.IMAGE = m
        self.XCO = self.blockxco
        self.YCO = self.Ay
        self.OPACITY = OP_AND
        self.add()

    def getloosey(self, a: int) -> int:          # 1433-1448
        if a < 0x80:
            return a
        a &= 0x7F
        if a >= self.bg.const("Ffalling") + 1:
            a = 1
        return a

    def getpiecea(self, x: int) -> int:          # 1413-1423
        if x == OBJ["loose"]:
            return self.bg.tab("loosea", self.getloosey(self.state))
        return self.bg.tab("piecea", x)

    def adda(self) -> None:          # 1195-1207
        self._src("adda")
        x = self.objid
        img = self.getpiecea(x)
        if img == 0:
            return
        self.adda1(img, x)

    def adda1(self, img: int, x: int) -> None:
        self.IMAGE = img
        self.XCO = self.blockxco
        self.YCO = u8(self.Ay + self.bg.tab("pieceay", x))
        self.OPACITY = OP_ORA
        self.add()

    def drawma(self) -> None:        # 1214-1232
        o = self.objid
        if o == OBJ["spikes"]:
            self.drawspikea()
        elif o == OBJ["slicer"]:
            self.drawslicera()
        elif o == OBJ["flask"]:
            self.drawflaska()
        elif o == OBJ["sword"]:
            self.drawsworda()

    def drawfrnt(self) -> None:      # 754-823
        bg = self.bg
        # PRECED == gate: DrawGateBF? needs the kid's position; not modelled.
        x = self.objid
        if x == OBJ["slicer"]:
            self.drawslicerf()
            return
        self._src("drawfrnt")
        img = None
        if x == OBJ["flask"]:
            s = self.state & 0xE0
            if s != 0xA0 and s >= 0x40:
                img = bg.const("specialflask")
        if img is None:
            img = bg.tab("fronti", x)
            if img == 0:
                return
        self.IMAGE = img
        self.YCO = u8(self.Ay + bg.tab("fronty", x))
        self.XCO = u8(self.blockxco + bg.tab("frontx", x))
        plain = False
        if x >= OBJ["archtop2"]:
            plain = True
        elif self.BGset1 != 1 and x == OBJ["posts"]:
            plain = True             # dungeon set: posts stored without masking
        elif x == OBJ["block"]:
            y = self.state
            if y >= bg.const("numblox"):
                y = 0
            self.IMAGE = bg.tab("blockfr", y)
            plain = True
        if plain:
            self.OPACITY = OP_STA
            self.addfore()
        else:
            self.maddfore()

    # --- special pieces ------------------------------------------------------------

    def drawloosed(self) -> None:    # 1365-1381
        self._src("drawloosed")
        img = self.bg.tab("loosed", self.getloosey(self.state))
        if img == 0:
            return
        self.IMAGE = img
        self.XCO = self.blockxco
        self.YCO = self.Dy
        self.OPACITY = OP_STA
        self.add()

    def drawlooseb(self) -> None:    # 1388-1403
        self._src("drawlooseb")
        y = self.getloosey(self.spreced)
        self.IMAGE = self.bg.const("looseb")
        self.YCO = u8(self.Ay + self.bg.tab("looseby", y))
        self.OPACITY = OP_ORA
        self.add()

    def _spike_frame(self, v: int) -> int:
        return self.bg.const("spikeExt") if v >= 0x80 else v

    def drawspikea(self) -> None:    # 1453-1468
        self._src("drawspikea")
        self._spike(self.bg.tab("spikea", self._spike_frame(self.state)))

    def drawspikeb(self) -> None:    # 1473-1488
        self._src("drawspikeb")
        self._spike(self.bg.tab("spikeb", self._spike_frame(self.spreced)))

    def _spike(self, img: int) -> None:
        if img == 0:
            return
        self.IMAGE = img
        self.XCO = self.blockxco
        self.YCO = u8(self.Ay - 1)
        self.OPACITY = OP_ORA
        self.add()

    def drawtorchb(self) -> None:    # 1493-1506, setupflame GAMEBG.S 735-750
        self._src("drawtorchb")
        if self.blockxco == 0:
            return                   # no flame on the leftmost torch
        self.XCO = self.blockxco
        self.YCO = self.Ay
        x = self.spreced
        if x < TORCH_LAST + 1:
            self.IMAGE = self.bg.torchflame[x]
            self.XCO = u8(self.XCO + 1)
            self.YCO = u8(self.YCO - 43)
            self.OPACITY = OP_STA
        else:
            self.stale_reads.append("torch state %d: flame added with stale IMAGE" % x)
        self.addback()

    def drawflaska(self) -> None:    # 1512-1525, setupflask GAMEBG.S 675-721
        self._src("drawflaska")
        self.XCO = self.blockxco
        self.YCO = self.Ay
        x = self.state
        self.OFFSET = 2
        kind = x & 0xE0
        if kind == 0x00:             # empty
            x = 0
        else:
            if kind == 0x40:         # tall flask
                self.YCO = u8(self.YCO - 4)
            elif kind > 0x40:
                self.OFFSET += 1
                self.YCO = u8(self.YCO - 4)
            x &= 0x1F
            if x >= BUBB_LAST + 1:
                x = 0
        self.IMAGE = self.bg.bubble[x]
        self.XCO = u8(self.XCO + 2)
        self.YCO = u8(self.YCO - 14)
        self.OPACITY = OP_STA
        self.addmidezo()

    def drawsworda(self) -> None:    # 1530-1543
        self._src("drawsworda")
        self.IMAGE = (self.bg.const("swordgleam1") if self.state == 1
                      else self.bg.const("swordgleam0"))
        self.XCO = self.blockxco
        self.YCO = self.Ay
        self.OPACITY = OP_STA
        self.add()

    def _slicer_index(self) -> int:
        x = self.state & 0x7F
        if x >= self.bg.const("slicerRet"):
            x = self.bg.const("slicerRet")
        return u8(self.bg.tab("slicerseq", x) - 1)

    def drawslicera(self) -> None:   # 1548-1585
        self._src("drawslicera")
        bg = self.bg
        x = self.xsave = self._slicer_index()
        self.XCO = self.blockxco
        self.YCO = self.Ay
        img = 0
        if self.state >= 0x80:
            img = bg.tab("slicerbot2", x)
        if img == 0:
            img = bg.tab("slicerbot", x)
        if img != 0:
            self.IMAGE = img
            self.OPACITY = OP_ORA
            self.add()
        img = bg.tab("slicertop", x)
        if img == 0:
            return
        self.IMAGE = img
        self.YCO = u8(self.Ay - bg.tab("slicergap", x))
        self.OPACITY = OP_ORA
        self.add()

    def drawslicerf(self) -> None:   # 1590-1611
        self._src("drawslicerf")
        x = self.xsave = self._slicer_index()
        self.XCO = self.blockxco
        self.YCO = self.Ay
        img = self.bg.tab("slicerfrnt", x)
        if img == 0:
            return
        self.IMAGE = img
        self.maddfore()

    def drawexitb(self) -> None:     # 1616-1694
        self._src("drawexitb")
        bg = self.bg
        self.IMAGE = bg.const("stairs")
        self.YCO = u8(self.Ay - 12)
        if self.blockxco >= 36:
            return                   # can't protrude off the right edge
        self.XCO = self.blockxco + 1
        self.OPACITY = OP_STA
        if self.SCRNUM != self.KidStartScrn:
            self.add()               # on the start screen it is an entrance: no stairs
        a = u8(self.Dy - 67)
        if a >= 192:
            return
        self.blockthr = a
        self.gateposn = self.spreced >> 2
        a = u8(self.Ay - 14 - self.gateposn)
        self.doortop = a
        while True:
            self.YCO = a
            self.IMAGE = bg.const("doormask")
            self.OPACITY = OP_AND
            self.add()
            self.IMAGE = bg.const("door")
            self.OPACITY = OP_ORA
            self.add()
            a = u8(self.YCO - 4)
            if a < self.blockthr:
                break
        a = u8(self.Ay - 64)
        if a >= 192:
            return
        self.YCO = a
        self.IMAGE = bg.const("toprepair")
        self.OPACITY = OP_STA
        self.add()

    def drawgatec(self) -> None:     # 1701-1730
        self._src("drawgatec")
        bg = self.bg
        self.YCO = self.Dy
        self.IMAGE = bg.const("gatecmask")
        self.OPACITY = OP_AND
        self.add()
        g = min(self.SBELOW[self.colno], bg.const("gmaxval"))
        self.gateposn = g >> 2
        self.IMAGE = bg.tab("gate8c", self.gateposn % 8)
        self.OPACITY = OP_ORA
        self.add()

    def setupdgb(self) -> int:       # 1742-1762
        self.blockthr = u8(self.Dy - 62)
        g = min(self.spreced, self.bg.const("gmaxval"))
        self.gateposn = (g >> 2) + 1
        self.gatebot = u8(self.Ay - self.gateposn)
        return self.gatebot

    def drawgateb(self) -> None:     # 1814-1907
        self._src("drawgateb")
        bg = self.bg
        a = self.setupdgb()
        if u8(a + 12) < self.Ay:
            self.YCO = self.gatebot
            self.IMAGE = bg.const("gatebotSTA")
            self.OPACITY = OP_STA
            self.addback()
        else:                        # bottom piece crosses the floor line
            self.restorebot()
            self._src("drawgateb")
            self.YCO = u8(self.gatebot - 2)
            self.IMAGE = bg.const("gatebotORA")
            self.OPACITY = OP_ORA
            self.addback()
        self.OPACITY = OP_STA
        self.IMAGE = bg.const("gateB1")
        a = u8(self.gatebot - 12)
        while True:
            self.YCO = a
            if a >= 192:
                return
            if a < 7:
                break                # would stick out of the block area
            if a - 7 < self.blockthr:
                break
            self.addback()
            a = u8(self.YCO - 8)
            if a == 0:
                break
        h = u8(self.YCO - self.blockthr + 1)     # height of the top piece, 1-8
        if h == 0 or h >= 9:
            return
        self.IMAGE = bg.tab("gate8b", h - 1)
        self.addback()

    def restorebot(self) -> None:    # 1910-1927
        self._src("restorebot")
        g = OBJ["gate"]
        self.IMAGE = self.bg.tab("pieceb", g)
        self.YCO = u8(self.bg.tab("pieceby", g) + self.Ay)
        self.XCO = self.blockxco
        self.OPACITY = OP_STA
        self.add()
        if self.checkc():
            self.dodrawc()
        self.drawa()


# --- drawing the lists ----------------------------------------------------------------

def draw_room(builder: RoomBuilder, bgtable1, bgtable2, page: Optional[HiresPage] = None,
              planes: Tuple[str, ...] = ("back", "mid", "fore")) -> HiresPage:
    """DRAWALL for a screen with no characters: CLS, DRAWBACK, DRAWMID, DRAWFORE."""
    if page is None:
        page = HiresPage(0x80)       # CLS fills with $80

    def image(coded: int):
        table = bgtable2 if coded & 0x80 else bgtable1
        n = coded & 0x7F
        if not 1 <= n <= table.count:
            raise RoomError("image %d not in bgtable%d" % (n, 2 if coded & 0x80 else 1))
        return table.images[n - 1]

    if "back" in planes:
        for e in builder.back:       # GRAFIX.S 571-592: fastlay with the stored opacity
            im = image(e.image)
            fastlay(page, im.data, im.width_bytes, im.height, e.x, e.y, e.opacity)
    if "mid" in planes:
        for e in builder.mid:        # GRAFIX.S 738-740: lay with offset
            im = image(e.image)
            lay_gen(page, im.data, im.width_bytes, im.height, e.x, e.offset, e.y, e.opacity)
    if "fore" in planes:
        for e in builder.fore:       # GRAFIX.S 603-626
            im = image(e.image)
            if e.opacity == OP_MASK:
                fastmask(page, im.data, im.width_bytes, im.height, e.x, e.y)
            else:
                fastlay(page, im.data, im.width_bytes, im.height, e.x, e.y, e.opacity)
    return page


def bgset_file_suffix(bgset: int) -> str:
    """Image file used for a background set number.

    Set 1 is the palace (FRAMEADV.S 799-801 comments `cmp #1 ;pal`).  Sets 0
    and 2 are both taken to be the dungeon files: the repository has only
    one dungeon set, and why the game distinguishes 0 from 2 is not yet
    verified (MASTER.S 524-525 loads them from different tracks).
    """
    return "PAL" if bgset == 1 else "DUN"
