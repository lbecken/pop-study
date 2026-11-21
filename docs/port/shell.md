# Port notes: the shell

Milestone 7: a window, the keyboard, a sound device and a clock around the
ported game. This is the part that makes it a program one can start. It is
also the part that could be checked least: **this environment has no
display and no sound card**, so the shell has only been run with SDL's
"dummy" video and audio drivers. Section 7 says exactly what that covers.

Sources, all new code:

| File | What it is |
| --- | --- |
| `modern-port/src/app/pop_app.h`, `pop_app.c` | The game as one object: state, drawing, data files, input, sound samples. No SDL, no clock |
| `modern-port/tools/pop_headless.c` | The same game without a window: prints what each frame left, writes PNG frames and a WAV file |
| `modern-port/src/sdl/pop_sdl_main.c` | The SDL3 shell: window, keys, audio stream, timing. Built only when SDL3 is found |

Nothing in these files is a port of original code. What the original does
in the corresponding places (`STROBE` and `KEYS` in `SPECIALK.S`, `PLAYBACK`
in `SOUND.S`, the loader in `MASTER.S`, the attract loop in `TOPCTRL.S`) is
replaced, and each replacement is listed below as a deviation.

## 1. Building and running

```
cmake -S modern-port -B build -DPOP_SOURCE_DIR=<original checkout> \
      [-DCMAKE_PREFIX_PATH=<where SDL3 is installed>]
cmake --build build
build/pop_sdl --source <original checkout> --data build/data
```

The build generates the tables the game needs from the checkout
(`build/data`); the shell reads levels and image files from the checkout
itself at run time. Without SDL3 everything else still builds, and
`pop_headless` plays the game into PNG files.

Options: `--level n` (0, the default, is the self-running demo; a key
turns it into a game on level 1), `--scale n`, `--mono`, `--rate n`
(frames per minute), `--stretch`, `--mute`, `--images dir` (section 6), and
for tests `--frames n`, `--no-wait`, `--screenshot file`.

## 2. Keys

| Key | |
| --- | --- |
| arrows, or J L I K | left, right, up, down |
| U, O | up-left, up-right |
| Shift | the button (pick up, careful step, strike) |
| P | pause |
| F1 | colour / monochrome |
| Ctrl-A | start the level again |
| Ctrl-R | start the game again on level 1 |
| Escape | leave |

J L I K U O and the button are the original's player keys
(`SPECIALK.S:73-98`). The rest are the shell's.

**Deviation: the keys reach the game as a joystick.** The Apple II
keyboard delivers one key at a time, and the original's keyboard routine
turns the last key into a direction (`KREAD`, ported and tested in
`pop_loop.c`). The shell does not go through it: it sets the joystick
position and both buttons each frame (`joyon`, `joyX`, `joyY`, `BTN0`,
`BTN1`) and lets the game read them through its joystick path
(`getselect`). The difference a player would notice: two arrow keys held
together give a diagonal, which the original keyboard could only do with U
and O. Everything downstream of the device (which presses count as new,
`clrjstk` and the per-device flags) is the ported code.

**Not ported: the special keys** (`KEYS`, `SPECIALK.S`): pause, sound on and
off, restart, saving and loading a game, and the development keys. Pause
and the two restarts above are the shell's own and simpler: a restart is a
cold start (full strength, the hour from its beginning).

## 3. Time

The original has no frame timer: a frame takes as long as its logic, its
drawing and its sounds take (unknown T1; counted in the simulator, 50 to
220 ms and 106 ms on average over the demo, before sound).

The shell follows decision D2: a fixed 725 frames a minute, the number the
game's own clock assumes (`SPECIALK.S:68`), which is 82.8 ms a frame,
against the 106 ms counted as the demo's average. `--rate` changes
it; `--stretch` adds to each frame the time the original would have spent
playing that frame's sound effects. Neither reproduces the original's
unevenness, in which a crowded screen runs slower than an empty one.

