# Graphics formats

Status: the image-table container is decoded and tested. Colour, masks and
mirroring are not interpreted yet.

## Image table (`IMG.*`)

Used for all sprites and background pieces. Citations are to
`01 POP Source/Source` at original commit `25243550`.

```
offset 0        N        image count (1 byte)
offset 1        ptr[1]   16-bit little-endian address of image 1
offset 3        ptr[2]
...
offset 2N-1     ptr[N]
offset 2N+1     ptr[N+1] address just past image N
then, for each image, at file offset (ptr[i] − base):
                WIDTH    in bytes (7 dots each)
                HEIGHT   in lines
                WIDTH × HEIGHT bitmap bytes
```

| Property | Value | Evidence |
| --- | --- | --- |
| Image numbering | 1-based; pointer for image *n* at offsets 2n−1, 2n | **confirmed**, `HIRES.S:264-277` (`setimage`) |
| Header | width (bytes), height (lines) | **confirmed**, `HIRES.S:331-339` (`PREPREP`) |
| Row order | bottom row first | **confirmed**, `HIRES.S:718, 800-812`: starts at `YCO`, decrements per row |
| Byte order in a row | left to right | **confirmed**, `HIRES.S:769-786` |
| Bit 7 of every bitmap byte | always set | **confirmed** in data (0 exceptions in 16 files); expected by `SHIFTn-$80` table addressing, `HRTABLES.S:46-56, 255-261` |
| Byte 0 = image count | | **inferred** from data; `setimage` never reads it |
| N+1 pointers, last = end | | **inferred** from data; sizes agree for every image in all 16 files |
| Pointer base | `$6000` in every file, including tables the game loads elsewhere | **confirmed** in data. DRAZ has a `relocate` routine (`04 Support/MakeDisk/DRAZ/DRAZ.S:3081-3101`); when it was applied is **not yet verified** |
| Dot order within a byte | bit 0 leftmost, 7 dots per byte | Apple II hardware behaviour, not stated in the repository; the decoded pictures are coherent only with this order |
| Compression | none | **confirmed**: sizes equal 2 + width × height |
| Trailing bytes after the last image | 0–3718 per file | **confirmed** in data; presumed editor slack, content not examined |

In the 16 game tables, background images are at most 16 bytes (112 dots) wide
and 64 lines high; character images at most 8 bytes (56 dots) wide and 58
lines high.

### What the decoder does not do yet

- **Colour.** Apple II hi-res colour comes from dot position parity and bit 7,
  not from stored colour values. PNGs are literal: set dot = white. The
  vertical stripes visible inside the figures are that dot pattern.
- **Transparency.** Images carry no separate mask, and `extract_images.py`
  produces no alpha channel. The game derives the mask from the image while
  drawing (each dot clears itself and its horizontal neighbours); this is now
  modelled in `pop_hires.py` and described in `docs/run-cycle-analysis.md`.
- **Mirroring** for right-facing characters is likewise modelled there, not in
  the per-image PNGs.
- **Aspect ratio.** 280 × 192 dots on a 4:3 screen are not square; PNGs are
  1 dot = 1 pixel.
- **Anchors.** The foot/base position of each frame lives in `FRAMEDEF.S`
  (`Fcheck` bits 0–4, `Fdx`, `Fdy`), not in the image table. The frame data is
  extracted (`docs/animation-data.md`) and composed with the images by
  `render_run_cycle.py` (`docs/run-cycle-analysis.md`).

## Other formats

| Data | Status |
| --- | --- |
| Level blueprint (`LEVEL*`, 2304 bytes) | Parsed; see `docs/level-format.md` |
| Background piece tables (`BGDATA.S`) and room assembly | Modelled; see `docs/room-rendering.md` |
| Frame definitions | Decoded; see `docs/animation-data.md` |
| Sequence table | Decoded; see `docs/animation-data.md` |
| `PAC.*` packed pictures | **Not yet verified.** Candidate decoders: `SNGEXPAND` / `DBLEXPAND` in `UNPACK.S`, pack/unpack in `DRAZ.S` |
| 8192-byte files | **Inferred** raw hi-res pages from size only |

## Visual verification procedure

1. Extract at 4× so individual dots are visible:

   ```bash
   python3 tools/extract_assets/extract_images.py --source ../pop-original --out build/extracted --scale 4
   ```

2. Open `build/extracted/IMG.CHTAB1/sheet.png`. Expected: 65 upright figures,
   heads at the top, facing left. Images 1–14 are the start-run and run-cycle
   poses in order; the last image is a starburst.
   - Figures upside-down → row order wrong.
   - Figures sheared into 7-dot-wide scrambled columns → bit order wrong.
   - Image boundaries drifting → pointer/size interpretation wrong.
3. Check `table.json` next to it: every `hibit_clear_bytes` should be 0.
4. Run the tests with the original present:

   ```bash
   POP_SOURCE_DIR=../pop-original python3 -m pytest tests -q
   ```

5. **Not yet done:** comparison against an Apple II emulator screenshot. That
   needs a disk image, which the repository does not include. Until then the
   decode is verified for internal consistency and plausibility, not against
   the running game.

Result recorded on 2026-11-17 against commit `25243550`: all 16 tables parse,
14 tests pass, sheet for `IMG.CHTAB1` inspected by eye and matches step 2.
