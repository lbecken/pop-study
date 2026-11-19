"""Sound effects by running the original sound module in a 6502 simulator.

New code for the pop-study project; contains no original code or data.

SOUND.S is assembled from the user's checkout and executed instruction by
instruction; every access to the speaker soft switch $C030 is recorded with
its CPU cycle count.  Nothing about the effects is typed in here: pitches,
durations and the dispatch all come from the assembled original.

Evidence (original repository @ 25243550, "01 POP Source/Source"):

    PLAYBACK, makesound   SOUND.S 100-140   play the queue, last entry first
    effect routines       SOUND.S 149-320   load pitch (Y lo, X hi), duration (A)
    tone                  SOUND.S 331-354   one speaker access per outer loop
    ADDSOUND, ZEROSOUND   SOUND.S 64-91     the queue (also SPECIALK.S 992-1004)
    names                 SOUNDNAMES.S 6-25

Hardware facts used, which are not in the repository:

    * Any access to $C030 flips the speaker output (a 1-bit signal).
    * The Apple II CPU clock averages 1,020,484 Hz on an NTSC machine
      (14.31818 MHz / 14, with one stretched cycle in every 65).  The
      simulator counts whole cycles and ignores the stretch, so a time is
      cycles / 1,020,484.
"""

from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass
from typing import Callable, Dict, List

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "extract_assets"))

from merlin_data import MerlinError, parse_equates  # noqa: E402
from mos6502 import Cpu6502, assemble_6502  # noqa: E402

CPU_HZ = 1_020_484
SPEAKER = 0xC030
NOMINAL_TICKS_PER_MINUTE = 725          # SPECIALK.S 68
TICK_CYCLES = CPU_HZ * 60 / NOMINAL_TICKS_PER_MINUTE


def include_resolver(*directories: str) -> Callable[[str], str]:
    def resolve(name: str) -> str:
        for d in directories:
            path = os.path.join(d, name.upper() + ".S")
            if os.path.exists(path):
                with open(path, encoding="ascii") as f:
                    return f.read()
        raise MerlinError("put %s: file not found" % name)
    return resolve


def tone_half_period(pitch: int) -> int:
    """Cycles between speaker accesses in `tone`, by counting its loop.

        :outloop bit spkr        4
                 ldx #0          2
        :midloop ldy #0          2
        :inloop  iny             2
                 cpy :pitch      4
                 bcc :inloop     3 taken, 2 not
                 inx             2
                 cpx :pitch+1    4
                 bcc :midloop    3 taken, 2 not
                 sec             2
                 sbc #1          2
                 bne :outloop    3 taken

    The inner loop runs max(lo, 1) times and the middle loop max(hi, 1)
    times, so the 16-bit "pitch" is not a linear delay.  Valid while no
    branch of the loop crosses a page (true where SOUND.S places it).
    """
    lo, hi = max(pitch & 0xFF, 1), max((pitch >> 8) & 0xFF, 1)
    return hi * (9 * lo + 10) + 12


@dataclass
class Effect:
    number: int
    name: str
    routine: str             # label the dispatch table points at
    toggles: List[int]       # cycle of each speaker access, from routine entry
    total_cycles: int        # PLAYBACK entry to its RTS

    @property
    def intervals(self) -> List[int]:
        return [b - a for a, b in zip(self.toggles, self.toggles[1:])]

    def segments(self) -> List[Dict[str, float]]:
        """Runs of equal toggle spacing: [{half_period, toggles, hz}]."""
        out: List[Dict[str, float]] = []
        for d in self.intervals:
            if out and out[-1]["half_period"] == d:
                out[-1]["toggles"] += 1
            else:
                out.append({"half_period": d, "toggles": 1})
        for s in out:
            s["hz"] = CPU_HZ / (2 * s["half_period"])
        return out

    def to_json(self) -> dict:
        return {
            "number": self.number, "name": self.name, "routine": self.routine,
            "speaker_accesses": len(self.toggles),
            "toggle_cycles": self.toggles,
            "segments": [{"half_period_cycles": s["half_period"], "intervals": s["toggles"],
                          "frequency_hz": round(s["hz"], 2)} for s in self.segments()],
            "total_cycles": self.total_cycles,
            "total_ms": round(1000 * self.total_cycles / CPU_HZ, 3),
            "share_of_nominal_tick": round(self.total_cycles / TICK_CYCLES, 4),
        }


