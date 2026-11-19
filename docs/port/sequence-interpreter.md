# Port notes: sequence interpreter

First C17 subsystem (milestone 4): `ADDCHARX`, `JUMPSEQ`, `GETSEQ`, `ANIMCHAR`.

C sources: `modern-port/src/core/pop_char.h`, `pop_seq.h`, `pop_seq.c`.
Citations are `FILE.S:lines` in `01 POP Source/Source` of the original
repository at commit `25243550`.

Addresses: the modules containing these routines were not assembled, so the
addresses below are the **jump-table slots** callers use (computed from the
`dum` sections in `GAMEEQ.S`), not the addresses of the routine bodies.

## 1. Interface of the original routines

### `ADDCHARX` — `CTRLSUBS.S:353-363`, slot `$D015`

| | |
| --- | --- |
| In | A = signed distance (+ = forward); `CharX` `$41`; `CharFace` `$43` |
| Out | A = new x. **Does not store it**; a caller that wants to move the character does `sta CharX` itself |
| Side effects | none (flags only) |
| Called from | 31 sites |

### `JUMPSEQ` — `CTRLSUBS.S:700-711`, slot `$D0A2`

| | |
| --- | --- |
| In | A = sequence number, documented range 1–127 |
| Out | `CharSeq` `$49-$4A` = entry address read from `seqtab` (`$3000`) |
| Side effects | X destroyed. `CharPosn` is **not** changed |
| Called from | 78 sites |

### `GETSEQ` — `CTRLSUBS.S:1629-1639`, slot `$D003`

| | |
| --- | --- |
| In | `CharSeq` |
| Out | A = byte at `(CharSeq)`; `CharSeq` incremented (16-bit) |
| Called from | `ANIMCHAR` only (12 sites, all in `COLL.S`) |

### `ANIMCHAR` — `COLL.S:994-1156`, slot `$450C`

| | |
| --- | --- |
| In | `CharSeq`; `CharX`, `CharY`, `CharFace`, `CharBlockY`; `weightless` `$C5` |
| Out | `CharPosn` `$40` = next frame; `CharSeq` advanced past it |
| May change | `CharX`, `CharY`, `CharFace`, `CharBlockY`, `CharAction` `$46`, `CharXVel` `$47`, `CharYVel` `$48`; `jarabove` `$CD`; `alertguard` `$E4` |
| Calls | `getseq`, `addcharx`, `addslicers` (`$E01E`), `addsound` (`$D909`), `potioneffect` (`$F90F`), `GoneUpstairs` (local, `COLL.S:1162-1176`) |
| Called from | 33 sites in six files |

Invariants the routine relies on but does not check:

- `CharSeq` points into the sequence table at an instruction boundary.
- Every path through the table reaches a frame byte. Otherwise it never
  returns.

It operates on the working record `Char`; callers load the kid or the opponent
into it first (`LoadKid`, `LoadShad`) and save it afterwards.

