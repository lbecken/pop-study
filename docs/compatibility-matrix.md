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
| Animation | Frame definitions, alternate frame sets, sword table | faithful (data) | generated `framedef.bin`, `tools/extract_assets/pop_animdata.py` | the assembled tables, loaded by the port; the sword table is not used yet |
| Animation | Extra `animchar` calls within a tick (33 sites) | faithful, checked against the original code | `pop_control.c`, `pop_collide.c`, `pop_auto.c`, `pop_top.c` | ported with the routines that make them; the sites in drawing and cut-scene code are not |
| Rendering | Character position (`SETUPCHAR`, `GETBASEX`) | faithful, checked against the original code | `modern-port/src/core/pop_control.c`, `pop_world.c` | also as a Python model, `tools/extract_assets/pop_charpos.py`; `CVTX` model only |
| Rendering | The blitter: six draw modes, dot shifting, mirroring, cropping to a usable area, byte-aligned fast routines, rectangle fill, page copy, upside-down line table | faithful, checked against the original code | `modern-port/src/video/pop_hires.c` | every routine of `HIRES.S` compared with the original blitter byte for byte over both pages; nothing draws with it yet |
| Rendering | Save-under buffers (`LAYRSAVE`, `PEEL`) | faithful, checked against the original code | same | the lists that drive them (`ADDPEEL`, `SNGPEEL`) are missing |
| Rendering | Full-screen background redraw (`SURE`), background/mid/foreground lists | model only, checked against the original code | `tools/extract_assets/pop_room.py`, `pop_hires.py` | the Python model's page equals the original code's for all 336 screens of the 15 levels; not in C yet (milestone 10b); no characters |
| Rendering | Partial redraw, redraw buffers, wipes, page flipping | missing | – | unknown G17 |
| Rendering | Colour | missing | – | decision D3 |
| World | Block reading across screens, foot position, distances, coordinate conversion, piece classes | faithful, checked against the original code | `modern-port/src/core/pop_world.c` | 39 routines fuzzed against the original in the simulator; not compared with a running game |
| Animation | Frame lookup incl. alternate sets (`GETFRAMEINFO`) | faithful, checked against the original code | same | |
| Player | Gravity and fall movement (`GRAVITY`, `ADDFALL`) | faithful, checked against the original code | same | |
| Player | Control: dispatch by frame, standing / running / turning / crouching / hanging handlers, jumps, careful steps, pick-up, running-jump take-off adjustment | faithful, checked against the original code | `modern-port/src/core/pop_control.c` | fuzzed against the original routine by routine; calls into unported subsystems are hooks |
| Player | Sword-fight moves of the controlled character (`FightCtrl`, strike, block, advance, retreat, sheathe) | faithful, checked against the original code | same | the opponent's decisions (`AUTO.S`) are missing |
| Player | Floor checks, starting to fall, ledge grab, landing and fall damage, impalement | faithful, checked against the original code | same | |
| Player | A whole game frame (`NextFrame`, `DoKid`, `misctimers`) | faithful, checked against the original code | `modern-port/src/core/pop_top.c` | the original's `NextFrame` and the port's agree over random play and over the whole demo level; drawing is a hook |
| Player | Input: keyboard as joystick (`KREAD`), device selection, the five press flags per device (`CLRJSTK`) | faithful, checked against the original code | `modern-port/src/core/pop_loop.c` | reading the hardware (key latch, paddles, buttons) and the special keys are the shell's: `strobe` hook |
| Player | Death: waiting until he lies still, the song, "press button to continue", time-out, restart | faithful, checked against the original code | `pop_top.c` | loading the level for the restart is the shell's (`restart` hook) |
| World | A level begins: per-level reset, the kid's start and its per-level special cases, the entrance closing, the first screen | faithful, checked against the original code | same | from the point where the level file is in memory; loading it is the caller's job |
| World | The 60-minute clock, its scheduled messages and the seconds countdown; which message is on screen and when it blinks; when a queued song may play; screen flashes | faithful, checked against the original code | `pop_loop.c` | drawing the messages and playing the songs are hooks |
| World | One whole pass of `MainLoop`; game start; the step to the next level and which princess scene precedes it | faithful, checked against the original code | same | the original's `MainLoop` and `pop_mainloop` agree pass by pass over random play and over the whole demo; scenes, disk and copy protection are hooks |
| Rendering | Game state changed inside the drawing code (cut timer, redraw-buffer reset, forced redraw, mirror, meter marks, message timer) | faithful, checked against the original code | same | the drawing itself is missing |
| Rendering | Character edges and blocks covered (`GETEDGES`) | faithful, checked against the original code | same | image sizes from a generated table instead of the image data |
| Rendering | Redraw marks (`MARKRED`, `MARKWIPE`, `INDEXBLOCK`) | faithful, checked against the original code | same | the buffers are filled; nothing draws from them yet |
| World | Level blueprint and initial states (`SETINITIALS`) | faithful, checked against the original code | `pop_top.c`; Python model in `tools/extract_assets/pop_level.py` | |
| World | Collision with walls, gates, slicers and the mirror: edge-crossing detection, push-back, soft / hard / air / en-garde bumps, gate knocking a stander aside, being sliced | faithful, checked against the original code | `modern-port/src/core/pop_collide.c` | 8 entry points fuzzed against the original, and inside the kid-frame trace; the end-of-level music is requested (`SongCue`) but nothing plays it |
| World | Kid running into a guard (`firstguard`) | faithful, checked against the original code | `pop_auto.c` | |
| World | Gates and the exit: opening, waiting, closing, fast drop, jamming | faithful, checked against the original code | `modern-port/src/core/pop_mover.c` | 27 entry points fuzzed against the original, and inside the kid-frame trace |
| World | Pressure plates, their timers and link chains | faithful, checked against the original code | same | including a dead body's weight and rubble |
| World | Spikes, slicers | faithful, checked against the original code | same | |
| World | Loose floors: shaking, falling, landing as rubble, knocking others loose, crushing the kid | faithful, checked against the original code | same | simulated; not drawn (`ADDMOBS` missing) |
| World | Torch, flask and sword animation states; random numbers (`RND`) | faithful, checked against the original code | same | the seed's start value and the other callers of `RND` are not ported |
| World | Picking up a flask or sword (`REMOVEOBJ`), level 12's phantom bridge, level 4's mirror appearing, level 13's collapsing floor | faithful, checked against the original code | `pop_control.c`, `pop_mover.c` | the potion's effect is a hook |
| Opponents | Guards: noticing the kid, drawing, advancing, blocking, striking by their type's probabilities, following a kid who drops off a ledge | faithful, checked against the original code | `modern-port/src/core/pop_auto.c` | fight tables generated from the user's checkout; 29 entry points fuzzed, and in the frame trace |
| Opponents | The shadow's appearances (levels 4, 5, 6, 12), the skeleton, the mouse | faithful, checked against the original code | same | level-specific; exercised by fuzzing, not yet by playing those levels through |
| Opponents | Sword contact: thrust, block, wound, kill, tie; strength meters; potions | faithful, checked against the original code | same | upside-down potion's redraw is a hook |
| Opponents | Guards left behind, taken along, remembered dead; guards entering with the screen | faithful, checked against the original code | same | |
| World | Passing from screen to screen (`CUTCHECK`, `CUT`) | faithful, checked against the original code | same | the new screen's set-up (`PrepCut`) is in `pop_top.c`; drawing it is a hook |
| Opponents | The demo level's scripted kid and guard (`DemoCtrl`) | faithful, checked against the original code | `pop_top.c` | the script is generated data; the whole demo, 393 frames with the generator's state fixed, agrees with the original call by call |
| Audio | Sound-effect queue (`ADDSOUND`, `ZEROSOUND`, play order) | faithful, unverified | `modern-port/src/core/pop_sound.c` | |
| Audio | Sound-effect waveforms | faithful timing, approximate sound, unverified | `modern-port/src/audio/pop_sfx.c`, generated `sfx.bin` | flip times come from executing the original code; output is the 1-bit signal, not speaker acoustics |
| Audio | Effects delaying the frame they play in | missing | – | decision D2 |
| Audio | Audio output device | missing | – | SDL3 backend, milestone 7 |
| Audio | `GTONE`, `WHOOP`, cheat-timer click | missing | – | |
| Audio | Music | missing | – | player source present, score data absent from the repository |
| Timing | Frame cadence | missing | – | decision D2; original has no fixed rate. Game time is counted in frames (`pop_keeptime`), so the cadence chosen sets the length of the hour |
| Platform | SDL3 window, input, audio | missing | – | |
