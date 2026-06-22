#!/usr/bin/env python3
"""
VB-1 waveguide synth — playable GUI frontend.
=============================================

A standalone, real-time, polyphonic (8-voice) Python reimplementation of the
VB-1 waveguide bass, faithful to Source/VB1DSP.h:

  * frequency + Damper detune            (noteToN)
  * delay-line length N                   (noteToN)
  * one-pole loop coefficient g           (startNote)
  * pickup comb offset pk                 (startNote)
  * pluck split r                         (startNote)
  * shaped excitation seed                (seedExcitation)
  * per-sample bidirectional waveguide    (renderNextBlock)
  * linear release envelope               (stopNote / FUN_0001e630)

Excitation tables are loaded from Source/VB1ExcitationTables.h at runtime and
selected by PROGRAM (0..15), exactly like the binary — NOT by the Shape value.
This is the fix for the fabricated make_excitation_table() stand-in.

Run:
    pip install numpy sounddevice
    python tools/vb1_frontend.py

If sounddevice is unavailable, the GUI still works in "render-to-WAV" mode
(the Play button renders offline and plays via the OS).
"""

from __future__ import annotations

import math
import re
import sys
import threading
import wave
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Deque, Dict, List, Optional, Tuple

import numpy as np

# -----------------------------------------------------------------------------
# Constants — mirror Source/VB1DSP.h
# -----------------------------------------------------------------------------
K_NUM_VOICES = 8
K_MAX_DELAY = 9599          # N clamp (binary 0x257f)
K_EXC_LEN = 4096            # excitation table length (0x4000)
K_OUT_GAIN = 0.7795         # binary runtime-dumped output gain

# 16 factory programs — Source/VB1DSP.h presets().
# Columns: Damper, PickUp, Pick, Release, Shape, Volume.
PRESETS: List[Tuple[str, Tuple[float, float, float, float, float, float]]] = [
    ("Bassic Bass",  (0.1,        0.75,       0.33329999, 0.98, 0.0,       0.8)),
    ("Sustain Bass", (0.60227299, 0.91304302, 0.53953499, 0.98, 0.0,       0.8)),
    ("Round Bass",   (0.35227299, 0.95652199, 1.0,        0.98, 0.0,       0.8)),
    ("Fretless",     (1.0,        0.0,        1.0,        0.98, 0.0,       0.8)),
    ("Synthi Bass",  (0.113636,   0.289855,   0.0,        0.98, 0.0,       0.8)),
    ("Clavinet",     (0.625,      0.30434799, 0.0,        0.98, 1.0,       0.8)),
    ("DX Bass",      (0.63636398, 0.52173901, 0.40465099, 0.98, 0.79828799, 0.8)),
    ("Hollow Bass",  (0.715909,   0.39130399, 0.34883699, 0.98, 0.24428099, 0.8)),
    ("Sequenz Bass", (0.136364,   0.0,        1.0,        0.98, 0.228516,   0.8)),
    ("Warm Bass",    (0.80681801, 0.0,        0.40465099, 0.98, 0.160605,   0.8)),
    ("Slap Frets",   (0.238636,   0.0,        0.0,        0.98, 1.0,       0.8)),
    ("Buzz Bass",    (1.0,        0.55072498, 0.20930199, 0.98, 0.419047,   0.8)),
    ("Add Chorus",   (0.67045498, 0.0,        0.45116299, 0.98, 0.83333302, 0.8)),
    ("Synth Bass 2", (1.0,        0.30434799, 1.0,        0.98, 0.387485,   0.8)),
    ("Dark Click",   (0.88636398, 0.18840601, 0.38604599, 0.98, 0.118538,   0.8)),
    ("Synth Bass 3", (0.147727,   1.0,        0.0,        0.98, 0.67822999, 0.8)),
]

# Program -> excitation table index. From VB1ExcitationTables.h excitationTable().
PROGRAM_TABLE = [0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 1, 6, 7, 8, 9, 10]


