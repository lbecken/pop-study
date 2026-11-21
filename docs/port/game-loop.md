# Port notes: the loop around a frame — input, the clock, messages, songs

Eighth C17 subsystem (milestone 9d, part 2): what `MainLoop` does besides
calling `NextFrame`. It covers the state of the input devices, the 60-minute
clock, the three messages, when a queued song may play, the screen flashes,
the game state that the drawing code changes, and the step from one level to
the next. With it the port has a complete frame function, `pop_mainloop`,
whose only openings are hooks for the hardware: keys and joystick in, picture
and sound out.

C sources: `modern-port/src/core/pop_loop.h`, `pop_loop.c`; state in
`pop_game.h`. Citations are `file:lines` in `01 POP Source/Source` of the
original repository at commit `25243550`. Everything in sections 1–10 is
**confirmed** by reading the source and by running the original code
(section 12) unless marked otherwise.

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `MainLoop` `TOPCTRL.S:366-399` | `pop_mainloop` | One frame (game-frame.md §2) |
| `StartGame` 142-161, `initgame` 219-241 (`TOPCTRL.S`) | `pop_startgame`, `pop_initgame` | Before a game or the demo |
| `LoadNextLevel`, `LoadNext1` `TOPCTRL.S:414-490` | `pop_loadnextlevel`, `pop_loadnext1` | The step to the next level (section 7) |
| `FrameAdv` 606-613, `DoCleanCut` 891-906, `drawbg` 918-931, `DoFast` 961-982, `develpatch` 1721-1734, `checkmeters` 1299-1306 (`TOPCTRL.S`), `ZERORED` `GRAFIX.S:1805-1823` | `pop_frameadv`, `pop_cleancut`, `pop_zerored` | The drawing code's share of the game state (section 6) |
| `flashon`, `flashoff` `TOPCTRL.S:990-1008` | `pop_flashon`, `pop_flashoff` | Screen flashes |
| `songcues` `TOPCTRL.S:1388-1467` | `pop_songcues` | When a queued song plays (section 5) |
| `dispmsg` `TOPCTRL.S:1495-1537` | `pop_dispmsg` | Which message is on screen (section 4) |
| `KEEPTIME` 1261-1297, `GETMINLEFT` 1173-1232, `timetable` 1234-1253, `SHORTENTIME` 1305-1316 (`SPECIALK.S`), `SHOWTIME` `SUBS.S:1304-1366` | `pop_keeptime`, `pop_getminleft`, `pop_timetable`, `pop_shortentime`, `pop_showtime` | The clock (section 3) |
| `GETSELECT`, `GETDESEL` `GRAFIX.S:1030-1062` | `pop_getselect`, `pop_getdesel` | Joystick variables from the chosen device |
| `CLRJSTK` `SPECIALK.S:883-962` | `pop_clrjstk` | The "press" flags (section 2) |
| `SAVESELECT` … `INITINPUT` `SPECIALK.S:807-843`, `clearjoy` `TOPCTRL.S:1638-1645` | `pop_saveselect` … `pop_initinput`, `pop_clearjoy` | The flags, kept per device |
| `KREAD` `SPECIALK.S:718-786` | `pop_kread` | The keyboard as a joystick |
| `DEMOKEYS` `SPECIALK.S:1015-1031` | `pop_demokeys` | Does a key interrupt the demo |

## 2. Input

There are two devices, keyboard and joystick, and two buttons. One device is
"selected" (`joyon`); the kid is played with it. The other one plays the
opponent when `ManCtrl` is set. Only two places write `ManCtrl`: `initgame`
clears it (`TOPCTRL.S:234`) and a development key toggles it
(`SPECIALK.S:497-502`, inside `do DebugKeys`, which is 0 in this checkout),
so the two-player control exists in the code but cannot be switched on in
this build.

Per frame, for the device in use:

1. `KREAD` turns the key held into `kbdX`, `kbdY` (-1, 0, 1). Keys: J left,
   L right, I up, K down, U up-left, O up-right, in either case. A key
   counts for as long as it is held: a press whose "new" bit has been read
   still counts while the keyboard's any-key-down flag is set.
2. `GETSELECT` copies the device's X, Y to `JSTKX`, `JSTKY` and merges the
   buttons into `btn`: either button serves either device, except with
   `ManCtrl` set, when button 0 belongs to the keyboard and button 1 to the
   joystick.
