# Audio analysis

Status: sound effects are extracted by executing the original code
(milestone 6). Music is not: its score data is not in the repository.

Citations are `FILE.S:lines` in `01 POP Source/Source` of the original
repository at commit `25243550` unless a longer path is given.

## What is present

| Item | Status |
| --- | --- |
| Sound-effect code and parameters | **present and complete**: `SOUND.S`, 303 bytes assembled |
| Sound-effect samples or waveforms | none exist: an effect is code that clicks the speaker |
| Music player | **present**, only under `04 Support/MakeDisk/S/MSYS.S` ("Music System ][", Kyle Freeman, 1986) |
| Music score data | **absent**: loaded from disk tracks 20 and 34 (`MASTER.S:247-280`), not in any repository file |
| Song numbers, names and cue points | present (`SOUNDNAMES.S:27-54`, `MASTER.S:113-130`, call sites of `cuesong`) |

Authorship and licence are separate from availability: `SOUND.S` is part of
the author's source release; `MSYS.S` carries its own 1986 copyright notice.
See `docs/source-inventory.md`.

## How the Apple II makes sound

Hardware facts, not from the repository: the speaker is driven by one bit.
Any access to address `$C030` flips it. There is no volume, no timer and no
sound chip, so a program makes a tone by touching `$C030` at regular
intervals, and the only clock it has is the time its own instructions take.
The CPU runs at an average of 1,020,484 cycles per second on an NTSC machine.

So "sound data" in this game is three numbers per effect: how long to wait
between clicks, how many clicks, and sometimes a second such pair.

## The tone routine

`tone`, `SOUND.S:331-354`, called with the pitch in Y (low) and X (high) and
the duration in A. Cycle counts are the documented 6502 timings:

```
tone     sty :pitch
         stx :pitch+1
:outloop bit spkr        4   flip the speaker
         ldx #0          2
:midloop ldy #0          2
:inloop  iny             2
         cpy :pitch      4
         bcc :inloop     3 (2 on exit)
         inx             2
         cpx :pitch+1    4
         bcc :midloop    3 (2 on exit)
         sec             2
         sbc #1          2
         bne :outloop    3
         rts
```

**Confirmed** by counting and by simulation:

- One speaker flip per outer loop, so `duration` is the number of flips, i.e.
  half-cycles of a square wave. Two flips make one full cycle.
- Cycles between flips: `hi × (9 × lo + 10) + 12`, where a zero byte counts
  as 1.
- The 16-bit "pitch" is therefore **not a linear delay**. The inner loop
  counts the low byte and the middle loop repeats it high-byte times. "Pitch
  1000" is `$03E8`: three passes of 232, giving 6,306 cycles, not 9,000. "Pitch
  500" is `$01F4`: one pass of 244.
- The formula assumes no branch in the loop crosses a memory page (that would
  add a cycle). As assembled the loop occupies `$EB13-$EB2A`, inside one page.

## Method

Nothing about the effects is typed into the tools.

1. `mos6502.py` assembles the original `SOUND.S` (with its `put` files) to
   303 bytes at `$EA00-$EB2E`, the address `GAMEEQ.S:36` gives for `sound`.
2. A cycle-counting 6502 simulator loads those bytes, puts a sound number in
   the queue at `soundtable`, sets `soundon`, and calls the module's entry
   point (`PLAYBACK`).
3. Every read of `$C030` is logged with the cycle count.

```bash
python3 tools/extract_audio/extract_sfx.py --source ../pop-original --out build/audio
```

writes `sfx.json` (flip times in cycles, per effect), `sfx.csv`, one WAV per
effect and `sfx_all.wav`. All are derived data and are not committed.

The simulator's steady flip intervals equal the formula above for every
effect (**confirmed** by test).

## The effects

Measured by the simulation. "Flips" is the number of speaker accesses;
frequency is that of the square wave the flips form.

| # | Name (`SOUNDNAMES.S`) | Flips | Cycles between flips | Frequency | Length | Share of a nominal tick |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | PlateDown | 4 | 652 | 783 Hz | 2.6 ms | 3% |
| 1 | PlateUp | 4 | 832 | 613 Hz | 3.4 ms | 4% |
| 2 | GateDown | same as 0 | | | | |
| 3 | SpecialKey1 | 50 | 157 | 3,250 Hz | 7.8 ms | 9% |
| 4 | SpecialKey2 | 50 | 382 | 1,336 Hz | 18.8 ms | 23% |
| 5 | Splat | 3 | 6,306 | 81 Hz | 18.6 ms | 23% |
| 6, 7 | MirrorCrack, LooseCrash | same as 5 | | | | |
| 8 | GotKey | 80 | 2 × (15 at 2,218, then 25 at 922) | 230 Hz / 553 Hz | 110.6 ms | **134%** |
| 9 | Footstep | 3 | 337 | 1,514 Hz | 1.1 ms | 1% |
| 10 | RaisingExit | 6 | 382 | 1,336 Hz | 2.3 ms | 3% |
| 11 | RaisingGate | 2 | 202 | – | 0.5 ms | 1% |
| 12 | LoweringGate | 8 | 85 | 6,003 Hz | 0.8 ms | 1% |
| 13, 14, 15 | SmackWall, Impaled, GateSlam | same as 5 | | | | |
| 16 | FlashMsg | same as 8 | | | | |
| 17, 18 | SwordClash1, SwordClash2 | same as 3 | | | | |
| 19 | JawsClash | 50 | 112 | 4,556 Hz | 5.6 ms | 7% |

