#!/usr/bin/env python3
"""Differential test: C player-control, floor, collision, moving-object,
opponent and top-level routines against the original.

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
    potions, the mirror, the input devices), in order.

The original's DIMCHAR reads image tables that live in main memory; here it
is replaced by a trap that answers from the same image files.  Cases in which
the original reaches memory the port does not model (an image number the
table does not hold, an index beyond a table) are counted and skipped, never
compared.

Usage:
    fuzz_control.py <pop_worldrun> <data dir> <original checkout> [cases] [seed] [only]

`only` limits the run to some routines: a comma-separated list of names, or
@control, @collision, @mover, @auto, @top, @loop or @objs for those groups.
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
# collision variables (added later; generated from their own random stream so
# that the control cases stayed what they were)
COLL_VARS = ["blocky", "collideL", "collideR", "collX", "begrange", "endrange", "AMtimer",
             "VisScrn", "scrnLeft", "scrnRight", "scrnBelowL", "scrnBelowR", "createshad",
             "NextLevel", "BlockYthis", "BlockYlast", "SongCue", "SongCount"]
# moving-object variables (again with their own random stream)
MOVER_VARS = ["numtrans", "nummob", "trobcount", "tempnt", "exitopen", "RNDseed",
              "scrnAbove", "scrnBelow", "BGset1"]
# opponent variables (their own random stream too)
AUTO_VARS = ["guardprog", "justblocked", "shadowaction", "PlayCount", "PreRecPtr", "CUTTIMER",
             "cutscrn", "milestone", "MaxKidStr", "MaxOppStr", "origstrength", "GuardColor",
             "lightcolor", "lightning", "ManCtrl", "vibes", "invert", "redrawflg", "msgtimer",
             "mirrx", "dmirr", "timerequest"]
# top-level variables (their own random stream)
TOP_VARS = ["skipmessage", "cutplan", "cutorder", "message", "MinLeft", "SecLeft", "BTN0",
            "BTN1", "waitingtojump", "yellowflag", "scrnAboveL", "scrnAboveR", "SCRNUM"]
# the loop around the frame (their own random stream).  FrameCountH is the
# high byte of the 16-bit FrameCount.
LOOP_VARS = ["keypress", "keydown", "kbdX", "kbdY", "joyX", "joyY", "joyon", "NextTimeMsg",
             "soundon", "FrameCount", "FrameCountH"]
# the object list (its own random stream)
OBJ_VARS = ["FCharIndex", "doortop"]
# keys and the controller (their own random stream)
KEY_VARS = ["SINGSTEP", "develment", "keybufptr", "musicon", "jvert", "jhoriz", "jbtns",
            "jthres1x", "jthres1y", "jthres2x", "jthres2y"]
BYTES = fw.BYTES + EXTRA + COLL_VARS + MOVER_VARS + AUTO_VARS + TOP_VARS + LOOP_VARS + OBJ_VARS \
    + KEY_VARS
WORDS = fw.WORDS + ["FCharX"]
CD_BUFFERS = (("CDthisframe", 16), ("CDlastframe", 16), ("CDbelow", 16), ("CDabove", 16))
SN_BUFFERS = (("SNthisframe", 16), ("SNlastframe", 16), ("SNbelow", 16), ("SNabove", 10))
TROB_LISTS = (("trloc", 32), ("trscrn", 32), ("trdirec", 32))
MOB_LISTS = (("mobx", 16), ("moby", 16), ("mobscrn", 16), ("mobvel", 16), ("mobtype", 16),
             ("moblevel", 16))
# the four buffers random_state fills itself (kept as they were, so that adding
# buffers later does not change the cases the earlier routines get)
BASE_BUFFERS = (("redbuf", 30), ("wipebuf", 30), ("whitebuf", 30), ("topbuf", 10))
OTHER_REDRAW = (("fredbuf", 30), ("movebuf", 30), ("halfbuf", 30), ("floorbuf", 30),
                ("objbuf", 30))
# "script": a made-up movement script for AUTOPLAYBACK, kept in memory the
# original does not use ($9000) and in the runner
SCRIPT_AT = 0x9000
OBJ_LISTS = tuple((n, 20) for n in ("objINDX", "objX", "objOFF", "objY", "objIMG", "objFACE",
                                    "objTYP", "objCU", "objCD", "objCL", "objCR", "objTAB"))
BUFFERS = BASE_BUFFERS + CD_BUFFERS + SN_BUFFERS + OTHER_REDRAW + TROB_LISTS + MOB_LISTS \
    + (("script", 32), ("clrSEL", 5), ("clrDESEL", 5)) + OBJ_LISTS \
    + (("keybuf", 10), ("savedgame", 16))
DEAD_SEQS = []       # sequence addresses one frame before a "dead" frame; set by Harness
STAND_SEQS = []      # the same for the standing frame
FDY = {}             # Fdy of a few of the kid's frames; set by Harness
FHEIGHT = {}         # and the height of their images
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
    # collision (COLL.S)
    "checkbarr": ("COLL", "CHECKBARR", ""), "collisions": ("COLL", "COLLISIONS", ""),
    "checkcoll": ("COLL", "CHECKCOLL", "c"), "checkgate": ("COLL", "CHECKGATE", ""),
    "checkslice": ("COLL", "CHECKSLICE", ""), "checkslice2": ("COLL", "CHECKSLICE2", "c"),
    "enemycoll": ("COLL", "ENEMYCOLL", ""), "goneupstairs": ("COLL", "GoneUpstairs", ""),
}
MOVER = {
    # moving objects (MOVER.S and the routines that set them off)
    "rnd": ("GRAFIX", "RND", "a"), "addtrob": ("MOVER", "addtrob", ""),
    "animtrans": ("MOVER", "ANIMTRANS", ""), "animmobs": ("MOVER", "ANIMMOBS", ""),
    "trigslicer": ("MOVER", "TRIGSLICER", ""), "closeexit": ("MOVER", "CLOSEEXIT", ""),
    "smashmirror": ("MOVER", "SMASHMIRROR", ""), "trigflask": ("MOVER", "TRIGFLASK", ""),
    "trigsword": ("MOVER", "TRIGSWORD", ""), "trigtorch": ("MOVER", "TRIGTORCH", ""),
    "trigspikes": ("MOVER", "TRIGSPIKES", ""), "jamspikes": ("MOVER", "JAMSPIKES", ""),
    "breakloose": ("MOVER", "BREAKLOOSE", ""), "breakloose1": ("MOVER", "BREAKLOOSE1", ""),
    "pushpp": ("MOVER", "PUSHPP", ""), "jampp": ("MOVER", "JAMPP", ""),
    "getflameframe": ("MOVER", "GETFLAMEFRAME", "a"),
    "getflaskframe": ("MOVER", "GETFLASKFRAME", "a"),
    "shakem": ("MOVER", "SHAKEM", ""), "checkpress": ("CTRL", "CHECKPRESS", ""),
    "checkspikes": ("CTRLSUBS", "CHECKSPIKES", ""), "shakeloose": ("TOPCTRL", "shakeloose", ""),
    "addslicers": ("SUBS", "ADDSLICERS", ""), "addtorches": ("SUBS", "ADDTORCHES", ""),
    "crumble": ("SUBS", "CRUMBLE", ""), "mirappear": ("SUBS", "MIRAPPEAR", ""),
    "addlowersound": ("SUBS", "ADDLOWERSOUND", ""),
}
ROUTINES.update(MOVER)
# routines that take the block just read (Y, BlueSpec): the piece the test
# puts there, and states worth trying
TAKES_BLOCK = {
    "trigslicer": (18, (0, 1, 2, 5, 6, 7, 15, 0x82, 0x86, 0x80)),
    "closeexit": (16, (0, 4, 172)), "smashmirror": (13, (0, 5)),
    "trigflask": (10, (0, 0x20, 0x40, 0xA0, 0xE7)), "trigsword": (22, (0, 1, 40)),
    "trigtorch": (19, (0, 5, 17)),
    "trigspikes": (2, (0, 0, 1, 4, 5, 8, 0x80, 0x81, 0x8F, 0xFF)),
    "jamspikes": (2, (0, 3, 0x85, 0xFF)),
    "breakloose": (11, (0, 0, 1, 5, 0x80, 0x82)), "breakloose1": (11, (0, 0, 1, 5, 0x80, 0x82)),
    "pushpp": (6, None), "jampp": (6, None),
}
ANIMATED = {  # piece -> states for objects on the transition list
    19: (0, 5, 16, 17, 18, 200), 6: None, 15: None,
    2: (0, 1, 4, 5, 6, 8, 9, 10, 0x80, 0x81, 0x82, 0x8F, 143, 0xFF),
    11: (0, 1, 8, 9, 10, 0x80, 0x82, 0x83, 0x84, 0xF5, 0xFF), 0: (0, 1),
    18: (0, 1, 2, 5, 6, 14, 15, 0x81, 0x82, 0x85, 0x8F, 0x86),
    4: (0, 1, 2, 4, 19, 20, 21, 40, 60, 80, 100, 120, 184, 185, 187, 188, 189, 237, 238,
        0xFF),
    16: (0, 4, 19, 20, 40, 60, 80, 100, 120, 164, 167, 168, 171, 172),
    10: (0, 7, 8, 0x28, 0xE8, 0x41), 22: (0, 1, 2, 40), 1: (0, 3), 20: (0,),
}

AUTO = {
    # opponents, sword hits, strength, screen changes (AUTO.S, MISC.S and helpers)
    "autoctrl": ("AUTO", "AUTOCTRL", ""), "shadctrl": ("CTRL", "SHADCTRL", ""),
    "autoplayback": ("AUTO", "AUTOPLAYBACK", ""), "playscript": ("AUTO", "AUTOPLAYBACK", ""),
    "doshad": ("TOPCTRL", "DoShad", ""),
    "checkstrike": ("AUTO", "CHECKSTRIKE", ""), "checkstab": ("AUTO", "CHECKSTAB", ""),
    "stabchar": ("MISC", "STABCHAR", ""), "chgmeters": ("TOPCTRL", "chgmeters", ""),
    "rechargemeter": ("CTRLSUBS", "RECHARGEMETER", ""),
    "boostmeter": ("CTRLSUBS", "BOOSTMETER", ""), "markmeters": ("MISC", "MARKMETERS", ""),
    "unholy": ("MISC", "UNHOLY", ""), "kill0": ("TOPCTRL", "kill0", ""),
    "deathsong": ("TOPCTRL", "deathsong", ""), "deadenemy": ("SUBS", "DEADENEMY", ""),
    "checkalert": ("MISC", "CHECKALERT", ""), "firstguard": ("MISC", "FIRSTGUARD", ""),
    "vanishchar": ("MISC", "VANISHCHAR", ""), "bonesrise": ("MISC", "BONESRISE", ""),
    "mouserescue": ("MISC", "MOUSERESCUE", ""), "potioneffect": ("MISC", "POTIONEFFECT", ""),
    "reflection": ("MISC", "REFLECTION", ""), "cutcheck": ("AUTO", "CUTCHECK", ""),
    "cutguard": ("AUTO", "CUTGUARD", ""), "addguard": ("AUTO", "ADDGUARD", ""),
    "initialguards": ("SUBS", "INITIALGUARDS", ""),
    "cutchar": ("AUTO", "cutchar", "a"), "cut": ("AUTO", "CUT", ""),
}
ROUTINES.update(AUTO)
TOP = {
    # the frame and the level as a whole (TOPCTRL.S and what it calls)
    "nextframe": ("TOPCTRL", "NextFrame", ""), "dokid": ("TOPCTRL", "DoKid", ""),
    "ctrlplayer": ("TOPCTRL", "ctrlplayer", ""), "misctimers": ("TOPCTRL", "misctimers", ""),
    "wtlessflash": ("TOPCTRL", "wtlessflash", ""), "addsfx": ("TOPCTRL", "addsfx", ""),
    "static": ("TOPCTRL", "static?", "a"), "cold": ("TOPCTRL", "cold?", "a"),
    "getscrns": ("CTRLSUBS", "GETSCRNS", ""), "prepcut": ("TOPCTRL", "PrepCut", ""),
    "firstframe": ("TOPCTRL", "FirstFrame", ""), "democtrl": ("CTRL", "DemoCtrl", ""),
    "setinitials": ("SUBS", "SETINITIALS", ""), "startkid": ("SUBS", "STARTKID", ""),
    "startkid1": ("SUBS", "STARTKID1", ""), "entrance": ("TOPCTRL", "entrance", ""),
    "initcdbuf": ("TOPCTRL", "initCDbuf", ""),
    # RESTART from the point where the level has been loaded (no label there)
    "restartlevel": ("TOPCTRL", None, ""),
}
ROUTINES.update(TOP)
LOOP = {
    # the loop around the frame: input, clock, messages, songs, level change
    "kread": ("SPECIALK", "KREAD", ""), "getselect": ("GRAFIX", "GETSELECT", ""),
    "getdesel": ("GRAFIX", "GETDESEL", ""), "clrjstk": ("SPECIALK", "CLRJSTK", ""),
    "saveselect": ("SPECIALK", "SAVESELECT", ""), "loadselect": ("SPECIALK", "LOADSELECT", ""),
    "savedesel": ("SPECIALK", "SAVEDESEL", ""), "loaddesel": ("SPECIALK", "LOADDESEL", ""),
    "initinput": ("SPECIALK", "INITINPUT", ""), "clearjoy": ("TOPCTRL", "clearjoy", ""),
    "demokeys": ("SPECIALK", "DEMOKEYS", "a"),
    "keeptime": ("SPECIALK", "KEEPTIME", ""), "getminleft": ("SPECIALK", "GETMINLEFT", ""),
    "shortentime": ("SPECIALK", "SHORTENTIME", ""), "showtime": ("SUBS", "SHOWTIME", ""),
    "dispmsg": ("TOPCTRL", "dispmsg", ""), "flashon": ("TOPCTRL", "flashon", ""),
    "flashoff": ("TOPCTRL", "flashoff", ""), "songcues": ("TOPCTRL", "songcues", ""),
    "frameadv": ("TOPCTRL", "FrameAdv", ""), "cleancut": ("TOPCTRL", "DoCleanCut", ""),
    "zerored": ("GRAFIX", "ZERORED", ""), "initgame": ("TOPCTRL", "initgame", ""),
    "startgame": ("TOPCTRL", "StartGame", ""),
    "loadnextlevel": ("TOPCTRL", "LoadNextLevel", ""),
    "loadnext1": ("TOPCTRL", "LoadNext1", ""),
    # one pass of MainLoop: entered after its `jsr rnd`, left where it loops
    "mainloop": ("TOPCTRL", None, ""),
}
ROUTINES.update(LOOP)
OBJS = {
    # the object list: what is to be drawn besides the level
    "markfloor": ("CTRLSUBS", "MARKFLOOR", "c"), "markhalf": ("CTRLSUBS", "MARKHALF", "c"),
    "addfcharx": ("CTRLSUBS", "ADDFCHARX", ""), "addcharobj": ("GAMEBG", "ADDCHAROBJ", ""),
    "indexchar": ("CTRLSUBS", "INDEXCHAR", ""), "quickfloor": ("CTRLSUBS", "QUICKFLOOR", ""),
    "quickfg": ("CTRLSUBS", "QUICKFG", ""), "cropchar": ("CTRLSUBS", "CROPCHAR", ""),
    "unevenfloor": ("CTRLSUBS", "UNEVENFLOOR", ""),
    "setupsword": ("CTRLSUBS", "SETUPSWORD", ""), "setupcomix": ("GAMEBG", "SETUPCOMIX", ""),
    "setupkid": ("TOPCTRL", "setupkid", ""), "setupshad": ("TOPCTRL", "setupshad", ""),
    "addchars": ("TOPCTRL", "addchars", ""), "addmobs": ("MOVER", "ADDMOBS", ""),
}
ROUTINES.update(OBJS)
KEYGROUP = {
    # the keyboard's special keys and code words, the controller, saved games
    "keys": ("SPECIALK", "KEYS", ""), "legitkeys": ("SPECIALK", "LegitKeys", ""),
    "develkeys": ("SPECIALK", "DevelKeys", ""), "checkcodes": ("SPECIALK", "checkcodes", ""),
    "addkey": ("SPECIALK", "addkey", ""), "strobe": ("SPECIALK", "STROBE", ""),
    "jread": ("GRAFIX", "JREAD", ""), "bread": ("GRAFIX", "BREAD", ""),
    "controller": ("GRAFIX", "CONTROLLER", ""), "setcenter": ("GRAFIX", "SETCENTER", ""),
    "dosavegame": ("MISC", "DOSAVEGAME", ""), "startresume": ("TOPCTRL", "STARTRESUME", ""),
    # one pass of MainLoop with its STROBE: the keyboard and the controller
    # as hardware, where "mainloop" has the variables they leave
    "mainloopk": ("TOPCTRL", None, ""),
}
ROUTINES.update(KEYGROUP)
# the special keys (SPECIALK.S 91-108), and keys that are none
SPECIAL_KEYS = (0x9B, 0x92, 0x81, 0x93, 0x8E, 0x8B, 0x8A, 0x87, 0x96, 0xA0, 0x98, 0x99, 0x8D)
CODE_WORDS = ("SKIP", "POP", "BOOST", "R", "ZAP", "Z", "TINA")
# these work on a character as SETUPCHAR and GETEDGES leave it
OBJ_CHAINED = ("indexchar", "quickfloor", "quickfg", "cropchar", "setupsword", "setupcomix")
FIGHT_FRAMES = (15, 150, 151, 152, 153, 154, 157, 158, 158, 161, 162, 165, 166, 167, 168, 169,
                170, 171, 171)
OTHER_FRAMES = (2, 3, 7, 9, 14, 15, 33, 34, 40, 43, 44, 101, 102, 110, 117, 118, 126, 127, 132,
                133, 135, 149, 162, 163, 166, 168, 169, 177, 178, 185, 219, 228, 229)
# distances the fight code compares with, and their neighbours
RANGES = (-16, -15, -14, -8, -7, -4, -3, 0, 7, 8, 9, 10, 11, 12, 13, 28, 29, 30, 34, 35, 36,
          39, 40, 41, 49, 50, 51, 89, 90, 91, 126, 127, 128, 140)

COLLISION = ("checkbarr", "collisions", "checkcoll", "checkgate", "checkslice",
             "checkslice2", "enemycoll", "goneupstairs")

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
    for name, size in BASE_BUFFERS:
        bufs[name] = bytes(rng.choice((0, 0, 0, 2, rng.randrange(256))) for _ in range(size))
    return s, pokes, bufs


def collision_state(rng, s, pokes, bufs, routine):
    """Values for the collision variables and buffers; for the collision
    routines also the situations they exist for."""
    for n in COLL_VARS:
        s[n] = rng.randrange(256)
    scrn = s["CharScrn"]
    s["AMtimer"] = rng.choice((0, 0, 0, 0, 0, 3))
    s["VisScrn"] = scrn if rng.random() < 0.7 else rng.randint(0, 24)
    for n in ("scrnLeft", "scrnRight", "scrnBelowL", "scrnBelowR"):
        s[n] = rng.randint(0, 24)
    s["collideL"] = rng.choice((0xFF, 0xFF) + tuple(range(10)))
    s["collideR"] = rng.choice((0xFF, 0xFF) + tuple(range(10)))
    s["BlockYthis"] = s["CharBlockY"]
    s["BlockYlast"] = (s["CharBlockY"] + rng.choice((0, 0, 0, 1, -1, 3, -3, -2, 2, 7))) & 0xFF
    for name, size in CD_BUFFERS:
        bufs[name] = bytes(rng.choice((0x00, 0x00, 0x0F, 0xF0, 0xFF, 0xFF)) for _ in range(size))
    for name, size in SN_BUFFERS:
        bufs[name] = bytes(rng.choice((scrn, scrn, scrn, scrn, 0xFF, 0, rng.randint(0, 24)))
                           for _ in range(size))
    if routine not in COLLISION:
        return
    r = rng.random()
    row = s["CharBlockY"] if s["CharBlockY"] < 3 else 1
    base = (scrn - 1) * 30 + row * 10
    if routine in ("checkslice", "checkslice2", "checkcoll", "collisions") and r < 0.6:
        for _ in range(rng.choice((1, 2, 4))):               # slicers on his row
            col = rng.randrange(10)
            pokes[base + col] = 18
            pokes[720 + base + col] = rng.choice((2, 2, 2, 0x82, 0, 1, 3, 0x81))
    if routine in ("checkgate", "enemycoll", "collisions", "checkcoll") and 0.3 <= r:
        for _ in range(rng.choice((1, 2, 3))):               # gates, mostly nearly shut
            col = rng.randrange(10)
            pokes[base + col] = 4
            pokes[720 + base + col] = rng.choice((0, 0, 4, 40, 60, 100, 120, 140, 188))
    if routine == "checkgate":
        s["CharPosn"] = rng.choice((15, 15, 15, 107, 108, 109, 110, 111, 7, 50))
        s["CharAction"] = rng.choice((0, 1, 7))
        if rng.random() < 0.7:                               # inside the gate's column
            for name in ("CDthisframe", "CDlastframe"):
                bufs[name] = bytes(rng.choice((0xFF, 0xFF, 0xFF, 0xFF, 0x0F)) for _ in range(16))
    if routine == "checkslice" and rng.random() < 0.7:
        bufs["CDthisframe"] = bytes(rng.choice((0xFF, 0xFF, 0x0F, 0xF0, 0)) for _ in range(16))
    if routine == "enemycoll" and rng.random() < 0.8:
        s.update(CharAction=1, CharLife=0xFF, CharSword=2, AMtimer=0,
                 CharID=rng.choice((0, 1, 2, 4)), CharPosn=rng.choice((150, 158, 161, 170, 171)))
        for _ in range(rng.choice((1, 2))):                  # walls to back into
            col = rng.randrange(10)
            pokes[base + col] = rng.choice((20, 7, 7, 12))
    if routine == "collisions" and rng.random() < 0.8:
        s.update(AMtimer=rng.choice((0, 0, 0, 0, 0, 0, 2)), CharLife=rng.choice((0xFF,) * 5 + (0, 3)),
                 CharAction=rng.choice((0, 1, 1, 3, 4, 4, 5, 2, 6, 7)),
                 CharPosn=rng.choice((7, 9, 11, 15, 23, 24, 25, 26, 39, 39, 40, 41, 42, 43, 43,
                                      43, 44, 101, 102, 106, 107, 107, 134, 135, 148, 149,
                                      158, 171, 177)),
                 CharYVel=rng.choice((0, 0, 3, 21, 22, 30)))
        if rng.random() < 0.5:                               # exactly one side hit
            s["collideL" if rng.random() < 0.5 else "collideR"] = 0xFF
        for _ in range(rng.choice((1, 2, 3))):               # barriers on his row
            col = rng.randrange(10)
            pokes[base + col] = rng.choice((20, 20, 7, 12, 13, 4, 18))
            if rng.random() < 0.4 and row < 2:               # sometimes nothing under it
                pokes[base + col + 10] = 0
        row_y = FLOOR_Y[row]
        s["CharY"] = (row_y - rng.choice((0, 0, 0, 5, 14, 15, 16, 40))) & 0xFF
    if routine == "collisions" and rng.random() < 0.3:
        # a plain hit: floor under him, a wall in the next column, facing it
        col = rng.randint(1, 8)
        right = rng.random() < 0.5
        wall = col + 1 if right else col - 1
        pokes[base + col] = 1
        pokes[base + wall] = rng.choice((20, 20, 7, 12))
        s.update(CharBlockY=row, CharY=FLOOR_Y[row], CharYVel=rng.choice((0, 0, 21, 22)),
                 CharLife=0xFF, CharSword=rng.choice((0, 0, 0, 2)), AMtimer=0,
                 CharAction=rng.choice((0, 1)), CharFace=0x00 if right else 0xFF,
                 CharX=58 + 14 * col + rng.randint(0, 13),
                 CharPosn=rng.choice((15, 23, 24, 25, 26, 39, 40, 42, 43, 44, 101, 102, 106,
                                      107)))
        s["collideL"], s["collideR"] = (wall, 0xFF) if right else (0xFF, wall)
        bufs["SNthisframe"] = bytes([scrn] * 16)
    if routine == "checkcoll" and rng.random() < 0.4:
        # a gate or slicer in each kind of state, read directly
        y = rng.randrange(30)
        piece = rng.choice((4, 18))
        s["_y"], s["_a"], s["_noreread"] = y, piece, 1
        s["imheight"] = rng.choice((20, 34, 39, 40, 41, 45))
        pokes[(scrn - 1) * 30 + y] = piece
        pokes[720 + (scrn - 1) * 30 + y] = rng.choice(
            (0, 132, 136, 140, 156, 160, 188) if piece == 4 else (0, 1, 2, 2, 3, 0x82, 0x82, 0x81))
    elif routine == "checkcoll" and rng.random() < 0.4:      # the mirror, mid running jump
        s.update(CharID=rng.choice((0, 0, 0, 1)), CharPosn=rng.choice((38, 39, 41, 43, 44)),
                 CharFace=rng.choice((0xFF, 0xFF, 0x00)))
    if routine == "checkslice2" and rng.random() < 0.35:
        # edges given directly, at and around a shut slicer's own edges
        col = rng.randrange(10)
        edge = 58 + 14 * col + 7                             # blades occupy edge..edge+2
        pokes[base + col] = 18
        pokes[720 + base + col] = rng.choice((2, 2, 0x82, 3))
        s.update(CharBlockX=(col - rng.choice((0, 0, 1))) & 0xFF, CharBlockY=row,
                 CDRightEj=edge + rng.choice((-1, 0, 1, 5)),
                 CDLeftEj=edge + 2 + rng.choice((-5, -1, 0, 1)), _noreread=1, _plain=1)
    if routine == "goneupstairs":
        s["level"] = rng.choice((1, 4, 4, 12, 13, 13, 14))


def put_links(rng, pokes, scrn, first, count):
    """Link entries first..first+count-1 pointing at gates and exits (and
    sometimes at nothing in particular), as PUSHPP's chain expects."""
    for k in range(count):
        target = scrn if rng.random() < 0.7 else rng.randint(1, 24)
        loc = rng.randrange(30)
        last = 0x80 if k == count - 1 else 0
        x = (first + k) & 0xFF
        pokes[1440 + x] = loc | ((target & 3) << 5) | last
        pokes[1696 + x] = ((target >> 2) << 5) | rng.choice((0, 0, 1, 2, 4, 5, 31))
        piece = rng.choice((4, 4, 4, 16, 1))
        pokes[(target - 1) * 30 + loc] = piece
        pokes[720 + (target - 1) * 30 + loc] = rng.choice(ANIMATED[piece])
    if rng.random() < 0.15:
        pokes[1440 + first] = 0xFF                           # linked to nothing


