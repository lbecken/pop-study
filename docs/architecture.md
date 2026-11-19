# Architecture

Scope of this revision: the main loop, the player run-animation pipeline and an
outline of the draw path. Everything else is listed in `docs/unknowns.md`.

Citations are `FILE.S:lines` in `01 POP Source/Source` of the original
repository at commit `25243550` unless a longer path is given. Labels are quoted
with the original spelling. **Confirmed** = read in the source; **inferred** =
my deduction; **not yet verified** = not checked.

## 1. Module map

Call direction is top to bottom. Every module exports a jump table at its
`org`, and callers go through the addresses declared in `EQ.S` / `GAMEEQ.S`.

```
MASTER   boot, disk I/O, attract mode, titles        (main language card)
  │
TOPCTRL  START / RESTART / MainLoop / NextFrame / FrameAdv
  │
  ├─ SPECIALK  keyboard, timer, ADDSOUND, CUESONG
  ├─ MOVER     animtrans / animmobs  (gates, spikes, loose floors)
  ├─ CTRL      PlayerCtrl → GenCtrl → standing / arunning / FightCtrl …
  │    └─ CTRLSUBS  jumpseq, setupchar, getframe, addcharx
  ├─ COLL      animchar (sequence interpreter), checkbarr, collisions
  ├─ AUTO      ShadCtrl (opponent AI), checkstrike/checkstab, cutcheck
  ├─ FRAMEADV  sure / fast: level + object list → image lists
  │    └─ GAMEBG, BGDATA
  ├─ GRAFIX    drawall: image lists → blitter calls; controller; vblank
  │    └─ HIRES + HRTABLES   lay / fastlay / layrsave / peel
  ├─ SOUND     playback → tone (speaker)
  └─ MSYS      minit / mplay (music; source only under 04 Support)
```

Data tables consumed by the above: `SEQTABLE.S` (what frame comes next),
`FRAMEDEF.S` (what a frame looks like), `BGDATA.S` (what a tile looks like),
level blueprints, and the image tables.

## 2. Main loop

**Confirmed**, `TOPCTRL.S:366-401`. One iteration is one game frame.

```
MainLoop:
    rnd                      ; advance random seed
    ChgKidStr = ChgOppStr = 0
    strobe                   ; read keyboard + joystick    (SPECIALK.S:1343)
    demokeys                 ; in demo, any key starts a game
    misctimers
    NextFrame                ; simulate one frame          (TOPCTRL.S:503)
    flashon
    FrameAdv                 ; draw hidden page and flip   (TOPCTRL.S:606)
    playback                 ; play queued sound effects   (SOUND.S:100)
    zerosound                ; clear the queue
    flashoff
    songcues                 ; play cued music, if any     (TOPCTRL.S:1388)
    if NextLevel == level: goto MainLoop
    yellowcheck              ; copy-protection check
    goto LoadNextLevel
```

### NextFrame – simulation order

**Confirmed**, `TOPCTRL.S:503-597`:

1. `animmobs`, `animtrans` – moving level objects
2. `bonesrise`, `checkalert`
3. `DoKid` – the player
4. `DoShad` – the opponent, if any
5. `checkstrike`, `checkstab` – sword hits
6. `addsfx`, `chgmeters`
7. `cutcheck`, `PrepCut`, `cutguard` – screen transitions
8. level-specific exits (levels 0, 6, 12), then `keeptime` / `showtime` and the
   time-up check

### DoKid – per-frame player update

**Confirmed**, `TOPCTRL.S:637-692`. Order matters and must be preserved:

```
LoadKidwOp            ; copy Kid vars → Char vars, Shad → Op
rereadblocks
unholy
ctrlplayer            ; input → maybe choose a new sequence   (step A below)
wtlessflash
if CharScrn != 0:
    animchar          ; advance sequence → new CharPosn, CharX (step B)
    gravity; addfall
    setupchar; rereadblocks; getedges
    firstguard; checkbarr; collisions; checkgate
    checkfloor; checkpress; checkspikes; checkimpale; checkslice
    shakeloose
SaveKid               ; copy Char vars back to Kid vars
```

The 16-byte character record (`CharPosn`, `CharX`, `CharY`, `CharFace`,
`CharBlockX/Y`, `CharAction`, `CharXVel/YVel`, `CharSeq`, `CharScrn`,
`CharRepeat`, `CharID`, `CharSword`, `CharLife`) is defined at
`GAMEEQ.S:566-581` and documented by the author in
`04 Support/MakeDisk/S/CHARCOMMENTS.S:7-106`. The same control code serves kid
and opponent by loading either record into the `Char` slot.