# -----------------------------------------------------------------------------
# Excitation tables — parsed from the real VB-1 dump
# -----------------------------------------------------------------------------
def load_excitation_tables(path: Optional[Path] = None) -> Optional[Dict[int, np.ndarray]]:
    """Parse exc_table_0..10 from VB1ExcitationTables.h. Returns {idx: float32[4096]}."""
    if path is None:
        path = Path(__file__).resolve().parent.parent / "Source" / "VB1ExcitationTables.h"
    path = Path(path)
    if not path.exists():
        return None
    text = path.read_text()
    tables: Dict[int, np.ndarray] = {}
    for m in re.finditer(r"exc_table_(\d+)\[4096\]\s*=\s*\{([^}]*)\}", text, re.S):
        idx = int(m.group(1))
        nums = re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", m.group(2))
        tables[idx] = np.asarray([float(x) for x in nums[:K_EXC_LEN]], dtype=np.float32)
    return tables if len(tables) else None


def fallback_table() -> np.ndarray:
    """Flat 0.5 table == original Shape=0 default (T_0 == 0.5)."""
    return np.full(K_EXC_LEN, 0.5, dtype=np.float32)


# -----------------------------------------------------------------------------
# DSP
# -----------------------------------------------------------------------------
@dataclass
class VB1Params:
    damper: float = 0.1
    pickup: float = 0.75
    pick: float = 0.333
    release: float = 0.98
    shape: float = 0.0
    volume: float = 0.8


def midi_to_frequency(m: int, damper: float) -> float:
    """f = 880 * 2^((m-69)/12 + delta), delta = (0.5-D)/18 when D<0.5 else 0."""
    octave = (m - 69) / 12.0
    if damper < 0.5:
        octave += (0.5 - damper) * (1.0 / 12.0) * (2.0 / 3.0)   # == (0.5-D)/18
    return (2.0 * 440.0) * (2.0 ** octave)


