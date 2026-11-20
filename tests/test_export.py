"""Integration test for export_port_data.py (needs POP_SOURCE_DIR)."""

import os
import struct
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "extract_assets"))

SOURCE = os.environ.get("POP_SOURCE_DIR")
needs_source = pytest.mark.skipif(
    not (SOURCE and os.path.isdir(os.path.join(SOURCE, "01 POP Source", "Source"))),
    reason="set POP_SOURCE_DIR to a checkout of the original repository")


@needs_source
def test_seqtable_bin_header_and_names(tmp_path):
    import export_port_data
    assert export_port_data.main(["--source", SOURCE, "--out", str(tmp_path)]) == 0
    blob = (tmp_path / "seqtable.bin").read_bytes()
    assert blob[:8] == b"POPSEQT1"
    origin, size, entries, reserved = struct.unpack_from("<HHHH", blob, 8)
    assert (origin, entries, reserved) == (0x3000, 114, 0)
    assert len(blob) == 16 + size and origin + size <= 0x3A00
    # entry 1 points just past the 114-entry pointer table and its first sequence
    first = struct.unpack_from("<H", blob, 16)[0]
    assert origin + 2 * entries <= first < origin + size

    header = (tmp_path / "pop_seqnames.h").read_text()
    assert "#define POP_SEQ_ENTRY_COUNT 114" in header
    defs = dict(line.split()[1:3] for line in header.splitlines()
                if line.startswith("#define POP_SEQNO_"))
    assert defs["POP_SEQNO_STARTRUN"] == "1"
    assert defs["POP_SEQNO_STAND"] == "2"
    assert defs["POP_SEQNO_RUNNING"] == "84"
    assert defs["POP_SEQNO_MRAISE"] == "114"
    assert len(set(defs.values())) == len(defs)        # one name per number

    sfx = (tmp_path / "sfx.bin").read_bytes()
    assert sfx[:8] == b"POPSFX01"
    cpu_hz, count, reserved = struct.unpack_from("<IHH", sfx, 8)
    assert (cpu_hz, count, reserved) == (1020484, 20, 0)
    pos, flips = 16, []
    for _ in range(count):
        total, n = struct.unpack_from("<II", sfx, pos)
        cycles = struct.unpack_from("<%dI" % n, sfx, pos + 8)
        assert list(cycles) == sorted(cycles) and (not cycles or cycles[-1] <= total)
        flips.append(n)
        pos += 8 + 4 * n
    assert pos == len(sfx)
    assert flips[9] == 3 and flips[8] == 80            # Footstep, GotKey
    names = (tmp_path / "pop_sfxnames.h").read_text()
    assert "#define POP_SFX_FOOTSTEP          9" in names


@needs_source
def test_imgdims_bin(tmp_path):
    """Image sizes for DIMCHAR: seven character tables plus four more
    opponent sets, no image data."""
    import export_port_data
    assert export_port_data.main(["--source", SOURCE, "--out", str(tmp_path)]) == 0
    blob = (tmp_path / "imgdims.bin").read_bytes()
    assert blob[:8] == b"POPIDIM1"
    tables, reserved = struct.unpack_from("<HH", blob, 8)
    assert (tables, reserved) == (11, 0)
    pos, dims = 12, []
    for _ in range(tables):
        n = struct.unpack_from("<H", blob, pos)[0]
        dims.append([(blob[pos + 2 + 2 * i], blob[pos + 3 + 2 * i]) for i in range(n)])
        pos += 2 + 2 * n
    assert pos == len(blob)
    # every size is a real image: 1-40 bytes wide, 1-192 lines high
    assert all(1 <= w <= 40 and 1 <= h <= 192 for t in dims for w, h in t)
    # the opponent sets (table 3 and the four after the seven) have the same
    # number of images but not the same sizes
    opponents = [dims[3]] + dims[7:]
    assert [len(t) for t in opponents] == [32] * 5
    assert len({tuple(t) for t in opponents}) == 5
    # the widest character image fits the 16-entry Mult7 table GETEDGES uses
    assert max(w for t in dims for w, _ in t) < 16
