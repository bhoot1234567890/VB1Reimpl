<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B" alt="C++17">
  <img src="https://img.shields.io/badge/JUCE-8.0.4-orange" alt="JUCE 8.0.4">
  <img src="https://img.shields.io/badge/macOS-arm64%20%7C%20x86__64-silver?logo=apple" alt="macOS">
  <img src="https://img.shields.io/badge/error%20RMS%20vs%20original--109%20dB-brightgreen" alt="Error RMS">
  <img src="https://img.shields.io/badge/license-Personal%20Use%20Only-lightgrey" alt="License">
</p>

# VB-1 Reimplementation

> A native arm64 reconstruction of Steinberg's VB-1 virtual bass synth — reverse-engineered from the 2001 VST2 binary and verified to **−109 dB** (float noise floor) against the original.

JUCE-based VST3/AU plugin. Digital waveguide synthesis with 8-voice polyphony, 16 factory presets, 6 performance parameters, and a Shape-controlled wavetable excitation system. Built for macOS (arm64 + x86_64 universal).

---

## Table of Contents

- [Quick Start](#quick-start)
- [What is this?](#what-is-this)
- [Fidelity Status](#fidelity-status)
- [Parameters](#parameters)
- [DSP Architecture](#dsp-architecture)
- [The Reverse-Engineering Journey](#the-reverse-engineering-journey)
- [Project Structure](#project-structure)
- [Tools](#tools)
- [License](#license)

---

## Quick Start

```bash
# 1. Clone JUCE (tag 8.0.4)
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE

# 2. Configure
cmake -B build -DJUCE_DIR=$PWD/JUCE -DCMAKE_BUILD_TYPE=Release

# 3. Build & auto-install to ~/Library/Audio/Plug-Ins/
cmake --build build --config Release -j

# Restart your DAW — "VB-1 reimpl" appears under Instruments.
```

**Universal binary (arm64 + x86_64):** add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

---

## What is this?

Steinberg's VB-1 is a physical-modeling bass synthesizer from the early VST era (~2001). It uses a digital waveguide to model a plucked string — the same family of synthesis as Karplus-Strong, but with bidirectional delay lines, a loop lowpass, and a movable pickup for comb-filter timbre shaping. The result is an unusually organic, expressive bass sound that modern sample-based plugins struggle to replicate.

The problem: **VB-1 is VST2-only**, compiled for PowerPC/Intel, and won't load in modern DAWs on Apple Silicon. Ableton, Logic, and other hosts have dropped VST2. Rosetta is deprecated. The plugin is effectively stranded.

This project reverse-engineers the original binary using Ghidra, rebuilds every DSP path in modern C++/JUCE, and verifies the result is **perceptually identical** — error RMS of **−109.26 dB** (below the 32-bit float noise floor), with all 16 presets matching at 0.00 dB peak difference.

---

## Fidelity Status

**Complete.** All 6 parameters are structurally decoded, Ghidra-verified, and runtime-confirmed.

| Metric | Value |
|---|---|
| **Error RMS** | **−109.26 dB** (float noise floor) |
| **Peak error** | −89.02 dB |
| **Spectral delta** | −34.34 dB |
| **Per-program peak diff** | All 16 programs: < 1.6 × 10⁻⁵ |
| **Signal RMS** | −20.24 / −20.24 dB (identical) |

### Parameter status

| # | Parameter | Status | How it works (Ghidra-verified) |
|---|---|---|---|
| 0 | **Damper** | ✅ Decoded | Drives the loop lowpass `g` and a minute pitch detune when < 0.5. `g = 1 − min(0.9, (float)((9600/N) × 0.0125 × (Damper×0.8 + 0.1f)))` |
| 1 | **PickUp** | ✅ Decoded | Pickup position: `(PickUp/6 + 1/64) × N` |
| 2 | **Pick** | ✅ Decoded | Pluck split: `int(Pick × 0.5 × N)` — divides the string into rising/falling segments |
| 3 | **Release** | ✅ Decoded | At note-off, `g` switches to this value. Linear envelope from 1.0→0 over `max(256, int(SR × 2.5 × (1−Release)))` samples |
| 4 | **Shape** | ✅ Decoded | Wavetable oscillator: 4 banks × 256 morph positions → fills a 4096-float excitation table. Shape=0 → flat 0.5 (clean); Shape=1 → white noise (rough) |
| 5 | **Volume** | ✅ Decoded | Output gain: `gainL = (1−pan) × Volume × (vel/127) × 0.7795` |

---

## Parameters

| # | Parameter | Range | Description |
|---|---|---|---|
| 0 | **Damper** | 0.0 – 1.0 | String damping and brightness. Low = bright snappy attack; high = muted, warm |
| 1 | **PickUp** | 0.0 – 1.0 | Pickup position along the string. Comb-filter character — bridge = nasally, neck = round |
| 2 | **Pick** | 0.0 – 1.0 | Pluck position. Splits the excitation envelope — sharp at bridge, soft at center |
| 3 | **Release** | 0.0 – 1.0 | Note decay. At note-off, becomes the loop lowpass `g` + controls linear envelope duration |
| 4 | **Shape** | 0.0 – 1.0 | Excitation texture. 0 = clean triangular ramp; 1 = white noise. Morphs through 4 wavetable banks |
| 5 | **Volume** | 0.0 – 1.0 | Output level |

---

## DSP Architecture

```
                    Shape Parameter
                         │
                         ▼
               ┌─────────────────────┐
               │  Wavetable Oscillator │  4 banks × 256 morph positions
               │  fills 4096-float    │  setParameter handler (FUN_0001e494)
               │  excitation table    │
               └─────────┬───────────┘
                         │
   Note On               ▼
  ┌─────────┐    ┌───────────────┐    ┌──────────────────────────────────────┐
  │ Damper  │───▶│  Note-On Init │───▶│         Waveguide Loop               │
  │ PickUp  │───▶│  (FUN_0001e068)│   │                                      │
  │ Pick    │───▶│  N, g, pickup, │   │  dlA[N] ◄────── inverted ──────────► │
  └─────────┘    │  seeder        │   │    │           coupling                │ dlB[N]
                 └───────────────┘    │    ▼                                  │
                                      │  pickup ──▶ × env ──▶ × gainL ──▶ out │
                 Note Off              │  (PickUp)    (linear)   (Volume×vel)  │
                 ┌─────────┐          └──────────────────────────────────────┘
                 │ Release │───▶ g switches to Release param
                 │         │     env = 1.0, dec = 1.0/duration (LINEAR)
                 └─────────┘     output = pickup × env × gainL
```

### Key DSP details (all Ghidra-verified)

- **Synthesis**: Bidirectional waveguide, 8-voice polyphonic with voice stealing
- **Delay line length**: `N = int(SR / (2 × freq_musical)) + 1`, clamped to 9599
- **Loop lowpass**: `filt = g × filt + (1−g) × dlB[idxB]` where g comes from Damper (sustain) or Release (release)
- **Inverting termination**: `dlA[idxA] = −(float)filt; dlB[idxB] = −dlA[idxA−1]`
- **Excitation**: Triangular window seeded from a Shape-dependent wavetable table. Formula: `e = 2 × table[phase] × env − 0.1f`, phase steps by `(float)(1.0/N × 4096)`
- **g float truncation**: The original truncates the g computation to `(float)` mid-expression — replicated exactly. `0.1f` (not `0.1`) is critical.
- **Release envelope**: Linear, not exponential. Duration = `max(256, int(SR × 2.5 × (1 − Release)))`. Voice ends deterministically when envelope ≤ 0.
- **Output gain**: `gainL = 0.5 × Volume × (velocity/127) × 0.7795`, constant across all programs
- **Panning**: Centered (0.5), constant-power stereo

---

## The Reverse-Engineering Journey

The project progressed from −17 dB error to −109 dB through systematic root-cause analysis — every fix driven by understanding the binary, not by coefficient tuning.

| Fix | Error RMS | Root Cause |
|---|---|---|
| **Pitch (freq × 2)** | −28.6 dB | Ghidra's frequency function returns 2× musical freq (waveguide period = 2N) |
| **Seeder (constant 0.5)** | −28.6 dB | The seeder reads a constant-0.5 table, not noise. The noise-table investigation was a red herring |
| **g float truncation** | −40.4 dB | Original truncates gcoeff to `(float)` and uses float `0.1f`, not double `0.1`. Difference: 3.5 × 10⁻⁹ in g, accumulating over round trips |
| **kOutGain calibration** | −40.4 dB | Runtime-dumped gainL = 0.245520 → `kOutGain = 0.7795` |
| **Shape wavetable tables** | −40.4 dB | Shape fills a 4096-float excitation table via 4-bank wavetable crossfade. 16 tables dumped from original and hardcoded |
| **Linear release envelope** | −63.5 dB | At note-off, g switches to Release param. Envelope is LINEAR (1.0→0 over 2205 samples), not exponential |
| **Render alignment** | −67.0 dB | VST2 host was discarding the first sample of each note |
| **Exact preset values** | **−109.3 dB** | All 16 presets had rounded floats (e.g., `0.636f` instead of `0.63636398f`). Read exact values via `getParameter` |

**Lesson**: The difference between "close enough" and "exact" is measured in parts per billion — `0.1 ≠ 0.1f`, `0.636 ≠ 0.63636398`. Every constant matters. Verify at runtime, not just in the decompile.

---

## Project Structure

```
.
├── Source/
│   ├── VB1DSP.h                  # Waveguide DSP, 16 presets (exact float values)
│   ├── VB1ExcitationTables.h     # 16 × 4096-float Shape wavetable tables (525 KB, auto-generated)
│   ├── PluginProcessor.h/cpp     # AudioProcessor + APVTS + 8-voice Synthesiser
│   ├── PluginEditor.h/cpp        # 6-knob GUI + program selector
│   └── JuceHeader.h
├── tools/
│   ├── vst2_render.c             # Headless VST2 host for original (Rosetta) + dumpvoices mode
│   ├── render_reimpl.cpp         # Headless reimpl renderer with state-dump support
│   ├── ab_diff.py                # A/B comparison: RMS, peak, spectral delta
│   ├── cma_fit.py                # CMA-ES fitter (abandoned — spectral metrics are gameable)
│   ├── generate_test_midi.py     # Test MIDI exercising all 16 programs
│   └── package.sh                # Codesign + install
├── docs/
│   ├── VB1_spectrograms.png      # A/B spectrogram comparison (final: −109 dB)
│   ├── VB1_ab_diff.png           # Waveform overlay + difference signal
│   ├── VB1_AB_listen.wav         # Narrated A/B listening test
│   ├── VB1_RE_Spec.md            # Original binary RE specification
│   ├── VB1_DSP_Reference.cpp     # Reference DSP code from Ghidra decompile
│   ├── VB1_FINDINGS.md           # Key findings and discoveries
│   └── VB1_FLOAT_PRECISION_PLAN.md  # Final fix documentation
├── CMakeLists.txt                # Build: VST3 + AU plugin + headless renderer
├── BUILD.md                      # Detailed build notes
└── AGENTS.md                     # Agent/automation guidelines
```

---

## Tools

### A/B Comparison

```bash
# Render original (requires Rosetta)
arch -x86_64 ./tools/vst2_render <path-to-vb1.vst-binary> /tmp/original.wav

# Render reimpl
./build/VB1Render_artefacts/Release/VB1Render /tmp/reimpl.wav

# Compare
python tools/ab_diff.py /tmp/original.wav /tmp/reimpl.wav --plot diff.png
```

The diff tool cross-correlation-aligns the renders, then reports time-domain (RMS, peak) and spectral (per-block FFT) differences.

### Voice State Dump

```bash
# Dump the original's internal voice state for RCA
arch -x86_64 ./tools/vst2_render <binary> /tmp/x.wav dumpvoices
```

The `dumpvoices` mode walks the original's live memory (`AEffect.object → +0x40f8 → +0x2b8 → voice`) and reads the delay-line contents, filter coefficients, and excitation tables for all 16 programs.

### Headless Renderer

```bash
./build/VB1Render_artefacts/Release/VB1Render output.wav        # full 36s render
./build/VB1Render_artefacts/Release/VB1Render output.wav short  # program 0 state dump
```

### Package & Install

```bash
# Ad-hoc codesign (local use)
tools/package.sh

# Distribution signing
DEV_ID="Developer ID: Name (TEAMID)" tools/package.sh
```

---

## License

Personal use only. The original VB-1 is a copyrighted Steinberg product. Do not redistribute a derived plugin or its assets. The excitation tables in `VB1ExcitationTables.h` are extracted from the original binary and are included for personal compatibility testing only.
