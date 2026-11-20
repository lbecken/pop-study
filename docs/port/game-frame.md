# Port notes: one frame, the kid's death, the start of a level

Seventh C17 subsystem (milestone 9d, part 1): the routine that produces one
frame of the game from the previous one, the kid's part of it including what
happens after he dies, the preparation of a new screen, and everything that
happens when a level begins. With this the subsystems of milestones 6–9c run
together in the original's order, and whole stretches of play can be compared
with the original code.

C sources: `modern-port/src/core/pop_top.h`, `pop_top.c`; state in
`pop_game.h`. Citations are `TOPCTRL.S:lines` in `01 POP Source/Source` of
the original repository at commit `25243550` unless another file is named.
Everything in sections 1–8 is **confirmed** by reading the source and by
running the original code (section 10) unless marked otherwise.

Not in this part: the input devices, the clock and the messages, the songs'
cue logic, and drawing. Where the frame needs them the port calls a hook
(section 9).

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `NextFrame` 503-597 | `pop_nextframe` | Everything that decides what the next frame looks like (section 2) |
| `DoKid` 637-690 | `pop_dokid` | The kid's whole frame (section 3) |
| `ctrlplayer` 1095-1170 | `pop_ctrlplayer` | Control, and what follows his death (section 4) |
| `misctimers` 1652-1678, `wtlessflash` 1685-1700 | `pop_misctimers`, `pop_wtlessflash` | Small per-frame timers |
| `addsfx` 1476-1487 | `pop_addsfx` | The clash of a blocked thrust |
| `static?`, `cold?` 1608-1631 | `pop_static`, `pop_cold` | Is this a frame a character rests in / lies dead in |
| `PrepCut` 1039-1064, `FirstFrame` 622-628, `GETSCRNS` `CTRLSUBS.S:284-318` | `pop_prepcut`, `pop_firstframe`, `pop_getscrns` | Entering a screen (section 5) |
| `RESTART` 274-359 (from the point where the level is in memory) | `pop_restart_level` | A level begins (section 6) |
| `SETINITIALS` `SUBS.S:1441-1467`, `GETINITOBJ` `FRAMEADV.S:2217-2249` | `pop_setinitials` | Editor settings to game states |
| `STARTKID`, `STARTKID1` `SUBS.S:1476-1604` | `pop_startkid`, `pop_startkid1` | The kid at his start |
| `entrance` 1361-1378 | `pop_entrance` | The door he came in by slams shut |
| `initCDbuf` 1015-1025 | `pop_initcdbuf` | Empty collision buffers |
| `DemoCtrl` `CTRL.S:587-606`, `DEMO` `SUBS.S:1233-1236` | `pop_democtrl` | The demo's "player" (section 7) |

## 2. The order of a frame

`MainLoop` (366-399) is the game's outer loop; one pass is one frame:

```
MainLoop
    rnd                    step the random number generator once
    ChgKidStr = ChgOppStr = 0
    strobe, demokeys       keyboard and joystick; a key during the demo starts a game
    misctimers
    NextFrame              the game logic
    flashon
    FrameAdv               draw (a whole new screen if cutplan is set, else what changed)
    playback, zerosound    play this frame's queued sounds
    flashoff
    songcues               play a queued song when nothing is moving
    until NextLevel differs from level; then yellowcheck, LoadNextLevel
```

and `NextFrame` (503-597) is

```
NextFrame
    animmobs               falling loose floors
    animtrans              gates, plates, spikes, slicers, torches ...
    bonesrise              level 3's skeleton
    checkalert             can kid and opponent see or reach each other
    DoKid                  the kid (section 3)
    DoShad                 the opponent, same steps
    checkstrike, checkstab sword hits
    addsfx
    chgmeters              apply the strength changes of this frame
    cutcheck               has the kid left the screen?
    PrepCut                if so, set up the new one
    cutguard               an opponent fallen off the bottom is removed
    level 0:  kid on screen 24            -> GOATTRACT (the demo is over)
    level 6:  kid on screen 1 with y < 20 -> y = 255, NextLevel + 1
    level 12: kid on screen 23            -> NextLevel + 1, skipmessage = 1, LoadNext1
    keeptime               unless level 13 with the exit open, or level 14
    showtime
    levels 0-12: if MinLeft and SecLeft are both 0 -> YouLose
```

Consequences of this order, all visible in play:

- Moving pieces move *before* the characters. A gate that closes this frame
  is closed when the kid's collision check runs.
- The kid moves before his opponent. Both have moved before any thrust is
  judged, so a thrust is judged on the positions both reached this frame.
