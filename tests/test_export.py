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
