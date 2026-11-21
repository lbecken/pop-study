# Port notes: the keyboard's special keys, the controller, saved games

Thirteenth C17 subsystem (milestone 13): what `STROBE` does at the start of
every pass of the main loop. It reads the keyboard, steers by it, acts on
the special keys (freeze, sound, restart, save, show the time), recognises
typed code words, and reads the joystick and the buttons. Until now the
port left all of this to the shell, which did it its own way; now the
original's code decides, and the shell only stands in for the hardware.

C sources: `modern-port/src/core/pop_keys.h`, `pop_keys.c`; state in
`pop_game.h`. Citations are `file:lines` in `01 POP Source/Source` of the
original repository at commit `25243550`. Everything in sections 1–8 is
**confirmed** by reading the source and by running the original code
(section 11) unless marked otherwise.

## 1. Routines

| Original | C | What it does |
| --- | --- | --- |
| `STROBE` `SPECIALK.S:1342-1345` | `pop_strobe` | `KEYS`, then `CONTROLLER` |
| `KEYS`, `freeze`, `KEYS1`, `KEYS2` `SPECIALK.S:138-250` | `pop_keys` | Read the keyboard; freeze; code words; special keys |
| `LegitKeys` `SPECIALK.S:265-352` | `pop_legitkeys` | The special keys (section 3) |
| `DevelKeys` 360-372, `checkcodes` 585-637 (`SPECIALK.S`) | `pop_develkeys`, `pop_checkcodes` | Code words of development mode (section 5) |
| `addkey` 567-576, `checkcode` 647-671, the words 680-704 (`SPECIALK.S`) | `pop_addkey`, `pop_checkcode` | The last ten keys typed, and whether they spell a word |
| `CONTROLLER` 1071-1074, `JREAD` 1086-1114, `cvtpdl` 1176-1187, `BREAD` 1124-1136 (`GRAFIX.S`) | `pop_controller`, `pop_jread`, `pop_bread` | Joystick position as -1, 0, 1; the buttons |
| `SETCENTER` `GRAFIX.S:1232-1269` | `pop_setcenter` | Select the joystick; where it stands is its centre |
| `DOSAVEGAME` `MISC.S:743-765` | `pop_dosavegame` | Fill the saved-game record and write it |
| `STARTRESUME` 110-114, `ResumeGame` 168-218 (`TOPCTRL.S`) | `pop_startresume` | Start from the saved-game record |

`KREAD` (the keyboard as a joystick) and `DEMOKEYS` were ported with the
loop (game-loop.md §2).

Not ported: `TempDevel` (`SPECIALK.S:379-545`), which is inside
`do DebugKeys` and so is not assembled in this checkout (section 5);
`MUSICKEYS` (`GRAFIX.S:1893-1935`), the keys that work while a song plays,
there being no music; `DISPVERSION` (`MISC.S:985-1012`), which writes a
line of text on the text screen (a hook: the port has no Apple II
character set to draw it with); `PREAD` (`GRAFIX.S:1198-1222`), the timing
loop that measures the paddles, which is hardware.

## 2. The keyboard

The Apple II keyboard is two addresses. `$C000` holds the code of the last
key typed, ASCII with bit 7 set, and bit 7 stays set until the program
clears it: that is the "strobe", and it is how a program tells a new key
from one it has already seen. Any access to `$C010` clears the strobe; on
the IIe, reading `$C010` also returns in bit 7 whether any key is down at
this moment. (These two paragraphs are Apple II hardware as the code uses
it, not something the repository states.)

`KEYS` (`SPECIALK.S:160-169`), once per pass:

1. `keypress` = `$C000`: the key, new if bit 7 is set;
2. `keydown` = `$C010`: is a key held; and the strobe is now clear;
3. `KREAD`: `kbdX`, `kbdY` from the key (game-loop.md §2);
4. if the key is not new, that is all.

A new key is put into the key buffer (section 5), the two code words that
work without development mode are looked for, and then `LegitKeys` and
`DevelKeys` look at it.

In the port the two addresses are two hooks, `key_latch` and `key_strobe`
(`pop_game_hooks`).

## 3. The special keys