### Frame cadence

- **Confirmed:** `MainLoop` contains no delay and no vertical-blank wait. The
  only `vblank` call sites are in level restart, full-screen redraw, flash and
  text/picture code (`TOPCTRL.S:266, 927, 1581, 1595`; `SUBS.S:213, 221, 929`;
  `UNPACK.S:631, 643`). `PAGEFLIP` flips immediately (`SUBS.S:397-418`). So the
  frame rate is set by how long the frame's work takes on a 1 MHz 6502.
- **Confirmed:** the game clock assumes `min = 725` frames per minute, with the
  author's comment "actual frame rate approx. 11 fps" (`SPECIALK.S:68-70`).
  725/60 = 12.08 frames per second.
- **Confirmed:** `SPEED` is a delay used by the cut-scene playback loop only
  (`SUBS.S:876-882`); it is not read on the `MainLoop` path.
- **Confirmed:** sound effects and music are produced by busy loops that run
  inside the frame (`SOUND.S:331-352`; `TOPCTRL.S:1448-1463`), so a frame with
  sound takes longer, and while a song plays the simulation does not advance
  at all (`songcues` only calls `burn` to keep torches animating).
- **Not yet verified:** the real per-frame duration and how much it varies with
  scene content. This needs an emulator or a cycle count.

For the port this means a fixed timestep is a modelling choice, not a
transcription. See the open decision in `docs/milestones.md`.

## 3. Player run-animation pipeline

From joystick to pixels, for the case "standing, push forward, keep running".

### Step A – input chooses a sequence

`ctrlplayer` (`TOPCTRL.S:1095-1098`) → `PLAYERCTRL` (`CTRL.S:554-582`) →
`UserCtrl` (`CTRL.S:610-622`, which swaps forward/back when the character faces
right) → `GENCTRL` (`CTRL.S:648-731`).

`GENCTRL` dispatches on `CharPosn`, the frame shown *last* frame
(**confirmed**, `CTRL.S:689-731`):

| `CharPosn` | Handler | Meaning |
| --- | --- | --- |
| 15 | `standing` | standing still |
| 48 | `turning` | mid-turn |
| 50–52 | `standing` | end of turn / crouch |
| 1–3 | `starting` | first frames of start-run |
| 67–69 | `stjumpup` | starting to jump up |
| 4–14 | `arunning` | running |
| 87–99 | `hanging` | hanging from a ledge |
| 109 | `crouching` | crouched |
| other | (none) | input ignored this frame |

Input is expressed relative to the facing direction: `JSTKX` negative = forward,
positive = back; `JSTKY` negative = up; and "fresh press" flags `clrF`, `clrB`,
`clrU`, `clrD`, `clrbtn`, which are negative on a new press and are set to 1
once consumed (`CTRL.S:636-646`).

Run-related decisions (**confirmed**):

- `standing`, button up, forward → `DoStartrun` (`CTRL.S:1122-1137`).
- `DoStartrun`: if a solid barrier is less than 8 units ahead, take a careful
  step instead; otherwise `jumpseq(startrun)` (`CTRL.S:1537-1561`).
- `arunning` (`CTRL.S:1348-1401`):
  - stick centred → `runstop`, **but only when `CharPosn` is 7 or 11**; on other
    frames the run continues until one of those is reached;
  - stick back → `runturn`, immediately;
  - stick up with a fresh press → `DoRunjump`;
  - fresh down → `rdiveroll`.

`JUMPSEQ` (`CTRLSUBS.S:700-711`) sets `CharSeq` to entry `A-1` of the 2-byte
address table at the start of the sequence table (`SEQTABLE.S:33-146`). It does
not change the displayed frame by itself.

### Step B – the sequence interpreter

`ANIMCHAR` (`COLL.S:994-1156`) is called from `DoKid` and `DoShad` once per
frame, and from 31 other places that need the first frame of a new sequence
immediately, so a character can advance two frames in one tick (see
`docs/port/sequence-interpreter.md`). It reads
bytes at `CharSeq` (via `GETSEQ`, `CTRLSUBS.S:1629-1639`), executing instruction
bytes until it reads a byte that is not an instruction. That byte is the new
frame number: it is stored in `CharPosn` and the routine returns.

Instruction codes are the values −1 … −15 (`SEQDATA.S:116-130`):