class VB1Voice:
    """One note's waveguide. Mirrors VaStringVoice in Source/VB1DSP.h."""

    def __init__(self, sample_rate: int):
        self.sr = int(sample_rate)
        self.params = VB1Params()
        self.N = 256
        self.pk = 0
        self.r = 1
        self.g = 0.0
        self.idxA = 0
        self.idxB = 0
        self.dlA: List[float] = [0.0]
        self.dlB: List[float] = [0.0]
        self.filt = 0.0
        self.env = 1.0
        self.release_dec = 0.0
        self.releasing = False
        self.gainL = 0.0
        self.gainR = 0.0
        self.midi = -1
        self.active = False
        self.age = 0           # for voice stealing

    # -- note lifecycle -------------------------------------------------------
    def start_note(self, midi: int, velocity: float, params: VB1Params,
                   table: np.ndarray) -> None:
        p = self.params = params
        v = max(0.0, min(1.0, velocity))
        self.midi = midi

        f = midi_to_frequency(midi, p.damper)
        n = int(self.sr / f + 1.0)
        self.N = min(n, K_MAX_DELAY)

        # g = 1 - min(0.9, (float)((9600/N)*0.0125*(0.8D+0.1)))
        # The (float) cast truncates to single precision; replicate it.
        gcoeff = np.float32((9600.0 / self.N) * 0.0125 * (0.8 * p.damper + 0.1))
        self.g = 1.0 - min(0.9, float(gcoeff))

        self.pk = max(0, min(self.N - 1, int((p.pickup / 6.0 + 1.0 / 64.0) * self.N)))
        self.r = max(1, int(p.pick * 0.5 * self.N))

        self._seed(table)

        self.gainL = 0.5 * p.volume * v * K_OUT_GAIN
        self.gainR = 0.5 * p.volume * v * K_OUT_GAIN

        self.idxA = 0
        self.idxB = 0
        self.filt = 0.0
        self.env = 1.0
        self.releasing = False
        self.release_dec = 0.0
        self.age = 0
        self.active = True

    def stop_note(self) -> None:
        if not self.active:
            return
        # FUN_0001e630: g switches to Release, linear env 1->0.
        self.releasing = True
        self.g = float(self.params.release)
        rel_samples = int(self.sr * 2.5 * (1.0 - self.params.release))
        rel_samples = max(256, rel_samples)
        self.release_dec = 1.0 / float(rel_samples)
        self.env = 1.0

    def recompute_gains(self) -> None:
        # Called when Volume changes mid-note (mirrors recomputeGains()).
        v = self.gainL  # keep velocity ratio by recomputing from current level is impossible;
        # store velocity instead — see set_velocity below.
        pass

    def set_volume(self, volume: float) -> None:
        vel = getattr(self, "_vel", 1.0)
        self.gainL = 0.5 * volume * vel * K_OUT_GAIN
        self.gainR = 0.5 * volume * vel * K_OUT_GAIN

    def _seed(self, table: np.ndarray) -> None:
        """Seed both delay lines with the shaped triangular excitation."""
        N = self.N
        r = self.r
        dlA = [0.0] * N
        dlB = [0.0] * N
        rise = 1.0 / r
        fall = 1.0 / max(1, N - 1 - r)
        step = 4096.0 / N
        phi = 0.0
        for i in range(r):
            tv = float(table[int(phi) & 0xFFF])
            f = tv * rise * i
            val = (f + f) - 0.1
            dlA[i] = val
            dlB[i] = val
            phi += step
        for i in range(r, N):
            tv = float(table[int(phi) & 0xFFF])
            f = tv * fall * (N - 1 - i)
            val = (f + f) - 0.1
            dlA[i] = val
            dlB[i] = val
            phi += step
        self.dlA = dlA
        self.dlB = dlB

    # -- render ---------------------------------------------------------------
    def render_into(self, out: np.ndarray, n: int) -> None:
        """Add n samples (stereo) into out (shape (n,2) float32)."""
        if not self.active:
            return
        N = self.N
        dlA = self.dlA
        dlB = self.dlB
        pk = self.pk
        g = self.g
        om = 1.0 - g
        idxA = self.idxA
        idxB = self.idxB
        filt = self.filt
        gainL = self.gainL
        gainR = self.gainR
        releasing = self.releasing
        env = self.env
        dec = self.release_dec
        age = self.age

        for s in range(n):
            pi = idxB + pk
            if pi >= N:
                pi -= N
            pick = dlB[pi]

            filt = g * filt + om * dlB[idxB]
            dlA[idxA] = -filt

            ap = idxA + N - 1
            if ap >= N:
                ap -= N
            dlB[idxB] = -dlA[ap]

            idxA -= 1
            if idxA < 0:
                idxA += N
            idxB += 1
            if idxB >= N:
                idxB -= N

            e = env
            out[s, 0] += pick * e * gainL
            out[s, 1] += pick * e * gainR

            if releasing:
                env -= dec
                if env <= 0.0:
                    self.active = False
                    break
            age += 1

        self.idxA = idxA
        self.idxB = idxB
        self.filt = filt
        self.env = env
        self.age = age