- A character who leaves the screen is moved to the next one in the same
  frame (`cutcheck`), and the new screen's slicers, torches, loose floors and
  guard are set up (`PrepCut`) before the frame is drawn.
- The clock stops for good when the vizier dies: on level 13 `keeptime` is
  skipped once `exitopen` is set, which `DEADENEMY` does (`SUBS.S:1720-1738`),
  and level 14 has no clock. After time has run out the game ends on levels
  1–12 only (592-596: "one chance to finish level 13").
- Level 6 ends by *falling*: a kid on screen 1 above y 20 has fallen in from
  the screen above the level's exit drop; his y is set to 255 and the next
  level is loaded with him still falling (that is how level 7 starts, section 6).

## 3. The kid's frame

`DoKid` (637-690):

```
DoKid
    LoadKidwOp, rereadblocks
    unholy                 level 12: kid and shadow share a life
    ctrlplayer             control (section 4)
    if the screen is upside down and he is dead:
        redrawflg = 2, invert = 0, jmp inverty      (654)
    wtlessflash
    if CharScrn != 0:
        animchar, gravity, addfall, setupchar, rereadblocks, getedges
        firstguard, checkbarr, collisions, checkgate, checkfloor
        checkpress, checkspikes, checkimpale, checkslice, shakeloose
    SaveKid
```

The upside-down case leaves with a `jmp`, not a `jsr`: on the frame the kid
dies under the "flip" potion, the screen is turned back and **the rest of his
frame is skipped**: no animation step, no gravity, and his changes of this
frame are not saved (`SaveKid` is not reached). From the next frame on
`invert` is 0 and the frame runs normally. The port does the same.

`DoShad` (`pop_doshad`, opponents.md) has the same list without `unholy`,
`firstguard`, and with `SHADCTRL` for control.

`wtlessflash` (1685-1700) counts the "float" potion down: `vibes` (the
display's shimmer) is on while more than 15 frames remain and alternates
each frame for the last 15.

`misctimers` (1652-1678) runs `mergetimer` (level 12: counts down to 1 and
then is set to -1, never 0 again) and, on level 8, sends the mouse: once the
exit is open, `exitopen` counts frames spent on screen 16 up to 150
(`mousetimer`), then `MOUSERESCUE` is called once.

## 4. After the kid's death

`ctrlplayer` (1095-1170) first kills a character on screen 0 (`kill0`), then
runs `PlayerCtrl`. While `CharLife` is negative he is alive. After death:

1. Nothing happens until he lies still (`cold?`: frames 185 dead, 177
   impaled, 178 halved). A kid falling to his death is not "dead enough"
   until he has landed.
2. `CharLife` then counts 0, 1, 2, 3, 4 (`deadenough`), one per frame. At 0
   `deathsong` (1200-1212) queues the song: the shadow's if the opponent is
   the shadow (`ShadID` 1), else "heroic" if he died in a sword fight
   (`heroic` set), else "accident".
3. Level 0: `GOATTRACT`: the demo is over.
4. While a song is queued (`SongCue` not 0) nothing more happens.
5. No time left: `YouLose`.
6. Otherwise the message "Press button to continue" is put up with a timer
   of 255 frames. When the timer has run down to 1: `GOATTRACT` (back to the
   title sequence). A button (`BTN0` or `BTN1`, read directly) restarts the
   level: `RESTART`.

At the fixed 11 frames per second of design default D2 the 255 frames are
about 23 seconds (inferred from the frame rate; the timer itself is counted
down by the message code, which is part 2 of this milestone).

## 5. Entering a screen

`PrepCut` (1039-1064) compares `cutscrn` (set by `CUT` when the kid crosses an
edge) with `VisScrn`, the screen on display. If they differ, and `cutscrn`
is not 0:

```
VisScrn = cutscrn
level 14, screen 5:  YouWin         (the princess's room)
cutplan = 1                          FrameAdv will draw the whole screen
getscrns                             the eight neighbours of VisScrn
LoadKid
addslicers, addtorches, crumble      this screen's moving pieces
addguard                             and who is waiting there
```

The game never "cuts" to screen 0: a character who walks off the map stays
drawn on the old screen while `kill0` kills him.

Three pieces of game state are changed by the *drawing* code, not by
`NextFrame`, and whoever replaces the drawing must keep them:

| Where | What |
| --- | --- |
| `drawbg` 918-923, called when a whole screen is drawn | `cutplan = 0`, `CUTTIMER = 2`: no second cut for 2 frames |
| `flashoff` 1000-1003, after the frame is shown | `lightning` counts down |
| `addchars` 747-760, in `DoFast` | `REFLECTION` (`MISC.S:486-582`): the mirror image on level 4, and through it `CreateShad`, the birth of the shadow |

The port's tests (section 10) do exactly these three things between frames.

## 6. A level begins

`RESTART` (from 274, after the level file has been read):

1. `SETINITIALS`: for every screen and block, `GETINITOBJ` turns the
   editor's setting into the game's state. Gate: setting 1 ("up") becomes 188
   (`gmaxval`, fully open), 2 ("down") becomes 0 (`initsettings`,
   `FRAMEADV.S:22-23`). Loose floor: 0. Flask: the kind moves to the top
   three bits (setting × 32) so the low bits can hold the bubble animation.
   Everything else is left alone. A gate with any other setting would read
   past the two-entry table; none of the 15 levels has one (the port marks
   the case `unmodelled`).
   The loop runs over screens 1 to `INFO` − 1, where `INFO` is the first byte
   of the level's information block ("number of screens +1",
   `SUBS.S:1442`). That byte is 25 in eleven levels, 24 in levels 1 and 3,
   21 in level 10 and 7 in level 14, so the last screens of those four
   files are not converted. None of them can be reached from the kid's
   start by following the level map (checked on the level files), so
   nothing depends on it. In level 14 the unconverted screens 8–20 still
   hold gadgets, with states that are already in the game's form (a gate at
   188, flasks at 32, 64 and 160): **inferred** to be the remains of another
   level under the six screens of the final one.
