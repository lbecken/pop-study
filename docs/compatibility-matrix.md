# Compatibility matrix

Status of each behaviour in the reconstruction.

- **faithful**: ported from the source and tested against original data
- **approximate**: works, with a stated difference
- **model only**: exists as a Python model used for analysis, not in the C port
- **missing**: not started
- **checked against the original code**: compared with the original routine
  executed in the project's 6502 simulator (`docs/oracle.md`)
- **unverified** is added where neither that nor a comparison with the running
  game has been done. Nothing at all has been compared with the running game
  (see `verification.md`)

| Area | Behaviour | Status | Where | Notes |
| --- | --- | --- | --- | --- |
| Animation | Sequence byte-code interpreter (`ANIMCHAR`) | faithful, checked against the original code | `modern-port/src/core/pop_seq.c` | hooks instead of direct calls; errors instead of hangs or stray reads. The original routine, run in the simulator, matches for all 114 sequences |
| Animation | Sequence start (`JUMPSEQ`), direction-relative x (`ADDCHARX`) | faithful, checked against the original code | same | 8-bit index and wraparound kept; exercised by every control comparison |
| Animation | Frame definitions, alternate frame sets, sword table | faithful (data) | generated `framedef.bin`, `tools/extract_assets/pop_animdata.py` | the assembled tables, loaded by the port; the sword table is read by `pop_setupsword` |
| Animation | Extra `animchar` calls within a tick (33 sites) | faithful, checked against the original code | `pop_control.c`, `pop_collide.c`, `pop_auto.c`, `pop_top.c` | ported with the routines that make them; the sites in drawing and cut-scene code are not |
| Rendering | Character position (`SETUPCHAR`, `GETBASEX`) | faithful, checked against the original code | `modern-port/src/core/pop_control.c`, `pop_world.c` | also as a Python model, `tools/extract_assets/pop_charpos.py`; `CVTX` is part of `pop_addcharobj` (`pop_objs.c`) |
| Rendering | The blitter: six draw modes, dot shifting, mirroring, cropping to a usable area, byte-aligned fast routines, rectangle fill, page copy, upside-down line table | faithful, checked against the original code | `modern-port/src/video/pop_hires.c` | every routine of `HIRES.S` compared with the original blitter byte for byte over both pages; all drawing goes through it |
| Rendering | Save-under buffers (`LAYRSAVE`, `PEEL`) and the lists that drive them (`ADDPEEL`, `SNGPEEL`) | faithful, checked against the original code | `pop_hires.c`, `pop_lists.c` | the blitter side compared with the original call by call; the lists inside partial redraws and whole frames (`compare_fast.py`, `replay_frames.py`) |
| Rendering | Full-screen background redraw (`SURE`), the display lists and `DRAWALL` | faithful, checked against the original code | `modern-port/src/video/pop_frameadv.c`, `pop_lists.c` | page bytes and lists equal the original's for every screen of the 15 levels, the null screens, and screens with random gadget states and pieces; the Python model (`tools/extract_assets/pop_room.py`) agrees too; characters are the object-list row below |
| Rendering | Partial redraw (`FAST`): redraw buffers, wipes, floor pieces over a character, top line, strength meters | faithful, checked against the original code | page, lists and buffers on random marks (`compare_fast.py`) | `docs/port/frame-drawing.md` |
| Rendering | Characters with cropping, swords, impact stars, falling floors: the object list and its drawing, save-under on two pages | faithful, checked against the original code | object list on random game states (`fuzz_control.py @objs`); pages on random object lists (`compare_fast.py`) | `docs/port/frame-drawing.md` |
| Rendering | Messages ("LEVEL n", time left, "Press button to continue"), page flipping, screen changes, the flash; the frame as a whole | faithful, checked against the original code | both hi-res pages and the display switches after every frame of the demo and of random-input traces (`replay_frames.py`) | `docs/port/frame-drawing.md` §5; what is compared is page bytes and switches, not a picture (next two rows) |
| Rendering | The dots of the page on the screen | faithful | derived from the compared page bytes (`pop_present_dots`) | `docs/port/presentation.md` §2 |
| Rendering | Colour | approximate, unverified | a rule-based artifact-colour model, 280 pixels wide; by eye only | decision D3; `docs/port/presentation.md` §3-4: no half-dot shift, no fringes, no signal blur; not compared with an emulator |
| World | Block reading across screens, foot position, distances, coordinate conversion, piece classes | faithful, checked against the original code | `modern-port/src/core/pop_world.c` | 39 routines fuzzed against the original in the simulator; not compared with a running game |
| Animation | Frame lookup incl. alternate sets (`GETFRAMEINFO`) | faithful, checked against the original code | same | |
| Player | Gravity and fall movement (`GRAVITY`, `ADDFALL`) | faithful, checked against the original code | same | |
| Player | Control: dispatch by frame, standing / running / turning / crouching / hanging handlers, jumps, careful steps, pick-up, running-jump take-off adjustment | faithful, checked against the original code | `modern-port/src/core/pop_control.c` | fuzzed against the original routine by routine; calls into unported subsystems are hooks |
| Player | Sword-fight moves of the controlled character (`FightCtrl`, strike, block, advance, retreat, sheathe) | faithful, checked against the original code | same | the opponent's decisions are `pop_auto.c` (Opponents rows) |
| Player | Floor checks, starting to fall, ledge grab, landing and fall damage, impalement | faithful, checked against the original code | same | |
| Player | A whole game frame (`NextFrame`, `DoKid`, `misctimers`) | faithful, checked against the original code | `modern-port/src/core/pop_top.c` | the original's `NextFrame` and the port's agree over random play and over the whole demo level; drawing is behind hooks, which `src/video` fills (`pop_app.c`) |
| Player | Input: keyboard as joystick (`KREAD`), device selection, the five press flags per device (`CLRJSTK`) | faithful, checked against the original code | `modern-port/src/core/pop_loop.c` | reading the hardware (key latch, paddles, buttons) and the special keys are the shell's: `strobe` hook |
| Player | Death: waiting until he lies still, the song, "press button to continue", time-out, restart | faithful, checked against the original code | `pop_top.c` | loading the level for the restart is the shell's (`restart` hook) |
| World | A level begins: per-level reset, the kid's start and its per-level special cases, the entrance closing, the first screen | faithful, checked against the original code | same | from the point where the level file is in memory; loading it is the caller's job |
| World | The 60-minute clock, its scheduled messages and the seconds countdown; which message is on screen and when it blinks; when a queued song may play; screen flashes | faithful, checked against the original code | `pop_loop.c` | the messages are drawn by `pop_screen.c` behind a hook; songs are a hook nothing is connected to (no music) |
| World | One whole pass of `MainLoop`; game start; the step to the next level and which princess scene precedes it | faithful, checked against the original code | same | the original's `MainLoop` and `pop_mainloop` agree pass by pass over random play and over the whole demo; scenes, disk and copy protection are hooks |
| Rendering | Game state changed inside the drawing code (cut timer, redraw-buffer reset, forced redraw, mirror, meter marks, message timer) | faithful, checked against the original code | same | the drawing itself: the Rendering rows above |
| Rendering | Character edges and blocks covered (`GETEDGES`) | faithful, checked against the original code | same | image sizes from a table, not from the image data: in the game those of the image tables loaded (`pop_imgtab_dims`), in the routine tests a generated one |
| Rendering | Redraw marks (`MARKRED`, `MARKWIPE`, `INDEXBLOCK`) | faithful, checked against the original code | same | `pop_fast` draws from them |
| World | Level blueprint and initial states (`SETINITIALS`) | faithful, checked against the original code | `pop_top.c`; Python model in `tools/extract_assets/pop_level.py` | |
| World | Collision with walls, gates, slicers and the mirror: edge-crossing detection, push-back, soft / hard / air / en-garde bumps, gate knocking a stander aside, being sliced | faithful, checked against the original code | `modern-port/src/core/pop_collide.c` | 8 entry points fuzzed against the original, and inside the kid-frame trace; the end-of-level music is requested (`SongCue`) but nothing plays it |
| World | Kid running into a guard (`firstguard`) | faithful, checked against the original code | `pop_auto.c` | |
| World | Gates and the exit: opening, waiting, closing, fast drop, jamming | faithful, checked against the original code | `modern-port/src/core/pop_mover.c` | 27 entry points fuzzed against the original, and inside the kid-frame trace |
| World | Pressure plates, their timers and link chains | faithful, checked against the original code | same | including a dead body's weight and rubble |
| World | Spikes, slicers | faithful, checked against the original code | same | |
| World | Loose floors: shaking, falling, landing as rubble, knocking others loose, crushing the kid | faithful, checked against the original code | same | drawn through `pop_addmobs` (object-list row) |
| World | Torch, flask and sword animation states; random numbers (`RND`) | faithful, checked against the original code | same | the seed's start value and the other callers of `RND` are not ported |
| World | Picking up a flask or sword (`REMOVEOBJ`), level 12's phantom bridge, level 4's mirror appearing, level 13's collapsing floor | faithful, checked against the original code | `pop_control.c`, `pop_mover.c` | the potion's effect is a hook |
| Opponents | Guards: noticing the kid, drawing, advancing, blocking, striking by their type's probabilities, following a kid who drops off a ledge | faithful, checked against the original code | `modern-port/src/core/pop_auto.c` | fight tables generated from the user's checkout; 29 entry points fuzzed, and in the frame trace |
| Opponents | The shadow's appearances (levels 4, 5, 6, 12), the skeleton, the mouse | faithful, checked against the original code | same | level-specific; exercised by fuzzing, not yet by playing those levels through |
| Opponents | Sword contact: thrust, block, wound, kill, tie; strength meters; potions | faithful, checked against the original code | same | the upside-down potion swaps the blitter's line table (`pop_hr_inverty`) behind a hook |
| Opponents | Guards left behind, taken along, remembered dead; guards entering with the screen | faithful, checked against the original code | same | |
| World | Passing from screen to screen (`CUTCHECK`, `CUT`) | faithful, checked against the original code | same | the new screen's set-up (`PrepCut`) is in `pop_top.c`; drawing it is `pop_cleancut` with `src/video` behind its hooks |
| Opponents | The demo level's scripted kid and guard (`DemoCtrl`) | faithful, checked against the original code | `pop_top.c` | the script is generated data; the whole demo, 393 frames with the generator's state fixed, agrees with the original call by call |
| Audio | Sound-effect queue (`ADDSOUND`, `ZEROSOUND`, play order) | faithful, unverified | `modern-port/src/core/pop_sound.c` | |
| Audio | Sound-effect waveforms | faithful timing, approximate sound, unverified | `modern-port/src/audio/pop_sfx.c`, generated `sfx.bin` | flip times come from executing the original code; output is the 1-bit signal, not speaker acoustics |
| Audio | Effects delaying the frame they play in | approximate, unverified | `pop_sdl --stretch` adds the effects' length to the frame; off by default | decision D2; `docs/port/shell.md` §3-4 |
| Audio | Audio output device | approximate, unverified | SDL3 audio stream in `pop_sdl`; effects are queued, they do not stop the game | run with the dummy driver only: nothing has been heard (`docs/port/shell.md` §7) |
| Audio | `GTONE`, `WHOOP`, cheat-timer click | missing | – | |
| Audio | Music | missing | – | player source present, score data absent from the repository |
| Timing | Frame cadence | approximate | fixed 725 frames a minute in the shell (`--rate`), decision D2 | the original has no fixed rate: 50 to 220 ms a frame over the demo by cycle count, 106 ms on average (unknown T1). Game time is counted in frames (`pop_keeptime`), so the cadence sets the length of the hour |
| Platform | SDL3 window, keyboard, audio; headless player | new code, unverified on a display | `pop_sdl`, `pop_headless` over `pop_app`; the game through them equals the tested runner frame for frame (`check_app.py`); the shell run with SDL's dummy drivers (`check_sdl.py`) | `docs/port/shell.md`; keys reach the game as a joystick; the original's special keys, title sequence, scenes and saved games are not ported |
| Platform | Replacement pictures (`--images <dir>`) | new code; the route tested, no artwork exists | `pop_app.c`; `tools/assets` (table encoder, set checker, placeholder generator); `check_images.py` | all tables from the directory or none; refused without a provenance file or with other image sizes than the original's; with generated placeholders the game state is the same frame for frame (`docs/blender-to-sdl3.md`) |