`LegitKeys` compares the key with these, in this order
(`SPECIALK.S:91-104`, `265-352`). "Beep" is `gtone` (section 7).

| Key | Code | What it does |
| --- | --- | --- |
| Escape | `$9B` | Freeze (section 4) |
| ctrl-R | `$92` | `jmp goattract`: the game is over, back to the title sequence |
| ctrl-A | `$81` | `jmp restart`: this level again, from the disk; the clock runs on |
| ctrl-K | `$8B` | The keyboard is the device (`joyon` = 0). Beep |
| ctrl-J | `$8A` | The joystick is the device, centred where it stands (`SETCENTER`). Beep |
| ctrl-X | `$98` | Reverse the joystick's left and right (`jhoriz`). Beep |
| ctrl-Y | `$99` | Reverse its up and down (`jvert`). Beep |
| ctrl-S | `$93` | Sound off or on; what was queued is dropped. Beep when it comes on |
| ctrl-N | `$8E` | Music off or on. Beep when it comes on |
| ctrl-V | `$96` | Show the version line until a key is pressed |
| ctrl-G | `$87` | Save the game (section 8) |
| space | `$A0` | Show the time left (`timerequest` = 3) |

Two keys leave the pass where it stands: ctrl-R and ctrl-A jump away and
`KEYS` never returns. The port sets `pop_game.leave` and returns up through
`pop_strobe` (the controller is then not read) and `pop_mainloop`, as it
does for every such jump (game-loop.md §8).

On an Apple IIe the up and down arrow keys send `$8B` and `$8A`, the codes
of ctrl-K and ctrl-J: pressing an arrow selects a device. (**Inferred**
from the IIe's key codes, which are not in the repository; not tried.)

## 4. Freeze

Escape freezes the game inside `KEYS`: it loops on `$C000` until a key
comes (`SPECIALK.S:142-143`), and nothing else runs meanwhile; no frame
passes, no time is counted.

- Another Escape: `SINGSTEP` = 1 and `KEYS` returns, with `keypress` = 1,
  which is not a key. One frame passes. The next `KEYS` finds `SINGSTEP`
  set and waits again before it does anything else. Escape, Escape, Escape
  steps through the game a frame at a time.
- Any other key: `SINGSTEP` = 0 and the game goes on. That key does nothing
  else: "ignore the keypress that breaks ESC" (`SPECIALK.S:152`). Ending a
  freeze with ctrl-S does not switch the sound off.

`initgame` and `RESTART` clear `SINGSTEP` (`TOPCTRL.S:233`, `281`).

**Deviation, in form only.** A program in a window cannot sit in a loop
reading a keyboard. The `key_latch` hook takes a flag, "the game is
waiting": the shell's implementation then does the waiting (it keeps its
window alive and returns when a key is typed), so control stays inside
`pop_keys` as it stays inside `KEYS`. If the hook returns without a key,
which a shell does only when it is closing, the port abandons the pass
with `POP_LEAVE_FROZEN` and stays frozen (`SINGSTEP` is set if it was not,
so that a later call waits again). The original has no such path.

## 5. Code words

Every new key goes into `keybuf`, ten keys used as a ring
(`addkey`, `SPECIALK.S:567-576`). `checkcode` (647-671) walks a word and
the ring backwards from the last key and reports a match; where the word
has a capital letter, the small letter typed counts too. So a word is
recognised at its last letter, whatever was typed before it, and nothing
may be typed in between.

| Word | Works | Effect |
| --- | --- | --- |
| `POP` | always | Development mode on (`develment` = 1). Beep. `KEYS` returns at once |
| `SKIP` | always | `NextLevel` + 1 and the clock shortened to 15 minutes (`SHORTENTIME`), from levels 1 to 3; in development mode from levels 1 to 11 |
| `BOOST` | development mode | One more strength unit, all of them filled (`boostmeter`); kept as the strength a level starts with |
| `R` | development mode | Strength filled up (`rechargemeter`) |
| `ZAP` | development mode | The opponent loses all his strength |
| `Z` | development mode | The opponent is left with one unit |
| `TINA` | development mode | `NextLevel` = 14, clock shortened |
| Return | development mode | Development mode off |

