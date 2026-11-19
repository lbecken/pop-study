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
| Animation | Sequence start (`JUMPSEQ`), direction-relative x (`ADDCHARX`) | faithful, unverified | same | 8-bit index and wraparound kept |
| Animation | Frame definitions, alternate frame sets, sword table | model only | `tools/extract_assets/pop_animdata.py` | decoded to JSON; not loaded by the port yet |
| Animation | Extra `animchar` calls within a tick (31 sites) | missing | – | belongs with each control routine |
| Rendering | Character position (`SETUPCHAR`, `GETBASEX`, `CVTX`) | model only | `tools/extract_assets/pop_charpos.py` | |
| Rendering | Mask blitter, normal and mirrored, on-screen images | model only | `tools/extract_assets/pop_hires.py` | no cropping |
| Rendering | Cropping, save-under buffers, XOR and other draw modes | missing | – | unknowns G13–G15 |
| Rendering | Full-screen background redraw (`SURE`), background/mid/foreground lists, fast blitters | model only | `tools/extract_assets/pop_room.py`, `pop_hires.py` | all 299 screens render; no characters |
| Rendering | Partial redraw, redraw buffers, wipes, page flipping | missing | – | unknown G17 |
| Rendering | Colour | missing | – | decision D3 |
| World | Block reading across screens, foot position, distances, coordinate conversion, piece classes | faithful, checked against the original code | `modern-port/src/core/pop_world.c` | 39 routines fuzzed against the original in the simulator; not compared with a running game |
| Animation | Frame lookup incl. alternate sets (`GETFRAMEINFO`) | faithful, checked against the original code | same | |
| Player | Gravity and fall movement (`GRAVITY`, `ADDFALL`) | faithful, checked against the original code | same | |
| Player | Input, control handlers, floor checks, landing | missing | – | milestones 8b, 8c |
| World | Level blueprint and initial states (`SETINITIALS`) | model only | `tools/extract_assets/pop_level.py` | |
| World | Collisions, moving objects, pressure-plate links | missing | – | |
| Opponents | AI, sword fighting | missing | – | |
| Audio | Sound-effect queue (`ADDSOUND`, `ZEROSOUND`, play order) | faithful, unverified | `modern-port/src/core/pop_sound.c` | |
| Audio | Sound-effect waveforms | faithful timing, approximate sound, unverified | `modern-port/src/audio/pop_sfx.c`, generated `sfx.bin` | flip times come from executing the original code; output is the 1-bit signal, not speaker acoustics |
| Audio | Effects delaying the frame they play in | missing | – | decision D2 |
| Audio | Audio output device | missing | – | SDL3 backend, milestone 7 |
| Audio | `GTONE`, `WHOOP`, cheat-timer click | missing | – | |
| Audio | Music | missing | – | player source present, score data absent from the repository |
| Timing | Frame cadence | missing | – | decision D2; original has no fixed rate |
| Platform | SDL3 window, input, audio | missing | – | |