Two things the original shows only *while a frame is being drawn* have no
natural length here: the flash (the lo-res screen in one colour, from
`flashon` to the next `PageFlip`) and the blank screen of a new room. The
shell shows them for the first half of the frame's time
(frame-drawing.md §6). **Inferred** to be about right; not compared with
anything.

## 4. Sound

The frame's effects are rendered from the toggle lists that
`tools/extract_audio` obtains by executing the original `SOUND.S`
(audio-analysis.md), at 44.1 kHz, and handed to an SDL audio stream.

- **Deviation: sound does not stop the game.** In the original `playback`
  plays the frame's effects there and then, and nothing else happens
  meanwhile. Here the samples are queued and the next frame starts on time
  (or later by their length, with `--stretch`).
- **No music** (decision D4): the song data is not in the repository. Where
  the original plays a song the port goes straight on, without the pause
  the song makes.
- The beep that goes with "Press button to continue" (`gtone`) and the
  click of the collision code (`COLL.S:403`) are hooks that the shell does
  not connect.

## 5. What happens where the port ends

| The original | The shell |
| --- | --- |
| Title sequence and attract loop | not ported: the demo level starts at once, and starts again when it ends |
| A key during the demo starts a game | ported (`demokeys`): level 1 |
| The kid dies and the button is not pressed in time: back to the title | the demo |
| The hour runs out: the princess scene, then the title | a line on standard error, then the demo |
| The game is won: the final scenes | a line on standard error, then the demo |
| The scenes with the princess between levels | not ported: the next level starts |
| Disk access, copy protection | none: levels and images are read from the checkout; the protection flags are set as a passed check leaves them |

## 6. Levels and image tables

`pop_app` loads `LEVEL<n>` and, when they change, the background tables
and the opponents' table for the level. Which sets a level uses comes from
the original's tables (`MISC.S:772-774`, generated into `bgdata.bin`).
Which *file* a set is has two uncertainties, both carried over from
earlier milestones: background sets 0 and 2 are both taken to be the
dungeon, and opponent sets are matched to files by who lives on their
levels (**inferred**; `pop_app.c` has the list). The game logic is given
the image sizes of the tables that are loaded (frame-drawing.md §6).

With `--images <dir>` the image tables, all of them, come from a directory
of replacement pictures instead (blender-to-sdl3.md §4). The directory must
say where its pictures come from (`PROVENANCE.txt`), and its character
tables must have the original's image sizes, or the shell does not start;
the window title says that the pictures are not the original's. The only
such set that exists is the generated placeholder set
(`tools/assets/make_placeholder_set.py`), which shows the game as boxes.

## 7. What has been checked, and what has not

| Check | Shows |
| --- | --- |
| `tests/check_app.py` (`app_matches_runner`) | The game through `pop_app` (the demo, and levels 1–14 with random keys, 3,025 frames) leaves the same bytes in both hi-res pages and touches the same display switches as the same games through `pop_worldrun`, the runner that `replay_frames.py` compares with the original. So the shell's game is the tested game |
| `tests/check_sdl.py` (`sdl_shell_headless`) | With SDL's dummy drivers: the shell starts, plays 150 frames of the demo, queues sound, leaves with status 0; its last picture is `pop_headless`'s picture of that frame; 60 timed frames take the time they should |
| `tests/check_images.py` (`images_option`) | `--images`: copies of the original tables give the same frames as the checkout; placeholder pictures give the same game state after every frame and other pictures; unlabelled, incomplete and wrongly sized directories are refused |
| by eye | Frames written by `pop_headless` (level 1's first room) look like the game; with the placeholder set, like the game's boxes |

**Not verified**, because it needs a screen, a sound device or a person:

- that the window appears, scales and repaints properly on any system;
- that the keys feel right (response, diagonals, the button);
- that the sound is audible, in time and at a sensible volume;
- pause, the restarts and F1 (the event paths were not exercised: the
  dummy driver delivers no key events);
- the pace: whether 725 frames a minute feels like the game.

The next step for all of these is to run `pop_sdl` on a machine with a
display.

SDL3 itself was not installed here either. It was built from the official
source release (3.2.0, static, console-only configuration) into the build
directory for the purpose; it is not part of this repository.