| Code | Operands | Effect (all **confirmed** in `COLL.S`) |
| --- | --- | --- |
| `goto` −1 | addr lo, hi | `CharSeq` = addr |
| `aboutface` −2 | – | `CharFace` ^= $FF |
| `up` −3 / `down` −4 | – | `CharBlockY` −1 / +1, then `addslicers` |
| `chx` −5 | dx | `CharX` = `addcharx(dx)`: +dx if facing right, −dx if facing left |
| `chy` −6 | dy | `CharY` += dy |
| `act` −7 | n | `CharAction` = n |
| `setfall` −8 | xv, yv | `CharXVel`, `CharYVel` |
| `ifwtless` −9 | addr | branch if `weightless`, else skip 2 bytes |
| `die` −10 | – | no operation here |
| `jaru` −11 / `jard` −12 | – | `jarabove` = 1 / −1 |
| `effect` −13 | n | n = 1: `potioneffect` |
| `tap` −14 | n | n = 1: queue `Footstep`; n = 2: queue `SmackWall`; n = 0 or 1 or 2 also sets `alertguard` |
| `nextlevel` −15 | – | `GoneUpstairs` |

Because the frame number terminates the scan, anything written *after* a frame
number in the table is executed at the start of the *next* frame.

### The run sequences

Transcribed from `SEQTABLE.S:151-177`, one row per game frame:

| Tick | Executed before the frame | Frame shown (`CharPosn`) | Frame name (`FRAMEDEF.S:23-36`) |
| --- | --- | --- | --- |
| 1 | `act 1` | 1 | run-4 |
| 2 | | 2 | run-5 |
| 3 | | 3 | run-6 |
| 4 | | 4 | run-7 |
| 5 | `chx 8` | 5 | run-8 |
| 6 | `chx 3` | 6 | run-9 |
| 7 | `chx 3` | 7 | run-10 |
| 8 | `chx 5` | 8 | run-11 |
| 9 | `chx 1`, `tap 1` | 9 | run-12 |
| 10 | `chx 2` | 10 | run-13 |
| 11 | `chx 4` | 11 | run-14 |
| 12 | `chx 5` | 12 | run-15 |
| 13 | `chx 2`, `tap 1` | 13 | run-16 |
| 14 | `chx 3` | 14 | run-17 |
| 15 | `chx 4`, `goto runcyc1` | 7 | run-10 |
| 16… | as ticks 8–15 | 8… | |

Derived from the table (**inferred**, arithmetic only):

- Start-up is 6 frames (1–6) and moves 8+3+3 = 14 units before the cycle begins.
- The cycle is 8 frames (7–14) and moves 5+1+2+4+5+2+3+4 = 26 units per lap,
  on the 140-unit-wide coordinate system. A block is 14 units wide
  (**confirmed** from `BlockEdge` in `TABLES.S`; see `run-cycle-analysis.md`).
- Footstep sounds are queued on the ticks that show frames 9 and 13, four
  frames apart. `runstop` is permitted on frames 7 and 11, each two frames
  before a footstep frame.
- The `running` entry point (`SEQTABLE.S:151-154`) joins the cycle at frame 7
  without the start-up; `turnrun` joins at frame 1 (`SEQTABLE.S:558-562`).

Leaving the run (**confirmed**, `SEQTABLE.S:891-900`): `runstop` shows frames
53, 54, 55, 56, 49, 50, 51, 52 with displacements 2, 7, 0, 2, −2 and two
footstep taps, then goes to `stand` (frame 15 repeating).

### Step C – frame definition

`SETUPCHAR` (`CTRLSUBS.S:794-841`) calls `GETFRAMEINFO`
(`CTRLSUBS.S:1648-1674`), which indexes the frame table with
`(CharPosn − 1) × 5` (`getfindex`, `CTRLSUBS.S:941-964`) and loads five bytes.
Their meaning is documented by the author in
`04 Support/MakeDisk/S/COMMENTS.S:346-400`:

| Byte | Name | Meaning |
| --- | --- | --- |
| 0 | `Fimage` | bits 0–6 image number; bit 7 = bit 2 of table number |
| 1 | `Fsword` | bits 6–7 = bits 0–1 of table number; bits 0–5 sword frame |
| 2 | `Fdx` | draw offset in x, 140-wide units, + = forward |
| 3 | `Fdy` | draw offset in y, + = down |
| 4 | `Fcheck` | bit 7 odd/even pixel; bit 6 floor check required; bit 5 "thin" for collisions; bits 0–4 distance from image left edge to the base x (weight-bearing foot) |

