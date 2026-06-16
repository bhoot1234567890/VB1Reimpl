<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B" alt="C++17">
  <img src="https://img.shields.io/badge/JUCE-7.0.5-orange?logo=juce" alt="JUCE 7.0.5">
  <img src="https://img.shields.io/badge/macOS-arm64%20%7C%20x86__64-silver?logo=apple" alt="macOS">
  <img src="https://img.shields.io/badge/license-Personal%20Use%20Only-lightgrey" alt="License">
</p>

# VB-1 Reimplementation

> Physical-modeling bass synthesizer — waveguide DSP, reverse-engineered from the original VB-1 VST2 binary.

JUCE-based VST3/AU plugin. Karplus-Strong waveguide synthesis with 8-voice polyphony, 16 factory presets, and 6 performance parameters. Built for macOS (arm64 + x86_64 universal).

## Table of Contents

- [Quick Start](#quick-start)
- [What is this?](#what-is-this)
- [Parameters](#parameters)
- [DSP Architecture](#dsp-architecture)
- [Project Structure](#project-structure)
- [Tools](#tools)
- [Fidelity Status](#fidelity-status)
- [License](#license)

## Quick Start

```bash
# 1. Clone JUCE dependency
git clone --depth 1 --branch 7.0.5 https://github.com/juce-framework/JUCE

# 2. Configure (arm64 by default)
cmake -B build -DJUCE_DIR=$PWD/JUCE -DCMAKE_BUILD_TYPE=Release

# 3. Build & auto-install to ~/Library/Audio/Plug-Ins/
cmake --build build --config Release -j

# Restart your DAW — "VB-1 reimpl" appears under Instruments.
```

**Universal binary (arm64 + x86_64):** add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to the cmake configure step.

## What is this?

This is a faithful reimplementation of Steinberg's VB-1 (Virtual Bassist) — a physical-modeling bass synth from the VST plugin era. Every signal path and algorithm was reverse-engineered from the original x86_64 VST2 binary and ported to modern JUCE, producing native VST3 + AU plugins for Apple Silicon.

**Why?** The original is 32-bit VST2 only — it doesn't run natively on modern macOS or ARM. This reimplementation preserves the exact waveguide character on current systems.

## Parameters

| # | Parameter | Range | What it does |
|---|---|---|---|
| 0 | **Damper** | 0.0 – 1.0 | Tone control — low-damping for bright attack, high-damping for muted pluck |
| 1 | **PickUp** | 0.0 – 1.0 | Pickup position — comb-filter character by offsetting the read tap |
| 2 | **Pick** | 0.0 – 1.0 | Pluck position — splits the string length; sharp at bridge, soft at center |
| 3 | **Release** | 0.0 – 1.0 | Loop lowpass coefficient — shorter release = faster decay |
| 4 | **Shape** | 0.0 – 1.0 | Excitation character — blends raw white noise ↔ smooth integrated noise |
| 5 | **Volume** | 0.0 – 1.0 | Output gain |

## DSP Architecture

```
   ┌──────────────┐     ┌─────────────────────────────────────┐
   │  Excitation   │     │         Waveguide Loop             │
   │  (noise tbl)  │────▶│  dlA[N] ◄──────── inverted ──────▶ dlB[N]  │
   │  16k samples  │     │    │                                │
   │  raw + smooth │     │    ▼                                │
   └──────────────┘     │  pickup ──▶ comb ──▶ lowpass(g) ──▶ out   │
                        │              (PickUp)   (Release)    (pan)  │
                        └─────────────────────────────────────┘
```

- **Synthesis:** Karplus-Strong waveguide, 8-voice polyphonic with voice stealing
- **Excitation:** 16,384-sample noise tables (raw white noise + one-pole smoothed), seeded per note-on with a triangular window over the fractional string position
- **Signal path:** Bidirectional delay-line pair with inverting termination, pickup-offset comb filtering, and per-sample loop lowpass for release damping
- **Panning:** Constant-power stereo, centered at 0.5

## Project Structure

```
.
├── Source/
│   ├── VB1DSP.h              # Waveguide DSP, excitation tables, 16 presets
│   ├── PluginProcessor.h/cpp # AudioProcessor + APVTS + 8-voice Synthesiser
│   ├── PluginEditor.h/cpp    # 6-knob GUI + program selector
│   └── JuceHeader.h
├── tools/
│   ├── generate_test_midi.py # Produce MIDI exercising all 16 programs
│   ├── ab_diff.py            # A/B spectral diff: original vs reimpl
│   ├── render_reimpl.cpp     # Headless WAV renderer (Phase 4)
│   ├── vst2_render.c         # VST2 host for original rendering
│   └── package.sh            # Codesign + install built plugin
├── CMakeLists.txt            # Build: VST3 + AU plugin + headless renderer
└── BUILD.md                  # Detailed build & handoff notes
```

## Tools

### A/B Diff Testing

Compare the original VB-1 against this reimplementation with identical MIDI input:

```bash
# Generate test MIDI (all 16 programs, bass range E1–E3)
python tools/generate_test_midi.py

# Render both plugins, then diff
python tools/ab_diff.py original.wav reimpl.wav --plot diff.png
```

Requires: `numpy soundfile scipy matplotlib`

The diff tool cross-correlation-aligns the renders, then reports RMS error, peak error, and per-band spectral delta. Target: **RMS < −60 dB** (inaudible).

### Headless Renderer

```bash
./build/VB1Render   # renders reimpl DSP to WAV for offline comparison
```

### Package & Install

```bash
# Ad-hoc codesign (local use)
tools/package.sh

# Distribution signing
DEV_ID="Developer ID: Name (TEAMID)" tools/package.sh
```

## Fidelity Status

**5 of 6 parameters** are structurally decoded and ported. Items marked `[TUNE]` in source need coefficient fitting via Phase 4:

- ✅ Damper — detunes N, decoded
- ✅ PickUp — pickup offset, decoded
- ✅ Pick — pluck position splitting, decoded
- ⚠️ **Shape** — the only unsolved parameter. Absent from both the linear render loop and note-on seeder. Likely an output waveshaper or excitation table selector. Test: Shape 0 vs Shape 1.
- ✅ Release — loop lowpass g, decoded (coefficients need fitting)
- ✅ Volume — gain, decoded (scaling needs fitting)
- ⚠️ Exact coupling coefficients, release-path decay rates, excitation amplitude scaling

The original is **VST2 x86_64 only**. A/B comparison requires a Rosetta-capable host (REAPER under Rosetta) to render the original alongside this reimplementation.

## License

Personal use only. Do not redistribute a derived VB-1 plugin or its assets.