# -----------------------------------------------------------------------------
# Engine — 8-voice polyphony, thread-safe note queue
# -----------------------------------------------------------------------------
class VB1Engine:
    def __init__(self, sample_rate: int = 44100, master_gain: float = 0.7):
        self.sr = int(sample_rate)
        self.master_gain = master_gain
        self.voices: List[VB1Voice] = [VB1Voice(sample_rate) for _ in range(K_NUM_VOICES)]
        self.params = VB1Params(*PRESETS[0][1])          # start on "Bassic Bass"
        self.program = 0
        tables = load_excitation_tables()
        self._tables = tables if tables is not None else {}
        self.table = self._table_for_program(0)
        self._steal = 0
        self._note_map: Dict[int, int] = {}              # midi -> voice index
        self._lock = threading.Lock()
        self._events: Deque[Tuple[str, int, float]] = deque()

    def _table_for_program(self, program: int) -> np.ndarray:
        idx = PROGRAM_TABLE[program] if 0 <= program < len(PROGRAM_TABLE) else 0
        return self._tables.get(idx, fallback_table())

    # ---- called from GUI thread -------------------------------------------
    def set_program(self, program: int) -> VB1Params:
        program = max(0, min(len(PRESETS) - 1, program))
        self.program = program
        self.params = VB1Params(*PRESETS[program][1])
        self.table = self._table_for_program(program)
        with self._lock:
            for v in self.voices:
                if v.active:
                    v.params = self.params
                    v.set_volume(self.params.volume)
        return self.params

    def set_param(self, name: str, value: float) -> None:
        setattr(self.params, name, float(value))
        if name == "volume":
            with self._lock:
                for v in self.voices:
                    if v.active:
                        v.set_volume(self.params.volume)

    def note_on(self, midi: int, velocity: float = 0.9) -> None:
        with self._lock:
            self._events.append(("on", midi, velocity))

    def note_off(self, midi: int) -> None:
        with self._lock:
            self._events.append(("off", midi, 0.0))

    def all_off(self) -> None:
        with self._lock:
            for v in self.voices:
                if v.active and not v.releasing:
                    v.stop_note()

    # ---- called from audio thread -----------------------------------------
    def render(self, out: np.ndarray, frames: int) -> None:
        with self._lock:
            while self._events:
                kind, midi, vel = self._events.popleft()
                if kind == "on":
                    self._alloc(midi, vel)
                else:
                    vi = self._note_map.pop(midi, None)
                    if vi is not None and self.voices[vi].active:
                        self.voices[vi].stop_note()
            for v in self.voices:
                if v.active:
                    v.render_into(out, frames)
        out *= self.master_gain
        np.clip(out, -1.0, 1.0, out=out)

    def _alloc(self, midi: int, velocity: float) -> None:
        # retrigger if already sounding
        if midi in self._note_map:
            self.voices[self._note_map[midi]].start_note(
                midi, velocity, self.params, self.table)
            self.voices[self._note_map[midi]]._vel = velocity
            return
        # find a free voice
        for i, v in enumerate(self.voices):
            if not v.active:
                self._start(i, midi, velocity)
                return
        # steal: oldest active voice (round-robin fallback)
        self._steal = (self._steal + 1) % K_NUM_VOICES
        self._start(self._steal, midi, velocity)

    def _start(self, idx: int, midi: int, velocity: float) -> None:
        old = self._note_map.get(midi)
        if old is not None:
            self._note_map.pop(old, None)
        self.voices[idx].start_note(midi, velocity, self.params, self.table)
        self.voices[idx]._vel = velocity
        self._note_map[midi] = idx


# -----------------------------------------------------------------------------
# WAV export (offline)
# -----------------------------------------------------------------------------
def write_wav(path: str, audio: np.ndarray, sample_rate: int = 44100) -> None:
    if audio.ndim == 1:
        audio = np.stack([audio, audio], axis=1)
    pcm = (np.clip(audio, -1.0, 1.0) * 32767.0).astype(np.int16)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm.tobytes())