`decodeim` (`CTRLSUBS.S:1017-1039`) combines the table bits into
`FCharTable` (0–7) and `FCharImage`. For the 14 run frames `Fsword` is 0 and
`Fimage` is `$01`–`$0E` (`FRAMEDEF.S:23-36`), giving table 0 = `chtable1`,
images 1–14. The decoder in `tools/extract_assets` confirms those 14 images
exist in `IMG.CHTAB1` and look like a run.

`Fdx`/`Fdy` only position the drawing; they do not accumulate into `CharX`.
Accumulated movement comes solely from `chx`/`chy` (and from `CharXVel` /
`CharYVel` while falling).

Other characters reuse the kid's frame numbers with substitution
(`usealtsets`, `CTRLSUBS.S:1685-1715`): for `CharID` 1–4, frames 150–189 map to
"alternate set 1" (`chtable4`); `CharID` ≥ 5 uses "alternate set 2".

### Step D – screen coordinates

**Confirmed**, `CTRLSUBS.S:804-840`:

```
FCharX = 2 × (addcharx(Fdx) − ScrnLeft)          ; ScrnLeft = 58 (EQ.S:479)
         16-bit, with a wrap fix for small negative values
FCharY = CharY + Fdy − ScrnTop                   ; ScrnTop = 0
if (Fcheck ^ CharFace) has bit 7 clear:  FCharX += 1
```

So game logic works on a 140 × 192 grid whose left edge is x = 58, and drawing
works on the 280 × 192 hi-res grid; the `Fcheck` bit supplies the half-unit.
`CharFace` is −1 for left ("normal") and 0 for right ("mirrored")
(`CTRLSUBS.S:354-355`); the stored artwork faces left.
The y coordinate handed to the blitter is the *bottom* line of the image
(`HIRES.S:364-367`).

### Step E – drawing

`FrameAdv` (`TOPCTRL.S:606-613`) → `DoFast` (`TOPCTRL.S:964-982`):

1. `zerolsts` – clear image lists
2. `addmobs`, `addchars` – put falling floors and characters into the object
   list; for the kid: `setupkid` → `setupchar`, `unevenfloor`, `getedges`,
   `indexchar`, `quickfg`, `quickfloor`, `cropchar`, `addkidobj`
   (`TOPCTRL.S:803-821`)
3. `fast` (FRAMEADV) – turn redraw buffers + object list into image lists
4. `dispmsg`
5. `drawall` (`GRAFIX.S:484-505`): `SNGPEEL` (restore what was under the
   characters on this page, from the list saved two frames ago), `DRAWWIPE`,
   `DRAWBACK`, `DRAWMID` (characters; saves the pixels they cover), `DRAWFORE`,
   `DRAWMSG`

then `PageFlip` (`SUBS.S:397-418`) shows the page just drawn and makes the
other one the hidden page.

So the display technique is **confirmed** to be two hi-res pages, page
flipping, per-block redraw counters ("redraw buffers",
`04 Support/MakeDisk/S/COMMENTS.S:156-229`) and save-under buffers ("peel"),
rather than full redraws. A full redraw (`DoSure`) happens only on a screen cut.

The mask-then-OR mode and the mirrored path used for right-facing characters
are now modelled and tested for on-screen images (`docs/run-cycle-analysis.md`,
`tools/extract_assets/pop_hires.py`). Still **not yet verified**: cropping at
screen edges (`CROP`), the save-under buffers, and the XOR mode used for the
shadow.

## 4. Sound in the frame

**Confirmed:**

- Game code queues effects with `addsound` into `soundtable` (max 32,
  `GAMEEQ.S:50, 63`; `SOUND.S:79-91`).
- `playback` plays every queued effect, last queued first, then the queue is
  cleared (`SOUND.S:100-118`; `TOPCTRL.S:388-389`).
- Each effect loads a pitch (16-bit) and a duration and calls `tone`
  (`SOUND.S:149-320`). `tone` reads `$C030` once per outer iteration (toggling
  the speaker), then spins in a counted delay, `duration` times
  (`SOUND.S:331-352`).
- Music is cued with `cuesong` (song number + a frame budget in `SongCount`) and
  played later by `songcues`, which waits until both characters are in a static
  pose and nothing fast is moving, then runs the player to completion
  (`TOPCTRL.S:1388-1466`).

The delay is not linear in the 16-bit pitch value: the inner loop counts the
low byte and the middle loop repeats it high-byte times, so "pitch 1000"
(`$03E8`) is three passes of 232. This and every effect's exact timing are now
**confirmed** by running the original routine in a 6502 simulator; see
`docs/audio-analysis.md`.