`ANIMCHAR` is not called exactly once per frame. The regular calls are
`DoKid` (`TOPCTRL.S:661`) and `DoShad` (`TOPCTRL.S:717`), but 31 other sites call
it too, usually right after a `jumpseq` to show the first frame of a new
sequence in the same tick (for example `CTRL.S:420-425`, "advance 1 frame into
fall"). So a character can advance two frames in one game tick. The port must
keep those extra calls where the original has them.

## 2. Pseudocode

Original labels are kept. All arithmetic is 8-bit unless stated.

```
ADDCHARX(A):
    if CharFace bit 7 set:          ; facing left
        A = -A
    return A + CharX                ; carry discarded

JUMPSEQ(A):
    X = (A - 1) * 2                 ; 8-bit: bit 7 of (A-1) is shifted out
    CharSeq.lo = seqtab[X]
    CharSeq.hi = seqtab[X + 1]

GETSEQ():
    A = mem[CharSeq]
    CharSeq = CharSeq + 1           ; 16-bit
    return A

ANIMCHAR():
  :next
    A = GETSEQ()
    chx:       CharX = ADDCHARX(GETSEQ());                        goto :next
    chy:       CharY = GETSEQ() + CharY;                          goto :next
    aboutface: CharFace = CharFace XOR $FF;                       goto :next
    goto:      lo = GETSEQ(); hi = GETSEQ(); CharSeq = hi:lo;     goto :next
    up:        CharBlockY -= 1; addslicers();                     goto :next
    down:      CharBlockY += 1; addslicers();                     goto :next
    act:       CharAction = GETSEQ();                             goto :next
    setfall:   CharXVel = GETSEQ(); CharYVel = GETSEQ();          goto :next
    ifwtless:  if weightless != 0: as goto
               else: GETSEQ(); GETSEQ()                           goto :next
    die:                                                          goto :next
    jaru:      jarabove = 1;                                      goto :next
    jard:      jarabove = $FF;                                    goto :next
    tap:       n = GETSEQ()
               n == 1: addsound(Footstep);  alertguard = 1
               n == 2: addsound(SmackWall); alertguard = 1
               n == 0:                      alertguard = 1
               else:   nothing                                    goto :next
    nextlevel: GoneUpstairs();                                    goto :next
    effect:    if GETSEQ() == 1: potioneffect();                  goto :next
    anything else:
               CharPosn = A
               return
```

Instruction byte values are in `docs/animation-data.md`.

## 3. The port

| Original | C | Notes |
| --- | --- | --- |
| `Char` record | `pop_char` | one `uint8_t` per original byte, `seq` is `uint16_t` |
| `ADDCHARX` | `pop_add_char_x(c, a)` | returns the value, like the original |
| `JUMPSEQ` | `pop_jumpseq(t, c, n)` | |
| `GETSEQ` | `pop_getseq(t, c, &b)` | |
| `ANIMCHAR` | `pop_animchar(t, c, g, hooks)` | |
| `seqtab` | `pop_seqtab` | pointer, size and origin address; `CharSeq` stays an original address |
| `weightless`, `jarabove`, `alertguard` | `pop_anim_globals` | will move into the game-state struct later |
| `addsound`, `addslicers`, `GoneUpstairs`, `potioneffect` | `pop_anim_hooks` | function pointers, called at the same point in the scan; NULL = skip |

Separation of concerns:

- `src/core` has no I/O and no SDL. The table is passed in as memory.
- `src/assets/pop_assets.c` loads `seqtable.bin`, which
  `tools/extract_assets/export_port_data.py` generates from the user's checkout
  (assembled `SEQTABLE.S` behind a 16-byte header). CMake runs the export when
  `POP_SOURCE_DIR` is set. No table data is stored in this repository.
- Sequence addresses are kept as original addresses (`$3000`-based) rather than
  converted to indices, so `CharSeq` values can be compared directly with an
  emulator's memory.

### Intentional deviations

| Situation | Original | Port |
| --- | --- | --- |
| `CharSeq` leaves the table | reads whatever is in memory there | returns `POP_SEQ_ERR_BOUNDS` |
| No frame byte is ever reached | never returns | returns `POP_SEQ_ERR_NO_FRAME` after 256 instructions |
| `JUMPSEQ` index beyond the table | reads the bytes that follow | returns `POP_SEQ_ERR_BOUNDS`, `CharSeq` unchanged |
| Subsystem calls | direct `jsr` | optional hooks |
| Register side effects (X, Y, flags) | callers may depend on them | not modelled; **not yet verified** that no caller uses them |

Neither error occurs for any of the 114 entry points of the shipped table
(tested for 300 ticks each, weightless and not).

## 4. Semantic pitfalls

1. **A frame byte ends the tick; what follows it runs next tick.** In
   `db 7,chx,5` the 5 units are applied on the call that shows the *next*
   frame.
2. **`chx` is direction-relative and wraps.** `ADDCHARX` negates for a
   left-facing character and adds in 8 bits with no clamping: x = 2 facing left
   with `chx 5` gives 253. The off-screen logic elsewhere depends on this.
3. **Only bit 7 of `CharFace` is tested** (`bit` / `bpl`). `aboutface` flips
   all bits, so the value stays `$FF` or `$00`, but any value with bit 7 set
   behaves as "left".
4. **Negating −128 gives −128.** `ADDCHARX` with A = `$80` adds `$80` in both
   directions. The table never uses it.
5. **`JUMPSEQ` computes its index in 8 bits.** Sequence 0 reads entry bytes
   `$FE/$FF`; sequence 129 aliases sequence 1. Only 1–114 are meaningful.
6. **`JUMPSEQ` does not change the frame.** The new sequence shows nothing
   until `ANIMCHAR` runs; code that needs the new frame immediately calls both.
7. **`CharBlockY` wraps.** `up` from row 0 gives `$FF`, i.e. row −1.
8. **`setfall` stores raw bytes**; they are signed when gravity uses them
   (not ported yet).
9. **`ANIMCHAR` only sets `jarabove` and `alertguard`**; it never clears them.
   They are consumed and reset elsewhere (`jarabove` in `shakeloose`,
   `TOPCTRL.S:1271-1286`; `alertguard` is written at five other sites), not
   ported yet.
10. **`tap` selectors above 2 are ignored**, including for `alertguard`.
    `effect` numbers other than 1 are ignored. `die` does nothing.
11. **`ifwtless` consumes its two operand bytes either way.**
12. **The order of side effects within one call matters** when several
    instructions precede a frame (for example `chx`, then `tap`, then the
    frame): hooks fire in table order, before `CharPosn` is updated.

## 5. Tests

| Test | Data | What it checks |
| --- | --- | --- |
| `seq_unit` (`tests/test_seq.c`) | synthetic tables | every instruction, tick semantics, both facings, hook order, 8-bit wraparound of x, y, block row and the `JUMPSEQ` index, bounds and no-frame errors, every byte `$00-$F0` is a frame |
| `assets_unit` | synthetic files | loader accepts a valid file, rejects six kinds of bad one |
| `seq_original` | original table | `startrun` frames and `CharX` for 23 ticks as derived in `architecture.md`; footsteps on frames 9 and 13; mirror symmetry of left/right runs; `running`, `runstop`, `stand`; all 114 entries run 300 ticks |
| `seq_c_vs_python` | original table | every field of every tick, 114 entries × 200 ticks × 2 facings = 45,600 ticks, identical between the C port and the independent Python decoder |

The last test compares two implementations written from the same reading of
the source. Since milestone 8 that gap is closed: `tests/test_oracle.py` runs
the **original** `ANIMCHAR` in the 6502 simulator for all 114 sequences, 80
ticks, both facings, and it matches the same model field for field, including
the sounds it queues (18,240 ticks). What remains is a comparison with the
real game in an emulator, which needs a disk image.

A deliberately broken build (sign of `chy` flipped) was confirmed to make
`seq_c_vs_python` fail, at entry 3, tick 10.