The development-mode words are tried in that order and the first match
ends the search (585-637). They end in five different letters, so at most
one of them can match and the order makes no difference.

**Which of this exists in the game as sold is not known.** The words are
inside `do FinalDisk … else … fin` in `SPECIALK.S`, whose own line 3 says
`FinalDisk = 0 ;removes all cheat keys`; `TOPCTRL.S:4` and `MASTER.S:3` set
`FinalDisk = 1`. As the files stand: `INITSYSTEM` starts with development
mode off (`lda #FinalDisk!1`, `TOPCTRL.S:125-126`, assembles to `lda #0`),
and typing `POP` turns it on. The port follows the files as they stand,
like everything else in it. Whether the released disks were assembled with
`FinalDisk = 1` in `SPECIALK.S`, which would remove all of the table above
including `SKIP`, is **not yet verified** (unknowns.md E24; next step: type
the words in an emulator with a disk image).

`TempDevel`, the "temp development keys (remove for final version)", is
compiled out by `DebugKeys = 0`. For the record it holds single keys for
development mode: redraw, blackout, level skips, the opponent's program,
speed, a screen dump, strength, moving the kid up a storey, the clock, and
erasing the saved game (`SPECIALK.S:109-131`, `379-545`).

## 6. The controller

`JREAD` does nothing when the keyboard is the device. Otherwise `PREAD`
times the two paddles, giving "raw counter values (approx. 0-70)"
(`GRAFIX.S:1190`), and `cvtpdl` turns each into -1, 0 or 1 by two
thresholds; then each axis is negated if its reversal flag is set.

`SETCENTER` makes the joystick the device, clears the reversal flags, reads
the paddles once, and sets the thresholds 10 either side of where the
stick stands for left and right, 15 either side for up and down
(`cwidthx`, `cwidthy`, `GRAFIX.S:177-178`). If either paddle reads 128 or
more, the counting loop's way of saying that nothing is connected,
`joyon` = 0: the keyboard stays the device. `INITSYSTEM` calls it at
start-up (`TOPCTRL.S:121`), which is how the game finds out whether there
is a joystick.

`BREAD` copies the two buttons (`$C061`, `$C062`) into `BTN0`, `BTN1`.

In the port `PREAD` is the `read_paddles` hook (raw values into `joyX`,
`joyY`) and the buttons are the `read_buttons` hook; the thresholds and
flags are the game's.

## 7. The beep

`GTONE` (`GRAFIX.S:1311-1317`) "confirm[s] special-key presses": `tone`
with pitch 15 and duration 50. Run in the simulator it accesses the
speaker 50 times, 157 cycles apart, the first 21 cycles in, and takes
7,872 cycles: about 3.25 kHz for 7.7 ms, a tick more than a tone. It
bypasses the sound queue and the sound switch. The app renders it from
those numbers (`pop_app.c`, `h_tone`; the spacing is `pop_tone_half_period`
of audio-analysis.md).

## 8. Saved games

The record is 16 bytes at `savedgame` (`GAMEEQ.S:67-74`), of which five are
used:

| Offset | Name | |
| --- | --- | --- |
| 0 | `SavLevel` | the level; negative: no game saved |
| 1 | `SavStrength` | the strength the level was started with (`origstrength`) |
| 2 | `SavMaxed` | not written or read by any code in the repository |
| 3-4 | `SavTimer` | `FrameCount` |
| 6 | `SavNextMsg` | `NextTimeMsg` |