A nominal tick is 60/725 s = 82.8 ms = 84,454 cycles (`SPECIALK.S:68`).

Findings (**confirmed**):

1. **Twenty names, eleven sounds.** Six effects share the "splat" thump, three
   share the key-press chirp, and two pairs are identical. The dispatch table
   (`SOUND.S:32-51`) points several entries at the same routine or at a routine
   that jumps to another.
2. **Most effects are clicks, not tones.** Three or four flips is one or two
   cycles of a wave; "frequency" describes the spacing, not a pitch you would
   hear as a note. A footstep is 1.1 ms long.
3. **Gate movement is one tiny click per frame.** RaisingGate is two flips half
   a millisecond apart; the rattle comes from it being queued on successive
   frames.
4. **One effect is longer than a frame.** GotKey / FlashMsg plays two low-high
   pairs and takes 110.6 ms, a third longer than a nominal tick, during which
   nothing else runs.

## Sound and game timing

**Confirmed** from the source:

- Game code queues effect numbers during the frame (`ADDSOUND`,
  `SOUND.S:79-91`; 27 call sites, plus the `tap` instruction of the sequence
  table). The queue holds 32; further requests are dropped.
- After the frame is drawn and shown, `PLAYBACK` plays the whole queue,
  **last queued first**, then the queue is cleared (`TOPCTRL.S:386-389`).
- Playing is synchronous: the CPU sits in `tone` and does nothing else. Each
  effect therefore lengthens the frame it occurs in by the "Length" above, and
  several effects in one frame add up.
- Numbers 20 and above are ignored by the dispatcher (`SOUND.S:127-130`).
- With sound switched off, `PLAYBACK` returns at once (`SOUND.S:101-102`).
  **Inferred:** the game therefore runs slightly faster with sound off.

Consequence for the port: effects are part of the original's frame pacing.
Reproducing the sound is separate from reproducing the delay; whether the port
stretches frames by the effect length is part of decision D2 in
`docs/milestones.md`.

## Other uses of the speaker

| Where | What | Status |
| --- | --- | --- |
| `GRAFIX.S:1314-1317` `GTONE` | key-press confirmation; uses a second copy of `tone` at `GRAFIX.S:1343-1360` with the same parameters as effect 3 | identical code, **not simulated** (the module does not assemble yet) |
| `GRAFIX.S:1324-1333` `WHOOP` | a rising sweep: 256 flips with a shrinking delay; used on disk errors (`MASTER.S:412`, `UNPACK.S:603`) | read, **not simulated** |
| `COLL.S:400-403` | one flip per frame while the "antimatter" cheat timer runs | read |
| `MSYS.S:534-537, 750-753` | the music player's four speaker accesses | not analysed |

## Music

- **Confirmed:** `MSYS.S` is a complete player with note-period and length
  tables and two families of data tables, `MV0`–`MV15` and `HM1`–`HM32`, reached
  through `ENVTBL` and `HARMTBL` (`MSYS.S:30-250`); what they encode is not
  yet analysed. It has two entry points, `MINIT` (select song) and `MPLAY` (play one step, returning 0 at
  the end).
- **Confirmed:** songs are played to completion in a loop that also polls keys
  (`TOPCTRL.S:1448-1463`); the game simulation is paused meanwhile, with only
  torch flames kept moving (`burn`).
- **Confirmed:** songs are cued with a frame budget and start only when both
  characters are in a static pose and nothing fast is moving
  (`songcues`, `TOPCTRL.S:1388-1446`).
- **Absent:** the song tables themselves.
- **Not analysed:** the song data format and the player's synthesis method
  (unknown A2). The player could be assembled and run in the simulator once
  song data is available from a disk image.

Nothing is invented here: no notes, no instruments, no substitute tunes.

## In the C port

| File | Contents |
| --- | --- |
| `modern-port/src/core/pop_sound.c` | the effect queue: `ZEROSOUND`, `ADDSOUND`, the play order, the valid-number test |
| `modern-port/src/audio/pop_sfx.c` | flip list → PCM samples; the tone-loop formula |
| `modern-port/src/assets/pop_assets.c` | loader for the generated `sfx.bin` |

`sfx.bin` holds the flip lists produced by the simulation and is generated at
build time from the user's checkout. There is **no SDL3 audio backend yet**:
SDL3 is not installed in the environment this was developed in, so it will be
written with the rest of the SDL3 shell (milestone 7) and cannot be claimed as
tested until then.

## Accuracy

| Aspect | Standing |
| --- | --- |
| Flip times in CPU cycles | exact for documented 6502 timings; obtained by running the original instructions |
| Conversion to seconds | uses the average NTSC clock; ignores the one stretched cycle in 65 (an error far below 0.1%) and PAL machines |
| Waveform | the 1-bit electrical signal, box-filtered to the sample rate. **Not** the sound of an Apple II speaker and case, which colours it heavily |
| Starting polarity | arbitrary; on a real machine it depends on everything played before |
| Comparison with a real machine or emulator | **not done** |

## Tests

- Python: the assembler and simulator on small programs written for the
  tests (addressing modes, flags, cycle counts, page-crossing penalties); the
  original module's layout, every effect's flips, the aliases, the queue order,
  the 32-entry limit and the sound-off path, all by running the original code.
- C: queue behaviour, formula, renderer and loader on synthetic data; the
  generated data against the values above; and the C renderer against the
  Python renderer for all 20 effects, sample by sample (26,204 samples at
  44.1 kHz, identical within 2 × 10⁻⁶).