3. `CLRJSTK` maintains five flags, `clrF`, `clrB`, `clrU`, `clrD`, `clrbtn`
   (forward, back, up, down, button; "forward" assumes the character faces
   left, and `FACEJSTK` swaps them otherwise):

   | Flag | Meaning | On a frame the control is held | On a frame it is not |
   | --- | --- | --- | --- |
   | 0 | idle | becomes -1 | stays 0 |
   | -1 | pressed, not acted on yet | stays -1 | **stays -1** |
   | 1 | pressed and acted on | stays 1 | becomes 0 |

   So a press is remembered until the control code uses it, even if the
   control was released again before that. This is what lets a tap during
   one animation take effect when the next one can accept it. The control
   code sets a flag to 1 when it acts (player-control.md).
4. The flags are kept per device (`clrSEL`, `clrDESEL`), loaded before and
   saved after each character's control step.

`clearjoy` zeroes the four direction flags of the selected device (not the
button's) after a song, during which the game stood still.

Reading the hardware (`KEYS`, `CONTROLLER`, `PREAD`, `BREAD`: the keyboard
latch, the paddle timers, the button inputs) is not ported. The shell sets
`keypress`, `keydown`, `joyX`, `joyY`, `BTN0`, `BTN1` in the `strobe` hook.
The special keys (pause, sound, restart …; `LegitKeys` `SPECIALK.S:265-352`)
are the shell's too; of them only "show time" changes game state
(`timerequest = 3`).

## 3. The clock

Time is counted in frames, not seconds. `FrameCount` (16 bits) goes up by
one in each frame in which `KEEPTIME` is called and the kid is alive, and
never wraps. Constants (`SPECIALK.S:68-71`):

```
min = 725       frames per "minute"   ("actual frame rate approx. 11 fps")
sec = min/60    = 12 frames per "second"
t   = 60        minutes: the time limit
```

so the whole game lasts 43,500 counted frames. At the stated 11 frames per
second that is about 66 minutes of play, not 60; the clock is a frame
budget, and how long it lasts in real time depends on how fast the machine
draws (**inferred**; unknown T1). Frames in which the kid is dead, the demo,
songs and the princess scenes are not counted.

`SUBS.S:83-89` has a second set of these constants with `min = 1090`. No
code in `SUBS.S` uses them (**confirmed** by search); the routines that use
`min` are all in `SPECIALK.S`.

**`GETMINLEFT`** computes the display values, both as two decimal digits
(BCD): `MinLeft` starts at 61 and loses one for every 725 frames below
`FrameCount`; in the last two values (1 and 0) `SecLeft` is computed the
same way with steps of 12 frames from frame 42,775. Consequences:

| `FrameCount` | `MinLeft` | `SecLeft` |
| --- | --- | --- |
| 0 | 61 | – |
| 1 – 725 | 60 | – |
| 726 – 1,450 | 59 | – |
| … | | |
| 42,051 – 42,775 | 2 | – |
| 42,776 – 42,787 | 1 | 60 |
| 42,788 – 42,799 | 1 | 59 |
| … | | |
| 43,484 – 43,495 | 1 | 1 |
| 43,496 – 43,500 | 1 | 0 |
| 43,501 – | 0 | 0 |

- Because a "second" is 12 frames and 60 × 12 = 720, the computed seconds
  reach 0 five frames before the minute is over.
- The game ends when `MinLeft` and `SecLeft` are both 0 (`NextFrame`,
  game-frame.md §2). But the two variables are only recomputed when the
  time is being shown, so what matters is when that happens:

**`KEEPTIME`** also schedules the messages. `timetable` lists 18 frame
counts; when `FrameCount` reaches the next one and no other message is up,
`timerequest` is set to 2 and the list advances:

| Entry | Frame | Meaning |
| --- | --- | --- |
| 0 | 0 | 60 minutes left (the start of level 1) |
| 1 – 11 | 3,625 … 39,875 | every 5 minutes: 55 … 5 left |
| 12 – 14 | 40,600, 41,325, 42,050 | 4, 3, 2 left |
| 15 | 42,776 | the last minute: the seconds countdown begins |
| 16 | 43,505 | "5 frames after t=0: game over" |
| 17 | 65,535 | never |

The comparison of `FrameCount` with the table entry tests the high bytes and
then the low bytes separately (`SPECIALK.S:1281-1286`), so it also says "not
yet" when the high byte is already past and the low byte is smaller. As long
as the count passes each entry one frame at a time this makes no difference;
if a message has to wait for another one, it can be held up to 255 frames
longer than necessary. The port does the same.

**`SHOWTIME`** acts on `timerequest` (1–2 automatic, 3 the space key, 4 the
vizier's death) while the kid is alive: it calls `GETMINLEFT`, then

- with 2 minutes or more: puts up the time message for 20 frames (25 after
  the vizier's death), once no other message is showing;
- in the last minute (levels 1–13): shows the seconds continuously, by
  setting the message timer to 1 and `timerequest` to 1 again every frame,
  until 1 second is left;
- at 0: nothing, unless the key asked.

**The last seconds**, as the original runs them (observed by running
`MainLoop` in the simulator from frame 43,380 with the kid standing still):
the countdown is drawn every frame down to "2 SECONDS LEFT" (frame
43,483). At 43,484 the computed value is 1, the message is taken down and
nothing recomputes the time any more: `MinLeft` and `SecLeft` stay at 1 and
1 for 21 frames. Then table entry 16 (43,505) asks for the time once more,
`SHOWTIME` computes 0 and 0, and the check in `NextFrame` ends the game in
that same frame. So the hour is 43,505 counted frames long, 5 more than 60
× 725, and its end is not shown on the clock.

**`SHORTENTIME`** is the penalty for skipping a level: if more than 10
minutes' worth of messages remain, the clock is set to 15 minutes left. Its
callers are the typed codes in `KEYS` (`SPECIALK.S:234-247`, `629-634`),
which belong to the key handling the port leaves to the shell; the routine
itself is ported.

## 4. Messages

One message at a time: `message` (1 level, 2 "Press button to continue",
3 time left) with a countdown `msgtimer`. `dispmsg` is called from the
drawing of every frame; it takes one off the timer and decides whether the
message is drawn in this frame:

- Kid alive: "LEVEL n" or the time, but not while the timer is 18 or more.
  Both timers start at 20, so the message appears two frames after it is
  asked for and is drawn in 18 frames.
- Kid dead: the continue message, timer starting at 255: steady down to 95,
  then blinking (on for 3 frames of every 8, with a beep as it comes on),
  off for the last 15. When the timer reaches 1 the game returns to the
  title sequence (game-frame.md §4).

The time message is drawn from `MinLeft`/`SecLeft` freshly computed in the
frame it is drawn (`TIMELEFTMSG` `GAMEBG.S:187-262` calls `getminleft`).
Drawing the messages is the `show_message` hook.

## 5. Songs

`CUESONG` records a song number and a patience (`SongCount` frames).
`songcues`, at the end of every frame, plays it when the picture is calm:

- not in the demo;
- the kid in a resting frame (`static?`); once he has gone up the stairs
  (frame 0), only when the level is about to change;
- a visible opponent in a resting frame too;
- no slicer or other fast-moving piece in action (`trobcount`), no falling
  floor, no flash;
- not while the shadow is merging, and no blow landed this frame (the two
  last conditions are skipped once `mergetimer` is negative).

Each frame of waiting takes one off `SongCount`; at 0 the song is dropped
unplayed. While a song plays the game loop does not run (the original
animates only the torches, `burn`); afterwards direction presses made in
the meantime are discarded. In the port the song is the `play_song` hook.

## 6. What drawing does to the game state

`FrameAdv` draws either the whole screen (`DoCleanCut`, when `PrepCut` has
set `cutplan`) or only what changed (`DoFast`). Inside that drawing code the
following is game state, and is in the port (`pop_frameadv`):

| Where | What |
| --- | --- |
| `drawbg` | `cutplan = 0`; `CUTTIMER = 2`: no further screen change for 2 frames |
| `DoSure` | `SCRNUM = VisScrn`; the redraw buffers are cleared (`ZERORED`: all but `whitebuf`) |
| `drawbg` | both strength meters marked for redraw |
| `develpatch` | `redrawflg`, if set, counts down and forces a full redraw (used after the upside-down screen is turned back: 2, one for each display page) |
| `addmobs`, `addchars` | the object list and the marks that go with it (since milestone 10c: frame-drawing.md §4); in `addchars` also `REFLECTION`: the mirror image, and the shadow stepping out of the mirror |
| `checkmeters` | a meter whose strength changed this frame is marked |
| `dispmsg` | the message timer (section 4) |
| `flashoff` | `lightning` counts down |

Everything else the drawing code does is picture: `draw_screen`,
`draw_changes` and `show_frame` hooks. The redraw buffers and the object
list are read by `draw_changes`, the original's `FAST`, which is ported in
`src/video` (`pop_fast`, frame-drawing.md) and not yet connected to these
hooks (milestone 10c-3).

`setupkid` (`TOPCTRL.S:803-810`) inserts a delay (`pause` 25) in frames in
which the kid has frame 0, that is after he has gone up the stairs: timing,
not state, and not in the port (unknown T1).

## 7. From one level to the next

`MainLoop` ends a level when `NextLevel` differs from `level`. That is set
by climbing the exit stairs (`GoneUpstairs`), by falling out of level 6, by
leaving level 12 (game-frame.md §2), and by development keys.

```
MainLoop:       if NextLevel = 2: yellowcheck         (disk copy protection)
LoadNextLevel:  unless NextLevel = 14: timerequest = 1 (show the time as the level starts)
LoadNext1:      origstrength = MaxKidStr               (strength gained is kept)
                milestone = 0
                NextLevel outside 1-14: NextLevel = level, restart this level
                level = NextLevel
                a princess scene before levels 2, 4, 6, 8, 9, 12
                    (scene numbers 1, 2, 3, 8, 4, 5)
                RESTART
```

Level 13 follows level 12 through `LoadNext1` directly, so without the time
message, and `skipmessage` suppresses "LEVEL 13": the two levels are one to
the player.

The copy-protection check is the `copy_check` hook; a shell with no
protected disk should set `yellowflag` negative there (what happens
otherwise: game-frame.md §6). The original also asks for the disk to be
turned over where the level files are on the other side (`flipdisk`); that
is not in the port.

## 8. Leaving a frame half-way

`GOATTRACT`, `YouLose`, `YouWin`, `RESTART`, `START` and `attractmode` are
reached by `jmp`, from as deep as five subroutine calls inside `NextFrame`;
each ends by resetting the stack. Whatever the frame had still to do is
never done.

The port has no jumps out of functions. Each such place sets `g->leave`
(why the level ended) and calls its hook; every caller above it returns at
once when `leave` is set, and `pop_mainloop` returns the value. Nothing
after the original's jump is executed in the port either. The shell then
does what the jump target does: load a level and call `pop_restart_level`,
or go to its title screen.

## 9. Not ported

- The hardware side of input and all special keys (section 2).
- Saving and resuming a game (`DoSaveGame`, `ResumeGame`): `SavLevel`,
  `SavStrength`, `SavTimer`, `SavNextMsg`. Resuming restores `origstrength`,
  `FrameCount`, `NextTimeMsg` and sets `timerequest = 1`
  (`TOPCTRL.S:166-213`); a shell can do that directly.
- Development keys and typed codes (`DevelKeys`, `TempDevel`, `checkcodes`,
  and the codes in `KEYS`). Which of them exist depends on two switches
  that this checkout sets inconsistently: `SPECIALK.S:3-4` has
  `FinalDisk = 0` ("removes all cheat keys" when 1) and `DebugKeys = 0`,
  while `TOPCTRL.S:4` and `MASTER.S:3` have `FinalDisk = 1`. As the files
  stand, the single-key development commands (`DebugKeys`) are out; the
  typed codes are assembled in: "POP" turns on the development flag
  (`SPECIALK.S:222-228`), and "SKIP" moves on a level, up to level 4, or
  up to 12 with that flag, shortening the clock (`234-247`). Whether the
  released disk was built with these settings is **not yet verified**
  (next step: try the codes in an emulator with a disk image; unknowns.md
  E24).
- The title sequence, the princess scenes, the ending.

## 10. Findings

- **The clock is a frame budget.** 43,500 counted frames, whatever the
  frame rate (section 3).
- **A "second" is 12 frames, a "minute" 725**, and the end of the hour is
  noticed only when the last table entry forces the time to be recomputed:
  the game ends at counted frame 43,505, after 21 frames in which no time
  is shown (section 3).
- **Before the first counted frame the answer is 61 minutes** (`GETMINLEFT`
  with `FrameCount` 0). It is not seen: the first time message waits for the
  level message, and by then frames have been counted.
- **The first time message is due at frame 0**: "60 MINUTES LEFT" appears at
  the start of level 1 as soon as "LEVEL 1" has gone. Observed in the
  simulator from `StartGame`: "LEVEL 1" is drawn in frames 2–19, the opening
  song plays at frame 7, the time message is drawn in frames 22–39.
- **The table comparison in `KEEPTIME` is not a 16-bit comparison**
  (section 3).
- **A press outlives its release** until the control code uses it
  (section 2).
- **The continue message's last blink is cut**: with the 3-in-8 pattern it
  would show once more at timer values 10–8; the cutoff at 15 suppresses
  that.
- **`GETMINLEFT` is the only code in the source that uses the 6502's
  decimal mode** (`sed`, `SPECIALK.S:1222`; **confirmed** by search). It
  takes the flags from a following `tay`, not from the decimal subtraction,
  so it behaves the same on a 6502 and a 65C02, which set flags differently
  in decimal mode.
- **The random number generator is never seeded.** `RNDseed` is zero-page
  byte `$9E`; `INITSYSTEM` clears the whole zero page at boot
  (`TOPCTRL.S:132-136`) and the only code that writes the seed afterwards is
  `RND` itself (`GRAFIX.S:1387-1394`; **confirmed** by search). Besides the
  game loop, `RND` is stepped by the princess scenes (`SUBS.S:879`, once per
  scene frame, and the star twinkle at `376-385`), which run in the title
  sequence too. So from power-on the sequence is fixed, and what varies a
  game is how many frames have gone by before and during it (**inferred**:
  the title sequence itself, in `MASTER.S`, has not been traced). A port
  that wants the original's variety has to step or seed the generator
  itself before a game; `pop_game.RNDseed` is the whole state.
- **Two switches disagree between files** (section 9): the typed codes
  "POP" and "SKIP" are assembled in as the files stand.
- **Unused second set of clock constants** in `SUBS.S` (section 3).

## 11. Intentional deviations

- **No jumps out of functions**: `g->leave` and early returns (section 8).
- **Hooks** for hardware, drawing, sound, the princess scenes and the
  copy-protection check (`pop_game_hooks`).
- **`DEMOKEYS` reads `BTN0`/`BTN1`**, the copies `BREAD` made a moment
  earlier in the same frame, where the original reads the two button
  inputs again.
- **`FrameCount` is two bytes** in the state, low and high, as in the
  original, because `KEEPTIME` compares them separately.
- **An odd `NextTimeMsg`** would make `KEEPTIME` read the table across two
  entries; no code produces one, and the port marks the case `unmodelled`.
- **Not in the port**: disk sides (`flipdisk`), the IIGS speed switch, the
  delay in `setupkid`, the torch animation during songs.

## 12. Checks

All run the original code, assembled by this project's assembler, in this
project's 6502 simulator (oracle.md); none compares with the game running on
an Apple II or an emulator.

| Check | What it compares |
| --- | --- |
| `tests/test_loop.c` (`loop_unit`) | 211 hand-worked checks on synthetic data; no original data needed |
| `tests/fuzz_control.py … @loop` | Each of the 27 routines on random states, among them whole passes of `MainLoop`: all variables, the level, buffers, and the calls out in order with their arguments (flash colour, song number, scene number, the sounds handed to `playback`) |
| `tests/trace_kidstep.py` | Random play, one `MainLoop` pass per frame with random keyboard input |
| `tests/replay_demo.py` | The demo from `StartGame` to `GOATTRACT`, one `MainLoop` pass per frame |
| `tests/mutation_check.py` | Deliberate one-token errors in `pop_loop.c` must be noticed by the random-state comparison |

To run `GETMINLEFT` the simulator had to learn decimal mode (`tests/test_audio.py`
has its checks); it follows the NMOS 6502.

In the original's runs the drawing, sound, keyboard and disk routines are
replaced by stand-ins that record the call (oracle.md lists them), and a
jump out of the level ends the run at that point, as it ends the frame in
the game.

Recorded results are in verification.md.