def mover_state(rng, s, pokes, bufs, routine):
    """The transition and falling-floor lists and their variables; for the
    moving-object routines also the pieces and states they exist for."""
    scrn = s["CharScrn"]
    s["numtrans"] = rng.choice((0, 0, 1, 2, 3, 5, 30, 31))
    s["nummob"] = rng.choice((0, 0, 1, 2, 3, 14, 15))
    s["trobcount"], s["tempnt"] = rng.randrange(256), rng.randrange(256)
    s["exitopen"] = rng.choice((0, 1))
    s["RNDseed"] = rng.randrange(256)
    s["scrnAbove"], s["scrnBelow"] = rng.randint(0, 24), rng.randint(0, 24)
    s["BGset1"] = rng.choice((0, 0, 1, 2))
    vis = s["VisScrn"]
    near = (vis, vis, vis, scrn, s["scrnLeft"] or 1, s["scrnAbove"] or 1, s["scrnBelow"] or 1,
            s["scrnBelowL"] or 1)
    bufs["trloc"] = bytes(rng.randrange(30) if rng.random() < 0.95 else rng.choice((30, 31))
                          for _ in range(32))
    bufs["trscrn"] = bytes(rng.choice(near) if rng.random() < 0.8 else rng.randint(1, 24)
                           for _ in range(32))
    bufs["trdirec"] = bytes(rng.choice((0, 1, 1, 2, 3, 4, 7, 8, 0xFF)) for _ in range(32))
    bufs["mobx"] = bytes(4 * rng.randrange(10) for _ in range(16))
    # (179-206: reaches line 209, where a floor leaves the null screen, after
    # the speeds below are added; 196-223: reaches 226, the "still above the
    # screen" limit)
    bufs["moby"] = bytes(rng.choice((rng.randrange(256), 60, 62, 65, 125, 128, 185, 188, 191,
                                     208, 209, 240, 179, 180, 194, 203, 206, 196, 197, 211,
                                     220, 223)) for _ in range(16))
    bufs["mobscrn"] = bytes(rng.choice((scrn, scrn, scrn, 0, rng.randint(1, 24)))
                            for _ in range(16))
    bufs["mobvel"] = bytes(rng.choice((0, 3, 12, 27, 29, 30, 0xFE, 0xFF)) for _ in range(16))
    bufs["mobtype"] = bytes(rng.choice((0, 0, 0, 0, 1)) for _ in range(16))
    bufs["moblevel"] = bytes(rng.choice((0, 1, 2, 2)) for _ in range(16))
    for name, size in OTHER_REDRAW:
        bufs[name] = bytes(rng.choice((0, 0, 0, 2)) for _ in range(size))
    if routine not in MOVER:
        return
    r = rng.random()
    s["level"] = rng.choice((1, 3, 3, 4, 4, 12, 13, 13))
    s["tempscrn"] = scrn if r < 0.8 else rng.randint(0, 24)
    s["KidLife"] = rng.choice((0xFF, 0xFF, 0))
    s["KidBlockY"] = rng.choice((0, 1, 2))

    if routine in TAKES_BLOCK:                               # the block "just read"
        piece, states = TAKES_BLOCK[routine]
        y = rng.randrange(30)
        s["_y"], s["_noreread"] = y, 1
        s["_a"] = rng.choice((0, 1, 15, 0x8F, scrn, vis, rng.randrange(256)))
        if routine in ("pushpp", "jampp"):
            piece = rng.choice((6, 6, 15, 15, 14))
            link = rng.randrange(256)
            pokes[720 + (scrn - 1) * 30 + y] = link
            put_links(rng, pokes, scrn, link, rng.choice((1, 2, 3)))
        else:
            pokes[720 + (scrn - 1) * 30 + y] = rng.choice(states)
        pokes[(scrn - 1) * 30 + y] = piece | rng.choice((0, 0, 0, 0x20, 0x40))
        # is it already on the list?
        if rng.random() < 0.4 and s["numtrans"]:
            i = rng.randint(1, s["numtrans"])
            for name, v in (("trloc", y), ("trscrn", s["tempscrn"])):
                b = bytearray(bufs[name])
                b[i] = v
                bufs[name] = bytes(b)
    elif routine == "animtrans":
        s["numtrans"] = rng.choice((0, 1, 2, 3, 3, 5, 8, 31))
        if rng.random() < 0.3:                               # at a corner of a neighbouring screen
            b = {n: bytearray(bufs[n]) for n in ("trloc", "trscrn")}
            b["trscrn"][1] = rng.choice((s["scrnBelowL"], s["scrnLeft"], s["scrnBelow"],
                                         s["scrnAbove"])) or 1
            b["trloc"][1] = rng.choice((9, 9, 19, 29, 0, 8, 20, 25))
            bufs["trloc"], bufs["trscrn"] = bytes(b["trloc"]), bytes(b["trscrn"])
        for i in range(1, s["numtrans"] + 1):                # something animated at each entry
            t, loc = bufs["trscrn"][i], bufs["trloc"][i]
            piece = rng.choice(list(ANIMATED))
            pokes[(t - 1) * 30 + loc] = piece | rng.choice((0, 0, 0x20))
            if piece in (6, 15):
                link = rng.randrange(256)
                pokes[720 + (t - 1) * 30 + loc] = link
                pokes[1696 + link] = rng.choice((0, 1, 2, 3, 5, 31)) | (rng.randrange(8) << 5)
            else:
                pokes[720 + (t - 1) * 30 + loc] = rng.choice(ANIMATED[piece])
    elif routine == "animmobs":
        s["nummob"] = rng.choice((0, 1, 1, 2, 3, 15))
        if r < 0.5 and s["nummob"]:                          # the kid under the first one
            if rng.random() < 0.5:                           # ... which has stopped moving
                b = bytearray(bufs["mobvel"])
                b[1] = 0xFE
                bufs["mobvel"] = bytes(b)
            s.update(KidScrn=bufs["mobscrn"][1], KidBlockX=bufs["mobx"][1] >> 2,
                     KidBlockY=bufs["moblevel"][1],
                     KidY=(bufs["moby"][1] + rng.choice((0, 1, 10, 29, 30, 31, 60))) & 0xFF,
                     KidPosn=rng.choice((4, 5, 9, 14, 15, 15, 109, 109)),
                     KidAction=rng.choice((0, 1, 1, 2, 4, 7)), KidLife=0xFF,
                     KidStrength=rng.choice((1, 2, 3)))
        for i in range(1, s["nummob"] + 1):                  # what it lands on
            t = bufs["mobscrn"][i]
            if not t:
                continue
            at = (t - 1) * 30 + bufs["moblevel"][i] * 10 + (bufs["mobx"][i] >> 2)
            piece = rng.choice((0, 0, 1, 1, 2, 6, 15, 10, 11, 11, 19, 4, 20))
            pokes[at] = piece
            if piece in (6, 15):
                link = rng.randrange(256)
                pokes[720 + at] = link
                put_links(rng, pokes, t, link, rng.choice((1, 2)))
            else:
                pokes[720 + at] = rng.choice((0, 0, 1, 0x80))
    elif routine in ("checkpress", "shakem", "shakeloose", "crumble"):
        row = s["CharBlockY"] if s["CharBlockY"] < 3 else 1
        base = (scrn - 1) * 30 + row * 10
        for _ in range(rng.choice((1, 2, 3))):               # plates and loose floors about
            col = rng.randrange(10)
            piece = rng.choice((6, 15, 11, 11))
            for b in ((base, (vis - 1) * 30 + rng.choice((0, 10, 20))) if vis else (base,)):
                pokes[b + col] = piece | rng.choice((0, 0, 0x20))
                if piece == 11:
                    pokes[720 + b + col] = rng.choice((0, 0, 0, 1, 0x80))
                else:
                    link = rng.randrange(256)
                    pokes[720 + b + col] = link
                    put_links(rng, pokes, scrn, link, rng.choice((1, 2)))
        if routine == "checkpress":
            s.update(CharPosn=rng.choice((15, 15, 9, 79, 79, 87, 99, 100, 109, 134, 135, 140,
                                          141, 44)),
                     CharAction=rng.choice((0, 1, 1, 2, 3, 5, 7)),
                     CharLife=rng.choice((0xFF, 0xFF, 0xFF, 0)))
        if routine == "shakeloose":
            s["jarabove"] = rng.choice((0, 1, 1, 0xFF, 0xFF))
        if routine == "crumble":
            s["level"] = rng.choice((13, 13, 13, 12))
            s["VisScrn"] = rng.choice((23, 16, 16, 5))
            above = rng.randint(1, 24)
            s["scrnAbove"] = above
            for col in range(1, 9):
                if rng.random() < 0.6:
                    pokes[(above - 1) * 30 + 20 + col] = 11 | rng.choice((0, 0, 0x20))
                    pokes[720 + (above - 1) * 30 + 20 + col] = rng.choice((0, 0, 3, 0x80))
    elif routine == "checkspikes":
        row = s["CharBlockY"] if s["CharBlockY"] < 3 else 1
        for _ in range(rng.choice((1, 2, 4))):               # spikes under and below him
            col, rr = rng.randrange(10), rng.choice((row, row, min(2, row + 1)))
            at = (scrn - 1) * 30 + rr * 10 + col
            pokes[at] = 2
            pokes[720 + at] = rng.choice((0, 0, 0, 3, 0x85, 0xFF))
            if rr > row and rng.random() < 0.8:
                pokes[at - 10] = 0
    elif routine == "addslicers":
        row = s["CharBlockY"] if s["CharBlockY"] < 3 else 1
        for _ in range(rng.choice((0, 1, 3, 5))):
            at = (scrn - 1) * 30 + row * 10 + rng.randrange(10)
            pokes[at] = 18
            pokes[720 + at] = rng.choice((0, 0, 1, 5, 6, 9, 0x80, 0x83, 0x8A))
    elif routine == "addtorches":
        if vis:
            for _ in range(rng.choice((0, 2, 5))):
                at = (vis - 1) * 30 + rng.randrange(30)
                pokes[at] = rng.choice((19, 10, 22)) | rng.choice((0, 0x20))
                pokes[720 + at] = rng.choice((0, 0x20, 0xA0))
    elif routine == "getflameframe" and r < 0.5:
        # the random number comes out equal to the present frame: the seed
        # that gives 5*seed + 23 = frame (5 * 205 = 1 mod 256)
        frame = rng.randrange(18)
        s["RNDseed"] = ((frame - 23) * 205) & 0xFF
        s["_a_only"] = frame
    elif routine == "mirappear":
        s["level"] = rng.choice((4, 4, 4, 3))
    elif routine in ("addlowersound", "addtrob"):
        b = {n: bytearray(bufs[n]) for n in ("trloc", "trscrn", "trdirec")}
        b["trscrn"][0] = rng.choice((vis, vis, s["scrnLeft"], 2, rng.randint(1, 24)))
        b["trloc"][0] = rng.choice((9, 19, 29, 9, 3, 14, 28))
        if routine == "addtrob" and s["numtrans"] and rng.random() < 0.5:
            i = rng.randint(1, s["numtrans"])                # already listed
            b["trloc"][0], b["trscrn"][0] = b["trloc"][i], b["trscrn"][i]
        for n in b:
            bufs[n] = bytes(b[n])
        s["level"] = rng.choice((3, 3, 1))
        s["_noreread"] = 1