2. `INITIALGUARDS` (opponents.md).
3. The per-level variables are cleared: `vibes`, `AMtimer`, `VisScrn`,
   `exitopen`, `lightning`, `mergetimer`, `numtrans`, `nummob`, `EnemyAlert`,
   `createshad`, `stunned`, `heroic`, `ChgKidStr`, `OppStrength`, `msgtimer`,
   `PreRecPtr`, `PlayCount`. A queued "danger" song survives a restart; any
   other is dropped.
4. `zerosound`, `zeropeels`, `initCDbuf`, `initinput`.
5. `gotsword = 1`, `cutorder = -1`, and an empty opponent slot of type guard.
6. `startkid`. Then on level 1 `gotsword = 0`: the game starts unarmed.
7. The level message ("LEVEL n") for 20 frames, except on level 0 and 14,
   and except on level 13 when it follows straight from 12 (`skipmessage`).
8. `entrance`: the first exit door found on the kid's screen (searching from
   block 29 down) starts closing.
9. `FirstFrame`: `cutscrn` = his screen, `PrepCut`, and the screen is drawn.

`STARTKID` (`SUBS.S:1476-1604`) puts the kid where the level's INFO block
says (level-format.md): screen, block, and facing (stored inverted). His x
is the block's left edge + 14 (`angle` 7 + 7), his y the block's floor line.
Strength is `origstrength`, or 4 on level 0. Then, per level:

| Level | Start |
| --- | --- |
| 1 | `rdblock(5, 2, 0)` + `pushpp`: the plate at screen 5, column 2, top row is "pressed", which slams the gate behind him (`SUBS.S:1540-1545`); sequence `stepfall`: he drops in |
| 3, with `milestone` set | The start is moved to screen 2 block 6 facing left, and the loose floor at screen 7, column 4, top row is replaced by empty space: after the first gate has been passed once, later attempts start beyond it |
| 7 | If he starts on screen 17, `cut(3)`: he is moved to the screen below at once, falling in from above (the end of level 6) |
| 13 | Sequence `running` |
| others | Sequence `turn` |

`STARTKID1` also holds a piece of the disk copy protection: on level 7, if
`yellowflag` is not negative, `timebomb` is set to `$40` (`SUBS.S:1593-1596`).
`yellowflag` is set by a check made before the first princess scene
(`yellowcheck` 1707-1713, `GAMEBG.S:67`); `timebomb` is OR-ed into the
number of the character's image whenever a frame is decoded
(`CTRLSUBS.S:1034-1037`, "must be 0!"). So on a copy that failed the check
every character image from level 7 on has bit 6 set in its number: the wrong
picture, with the wrong size for the collision code. That mechanism is
confirmed from the source and the port carries it. How the result looks and
plays is **not yet verified** (next step: run a level-7 trace with
`yellowflag = 0` and compare frames and edges; unknowns.md E9).

## 7. The demo

Level 0 is a playable level like the others; only the "player" differs.
`PlayerCtrl` calls `DemoCtrl` (`CTRL.S:587-606`) instead of reading the
joystick:

- once the guard is dead (`milestone` set): hold "forward" until the kid
  runs off the screen;
- while his sword is out: the *guard's* fight program, number 10, chooses his
  moves (`guardprog` is set to 10 for the call and to 11 after it, which is
  the demo guard's own program);
- otherwise: `AUTOPLAYBACK` of `DemoProg1` (`SUBS.S:1233` on), a list of
  (frame, command) pairs.

The demo ends when the kid reaches screen 24 (section 2) or dies (section 4).
Because the fight depends on the random number generator, the demo is not
the same every time on the original machine; with the generator's state
fixed it is exactly reproducible, which is what the port's end-to-end test
uses (section 10).

`DemoProg1` is original data: it is generated into `autodata.bin` at build
time and is not in this repository (design default D1).

## 8. Findings

- **A stale collision mark can make the slicer check read "screen 255".**
  The collision buffers keep, per column, whether the kid overlapped a
  barrier (`CD = $FF`) and which screen row was examined (`SN`). `initCDbuf`
  and normal play set both together. If a column drops out of range for a
  frame (`SN = $FF`) while its overlap mark from an earlier frame is still
  set, `CHECKSLICE` takes `$FF` as a screen number. `CALCBLUE(255)` then
  indexes its table of screen offsets 252 bytes past its start. In this
  project's assembly of the source that address (`$131C`) lies inside
  `FRAMEADV`'s code; the two bytes found there make the "screen" start at
  `$5500` (types) and `$57D0` (states), which is code of `AUTO`. None of the
  30 bytes read as block types there is a slicer (18), so the check finds
  nothing and the game continues unharmed. Confirmed by computing the
  addresses from the assembled image; the addresses, though not the mechanism,
  depend on the build. The port does not model memory outside the level and
  stops with `unmodelled` here; such cases are skipped in comparisons and
  counted (section 10).
- **Dying upside down skips the rest of the kid's frame** (section 3).
- **Game logic in the drawing path** (section 5).
- **`mergetimer` never returns to 0** once started (section 3): 0 means "not
  started", -1 "done".
- **Level 13 gives one last chance.** Time running out ends the game only on
  levels 0–12; on level 13 the fight with the vizier can still be finished.
- **The kid's death is not noticed while a song is queued**: the "Press
  button" message waits for the death song to be played.
- The demo needs only the first guard image set: `chset` for level 0 is 0
  (`MISC.S:774`). Which file each `chset` number names is still unknown G9.

## 9. What is a hook

The frame calls out of the modelled game state in these places. Each is a
function pointer in `pop_game_hooks`; a null pointer is "do nothing".

| Hook | Original | Meaning |
| --- | --- | --- |
| `go_attract` | `GOATTRACT` | Back to the title sequence |
| `you_lose`, `you_win` | `YouLose`, `YouWin` | The two endings |
| `load_next` | `LoadNext1` | Load level `NextLevel` |
| `restart` | `RESTART` | Start the level again |
| `clean_cut` | `DoCleanCut` | Draw the whole screen |
| `keep_time`, `show_time` | `keeptime`, `showtime` | The clock (part 2) |
| `init_input`, `zero_peels` | `initinput`, `zeropeels` | Input and drawing resets |
| `invert_y` | `inverty` | Turn the display upside down / back |
| `get_input`, `save_input` … | (player-control.md) | The joystick |

In the original the first five never return to their caller: they are jumps
that abandon the frame and reset the stack. The port calls the hook and
returns from the routine at once, so the caller of `pop_nextframe` must
check what the hook recorded before going on.

## 10. Checks

All run the original code, assembled by this project's assembler, in this
project's 6502 simulator (oracle.md); none compares with the game running on
an Apple II or an emulator.

| Check | What it compares |
| --- | --- |
| `tests/test_top.c` (`top_unit`) | 90 hand-worked checks on synthetic data; no original data needed |
| `tests/fuzz_control.py … @top` | Each of the 18 routines on random states: all variables, the level, sounds, buffers, and the list of hooks called |
| `tests/trace_kidstep.py` | Random play: after the original's own level start, `rnd; misctimers; NextFrame; REFLECTION; zerosound` per frame with random joystick input, compared after every call |
| `tests/replay_demo.py` | The original's demo from level start to `GOATTRACT`, compared after every call |
| `tests/mutation_check.py` | 89 deliberate one-token errors in `pop_top.c` must be noticed by the random-state comparison (88 are; the remaining one is hidden by the comparison's rule for unmodelled memory and is checked against the unit test, see verification.md) |

Recorded results are in verification.md.