ctrl-G puts the level into `SavLevel` and calls `DOSAVEGAME`, which refuses
below level 3 with the sound of a thud (`FirstSideB = 3`: "must have
reached side B"), and otherwise fills in the rest and writes the record to
a track of the disk's second side (`SAVEGAME`, `MASTER.S:394-414`). What is
saved is the *start* of the current level: the kid's position, the level's
state and the strength lost since are not in the record.

Resuming (`ResumeGame`, reached from the title by ctrl-L,
`MASTER.S:1046`) reads the record; with no game saved it starts level 1;
otherwise it restores strength, clock and the next time message, asks for
the time to be shown, sets the copy-protection flag as passed
(`yellowflag` = `$80`), and starts the saved level.

In the port the disk is two hooks, `save_game` and `load_game`; the app
writes and reads the 16 bytes as a file.

## 9. Findings

- **"Buttons switched" switches nothing.** `BREAD` tests `jbtns` and then
  loads `$C061` into `BTN0` and `$C062` into `BTN1` on either branch; the
  two branches differ only in the order of the two loads
  (`GRAFIX.S:1125-1136`). Nothing in this build can set `jbtns` anyway
  (`SETCENTER` clears it).
- **A reversal flag can be mistaken for a key.** After flipping `jhoriz`
  or `jvert` with `eor #1`, `LegitKeys` branches to the beep if the result
  is positive and otherwise falls through into the following comparisons
  with the flag's value in the accumulator in place of the key
  (`SPECIALK.S:299-311`). With the flags only ever 0 or 1 this never
  happens; a flag of `$98` would turn ctrl-X into ctrl-X and ctrl-Y. The
  port keeps the fall-through.
- **A key typed before a song is forgotten.** Where a queued song starts,
  the strobe is cleared (`TOPCTRL.S:1453`). This is in `songcues`, ported
  with the loop; the clearing was added to the port now that there is a
  keyboard to clear.
- **The game restarts unfrozen.** `RESTART` clears `SINGSTEP`
  (`TOPCTRL.S:281`), and `initgame` does (233): both were missing from the
  port, which had no such variable, and the comparison found both.
- **Development mode is off at start-up in this checkout, and `POP` turns
  it on** (section 5): the two files that decide it disagree about
  `FinalDisk`.

## 10. Intentional deviations

| Deviation | Why |
| --- | --- |
| The keyboard, the paddles, the buttons and the disk are hooks | hardware |
| Waiting for a key while frozen is done by the shell inside the `key_latch` hook; `POP_LEAVE_FROZEN` if it gives up | section 4 |
| `DISPVERSION` is a hook that the app does not connect | it needs the Apple II's text screen and character set |
| `MUSICKEYS` is not ported | no music is played |
| In the app, keys typed faster than one a frame wait in a queue instead of replacing each other in the latch | kinder, and a typed code word then cannot lose a letter; on the Apple II the game reads one key per pass and a faster typist loses keys |

The shell's own choices about keys (which key of a PC keyboard is which,
and that the arrows are the joystick) are in shell.md §2.

## 11. Checks

| Check | What it shows |
| --- | --- |
| `tests/fuzz_control.py @keys`, 400 cases per routine | Twelve routines of section 1 and whole passes of `MainLoop` with the original's own `STROBE` (`mainloopk`), C against the original in the simulator: every game variable, the key buffer, the saved-game record, sounds, hooks, and what is left in the keyboard latch. The simulator's keyboard is modelled as in section 2, with further keys arriving when the strobe is cleared; a freeze with no key coming is not compared |
| `tests/test_keys.c` (`keys_unit`) | 167 checks on made-up states, among them what the comparison cannot reach: a freeze the shell gives up on, and a key-buffer pointer beyond the buffer |
| `tests/mutation_check.py`, entries for `pop_keys.c` | Deliberate errors in the port, each to be noticed by the comparison or shown to be without effect (verification.md) |
| `tests/check_keys.py` (`app_keys`) | Keys typed at the game as the shell runs it have their effect on what is drawn and heard: freeze and single step (same frames, time stands still), the time message, sound off and on, restart, end of game, `SKIP`, `POP` and `TINA`, keyboard steering equal to joystick steering, saving and resuming |
| `tests/check_app.py` | The game through `pop_app` and through the runner agree across restarts of a level by ctrl-A |
| `tests/test_keys_original.py` (pytest) | The facts of sections 5–7 that are read off the assembled original: the beep's 50 accesses 157 cycles apart, `INITSYSTEM`'s `lda #0`, `BREAD`'s two identical branches |

Not checked against anything: the Apple IIe facts of sections 2 and 3
(arrow-key codes, any-key-down), which come from outside the repository;
and, as everywhere, nothing was compared with the running game.