def place_at(rng, d, x, face):
    """Where to put the other character, and which way he faces, so that
    GETOPDIST from a character at x facing `face` gives d (signed).  The
    routine adds 13 when the two face each other, so facing each other the
    measure is never 0-12."""
    opposite = d >= 13 and rng.random() < 0.75
    raw = d - 13 if opposite else d
    other_x = (x + (raw if face == 0 else -raw)) & 0xFF
    return other_x, (face ^ 0xFF) if opposite or (d < 0 and rng.random() < 0.5) else face


def fighter(rng, s, rec, scrn, row, x, face):
    """Fill one character record with someone who could be in a fight."""
    s.update({rec + "Scrn": scrn, rec + "BlockY": row, rec + "Y": FLOOR_Y.get(row, 55),
              rec + "X": x & 0xFF, rec + "Face": face,
              rec + "BlockX": ((x - 58) // 14) & 0xFF,
              rec + "Posn": rng.choice(FIGHT_FRAMES if rng.random() < 0.6 else OTHER_FRAMES),
              rec + "Sword": rng.choice((0, 2, 2)), rec + "Action": rng.choice((0, 1, 1, 2, 5, 6, 99)),
              rec + "Life": rng.choice((0xFF,) * 6 + (0, 1)),
              rec + "XVel": 0, rec + "YVel": 0})


def auto_state(rng, s, pokes, bufs, routine):
    """Opponent variables; for the opponent routines also two characters set
    against each other at the distances the code distinguishes, the level
    places it knows by number, and guard-start entries."""
    s["guardprog"] = rng.randrange(12) if rng.random() < 0.97 else rng.randrange(12, 30)
    s["justblocked"] = rng.choice((0, 0, 1, 4))
    s["shadowaction"] = rng.choice((0, 0, 1))
    s["PlayCount"] = rng.choice((0, 0, 1, 13, 14, 17, 18, 28, 29, 44, 45, 48, 49, 253, 254))
    s["PreRecPtr"] = rng.choice((0, 2, 4, 6, 8, 10, 12, 14))
    s["CUTTIMER"] = rng.choice((0, 0, 0, 2))
    s["cutscrn"], s["GuardColor"], s["origstrength"] = (rng.randrange(256) for _ in range(3))
    s["milestone"] = rng.choice((0, 1))
    s["MaxKidStr"] = rng.choice((3, 3, 4, 9, 10))
    s["MaxOppStr"] = rng.choice((3, 4, 5))
    s["lightcolor"], s["lightning"] = rng.randrange(256), rng.randrange(4)
    s["ManCtrl"] = rng.choice((0, 0, 0, 0, 1))
    s["vibes"], s["redrawflg"], s["msgtimer"] = rng.randrange(4), rng.randrange(3), rng.randrange(256)
    s["invert"] = rng.choice((0, 0xFF))
    s["mirrx"], s["dmirr"] = rng.randrange(256), rng.randrange(256)
    s["timerequest"] = rng.randrange(4)
    bufs["script"] = bytes(32)
    if routine not in AUTO and routine not in TOP:
        return
    r = rng.random()
    s["level"] = rng.choice((0, 1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 12, 12, 12, 13, 13))
    scrn = rng.choice((1, 3, 4, 7, 11, 15, 18, 24, rng.randint(1, 24)))
    row = rng.choice((0, 1, 1, 2))
    x = rng.randint(120, 128)
    face = rng.choice((0x00, 0xFF))
    d = rng.choice(RANGES)
    other_x, other_face = place_at(rng, d if d < 128 else 127, x, face)
    other_scrn = scrn if rng.random() < 0.85 else rng.randint(0, 24)
    other_row = row if rng.random() < 0.8 else rng.choice((0, 1, 2, 3, 0xFF))
    s.update(EnemyAlert=rng.choice((0, 1, 2, 2, 2)), droppedout=rng.choice((0, 0, 1)),
             offguard=rng.choice((0, 0, 1)), refract=rng.choice((0, 0, 0, 3)),
             gdtimer=rng.choice((0, 0, 2)), alertguard=rng.choice((0, 0, 1)),
             heroic=rng.choice((0, 1)), exitopen=rng.choice((0, 0, 1, 77)),
             mergetimer=rng.choice((0, 0, 0, 42, 0xFF)), SongCue=rng.choice((0, 0, 5)),
             KidStrength=rng.choice((0, 1, 2, 3, 3)), OppStrength=rng.choice((0, 1, 2, 3, 4)),
             ChgKidStr=rng.choice((0, 0, 1, 0xFF, 0xFD)), ChgOppStr=rng.choice((0, 0, 1, 0xFF)),
             lastpotion=rng.choice((0, 1, 2, 3, 4, 5, 6, 0xFF)), VisScrn=scrn,
             createshad=rng.choice((0, 0, 0xFF)), blue=scrn)
    ids = (2, 2, 2, 4, 1, 1, 0, 24)
    # the acting character and his opponent, for routines that use Char / Op
    fighter(rng, s, "Char", scrn, row, x, face)
    fighter(rng, s, "Op", other_scrn, other_row, other_x, other_face)
    s["CharID"], s["OpID"] = rng.choice(ids), rng.choice((0, 0, 0, 1, 2))
    # ... and the same pair as Kid and Shad, for routines that load them
    fighter(rng, s, "Kid", scrn, row, x, face)
    fighter(rng, s, "Shad", other_scrn, other_row, other_x, other_face)
    s["KidID"], s["ShadID"] = 0, rng.choice((2, 2, 2, 4, 1, 1, 24))
    if rng.random() < 0.15:
        s["ShadFace"] = 86                                   # no opponent at all
    if rng.random() < 0.15:
        s["CharFace"] = 86
    for rec in ("Char", "Kid", "Shad"):
        s[rec + "Seq"] = 0x3000 + rng.randrange(0x9F0)

    base = (scrn - 1) * 30 + row * 10
    for _ in range(rng.choice((0, 1, 2, 4))):                # what stands between and around them
        col = rng.randrange(10)
        piece = rng.choice((0, 0, 1, 1, 2, 4, 4, 7, 11, 12, 13, 18, 20, 10, 22, 21))
        pokes[base + col] = piece
        pokes[720 + base + col] = rng.choice((0, 0, 2, 19, 20, 100, 111, 112, 188))
        if row < 2 and rng.random() < 0.4:
            pokes[base + 10 + col] = rng.choice((0, 1, 2, 11, 20))

    for _ in range(rng.choice((1, 2, 3))):                   # guards waiting on screens
        t = rng.choice((scrn, scrn, s["ShadScrn"] or 1, rng.randint(1, 24)))
        pokes[2119 - 1 + t] = rng.choice((rng.randrange(30),) * 3 + (30, 0xFF))
        pokes[2143 - 1 + t] = rng.choice((0x00, 0xFF))
        pokes[2167 - 1 + t] = rng.randint(60, 190)
        seq = rng.choice((0, 0, 0x3000 + rng.randrange(0x9F0)))
        pokes[2191 - 1 + t], pokes[2239 - 1 + t] = seq & 0xFF, seq >> 8
        pokes[2215 - 1 + t] = rng.choice((0, 1, 2, 3, 5, 7, 11, 12, 200))

    if routine in ("autoctrl", "shadctrl") and r < 0.35:     # the shadow's set pieces
        lvl = rng.choice((4, 5, 6, 12, 12))
        s.update(level=lvl, CharID=1, CharLife=0xFF)
        if lvl == 4:
            s.update(CharScrn=4, CharX=rng.choice((60, 79, 80, 81, 150)))
        elif lvl == 5:
            s.update(CharScrn=24, CharX=rng.choice((10, 14, 15, 16, 55)))
            pokes[(24 - 1) * 30 + 1] = 4
            pokes[720 + (24 - 1) * 30 + 1] = rng.choice((0, 19, 20, 21, 188))
        elif lvl == 6:
            s.update(CharScrn=1, KidPosn=rng.choice((43, 43, 42)),
                     KidX=rng.choice((0x60, 0x7F, 0x80, 0x90)))
        else:
            s.update(CharScrn=rng.choice((15, 15, 3)), OpX=rng.choice((100, 149, 150, 151)),
                     CharSword=rng.choice((0, 0, 2)), OpSword=rng.choice((0, 0, 2)),
                     CharPosn=rng.choice((15, 15, 158)), OpPosn=rng.choice(OTHER_FRAMES))
            if rng.random() < 0.5:                           # swords down, face to face
                dd = rng.choice((-2, 20, 40, 60))
                ox, oface = place_at(rng, dd, s["CharX"], s["CharFace"])
                s.update(CharSword=0, OpSword=0, offguard=1, EnemyAlert=rng.choice((2, 2, 1)),
                         shadowaction=1, OpX=ox, OpFace=oface, OpScrn=s["CharScrn"],
                         OpPosn=rng.choice((2, 3, 9, 14, 15, 126, 127, 130, 132, 133)))
    elif routine in ("autoctrl", "shadctrl") and r < 0.55:   # a guard and the kid, alive
        s.update(CharID=rng.choice((2, 2, 2, 4, 0)), CharLife=0xFF, KidLife=0xFF, OpLife=0xFF,
                 CharPosn=rng.choice(FIGHT_FRAMES), CharAction=rng.choice((0, 1)),
                 OpPosn=rng.choice(FIGHT_FRAMES + OTHER_FRAMES))
    if routine in ("autoctrl", "shadctrl", "doshad") and 0.55 <= r < 0.9:
        # en garde, the kid within reach on a clear floor: the fight proper
        gap = rng.choice((7, 8, 9, 10, 11, 12, 13, 14, 20, 28, 28, 29, 29, 29, 30, 30, 34, 35,
                          36, 39, 40, 41, 49, 50, 51))
        col = rng.randint(4, 5)
        rr = row if row < 3 else 1
        cx = 58 + 14 * col + 7
        ox, oface = place_at(rng, gap, cx, face)
        s["guardprog"] = rng.choice((0, 0, 1, 3, 5, 7, 9, 11))
        s.update(CharID=rng.choice((2, 2, 4)), CharLife=0xFF, KidLife=0xFF, OpLife=0xFF,
                 CharSword=2, CharAction=1, CharPosn=rng.choice((150, 158, 161, 170, 171)),
                 EnemyAlert=2, CharScrn=scrn, OpScrn=scrn, CharBlockY=rr, OpBlockY=rr,
                 CharBlockX=col, CharX=cx, CharFace=face, OpFace=oface,
                 OpX=ox, OpSword=rng.choice((2, 2, 0, 0)),
                 OpPosn=rng.choice((151, 152, 153, 158, 162, 169, 171, 15, 6, 7, 9, 14, 15, 33,
                                    34, 40, 43, 44, 101, 102, 110, 117, 118)),
                 OpAction=rng.choice((0, 1, 5)), refract=rng.choice((0, 0, 0, 2)),
                 gdtimer=rng.choice((0, 0, 3)), justblocked=rng.choice((0, 0, 2)),
                 ShadFace=face)
        if rng.random() < 0.25:
            # both ready, in reach, the guard of a type that strikes often:
            # does he hold back while the kid starts his own thrust?
            ox, oface = place_at(rng, 20, cx, face)
            s.update(OpX=ox, OpFace=oface, OpSword=2, refract=0, justblocked=0,
                     guardprog=rng.choice((1, 6, 7)), CharPosn=rng.choice((158, 170, 171)),
                     OpPosn=rng.choice((151, 151, 169, 169, 158, 150)), OpAction=1)
        for c in range(10):                                  # a clear floor
            pokes[(scrn - 1) * 30 + rr * 10 + c] = 1
    if routine in ("autoctrl", "shadctrl", "doshad") and 0.9 <= r:
        # a guard whose opponent dropped out of the fight, at the edge he left by
        col = rng.randint(2, 7)
        ahead = col - 1 if face else col + 1
        s.update(CharID=2, CharLife=0xFF, KidLife=0xFF, CharSword=2, CharAction=1,
                 CharPosn=rng.choice((158, 170, 171)), EnemyAlert=0, droppedout=1,
                 CharBlockX=col, CharBlockY=row if row < 2 else 1, CharScrn=scrn,
                 CharX=58 + 14 * col + rng.randint(3, 10),
                 OpAction=rng.choice((0, 1, 2, 4, 6)), ShadFace=face)
        rr = s["CharBlockY"]
        s["OpBlockY"] = rng.choice((rr + 1, rr + 1, rr))
        here = (scrn - 1) * 30 + rr * 10
        pokes[here + col] = 1
        pokes[here + ahead] = rng.choice((0, 0, 0, 1, 20, 4))
        pokes[here + 10 + ahead] = rng.choice((1, 1, 1, 2, 11, 0, 20))
    if routine in ("autoctrl", "shadctrl") and 0.35 <= r < 0.40:     # the mouse
        s.update(CharID=24, CharX=rng.choice((165, 166, 167, 199, 200, 201)),
                 CharAction=rng.choice((0, 1, 1)), CharFace=rng.choice((0xFF, 0xFF, 86)))
    elif routine in ("autoctrl", "shadctrl") and 0.40 <= r < 0.55:   # a guard not yet en garde
        dd = rng.choice((-9, -8, -7, -5, -4, -3, 20, 126, 127, -128, -127, -60))
        ox, oface = place_at(rng, dd, x, face)
        s.update(CharID=2, CharSword=0, CharLife=0xFF, KidLife=rng.choice((0xFF,) * 4 + (0,)),
                 CharPosn=166, CharAction=1, CharX=x, CharFace=face, OpX=ox, OpFace=oface,
                 OpScrn=s["CharScrn"], OpBlockY=rng.choice((row, row, row, 0xFE)),
                 CharBlockY=row, alertguard=rng.choice((0, 1, 1)),
                 EnemyAlert=rng.choice((0, 1, 2, 2)), level=rng.choice((1, 13, 13)),
                 SongCue=rng.choice((0, 0, 9)))
    if routine == "firstguard" and r < 0.6:                  # the kid slipping past a guard
        dd = rng.choice((-17, -16, -15, -14, -13, -1, 5))
        ox, oface = place_at(rng, dd, x, face)
        s.update(EnemyAlert=2, CharSword=0, OpSword=2, OpAction=rng.choice((0, 1, 1, 2)),
                 CharX=x, CharFace=face, OpX=ox, OpFace=face ^ 0xFF, OpScrn=s["CharScrn"],
                 CharBlockY=row if row < 3 else 1)
    if routine == "reflection" and FDY and rng.random() < 0.4:
        # the reflection's bottom line at, just above and just below the
        # top of the mirror's block
        posn = rng.choice(sorted(FDY))
        rr = rng.choice((0, 1, 2))
        col = rng.randrange(10)
        pokes[(scrn - 1) * 30 + rr * 10 + col] = 13
        s.update(KidPosn=posn, KidID=0, KidBlockX=col, KidBlockY=rr, KidScrn=scrn,
                 KidX=58 + 14 * col + rng.randint(0, 13), createshad=0,
                 KidY=((3, 66, 129)[rr] - FDY[posn] + rng.choice((-1, 0, 1, 40))) & 0xFF,
                 _reflect=1)
    if routine == "kill0":
        s["CharScrn"] = rng.choice((0, 0, scrn))
    if routine == "playscript":
        # (frame, command) pairs with rising frame numbers
        frame, script = 0, bytearray()
        for _ in range(rng.randint(2, 8)):
            script += bytes((frame, rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 7, 9, 0xFF, 0xFE))))
            frame += rng.randint(1, 20)
        script += bytes((255, rng.choice((0xFE, 0xFE, 0xFF))))
        bufs["script"] = bytes(script) + bytes(32 - len(script))
        s["PreRecPtr"] = rng.choice(range(0, len(script), 2))
        s["PlayCount"] = rng.choice((0, 1, frame // 2, frame, 253, 254))
    if routine == "checkstrike" and r < 0.5:                 # one thrusts, the other may block
        a_, b_ = rng.choice((("Kid", "Shad"), ("Shad", "Kid")))
        gap = rng.choice((0, 7, 8, 9, 11, 12, 13, 28, 29, 30))
        sface = rng.choice((0x00, 0xFF))
        ox, oface = place_at(rng, gap, 124, sface)
        s.update({a_ + "Posn": rng.choice((153, 154, 154)), a_ + "Sword": 2,
                  b_ + "Posn": rng.choice((150, 161, 158, 171, 15)),
                  b_ + "Sword": rng.choice((2, 2, 0)),
                  "KidScrn": scrn, "ShadScrn": scrn, "KidBlockY": row, "ShadBlockY": row,
                  a_ + "Face": sface, a_ + "X": 124, b_ + "Face": oface, b_ + "X": ox})
    if routine == "addguard":
        if DEAD_SEQS and rng.random() < 0.3:                 # a guard who lies dead there
            seq = rng.choice(DEAD_SEQS)
            for t in range(1, 25):
                if pokes.get(2119 - 1 + t, 99) < 30:
                    pokes[2191 - 1 + t], pokes[2239 - 1 + t] = seq & 0xFF, seq >> 8
        s["level"] = rng.choice((1, 2, 3, 3, 5, 5, 6, 6, 12, 12, 12, 13))
        s["VisScrn"] = rng.choice((1, 1, 15, 15, 24, 24, scrn, s["ShadScrn"] or 1))
        if rng.random() < 0.5:                               # the sword / the potion there or gone
            pokes[(15 - 1) * 30 + 1] = rng.choice((22, 1))
            pokes[(24 - 1) * 30 + 3] = rng.choice((10, 1))
    if routine == "bonesrise":
        s.update(level=rng.choice((3, 3, 3, 3, 4)), VisScrn=rng.choice((1, 1, 1, 1, 2)),
                 exitopen=rng.choice((1, 1, 1, 0)), KidBlockX=rng.choice((1, 2, 3, 4)))
        if rng.random() < 0.8:
            s["ShadFace"] = 86
        pokes[(1 - 1) * 30 + 15] = rng.choice((21, 21, 21, 1))
    if routine in ("cutcheck", "cutchar"):
        rec = "Kid" if routine == "cutcheck" else "Char"
        s[rec + "Y"] = rng.choice((FLOOR_Y.get(row, 55),) * 3 + (5, 9, 10, 0xEF, 0xF0, 214, 215))
        s[rec + "X"] = rng.choice((x, x, 40, 50, 54, 55, 60, 190, 197, 200, 201, 208))
        s[rec + "Posn"] = rng.choice((15, 7, 9, 44, 109, 110, 119, 120, 134, 135, 149, 150,
                                      162, 163, 165, 166, 168, 169, 102))
        s[rec + "Action"] = rng.choice((0, 1, 1, 3, 4, 5, 7))
        s["leftej"] = rng.choice((40, 53, 54, 55, 100, 197, 198, 199))
        s["rightej"] = rng.choice((50, 56, 57, 58, 100, 200, 201, 202))
        s["level"] = rng.choice((3, 4, 6, 12, 13, 1))
        s[rec + "Scrn"] = rng.choice((1, 3, 7, 11, 18, scrn))
        if rng.random() < 0.4:                               # a panel at the right-hand edge
            at = (s[rec + "Scrn"] - 1) * 30 + (row if row < 3 else 1) * 10 + 9
            pokes[at] = rng.choice((7, 12, 1))
        s["ShadX"] = rng.choice((30, 90, 91, 92, 150, 164, 165, 166, 220))
        s["ShadBlockY"] = rng.choice((0, 1, 2, 3, 0xFF))
        s["ShadSword"], s["ShadLife"] = rng.choice((2, 2, 0)), rng.choice((0xFF, 0xFF, 0))
        if routine == "cutcheck" and rng.random() < 0.3:
            # the kid runs off a side edge with a guard at his heels and
            # nobody waiting beyond: is the guard close enough to come too?
            right = rng.random() < 0.5
            rr = row if row < 3 else 1
            s.update(KidFace=0x00 if right else 0xFF, KidX=215 if right else 40, KidPosn=9,
                     KidAction=1, KidBlockY=rr, KidY=FLOOR_Y[rr], KidScrn=scrn, KidLife=0xFF,
                     CUTTIMER=0, level=1, ShadFace=rng.choice((0x00, 0xFF)), ShadSword=2,
                     ShadLife=0xFF, ShadScrn=scrn, ShadID=2,
                     ShadX=rng.choice((164, 165, 166) if right else (90, 91, 92)))
            for t in range(1, 25):
                pokes[2119 - 1 + t] = 0xFF
            s["_done"] = 1
        if not s.pop("_done", 0) and rng.random() < 0.3:     # the screen edges that set things off
            lvl, sc, ex, face_ = rng.choice(((4, 11, 1, 0xFF), (4, 11, 77, 0xFF),
                                             (3, 7, 0, 0xFF), (12, 18, 0, 0x00),
                                             (13, 3, 0, 0x00)))
            s.update({"level": lvl, rec + "Scrn": sc, "exitopen": ex, rec + "Face": face_,
                      rec + "BlockY": rng.choice((0, 0, 1)), rec + "Posn": 9,
                      rec + "Action": 1, rec + "Y": 55, "leftej": 50, "rightej": 205})
            if routine == "cutcheck":
                s["KidX"] = 50 if face_ else 205
    if routine == "cutguard":
        s["ShadY"] = rng.choice((181, 214, 215, 216, 240))
        s["ShadAction"] = rng.choice((4, 4, 1))
    if routine == "checkalert":
        s.update(KidLife=rng.choice((0xFF,) * 5 + (0,)), ShadLife=rng.choice((0xFF,) * 5 + (0,)),
                 KidPosn=rng.choice((15, 15, 15, 0, 219, 228, 229)))
        s["KidBlockX"], s["ShadBlockX"] = rng.randrange(10), rng.randrange(10)
    if routine == "reflection" and not s.pop("_reflect", 0) and rng.random() < 0.7:
        col = rng.randrange(10)
        at = (scrn - 1) * 30 + (row if row < 3 else 1) * 10 + col
        pokes[at] = 13
        s.update(KidBlockX=col, KidBlockY=row if row < 3 else 1, KidPosn=rng.choice((15, 9, 44)),
                 KidX=58 + 14 * col + rng.randint(0, 13))
    if routine == "cut":
        s["_a_only"] = rng.choice((0, 1, 2, 3, 7))
    if routine in ("stabchar", "checkstab"):
        rec = "Char" if routine == "stabchar" else rng.choice(("Kid", "Shad"))
        s[rec + "Life"] = rng.choice((0xFF, 0xFF, 0xFF, 0))
        s[rec + "Sword"] = rng.choice((2, 2, 0))
        if routine == "checkstab":
            s["KidAction"], s["ShadAction"] = rng.choice((99, 1)), rng.choice((99, 99, 1))
    s["_noreread"] = 1


def top_state(rng, s, pokes, bufs, routine):
    """Top-level variables; for the top-level routines the situations that
    end a level, a life or a screen."""
    s["skipmessage"], s["cutplan"] = rng.choice((0, 0, 1)), rng.choice((0, 1))
    s["cutorder"], s["SCRNUM"] = rng.randrange(256), rng.randrange(256)
    s["message"] = rng.choice((0, 1, 2, 2, 3))
    s["MinLeft"], s["SecLeft"] = rng.choice((0, 0xFF, 0xFF, 5)), rng.choice((0, 0xFF, 0xFF, 9))
    s["BTN0"], s["BTN1"] = rng.choice((0, 0, 0x80, 0xFF)), rng.choice((0, 0, 0xFF))
    s["waitingtojump"] = rng.randrange(256)
    s["yellowflag"] = rng.choice((0x80, 0x80, 0x80, 0))
    s["scrnAboveL"], s["scrnAboveR"] = rng.randint(0, 24), rng.randint(0, 24)
    if routine not in TOP:
        return
    r = rng.random()
    if routine in ("ctrlplayer", "dokid", "nextframe") and r < 0.4:      # the kid has died
        for rec in ("Char", "Kid"):
            s[rec + "Life"] = rng.choice((0, 1, 2, 3, 4, 5))
            s[rec + "Posn"] = rng.choice((185, 185, 177, 178, 15, 106))
            s[rec + "ID"] = 0
        s.update(level=rng.choice((0, 1, 1, 5)), SongCue=rng.choice((0, 0, 1)),
                 msgtimer=rng.choice((0, 1, 2, 200, 255)), message=rng.choice((2, 2, 1)),
                 invert=rng.choice((0, 0, 0xFF)))
    if routine in ("nextframe", "dokid"):
        # keep to screens that exist: a whole frame visits every list entry
        s["cutscrn"] = rng.choice((s["VisScrn"], s["VisScrn"], rng.randint(1, 24)))
        bufs["trscrn"] = bytes(t if 1 <= t <= 24 else 1 for t in bufs["trscrn"])
        s["numtrans"] = rng.choice((0, 0, 1, 2, 3, 5))
        s["nummob"] = rng.choice((0, 0, 1, 2))
        if rng.random() < 0.9:
            # No stale "overlapping" marks in the collision data.  With one,
            # CHECKSLICE looks up a block on screen $FF ("no data"), which the
            # original turns into a read of unrelated memory
            # (docs/port/game-frame.md); the port stops there.
            for name, size in CD_BUFFERS:
                bufs[name] = bytes(b if b != 0xFF else 0x0F for b in bufs[name])
    if routine in ("ctrlplayer", "dokid") and 0.4 <= r < 0.5:            # alive, off the map
        for rec in ("Char", "Kid"):
            s[rec + "Scrn"], s[rec + "Life"] = 0, 0xFF
    if routine == "dokid" and 0.5 <= r < 0.7:                # standing where a gate has shut
        sc, by, bx = rng.randint(1, 24), rng.randint(0, 2), rng.randint(1, 8)
        at = (sc - 1) * 30 + by * 10 + bx
        pokes[at], pokes[720 + at] = 4, rng.choice((0, 0, 0, 4, 60))
        for side in (-1, 1):                                 # with or without floor beside it
            pokes[at + side] = rng.choice((0, 0, 1))
        for rec in ("Char", "Kid"):
            s.update({rec + "Scrn": sc, rec + "BlockY": by, rec + "BlockX": bx,
                      rec + "Y": (55, 118, 181)[by], rec + "Life": 0xFF, rec + "ID": 0,
                      rec + "Posn": 15, rec + "Action": rng.choice((0, 1)),
                      rec + "X": 58 + 14 * bx + rng.randrange(12, 28),
                      rec + "Face": rng.choice((0, 0xFF)), rec + "Sword": 0})
        s.update(level=1, cutscrn=sc, VisScrn=sc, KidStrength=3, stunned=0, weightless=0,
                 JSTKX=0, JSTKY=0, btn=0, invert=0, BlockYthis=by, BlockYlast=by,
                 clrF=0, clrB=0, clrU=0, clrD=0, clrbtn=0)
        if STAND_SEQS:
            s["CharSeq"] = s["KidSeq"] = STAND_SEQS[0]
        for name in ("CDthisframe", "CDlastframe"):          # he overlapped it last frame too
            bufs[name] = bytes(0xFF if i == bx else 0x0F for i in range(16))
    if routine == "nextframe":
        s["CUTTIMER"] = 0
        if 0.55 <= r < 0.65:                                 # level 3: the skeleton's cue
            bx = rng.choice((2, 3, 3, 4))
            s.update(level=3, VisScrn=1, cutscrn=1, KidScrn=1, KidBlockX=bx, KidBlockY=1,
                     KidX=58 + 14 * bx + 7, KidY=118, KidLife=0xFF, KidPosn=15, KidAction=0,
                     ShadFace=rng.choice((86, 86, 86, 0)), exitopen=rng.choice((1, 1, 0)))
            pokes[15] = rng.choice((21, 21, 21, 1))          # the bones, row 1 column 5
        if 0.4 <= r < 0.55:                                  # the ways a level ends
            lvl, sc, y = rng.choice(((0, 24, 118), (6, 1, 10), (6, 1, 19), (6, 1, 20),
                                     (12, 23, 118), (13, 3, 118), (14, 5, 118)))
            s.update(level=lvl, KidScrn=sc, KidY=y, KidBlockY=0 if y < 60 else 1,
                     KidLife=0xFF, KidPosn=15, KidAction=0, cutscrn=sc,
                     VisScrn=rng.choice((sc, sc, 2)))
    if routine in ("prepcut", "firstframe"):
        s["cutscrn"] = rng.choice((0, s["VisScrn"], 5, 5, rng.randint(1, 24), rng.randint(1, 24)))
        s["level"] = rng.choice((14, 14, 13, 5, 12, 1))
        s["KidScrn"] = s["cutscrn"] or 1
    if routine == "misctimers":
        s.update(level=rng.choice((8, 8, 8, 1)), CharScrn=rng.choice((16, 16, 16, 3)),
                 exitopen=rng.choice((0, 1, 77, 148, 149, 150, 151, 200)),
                 mergetimer=rng.choice((0, 1, 2, 42, 0x80, 0xFF)))
    if routine == "wtlessflash":
        s["weightless"] = rng.choice((0, 1, 2, 14, 15, 16, 200))
    if routine == "addsfx":
        s["KidPosn"], s["ShadPosn"] = rng.choice((167, 166, 15)), rng.choice((167, 168, 15))
    if routine == "democtrl":
        s.update(level=0, milestone=rng.choice((0, 0, 0, 1)), CharSword=rng.choice((0, 0, 2)),
                 PreRecPtr=rng.choice(range(0, 58, 2)), PlayCount=rng.choice((0, 1, 13, 30, 47,
                 65, 115, 128, 157, 193, 205, 232, 233, 253, 254)), CharID=0)
    if routine == "getscrns":
        s["VisScrn"] = rng.randint(0, 24)
    if routine in ("static", "cold"):
        s["_a_only"] = rng.choice((0, 15, 229, 109, 171, 166, 185, 177, 178, 9, 150, 186))
    if routine == "entrance" and r < 0.6:
        at = (max(1, s["KidScrn"] if 1 <= s["KidScrn"] <= 24 else 1) - 1) * 30 + rng.randrange(30)
        pokes[at], pokes[720 + at] = 16, rng.choice((0, 172))
    if routine in ("startkid", "startkid1"):
        s.update(level=rng.choice((0, 1, 2, 3, 3, 7, 7, 13)), milestone=rng.choice((0, 1)),
                 origstrength=rng.choice((3, 4, 7)), CharBlockY=rng.choice((0, 1, 2)),
                 CharScrn=rng.choice((17, 17, 5, 1)))
        if rng.random() < 0.7:                               # a start place that makes sense
            pokes[2112] = rng.choice((17, rng.randint(1, 24)))
            pokes[2113] = rng.randrange(30)
            pokes[2114] = rng.choice((0x00, 0xFF))
    if routine in ("setinitials", "restartlevel"):
        # these read the level as it comes from disk: start from an untouched one
        pokes.clear()
        s["_fresh"] = 1
        s.update(level=rng.choice((0, 1, 1, 3, 7, 13, 14, 12, 5)), milestone=rng.choice((0, 0, 1)),
                 origstrength=rng.choice((3, 4)), SongCue=rng.choice((0, 3, 5)))
        if routine == "setinitials" and rng.random() < 0.5:
            if rng.random() < 0.5:
                pokes[2048] = rng.choice((2, 3, 8, 12, 20))      # fewer screens
            for _ in range(rng.randint(1, 6)):
                at = rng.randrange(720)
                kind = rng.choice((4, 11, 10))
                pokes[at] = kind
                pokes[720 + at] = rng.choice((1, 2)) if kind == 4 else rng.randrange(8)


def timetable(i):
    """Frame count at which time message i falls due (SPECIALK.S 1234-1253)."""
    left = (60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5, 4, 3, 2)
    return ((60 - left[i]) * 725 if i < 15 else
            59 * 725 + 1 if i == 15 else 60 * 725 + 5 if i == 16 else 65535)


# frame counts at which the clock's answers change, and their neighbours
TIME_EDGES = sorted({v for v in
                     [0, 1, 2, 65534, 65535]
                     + [timetable(i) + d for i in range(17) for d in (-1, 0, 1)]
                     + [725 * k + d for k in (1, 2, 30, 57, 58, 59, 60, 61) for d in (-1, 0, 1)]
                     + [42775 + 12 * k + d for k in (0, 1, 2, 30, 58, 59, 60, 61) for d in (0, 1)]
                     + [0x0F00, 0x0F28, 0x0F29, 0x1000, 0xA7FF, 0xA800, 0xAA00]
                     if 0 <= v <= 65535})
KEYS = (0xEA, 0xEB, 0xEC, 0xF5, 0xE9, 0xEF, 0xCA, 0xCB, 0xCC, 0xD5, 0xC9, 0xCF, 0xA0, 0x9B, 0x93,
        0xE8, 0xED)


def loop_state(rng, s, pokes, bufs, routine):
    """The input devices, the clock and the messages; for the routines of
    the loop around the frame, the situations they exist for."""
    s["keypress"] = rng.choice((0, 0, 0xEA, 0x6A, 0xEC, 0xA0, 0x9B, 0x93, rng.randrange(256)))
    s["keydown"] = rng.choice((0, 0x80))
    for n in ("kbdX", "kbdY", "joyX", "joyY"):
        s[n] = rng.choice((0, 0, 1, 0xFF))
    s["joyon"] = rng.choice((0, 0, 0, 0xFF))
    s["NextTimeMsg"] = 2 * rng.randrange(19)
    s["soundon"] = rng.choice((0, 1))
    count = rng.choice(TIME_EDGES) if rng.random() < 0.7 else rng.randrange(65536)
    for name in ("clrSEL", "clrDESEL"):
        bufs[name] = bytes(rng.choice((0, 0, 1, 0xFF)) for _ in range(5))
    r = rng.random()

    if routine not in LOOP or routine == "mainloop":
        # The control routines now read the device themselves.  The device a
        # character is played with gets the stick and the press flags its
        # case was made with; the other device gets something else.  The kid
        # is played with the selected device, an opponent played by hand
        # with the other one.  (The top-level routines keep the buttons
        # their cases chose.)
        opponent = routine in ("shadctrl", "doshad")
        joystick = bool(s["joyon"]) != opponent
        mine, other = (("joy", "kbd") if joystick else ("kbd", "joy"))
        s[mine + "X"], s[mine + "Y"] = s["JSTKX"], s["JSTKY"]
        s[other + "X"], s[other + "Y"] = rng.choice((0, 0, 1, 0xFF)), rng.choice((0, 0, 1, 0xFF))
        if routine not in TOP and routine != "mainloop":
            s["BTN0"], s["BTN1"] = s["btn"], rng.choice((0, 0, s["btn"]))
        flags = bytes(s[n] for n in ("clrF", "clrB", "clrU", "clrD", "clrbtn"))
        stray = bytes(rng.choice((0, 0, 1, 0xFF)) for _ in range(5))
        bufs["clrDESEL" if opponent else "clrSEL"] = flags
        bufs["clrSEL" if opponent else "clrDESEL"] = stray
        if routine in ("playerctrl", "ctrlplayer", "dokid", "nextframe", "mainloop") \
                and rng.random() < 0.3:
            # what the opponent's turn left in the flags is not the kid's business
            for n in ("clrF", "clrB", "clrU", "clrD", "clrbtn"):
                s[n] = rng.choice((0, 1, 0xFF))
        count = rng.choice((count, 100, 100, 100))           # mostly far from any message
        if routine in ("nextframe", "mainloop") and rng.random() < 0.8:
            s["NextTimeMsg"] = 2 * sum(timetable(i) <= count for i in range(18))
    if routine in ("playerctrl", "ctrlplayer") and r < 0.07:
        # the demo's script reaches its "end of demo" command (frame 233)
        s.update(level=0, milestone=0, CharSword=0, CharID=0, PreRecPtr=48,
                 PlayCount=rng.choice((233, 240)))
        if routine == "ctrlplayer" and rng.random() < 0.6:   # ... with the kid lying dead
            s.update(CharLife=rng.choice((0, 2, 4)), CharPosn=185, KidLife=0, KidPosn=185)
        else:
            s.update(CharLife=0xFF, CharPosn=15, CharAction=0, JSTKX=0xFF, clrF=0xFF,
                     kbdX=0xFF, joyX=0xFF, stunned=0, KidStrength=3)
            bufs["clrSEL"] = bytes([0xFF, 0, 0, 0, 0])
    if routine == "kread":
        key = rng.choice(KEYS)
        s["keypress"] = rng.choice((key, key, key & 0x7F, 0))
    if routine in ("getselect", "getdesel"):
        s.update(BTN0=rng.choice((0, 0x80)), BTN1=rng.choice((0, 0x80)),
                 ManCtrl=rng.choice((0, 0, 0xFF, 1)))
    if routine == "clrjstk":
        for n in ("clrF", "clrB", "clrU", "clrD", "clrbtn"):
            s[n] = rng.choice((0, 0, 1, 0xFF))
        s.update(JSTKX=rng.choice((0, 1, 0xFF, 2)), JSTKY=rng.choice((0, 1, 0xFF, 0x80)),
                 btn=rng.choice((0, 0x80, 0xFF, 1)))
    if routine == "demokeys":
        s.update(level=rng.choice((0, 0, 0, 1)), BTN0=rng.choice((0, 0, 0, 0x80)),
                 BTN1=rng.choice((0, 0, 0, 0xFF)),
                 keypress=rng.choice((0, 0x20, 0xA0, 0x9B, 0x93, 0x1B, 0xEA)))
    if routine in ("getminleft", "keeptime", "showtime", "shortentime"):
        s.update(level=rng.choice((1, 1, 1, 5, 0, 13, 14)),
                 KidLife=rng.choice((0xFF,) * 6 + (0, 3)),
                 timerequest=rng.choice((0, 1, 2, 2, 3, 4)), msgtimer=rng.choice((0, 0, 0, 1, 5)),
                 message=rng.choice((0, 1, 2, 3, 3)))
        due = sum(timetable(i) <= count for i in range(18))  # messages already due
        s["NextTimeMsg"] = 2 * rng.choice((due, max(due - 1, 0), max(due - 1, 0),
                                           min(due + 1, 18), rng.randrange(19)))
    if routine == "keeptime" and r < 0.5:                    # just before a message falls due
        i = rng.randrange(17)
        count = max(0, timetable(i) + rng.choice((-2, -1, -1, 0, 1)))
        s.update(NextTimeMsg=2 * i, msgtimer=rng.choice((0, 0, 0, 3)), level=1, KidLife=0xFF)
    if routine == "shortentime":
        s["NextTimeMsg"] = 2 * rng.choice((0, 5, 8, 9, 10, 11, 17, 18))
    if routine == "dispmsg" or (routine in ("frameadv", "cleancut", "mainloop") and r < 0.4):
        s.update(msgtimer=rng.choice((0, 1, 2, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 20, 88,
                                      89, 90, 91, 92, 93, 94, 95, 96, 255)),
                 KidLife=rng.choice((0xFF, 0xFF, 0xFF, 0, 2, 4)), message=rng.choice((0, 1, 2, 3)))
    if routine in ("flashon", "flashoff") or (routine == "mainloop" and 0.4 <= r < 0.5):
        s.update(lightning=rng.choice((0, 0, 1, 2, 3, 0x80, 0x90, 0xFF)),
                 lightcolor=rng.choice((0, 0xFF, 0x11, 5)),
                 ChgKidStr=rng.choice((0, 0, 0xFF, 0xFE, 1)))
    if routine == "songcues":
        lvl = rng.choice((0, 1, 1, 1, 1, 4))
        s.update(level=lvl, NextLevel=rng.choice((lvl, lvl, lvl + 1)),
                 SongCue=rng.choice((0, 1, 1, 1, 7)), SongCount=rng.choice((0, 1, 5, 5, 5)),
                 KidPosn=rng.choice((0, 0, 15, 15, 15, 109, 185, 9)),
                 ShadFace=rng.choice((86, 86, 0, 0xFF)), ShadPosn=rng.choice((15, 15, 171, 150)),
                 VisScrn=rng.randint(1, 24), trobcount=rng.choice((0,) * 6 + (1,)),
                 nummob=rng.choice((0,) * 6 + (1,)), lightning=rng.choice((0,) * 6 + (1,)),
                 mergetimer=rng.choice((0, 0, 0, 0, 5, 0xFF)),
                 ChgKidStr=rng.choice((0,) * 5 + (0xFF,)), ChgOppStr=rng.choice((0,) * 5 + (0xFF,)))
        s["ShadScrn"] = rng.choice((s["VisScrn"], s["VisScrn"], 0))
    if routine in ("frameadv", "cleancut"):
        scrn = rng.randint(1, 24)
        s.update(cutplan=rng.choice((0, 1)), redrawflg=rng.choice((0, 0, 1, 2)), VisScrn=scrn,
                 KidScrn=rng.choice((scrn, scrn, 0)), ChgKidStr=rng.choice((0, 0, 0xFF, 1)),
                 ChgOppStr=rng.choice((0, 0, 0xFF, 1)))
    if routine in ("loadnextlevel", "loadnext1"):
        lvl = rng.choice((1, 3, 5, 7, 8, 11, 12, 13, 14, rng.randrange(15)))
        s.update(level=lvl, MaxKidStr=rng.randint(3, 10), milestone=rng.choice((0, 1)),
                 timerequest=0,
                 NextLevel=rng.choice((lvl + 1, lvl + 1, lvl + 1, 0, 15, 14, 2, 4, 6, 8, 9, 12,
                                       200)))
    if routine == "startgame":
        s["_a_only"] = rng.choice((0, 1, 1, 4))
    if routine == "mainloop":
        if 0.5 <= r < 0.58:                                  # a key during the demo
            s.update(level=0, keypress=rng.choice((0xA0, 0xEA, 0x9B, 0x20)))
        elif 0.58 <= r < 0.68:                               # the level has been left
            lvl = rng.choice((1, 3, 5, 7, 8, 11, 13))
            s.update(level=lvl, NextLevel=lvl + 1, KidLife=0xFF, MaxKidStr=rng.randint(3, 10))
        elif 0.68 <= r < 0.8:                                # a song is waiting
            s.update(SongCue=rng.choice((1, 7)), SongCount=rng.choice((0, 5, 5)),
                     level=rng.choice((1, 1, 0)))
        elif 0.8 <= r < 0.9:                                 # ... and all is quiet
            sc, by, bx = rng.randint(1, 24), rng.randint(0, 2), rng.randint(1, 8)
            pokes[(sc - 1) * 30 + by * 10 + bx] = 1          # floor to stand on
            for rec in ("Char", "Kid"):
                s.update({rec + "Scrn": sc, rec + "BlockY": by, rec + "BlockX": bx,
                          rec + "Y": (55, 118, 181)[by], rec + "Life": 0xFF, rec + "ID": 0,
                          rec + "Posn": 15, rec + "Action": 0, rec + "X": 58 + 14 * bx + 14,
                          rec + "Face": rng.choice((0, 0xFF)), rec + "Sword": 0,
                          rec + "XVel": 0, rec + "YVel": 0})
            s.update(level=1, NextLevel=1, cutscrn=sc, VisScrn=sc, KidStrength=3, stunned=0,
                     weightless=0, invert=0, BlockYthis=by, BlockYlast=by, ShadFace=86,
                     numtrans=0, nummob=0, mergetimer=0, exitopen=0, ChgKidStr=0, ChgOppStr=0,
                     lightning=rng.choice((0, 0, 1, 1)), lightcolor=0xFF, CUTTIMER=0,
                     SongCue=rng.choice((1, 7)), SongCount=5, msgtimer=0, timerequest=0,
                     kbdX=0, kbdY=0, joyX=0, joyY=0, BTN0=0, BTN1=0, keypress=0)
            bufs["clrSEL"] = bytes(5)
            if STAND_SEQS:
                s["CharSeq"] = s["KidSeq"] = STAND_SEQS[0]
            for name, _ in CD_BUFFERS:
                bufs[name] = bytes([0x0F] * 16)
        elif 0.9 <= r < 0.96:                                # level 8: the mouse is due
            s.update(level=8, NextLevel=8, CharScrn=16, KidScrn=16, VisScrn=16, cutscrn=16,
                     exitopen=rng.choice((149, 150, 150)), ShadFace=86, KidLife=0xFF)
    if routine in ("nextframe", "mainloop"):
        if rng.random() < 0.5:
            s["timerequest"] = 0                             # the time is not recomputed
        if rng.random() < 0.1:                               # the hour is over
            count = rng.choice((43501, 43505, 43600, 60000))
            s.update(NextTimeMsg=rng.choice((32, 34)), timerequest=rng.choice((0, 0, 2, 3)),
                     msgtimer=0, level=rng.choice((5, 12, 13, 13, 13, 14)),
                     exitopen=rng.choice((0, 0, 1)), MinLeft=rng.choice((0, 0, 1)),
                     SecLeft=0, KidLife=0xFF)
    s["FrameCount"], s["FrameCountH"] = count & 0xFF, count >> 8


def objs_state(rng, s, pokes, bufs, routine):
    """The object list and its variables; for the routines that fill it the
    characters, frames and falling floors they have cases for."""
    s["FCharIndex"], s["doortop"] = rng.randrange(256), rng.randrange(256)
    count = rng.choice((0, 0, 0, 1, 1, 2, 3, 5, 18, 19, 19))
    for name, size in OBJ_LISTS:
        bufs[name] = bytes(rng.randrange(256) for _ in range(size))
    bufs["objX"] = bytes([count]) + bufs["objX"][1:]
    if routine not in OBJS:
        return
    s["_noreread"] = 1
    vis = s["VisScrn"] = rng.randint(1, 24)
    special = (tuple(range(135, 149)) + (224, 225, 226, 227, 228, 78, 79, 80, 81, 2, 3, 4, 6,
                                         177, 178, 185, 106, 107, 110) + tuple(range(229, 238)))

    def person(rec, ident):
        """A character on the visible screen, as a frame would have him."""
        row = rng.choice((0, 1, 1, 2, 2))
        posn = rng.choice(special) if rng.random() < 0.4 else rng.choice(KEY_FRAMES)
        s.update({rec + "Scrn": vis, rec + "BlockY": row,
                  rec + "Y": FLOOR_Y[row] if rng.random() < 0.7
                  else (FLOOR_Y[row] + rng.randint(-40, 12)) & 0xFF,
                  rec + "X": rng.randint(50, 205), rec + "Face": rng.choice((0x00, 0xFF)),
                  rec + "Posn": posn, rec + "ID": ident,
                  rec + "Action": rng.choice((0, 0, 1, 1, 1, 2, 3, 4, 5, 6, 7)),
                  rec + "Sword": rng.choice((0, 0, 2, 2, 1)),
                  rec + "Life": rng.choice((0xFF, 0xFF, 0xFF, 0x00))})
        if 224 <= posn < 229:                        # on the stairs: the door's top is above him
            s["doortop"] = (s[rec + "Y"] - rng.choice((-3, 0, 1, 2, 3, 10, 40, 70))) & 0xFF

    if routine in ("addchars", "setupkid", "setupshad"):
        person("Kid", 0)
        person("Shad", rng.choice((1, 1, 2, 2, 2, 4)))
        person("Char", 0)
        if routine == "addchars":
            s["KidScrn"] = rng.choice((vis, vis, vis, vis, 0, rng.randint(1, 24)))
            s["ShadScrn"] = rng.choice((vis, vis, vis, rng.randint(1, 24)))
            s["ShadFace"] = rng.choice((0x00, 0xFF, 0xFF, 86))
            s["ChgKidStr"] = rng.choice((0, 0, 0xFF, 0xFE, 1))
            s["ChgOppStr"] = rng.choice((0, 0, 0xFF, 1))
            s["createshad"] = rng.choice((0, 0, 0, 0, 0xFF))
        if rng.random() < 0.15:
            s["KidPosn"] = 0                         # not drawn at all
        if rng.random() < 0.3:                       # the shadow by the mirror of level 4
            s["level"], s["ShadID"] = 4, 1
            if rng.random() < 0.7:
                vis = s["VisScrn"] = s["ShadScrn"] = 4
                if s["KidScrn"]:
                    s["KidScrn"] = 4
        if rng.random() < 0.3:                       # the kid before a mirror
            at = (vis - 1) * 30 + s["KidBlockY"] * 10 + rng.randrange(1, 9)
            pokes[at] = 13
            s["KidX"] = 58 + 14 * (at % 10) + rng.randint(0, 13)
    elif routine in OBJ_CHAINED:
        person("Char", rng.choice((0, 0, 1, 2, 2, 4)))
        if rng.random() < 0.15:
            s["_raw"] = 1                            # as it stands, without setting him up
        if routine == "cropchar" and rng.random() < 0.25:
            s["CharPosn"] = rng.randint(224, 228)    # on the stairs, with whatever is around
            s["doortop"] = (s["CharY"] - rng.choice((-3, 0, 1, 2, 3, 10, 40, 70))) & 0xFF
        if routine == "cropchar" and rng.random() < 0.15:
            # standing so that his feet are exactly on, or next to, the line
            # where the block above the next row begins (BlockTop)
            row = s["CharBlockY"] = rng.choice((1, 2))
            # (178, 186, 188: images under 16 lines high, less than a floor is thick)
            posn = s["CharPosn"] = rng.choice((15, 178, 178, 186, 188))
            s["CharY"] = ((3, 66, 129)[row] - FDY[posn] + rng.choice((-1, 0, 0, 1))) & 0xFF
            if rng.random() < 0.5:
                # or with the top of his image a floor's thickness (15 lines)
                # above that line, give or take one
                posn = s["CharPosn"] = 15
                s["CharY"] = ((3, 66, 129)[row] - 15 + FHEIGHT[15] - 1 - FDY[15]
                              + rng.choice((-1, 0, 0, 1))) & 0xFF
            for c in range(10):
                pokes[(vis - 1) * 30 + (row - 1) * 10 + c] = rng.choice((1, 1, 1, 20))
        if routine == "cropchar" and rng.random() < 0.15:
            # jumping up to touch a ceiling that is solid over one of his
            # edges only (frames 79 and 81; other frames for contrast)
            s["CharPosn"] = rng.choice((79, 81, 81, 80, 15))
            s["CharAction"] = rng.choice((0, 0, 0, 1))
            row = s["CharBlockY"] = rng.choice((1, 2))
            s["CharY"] = FLOOR_Y[row]
            for c in range(10):
                pokes[(vis - 1) * 30 + (row - 1) * 10 + c] = rng.choice((1, 20, 0, 0))
                pokes[(vis - 1) * 30 + row * 10 + c] = rng.choice((1, 1, 0))
        if routine == "cropchar" and rng.random() < 0.6:
            # panels, walls and solid ceilings around him
            row, col = s["CharBlockY"], (s["CharX"] - 58) // 14
            for _ in range(rng.randint(1, 4)):
                r = (row + rng.choice((0, 0, -1, -1))) % 3
                c = min(9, max(0, col + rng.choice((-1, 0, 0, 1, 1, 2))))
                pokes[(vis - 1) * 30 + r * 10 + c] = rng.choice((20, 20, 7, 12, 1, 0, 9, 4))
    elif routine == "addmobs":
        s["nummob"] = rng.choice((0, 1, 1, 1, 2, 3))
        below = s["scrnBelow"] = rng.choice((0, rng.randint(1, 24), rng.randint(1, 24)))
        scrns, ys, types = [0], [0], [0]
        for _ in range(15):
            where = rng.choice((vis, vis, vis, below, below, rng.randint(1, 24)))
            scrns.append(where)
            if where == vis:
                ys.append(rng.choice((rng.randrange(192), rng.randrange(192), 62, 63, 125, 126,
                                      188, 191, 192, 208, 209, 230)))
            else:
                ys.append(rng.choice((0, 5, 16, 17, 18, 100, 238, 239, 240, 255)))
            types.append(rng.choice((0, 0, 0, 0, 0, 0, 1)))
        bufs["mobscrn"], bufs["moby"], bufs["mobtype"] = bytes(scrns), bytes(ys), bytes(types)
        bufs["mobx"] = bytes(4 * rng.choice((0, 1, 2, 5, 8, 9, 9, 10)) for _ in range(16))
        bufs["objX"] = bytes([rng.choice((0, 0, 0, 1, 2, 17, 18))]) + bufs["objX"][1:]
    elif routine == "addcharobj":
        s["FCharX"] = rng.choice((rng.randrange(280), rng.randrange(280), rng.randrange(280),
                                  rng.randrange(252, 256), rng.randrange(256, 300),
                                  rng.randrange(500, 520), 0x10000 - rng.randrange(1, 40),
                                  0x10000 - rng.randrange(250, 260), rng.randrange(0x10000)))
        s["FCharIndex"] = rng.choice((rng.randrange(30),) * 5 + (30, 0xFF, rng.randrange(256)))
        s["_a_only"] = rng.choice((0, 1, 2, 3, 4, 5))
    elif routine == "addfcharx":
        s["FCharX"] = rng.choice((rng.randrange(280), rng.randrange(5), 0xFFFE, 0x00FF, 0x0100))
        s["FCharFace"] = rng.choice((0x00, 0xFF))
        s["_a_only"] = rng.choice((0, 1, 5, 127, 128, 129, 251, 255, rng.randrange(256)))


def typed(word, rng):
    """A word as key codes, in capitals or small letters."""
    return [(ord(c) | 0x80) | (0x20 if rng.random() < 0.5 else 0) for c in word]


def keys_state(rng, s, pokes, bufs, routine):
    """The keyboard, the controller and the disk as hardware (bufs["_hw"]),
    the variables of the key routines, and for those routines the
    situations they have cases for."""
    s["SINGSTEP"] = rng.choice((0,) * 6 + (1, 1, 0xFF))
    s["develment"] = rng.choice((0, 0, 1, 1, 0xFF))
    s["keybufptr"] = rng.choice((rng.randrange(10),) * 9 + (10, rng.randrange(256)))
    s["musicon"] = rng.choice((0, 1, 1))
    for n in ("jvert", "jhoriz", "jbtns"):
        s[n] = rng.choice((0, 0, 0, 1))
    cx, cy = rng.randrange(20, 50), rng.randrange(20, 50)
    s.update(jthres1x=cx - 10, jthres2x=cx + 10, jthres1y=cy - 15, jthres2y=cy + 15)
    keybuf = [rng.choice((0, rng.randrange(256))) for _ in range(10)]
    saved = [rng.randrange(256) for _ in range(16)]
    hw = {"latch": 0, "any": rng.randrange(2), "padx": rng.randrange(72),
          "pady": rng.randrange(72), "b0": rng.choice((0, 0, 0x80, 0xFF, 0x7F)),
          "b1": rng.choice((0, 0, 0x80, 0xFF, 0x7F)), "queue": [], "saved": list(saved)}
    bufs["keybuf"], bufs["savedgame"], bufs["_hw"] = bytes(keybuf), bytes(saved), hw
    if routine not in KEYGROUP:
        return
    r = rng.random()
    if routine == "mainloopk":
        # a frame's situation as the state has it, seen through the hardware:
        # the buttons, and a stick that reads as the state's joystick
        s["SINGSTEP"] = rng.choice((0,) * 8 + (1, 0xFF))
        hw["b0"], hw["b1"] = s["BTN0"], s["BTN1"]
        hw["padx"] = {0xFF: s["jthres1x"] - 1, 1: s["jthres2x"]}.get(s["joyX"], s["jthres1x"])
        hw["pady"] = {0xFF: s["jthres1y"] - 1, 1: s["jthres2y"]}.get(s["joyY"], s["jthres1y"])
        if rng.random() < 0.85:
            s["jhoriz"] = s["jvert"] = 0
    else:
        s["level"] = rng.choice((1, 1, 2, 3, 3, 4, 5, 10, 11, 11, 12, 12, 13, 14, 0))
        s["NextLevel"] = rng.choice((s["level"],) * 4 + (s["level"] + 1, 255))
        s["joyon"] = rng.choice((0, 0xFF, 0xFF, 1))
    if rng.random() < 0.5:
        s["keybufptr"] = rng.randrange(10)
    if rng.random() < 0.2:                           # flags that are not 0 or 1
        s["jhoriz"] = rng.choice((0x98, 0x99, 0x92, 0x8F, 0x97, 0x86, 0xA1, 0x80, 2, 0xFF))
        s["jvert"] = rng.choice((0x92, 0x93, 0x8F, 0x97, 0x86, 0xA1, 0x80, 2, 0xFF))
    if routine == "mainloopk":
        pass
    elif rng.random() < 0.25:                        # a paddle with nothing connected, or far off
        hw[rng.choice(("padx", "pady"))] = rng.choice((128, 200, 255, 127, 0, 70))
    elif rng.random() < 0.3:                         # at the edges of the dead zone
        hw["padx"] = (s[rng.choice(("jthres1x", "jthres2x"))] + rng.choice((-1, 0, 1))) & 0xFF
        hw["pady"] = (s[rng.choice(("jthres1y", "jthres2y"))] + rng.choice((-1, 0, 1))) & 0xFF
    if routine == "setcenter" and rng.random() < 0.3:
        hw["padx"], hw["pady"] = rng.choice((0, 5, 9, 10, 11, 120, 127)), rng.choice((0, 14, 15, 16, 110, 127))
    # a key: special, a player key, part of a code word, or none
    key = rng.choice(SPECIAL_KEYS + SPECIAL_KEYS + KEYS + (0xD2, 0xF2, 0xDA, 0xFA, 0xB0, 0xB9, 0xC1,
                                                           rng.randrange(128, 256)))
    if routine == "mainloopk" and rng.random() < 0.55:
        key = 0                                      # most frames, nothing is typed
    if routine in ("keys", "strobe", "develkeys", "checkcodes") and r < 0.45 \
            or routine == "mainloopk" and r < 0.15:
        # the end of a code word, typed whole, or with one key wrong, or cut short
        word = rng.choice(CODE_WORDS)
        keys = typed(word, rng)
        kind = rng.random()
        if kind < 0.15 and len(keys) > 1:
            keys[rng.randrange(len(keys))] ^= rng.choice((1, 0x80, 0x40, 2))
        elif kind < 0.25:
            keys = keys[1:] or keys
        elif kind < 0.3:                             # a small letter where no letter is
            keys[rng.randrange(len(keys))] |= 0x20
        key = keys[-1]
        # in the buffer up to the last key; KEYS adds that one itself
        upto = keys if routine in ("develkeys", "checkcodes") else keys[:-1]
        at = s["keybufptr"] = rng.randrange(10)
        for i, k in enumerate(reversed(upto)):
            keybuf[(at - i) % 10] = k
        if routine in ("develkeys", "checkcodes") and rng.random() < 0.8:
            s["develment"] = 1
        if word == "SKIP" and routine != "mainloopk":    # the last levels it works from
            s["level"] = rng.choice((1, 3, 3, 4, 4, 11, 11, 12, 12))
            s["NextLevel"] = s["level"]
            s["develment"] = rng.choice((0, 0, 1, 0xFF))
        bufs["keybuf"] = bytes(keybuf)
    if key in (0x98, 0x99) and rng.random() < 0.6:
        # a reversal flag that is negative once flipped is taken for the
        # key from there on: flags that turn into the keys that follow
        flags = (0x98, 0x92, 0x8F, 0x97, 0x86, 0xA1) if key == 0x98 else (0x92, 0x8F, 0x97,
                                                                          0x86, 0xA1)
        s["jhoriz" if key == 0x98 else "jvert"] = rng.choice(flags)
        s["level"] = rng.choice((s["level"], 5)) if routine != "mainloopk" else s["level"]
    s["keypress"] = key if rng.random() < 0.9 else key & 0x7F
    hw["latch"] = s["keypress"]
    # what is typed next: arrives when the strobe is cleared
    hw["queue"] = [rng.choice(SPECIAL_KEYS + (0xEA, 0xA0, 0x9B, 0x9B, 0x9B)) | 0x80
                   for _ in range(rng.choice((0, 1, 1, 2, 3)))]
    if routine in ("keys", "strobe", "mainloopk") and s["SINGSTEP"] and rng.random() < 0.8:
        hw["latch"] = rng.choice((0x9B, 0x9B, 0xA0, 0xEA, key | 0x80))    # frozen, and a key comes
    if s["keypress"] == 0x9B and rng.random() < 0.8 and not hw["queue"]:
        hw["queue"] = [rng.choice((0x9B, 0x9B, 0xA0, 0x93, 0xEA))]        # ESC, then another key
    if rng.random() < 0.3:
        s["soundon"] = rng.choice((0, 1, 2, 0xFF))
        s["musicon"] = rng.choice((0, 1, 2, 0xFF))
    if routine == "mainloopk":
        pass
    elif rng.random() < 0.5:
        s["OppStrength"] = rng.choice((0, 1, 2, 3, 5, 12))
        s["MaxKidStr"] = rng.choice((3, 4, 9, 10, 11))
        s["KidStrength"] = rng.choice((0, 1, 3, s["MaxKidStr"]))
    if routine in ("dosavegame", "startresume") or rng.random() < 0.3:
        saved[0] = rng.choice((1, 3, 7, 12, 14, 0xFF, 0x80, 0))
        hw["saved"] = list(saved)
        bufs["savedgame"] = bytes(rng.randrange(256) for _ in range(16))
    if routine == "addkey":
        s["_a_only"] = key


def random_args(rng, routine, s):
    a, x, y = rng.randrange(256), rng.randrange(256), rng.randrange(30)
    if routine in ("checkledge", "pickitup"):
        a = rng.choice(PIECES + [10, 22, 22])
    if routine == "dostrike":
        x = s["CharPosn"] if rng.random() < 0.5 else rng.choice(KEY_FRAMES)
    if routine == "checkcoll":
        a = rng.choice((4, 4, 18, 18, 13, 13, 10, 20, 7, 12, 1, 0))
    if routine == "decstr":
        a = rng.choice((0, 1, 1, 2, 3, 100))
    return a, x, y


class Left(Exception):
    """Raised by a trap where the original jumps out of the level."""


class Harness:
    def __init__(self, source, oracle=None):
        # (another oracle with the same interface: replay_frames.py runs
        # the original with its drawing code, in two memory banks)
        self.oracle = oracle or Oracle(source)
        self.addr = {n: self.oracle.addr(n, IN_MODULE.get(n)) for n in BYTES + WORDS
                     if n != "FrameCountH"}
        self.addr["FrameCountH"] = self.addr["FrameCount"] + 1
        self.bufaddr = {n: SCRIPT_AT if n == "script" else self.oracle.addr(n)
                        for n, _ in BUFFERS}
        self.soundtable = self.oracle.addr("soundtable")
        self.dims = image_dims(source)
        self.hooks = []
        self.stray = False
        cpu = self.oracle.cpu
        cpu.traps[self.oracle.addr("dimchar")] = self.trap_dimchar
        cpu.traps[self.oracle.addr("inverty")] = lambda c: self.hooks.append("v")
        self.shadprog5 = self.oracle.addr("ShadProg5", "AUTO")
        if not DEAD_SEQS:                            # where a corpse's sequence stands
            o = self.oracle
            for seq in ("dropdead", "impale", "halve", "stabkill"):
                o.reset()
                o.poke("CharLife", 0xFF)
                o.call("JUMPSEQ", o.addr(seq), module="CTRLSUBS")
                for _ in range(40):
                    at = o.peek("CharSeq") | (o.peek(o.addr("CharSeq") + 1) << 8)
                    o.call("ANIMCHAR", module="COLL")
                    if o.peek("CharPosn") in (185, 177, 178):
                        DEAD_SEQS.append(at)
                        break
            o.reset()
            o.poke("CharLife", 0xFF)
            o.call("JUMPSEQ", o.addr("stand"), module="CTRLSUBS")
            at = o.peek("CharSeq") | (o.peek(o.addr("CharSeq") + 1) << 8)
            o.call("ANIMCHAR", module="COLL")
            assert o.peek("CharPosn") == 15
            STAND_SEQS.append(at)
            for posn in (15, 9, 44, 109, 178, 186, 188):
                o.reset()
                o.set_vars({"CharPosn": posn, "CharID": 0})
                o.call("GETFRAMEINFO", module="CTRLSUBS")
                FDY[posn] = o.peek("Fdy")
                fimage, fsword = o.peek("Fimage"), o.peek("Fsword")
                table = (((fsword & 0xC0) >> 1) + (fimage & 0x80)) >> 5
                FHEIGHT[posn] = self.dims[table][(fimage & 0x7F) - 1][1]
            o.reset()
        cpu.on_read[0xC030] = lambda c: self.hooks.append("k")      # the speaker
        # The keyboard: $C000 is the latch, with the strobe in its high bit;
        # any access to $C010 clears the strobe, and reads the any-key-down
        # flag with the low bits of the latch.  The model: the next key
        # typed arrives the moment the strobe is cleared.  KEYS reads the
        # latch once; when frozen it reads until a key is there, and with
        # none coming the case is abandoned (CpuError).
        self.hw, self.keyless, self.watch_latch = None, 0, False

        def latch_read(c):
            if self.watch_latch and c.mem[0xC000] < 0x80:
                self.keyless += 1
                if self.keyless > 2:
                    raise CpuError("frozen, and no key comes")

        def strobe_cleared(c):
            hw = self.hw
            latch = c.mem[0xC000] & 0x7F
            if hw is not None and hw["queue"]:
                latch = hw["queue"].pop(0) | 0x80
            c.mem[0xC000] = latch
            c.mem[0xC010] = (c.mem[0xC010] & 0x80) | (latch & 0x7F)
        cpu.on_read[0xC000] = latch_read
        cpu.on_read[0xC010] = strobe_cleared
        cpu.on_write[0xC010] = strobe_cleared

        def paddles(c):                              # PREAD: raw positions
            self.oracle.poke("joyX", self.hw["padx"])
            self.oracle.poke("joyY", self.hw["pady"])
        cpu.traps[self.oracle.addr("PREAD", "GRAFIX")] = paddles
        cpu.traps[self.oracle.addr("savegame")] = lambda c: self.hooks.append("G")
        cpu.traps[self.oracle.addr("loadgame")] = lambda c: self.oracle.poke(
            "SavLevel", bytes(self.hw["saved"]))
        cpu.traps[self.oracle.addr("dispversion")] = lambda c: self.hooks.append("V")
        o = self.oracle

        def mark(text):
            return lambda c: self.hooks.append(text)

        def leave(text):
            # a jump out of the level: the frame it came from is never
            # finished, so the run stops here (docs/port/game-loop.md)
            def trap(c):
                if text:
                    self.hooks.append(text)
                raise Left(text)                     # ("" for MainLoop: the pass is over)
            return trap
        cpu.traps[o.addr("attractmode")] = leave("a")
        for label, text in (("GOATTRACT", "A"), ("YouLose", "L"), ("YouWin", "W"),
                            ("RESTART", "R"), ("MainLoop", "")):
            cpu.traps[o.addr(label, "TOPCTRL")] = leave(text)
        # drawing, sound and the devices: a mark each, or nothing
        for name, text in (("zeropeels", "z"), ("sure", "S"), ("fast", "Q"), ("PageFlip", "P"),
                           ("continuemsg", "c"), ("printlevel", "l"), ("timeleftmsg", "m"),
                           ("gtone", "g"), ("doflashoff", "f"), ("playback", "p"),
                           ("strobe", "s"), ("yellow", "y")):
            cpu.traps[o.addr(name)] = mark(text)
        for name in ("lrcls", "lrclse", "vblank", "copyscrn", "drawall", "fastspeed",
                     "normspeed", "burn", "musickeys", "listtorches", "cutprincess",
                     "setrecheck0"):
            cpu.traps[o.addr(name)] = lambda c: None
        cpu.traps[o.addr("flipdisk", "TOPCTRL")] = lambda c: None
        cpu.traps[o.addr("doflashon")] = lambda c: self.hooks.append("F%02x" % c.a)
        cpu.traps[o.addr("minit")] = lambda c: self.hooks.append("M%02x" % c.a)
        cpu.traps[o.addr("playcut")] = lambda c: self.hooks.append("x%d" % c.a)

        def song_over(c):                            # mplay: 0 = the song has ended
            c.a = 0
        cpu.traps[o.addr("mplay")] = song_over

        def disk_ok(c):                              # recheckyel: Z set = no disk error
            c.p |= 0x02
        cpu.traps[o.addr("recheckyel")] = disk_ok
        self.mainloop = o.addr("MainLoop", "TOPCTRL")
        self.played = ""

        def playback(c):                             # the frame's sounds, as they are played
            count = o.peek(self.soundtable)
            self.played = bytes(o.peek(self.soundtable + 1, count)).hex() if count else ""
            self.hooks.append("p" + self.played + ".")
        cpu.traps[o.addr("playback")] = playback
        # RESTART has no label after its disk load: find its `jsr setinitials`
        top = self.oracle.modules["TOPCTRL"]
        target = self.oracle.addr("setinitials")
        code = bytes((0x20, target & 0xFF, target >> 8))
        at = top.image.find(code, top.symbols["RESTART"] - top.origin)
        assert at > 0, "RESTART: jsr setinitials not found"
        self.restart_level = top.origin + at

    def trap_dimchar(self, cpu):
        """DIMCHAR: In A = image #, X = table #; out A = width, X = height."""
        image, table = cpu.a, cpu.x
        self.oracle.poke("IMAGE", image)             # (`sta IMAGE`; the same cell as `height`)
        if table >= len(self.dims) or not 1 <= image <= len(self.dims[table]):
            self.stray = True                        # the original would read stray memory
            cpu.a = cpu.x = 0
            return
        cpu.a, cpu.x = self.dims[table][image - 1]

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
            if routine == "autoplayback":
                a, x = self.shadprog5 & 0xFF, self.shadprog5 >> 8
            elif routine == "playscript":
                a, x = SCRIPT_AT & 0xFF, SCRIPT_AT >> 8
            # DEMOKEYS reads the buttons themselves, not the copies in BTN0/1
            o.cpu.mem[0xC061] = o.peek(self.addr["BTN0"])
            o.cpu.mem[0xC062] = o.peek(self.addr["BTN1"])
            hw = bufs.get("_hw")
            self.hw = dict(hw, queue=list(hw["queue"])) if hw else None
            self.keyless, self.watch_latch = 0, routine in KEYGROUP
            if routine in KEYGROUP:                  # the hardware as the case has it
                o.cpu.mem[0xC000] = hw["latch"]
                o.cpu.mem[0xC010] = (0x80 if hw["any"] else 0) | (hw["latch"] & 0x7F)
                o.cpu.mem[0xC061], o.cpu.mem[0xC062] = hw["b0"], hw["b1"]
            strobe_trap = None
            if routine == "mainloopk":               # the original's own STROBE
                strobe_trap = o.cpu.traps.pop(o.addr("strobe"))
            try:
                if routine in ("mainloop", "mainloopk"):
                    o.call("RND", module="GRAFIX")
                    o.cpu.a, o.cpu.x, o.cpu.y, o.cpu.sp = 0, 0, 0, 0xFF
                    o.cpu.call(self.mainloop + 3, 6_000_000)
                    ra, rx, ry, rp = o.cpu.a, o.cpu.x, o.cpu.y, o.cpu.p
                elif routine == "restartlevel":
                    if a & 1:                        # a sound queued before the restart
                        o.poke(self.soundtable, 1)
                        o.poke(self.soundtable + 1, 5)
                    o.cpu.a, o.cpu.x, o.cpu.y, o.cpu.sp = a, x, y, 0xFF
                    o.cpu.call(self.restart_level, 3_000_000)
                    ra, rx, ry, rp = o.cpu.a, o.cpu.x, o.cpu.y, o.cpu.p
                else:
                    if routine in ("keys", "legitkeys", "strobe") and a & 1:
                        o.poke(self.soundtable, 1)   # a sound waiting to be played
                        o.poke(self.soundtable + 1, 5)
                    ra, rx, ry, rp = o.call(label, a, x, y, module=module,
                                            max_cycles=3_000_000)
            except Left:
                ra, rx, ry, rp = o.cpu.a, o.cpu.x, o.cpu.y, o.cpu.p
            except CpuError:
                return "error"                       # ran into data or never returned
            finally:
                if strobe_trap is not None:
                    o.cpu.traps[o.addr("strobe")] = strobe_trap
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
            # the keyboard: what the latch holds, and how many keys have arrived
            "kbd": "%02x.%d" % (o.cpu.mem[0xC000], len(bufs["_hw"]["queue"])
                                - len(self.hw["queue"])) if self.hw else "-",
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
    if only == ["@collision"]:
        only = list(COLLISION)
    elif only == ["@mover"]:
        only = list(MOVER)
    elif only == ["@auto"]:
        only = list(AUTO)
    elif only == ["@top"]:
        only = list(TOP)
    elif only == ["@loop"]:
        only = list(LOOP)
    elif only == ["@objs"]:
        only = list(OBJS)
    elif only == ["@keys"]:
        only = list(KEYGROUP)
    elif only == ["@control"]:
        only = [n for n in ROUTINES if n not in COLLISION and n not in MOVER and n not in AUTO
                and n not in TOP and n not in LOOP and n not in OBJS and n not in KEYGROUP]

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
            # a pass of the main loop gets the situations of a frame
            # (a pass with the real STROBE is a pass like any other)
            base = "mainloop" if routine == "mainloopk" else routine
            like = "nextframe" if base == "mainloop" else routine
            s, pokes, bufs = random_state(rng, level, like)
            collision_state(random.Random("%d/%d/%s/coll" % (seed, round_no, routine)),
                            s, pokes, bufs, like)
            mover_state(random.Random("%d/%d/%s/mover" % (seed, round_no, routine)),
                        s, pokes, bufs, like)
            pokes = {i: v for i, v in pokes.items() if 0 <= i < len(level)}
            auto_state(random.Random("%d/%d/%s/auto" % (seed, round_no, routine)),
                       s, pokes, bufs, like)
            top_state(random.Random("%d/%d/%s/top" % (seed, round_no, routine)),
                      s, pokes, bufs, like)
            loop_state(random.Random("%d/%d/%s/loop" % (seed, round_no, routine)),
                       s, pokes, bufs, base)
            objs_state(random.Random("%d/%d/%s/objs" % (seed, round_no, routine)),
                       s, pokes, bufs, base)
            keys_state(random.Random("%d/%d/%s/keys" % (seed, round_no, routine)),
                       s, pokes, bufs, routine)
            if s.pop("_fresh", 0):                           # a level as it is on disk
                fresh = random.Random("%d/%d/%s/level" % (seed, round_no, routine))
                number = s["level"] if s["level"] < 15 else fresh.randrange(15)
                level = bytearray(levels[number])
                commands.append("L " + level.hex())
            pokes = {i: v for i, v in pokes.items() if 0 <= i < len(level)}
            a, x, y = random_args(rng, routine, s)
            carry = rng.randrange(2)
            if routine in ("markwipe", "markred", "markfloor", "markhalf"):
                x = carry                            # the runner takes the carry in x
                if carry and routine != "markwipe" and rng.random() < 0.5:
                    y = rng.randrange(256)           # with carry set only y < 10 is used
            if "_a_only" in s:
                a = s.pop("_a_only")
            noreread = s.pop("_noreread", 0)
            plain = s.pop("_plain", 0)
            fixed_y = s.pop("_y", None)
            if fixed_y is not None:                  # a piece poked at a chosen block
                a, y = s.pop("_a", 11), fixed_y
            for i, v in pokes.items():
                level[i] = v
            steps = [(routine, a, x, y)]
            if routine not in ("setupchar", "getedges", "markwipe", "markred", "indexblock",
                               "decstr", "getopdist", "clrall", "facejstk") \
                    and (routine not in MOVER or routine in ("checkpress", "shakeloose",
                                                             "addslicers")) \
                    and routine not in AUTO and routine not in TOP and routine not in LOOP \
                    and routine not in KEYGROUP \
                    and not noreread and rng.random() < 0.8:
                steps.insert(0, ("rereadblocks", 0, 0, 0))   # a consistent CharBlockX
            if routine == "getedges":
                steps.insert(0, ("setupchar", 0, 0, 0))
            if routine in OBJ_CHAINED and not s.pop("_raw", 0):
                steps = [("rereadblocks", 0, 0, 0), ("setupchar", 0, 0, 0), ("getedges", 0, 0, 0),
                         (routine, a, x, y)]
            if routine in ("checkbarr", "collisions", "checkgate", "checkslice2",
                           "enemycoll", "checkspikes") and not plain and rng.random() < 0.7:
                # the character's real edges, as the frame would have them
                steps = [("setupchar", 0, 0, 0), ("rereadblocks", 0, 0, 0),
                         ("getedges", 0, 0, 0), (routine, a, x, y)]
            if routine in ("getspikes", "checkledge", "pickitup", "removeobj", "doimpale",
                           "checkcoll") \
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
            hw = bufs["_hw"]
            commands.append("I %d %d %d %d %d %d %s %s" % (
                hw["latch"], hw["any"], hw["padx"], hw["pady"], hw["b0"], hw["b1"],
                bytes(hw["queue"]).hex() or "-", bytes(hw["saved"]).hex()))
            for st in (fixed or [(routine, a, x, 0)]):
                commands.append("R %s %d %d %d" % st)
                expected.append(None)
            expected[-1] = (routine, fixed, s, outcome)

    argv_c = [runner, os.path.join(data, "framedef.bin"), os.path.join(data, "seqtable.bin"),
              os.path.join(data, "imgdims.bin"), os.path.join(data, "autodata.bin")]
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
    causes = {}
    changed = dict.fromkeys(names, 0)
    chain_unmodelled = False
    for line, exp in zip(lines, expected):
        if exp is None:                              # an earlier step of a chain
            chain_unmodelled = chain_unmodelled or " unmodelled=1 " in line
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
        got["unmodelled"] = int(bool(got["unmodelled"]) or chain_unmodelled)
        chain_unmodelled = False
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
            why = want if isinstance(want, str) else "port only"
            causes[(routine, why)] = causes.get((routine, why), 0) + 1
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
        for key in ("snd", "hooks", "lvl") + (("kbd",) if routine in KEYGROUP else ()):
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
        for (n, why), count in sorted(causes.items()):
            print("  %-12s skipped %4d: %s" % (n, count, why))
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