# -----------------------------------------------------------------------------
# GUI (Tkinter) — no Tk calls from the audio thread
# -----------------------------------------------------------------------------
def build_gui(engine: VB1Engine, audio_ok: bool) -> None:
    import tkinter as tk
    from tkinter import ttk

    root = tk.Tk()
    root.title("VB-1 waveguide (Python)")
    root.resizable(False, False)

    status_var = tk.StringVar(value=("Audio: real-time (sounddevice)"
                                     if audio_ok else "Audio: offline only (install sounddevice)"))

    # ---- param sliders -----------------------------------------------------
    param_frame = ttk.LabelFrame(root, text="Parameters (affect NEXT note; Volume is live)")
    param_frame.grid(row=0, column=0, columnspan=2, sticky="ew", padx=8, pady=6)

    labels = [("Damper", "damper"), ("PickUp", "pickup"), ("Pick", "pick"),
              ("Release", "release"), ("Shape", "shape"), ("Volume", "volume")]
    sliders = {}

    def make_slider(row, col, label, attr):
        ttk.Label(param_frame, text=label).grid(row=row, column=col * 2, sticky="w", padx=(8, 2))
        var = tk.DoubleVar(value=getattr(engine.params, attr))
        sl = ttk.Scale(param_frame, from_=0.0, to=1.0, variable=var, length=160,
                       command=lambda e: on_slider(attr, var))
        sl.grid(row=row, column=col * 2 + 1, padx=2)
        val_lbl = ttk.Label(param_frame, text=f"{var.get():.3f}", width=5)
        val_lbl.grid(row=row, column=col * 2 + 2, padx=(0, 8))
        sliders[attr] = (var, val_lbl)

    def on_slider(attr, var):
        v = var.get()
        engine.set_param(attr, v)
        _, lbl = sliders[attr]
        lbl.config(text=f"{v:.3f}")

    for i, (label, attr) in enumerate(labels):
        make_slider(i // 2, i % 2, label, attr)

    # ---- program selector --------------------------------------------------
    prog_frame = ttk.LabelFrame(root, text="Program (selects the real excitation table)")
    prog_frame.grid(row=1, column=0, columnspan=2, sticky="ew", padx=8, pady=6)
    prog_var = tk.StringVar(value=PRESETS[0][0])

    def on_program(_ev):
        idx = next((i for i, (n, _) in enumerate(PRESETS) if n == prog_var.get()), 0)
        params = engine.set_program(idx)
        for attr, (var, lbl) in sliders.items():
            var.set(getattr(params, attr))
            lbl.config(text=f"{getattr(params, attr):.3f}")

    cb = ttk.Combobox(prog_frame, textvariable=prog_var, state="readonly",
                      values=[n for n, _ in PRESETS], width=22)
    cb.grid(row=0, column=0, padx=8, pady=6)
    cb.bind("<<ComboboxSelected>>", on_program)

    # ---- piano keyboard ----------------------------------------------------
    OCTAVE = [0, 2, 4, 5, 7, 9, 11]                      # white-key semitones
    BLACKS = {1: 0, 3: 1, 6: 2, 8: 3, 10: 4}            # semitone -> black index
    base_note = tk.IntVar(value=48)                       # C3
    held: set = set()

    kb = ttk.LabelFrame(root, text="Keyboard (type A S D F G H J K / W E T Y U = sharps; Z X = octave)")
    kb.grid(row=2, column=0, columnspan=2, padx=8, pady=6)
    canvas = tk.Canvas(kb, width=420, height=110, highlightthickness=0)
    canvas.grid(row=0, column=0, columnspan=7, padx=4, pady=4)

    WW, WH, BW, BH = 60, 100, 36, 62
    white_rects: Dict[int, int] = {}
    black_rects: Dict[int, int] = {}

    def draw_keys():
        canvas.delete("all")
        white_rects.clear()
        black_rects.clear()
        for i, semis in enumerate(OCTAVE):
            x = i * WW
            rid = canvas.create_rectangle(x, 0, x + WW, WH, fill="white", outline="black")
            white_rects[rid] = semis
            canvas.create_text(x + WW / 2, WH - 12,
                               text=f"{base_note.get() + semis}")
        for semis, bi in BLACKS.items():
            # position relative to previous white key
            white_idx = sum(1 for s in OCTAVE if s < semis) - 1
            x = (white_idx + 1) * WW - BW / 2
            rid = canvas.create_rectangle(x, 0, x + BW, BH, fill="black", outline="black")
            black_rects[rid] = semis

    def note_for(semis: int) -> int:
        return base_note.get() + semis

    def press(semis: int):
        n = note_for(semis)
        if n not in held:
            held.add(n)
            engine.note_on(n, 0.9)

    def release(semis: int):
        n = note_for(semis)
        if n in held:
            held.discard(n)
            engine.note_off(n)

    def on_click(_ev):
        rid = canvas.find_closest(_ev.x, _ev.y)[0]
        if rid in black_rects:
            press(black_rects[rid])
        elif rid in white_rects:
            press(white_rects[rid])

    def on_unclick(_ev):
        rid = canvas.find_closest(_ev.x, _ev.y)[0]
        if rid in black_rects:
            release(black_rects[rid])
        elif rid in white_rects:
            release(white_rects[rid])

    canvas.bind("<ButtonPress-1>", on_click)
    canvas.bind("<ButtonRelease-1>", on_unclick)

    # computer keyboard mapping
    KEYMAP = {"a": 0, "w": 1, "s": 2, "e": 3, "d": 4, "f": 5, "t": 6,
              "g": 7, "y": 8, "h": 9, "u": 10, "j": 11, "k": 12}

    def on_keydown(_ev):
        k = _ev.keysym.lower()
        if k == "z":
            base_note.set(max(0, base_note.get() - 12)); draw_keys(); return
        if k == "x":
            base_note.set(min(108, base_note.get() + 12)); draw_keys(); return
        if k in KEYMAP and not _ev.state & 0x4:
            press(KEYMAP[k])

    def on_keyup(_ev):
        k = _ev.keysym.lower()
        if k in KEYMAP:
            release(KEYMAP[k])

    root.bind("<KeyPress>", on_keydown)
    root.bind("<KeyRelease>", on_keyup)

    # ---- transport ---------------------------------------------------------
    transport = ttk.Frame(root)
    transport.grid(row=3, column=0, columnspan=2, padx=8, pady=6)
    ttk.Label(transport, textvariable=status_var).grid(row=0, column=0, columnspan=3)
    ttk.Button(transport, text="All notes off",
               command=lambda: (engine.all_off(), held.clear())).grid(row=1, column=0, pady=4)

    def render_demo():
        # offline: 3s bass line, then release tails
        import tempfile, os
        tmp = VB1Engine(engine.sr)
        tmp.params = VB1Params(*PRESETS[engine.program][1])
        tmp.table = engine.table
        seq = [36, 43, 41, 38]      # C2, G2, F2, D2
        sr = engine.sr
        chunks = []
        for n in seq:
            tmp.note_on(n, 0.9)
            tmp.note_off(n)
            # drive the event queue through render
            block = np.zeros((int(0.7 * sr), 2), dtype=np.float32)
            tmp.render(block, len(block))
            chunks.append(block.copy())
        audio = np.concatenate(chunks)
        path = os.path.join(tempfile.gettempdir(), "vb1_demo.wav")
        write_wav(path, audio, sr)
        status_var.set(f"Rendered {len(audio)/sr:.1f}s -> {path}")
        try:
            if sys.platform.startswith("win"):
                import winsound
                winsound.PlaySound(path, winsound.SND_FILENAME)
            elif sys.platform == "darwin":
                import subprocess
                subprocess.Popen(["afplay", path])
        except Exception as ex:
            status_var.set(f"Wrote {path} (playback failed: {ex})")

    ttk.Button(transport, text="Render demo WAV", command=render_demo).grid(row=1, column=1, pady=4)

    draw_keys()
    root.mainloop()


# -----------------------------------------------------------------------------
# Entry point
# -----------------------------------------------------------------------------
def main() -> int:
    sr = 44100
    engine = VB1Engine(sample_rate=sr)

    audio_ok = False
    stream = None
    try:
        import sounddevice as sd

        def audio_cb(outdata, frames, _time, _status):
            outdata.fill(0.0)
            engine.render(outdata, frames)

        stream = sd.OutputStream(samplerate=sr, channels=2, dtype="float32",
                                 blocksize=256, callback=audio_cb)
        stream.start()
        audio_ok = True
    except Exception as ex:  # pragma: no cover - environment dependent
        print(f"[warn] real-time audio unavailable ({ex}); falling back to offline render.",
              file=sys.stderr)

    try:
        build_gui(engine, audio_ok)
    finally:
        if stream is not None:
            stream.stop()
            stream.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