class SoundModule:
    """The assembled SOUND.S, ready to be called."""

    def __init__(self, source_dir: str, extra_include_dirs=()):
        resolve = include_resolver(source_dir, *extra_include_dirs)
        with open(os.path.join(source_dir, "SOUND.S"), encoding="ascii") as f:
            self.asm = assemble_6502(f.read(), resolve)
        self.sym = self.asm.symbols
        names = parse_equates(resolve("soundnames"))
        self.names = {v: k for k, v in names.items()
                      if not k.startswith("s_") and 0 <= v < 128}
        self.count = (self.sym["endlook"] - self.sym["lookup"]) // 2
        by_addr: Dict[int, List[str]] = {}
        for line in self.asm.label_lines:
            by_addr.setdefault(line.address, []).append(line.label)
        self._labels = by_addr

    def names_to_number(self, name: str) -> int:
        for number, n in self.names.items():
            if n == name:
                return number
        raise KeyError(name)

    def routine_name(self, number: int) -> str:
        a = self.sym["lookup"] + 2 * number - self.asm.origin
        target = self.asm.image[a] | (self.asm.image[a + 1] << 8)
        want = "Do" + self.names.get(number, "")
        labels = self._labels.get(target, ["$%04X" % target])
        return want if want in labels else labels[0]

    def _cpu(self) -> Cpu6502:
        cpu = Cpu6502()
        cpu.load(self.asm.origin, self.asm.image)
        return cpu

    def play_queue(self, queue: List[int], sound_on: bool = True):
        """Run PLAYBACK with `queue` in the sound table (in the order the
        game added them).  Returns (toggle cycles, total cycles)."""
        cpu = self._cpu()
        cpu.mem[self.sym["soundon"]] = 1 if sound_on else 0
        table = self.sym["soundtable"]
        cpu.mem[table] = len(queue)
        for i, n in enumerate(queue, 1):
            cpu.mem[table + i] = n
        toggles: List[int] = []
        cpu.on_read[SPEAKER] = lambda c: toggles.append(c.cycles)
        total = cpu.call(self.asm.origin)            # jmp PLAYBACK at the module origin
        return toggles, total

    def effect(self, number: int) -> Effect:
        toggles, total = self.play_queue([number])
        return Effect(number, self.names.get(number, "sound%d" % number),
                      self.routine_name(number), toggles, total)

    def all_effects(self) -> List[Effect]:
        return [self.effect(n) for n in range(self.count)]

    def add_sound(self, queue_state: bytearray, number: int) -> None:
        """Run the original ADDSOUND on a 33-byte queue image (count + 32)."""
        cpu = self._cpu()
        table = self.sym["soundtable"]
        cpu.mem[table:table + len(queue_state)] = queue_state
        cpu.a = number
        cpu.call(self.sym["ADDSOUND"])
        queue_state[:] = cpu.mem[table:table + len(queue_state)]


# --- rendering ----------------------------------------------------------------------

def render_pcm(toggles: List[int], total_cycles: int, rate: int = 44100,
               amplitude: float = 0.5, lead_ms: float = 5.0, tail_ms: float = 5.0,
               start_level: int = -1) -> List[float]:
    """Turn speaker toggles into samples of the 1-bit speaker signal.

    Each output sample is the average of the signal over that sample's
    time span (a box filter), which removes most aliasing of the square
    edges.  This is the electrical signal sent to the speaker, not the
    sound of an Apple II speaker and case.  The starting polarity is
    arbitrary: the real state depends on everything played before.
    """
    lead = lead_ms / 1000.0
    length = lead + total_cycles / CPU_HZ + tail_ms / 1000.0
    n = int(length * rate) + 1
    edges = [lead + c / CPU_HZ for c in toggles]
    out = []
    level = start_level
    k = 0
    for i in range(n):
        t0, t1 = i / rate, (i + 1) / rate
        acc, t = 0.0, t0
        while k < len(edges) and edges[k] < t1:
            acc += level * (edges[k] - t)
            t = edges[k]
            level = -level
            k += 1
        acc += level * (t1 - t)
        out.append(amplitude * acc * rate)
    return out


def wav_bytes(samples: List[float], rate: int = 44100) -> bytes:
    """16-bit mono PCM WAV."""
    pcm = b"".join(struct.pack("<h", max(-32768, min(32767, int(round(s * 32767)))))
                   for s in samples)
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
            + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
            + b"data" + struct.pack("<I", len(pcm)) + pcm)
