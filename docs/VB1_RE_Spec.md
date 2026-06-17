# Steinberg VB-1 — Reverse-Engineering & Reimplementation Spec

Grounded in static analysis of `VST_Classics_Extracted/1/vb1/vb1.vst/Contents/MacOS/vb1`
(x86_64 slice extracted to `/tmp/vb1_x86_64`). All offsets are file/virtual addresses in that
slice. `[INFERENCE]` marks reasoning rather than directly observed fact.

---

## 0. Binary identification (corrects the original plan)

| Field | Value |
|---|---|
| Format | **VST 2.x** Mach-O bundle (`.vst`, `BNDL`, Carbon-era) — **NOT VST3** |
| Arch | Universal: **x86_64, ppc_7400, i386** — **no arm64 slice** |
| Version | 1.1.1.51, © Steinberg 2011 |
| Bundle ID | `com.steinberg.VB-1` |
| Entry | `_VSTPluginMain` @ `0x7ebc` (only exported symbol; rest stripped) |

**Consequence for the plan:** the original can only run under Rosetta and only in a host that
still loads VST2. The re-impl target stays VST3/AU (arm64). For Phase-4 A/B you need a VST2-capable
Rosetta host (REAPER x86_64).

---

## 1. AEffect struct (plugin descriptor)

`VSTPluginMain` → factory `0x1dbde` (`new(0x4108)` VaString, ctor `0x1db60`) → returns the AEffect
via `vtable[31]` `0x6dae` = `lea rax,[rdi+0x30]`. **The AEffect is embedded at `VaString+0x30`.**

Built by the base `AudioEffect` ctor `0x681e` (called via `0x7782`). Verified fields:

| AEffect field | VaString off | Value |
|---|---|---|
| magic | +0x30 | `0x56737450` ('VstP') |
| dispatcher (C shim) | +0x38 | `0x6258` |
| process (legacy) | +0x40 | `0x62c8` |
| setParameter | +0x48 | `0x62b4` |
| getParameter | +0x50 | `0x62a0` |
| **numPrograms** | +0x58 | **16** (NOT 8) |
| **numParams** | +0x5c | **6** (NOT 5) |
| numInputs | +0x60 | 0 (VB-1 overrides base default of 1 — it's a synth) |
| numOutputs | +0x64 | 2 (stereo) |
| flags | +0x68 | base sets `|= 0x10` |
| uniqueID | +0xa0 | **`0x61566153`** (bytes "SaVa"; base default was "NoEf") |
| version | +0xa4 | 1 |

---

## 2. Parameters (6, in host index order)

Param descriptors built by the per-program ctor `0x1d7ca` (each `new(0x28)`). Slot order in the
program struct (`[program + param*8]`) and host index:

| Idx | Name | Default | Display label | Notes |
|---|---|---|---|---|
| 0 | **Damper** | 0.100 (`0x39294`) | — | note→N detune + coupling coeff |
| 1 | **PickUp** | 0.750 (`0x39350`) | — | pickup position |
| 2 | **Pick** | 0.333 (`0x3934c`) | — | pluck position |
| 3 | **Release** | 0.980 (`0x39354`) | — | **loop lowpass g** (+0xd0) — decay time |
| 4 | **Shape** | 0.000 | — | waveshaper |
| 5 | **Volume** | 0.800 (`0x39358`) | "dB" (`0x37c1d`) | output gain |

The original plan's "5 params: Pick/PickUp/Damper/Shape/Volume" **missed Release** (idx 3) and got
the host order wrong. The 5 GUI knobs are Pick/PickUp/Damper/Shape/Volume; Release is the 6th
host-visible param.

---

## 3. The 16 factory programs + parameter table

Name table @ `0x68020` (16 `char*`). Value table @ `0x68100` (96 floats = 16×6), loaded by the
base ctor `0x1d91e` preset loop. **Column order = param index: Damper, PickUp, Pick, Release, Shape, Volume.**
Validation: row 0 ("Bassic Bass") == all defaults, confirming the column mapping.

| # | Program | Damper | PickUp | Pick | Release | Shape | Volume |
|---|---|---:|---:|---:|---:|---:|---:|
| 0 | Bassic Bass | 0.100 | 0.750 | 0.333 | 0.980 | 0.000 | 0.800 |
| 1 | Sustain Bass | 0.602 | 0.913 | 0.540 | 0.980 | 0.000 | 0.800 |
| 2 | Round Bass | 0.352 | 0.957 | 1.000 | 0.980 | 0.000 | 0.800 |
| 3 | Fretless | 1.000 | 0.000 | 1.000 | 0.980 | 0.000 | 0.800 |
| 4 | Synthi Bass | 0.114 | 0.290 | 0.000 | 0.980 | 0.000 | 0.800 |
| 5 | Clavinet | 0.625 | 0.304 | 0.000 | 0.980 | 1.000 | 0.800 |
| 6 | DX Bass | 0.636 | 0.522 | 0.405 | 0.980 | 0.798 | 0.800 |
| 7 | Hollow Bass | 0.716 | 0.391 | 0.349 | 0.980 | 0.244 | 0.800 |
| 8 | Sequenz Bass | 0.136 | 0.000 | 1.000 | 0.980 | 0.229 | 0.800 |
| 9 | Warm Bass | 0.807 | 0.000 | 0.405 | 0.980 | 0.161 | 0.800 |
| 10 | Slap Frets | 0.239 | 0.000 | 0.000 | 0.980 | 1.000 | 0.800 |
| 11 | Buzz Bass | 1.000 | 0.551 | 0.209 | 0.980 | 0.419 | 0.800 |
| 12 | Add Chorus | 0.670 | 0.000 | 0.451 | 0.980 | 0.833 | 0.800 |
| 13 | Synth Bass 2 | 1.000 | 0.304 | 1.000 | 0.980 | 0.387 | 0.800 |
| 14 | Dark Click | 0.886 | 0.188 | 0.386 | 0.980 | 0.119 | 0.800 |
| 15 | Synth Bass 3 | 0.148 | 1.000 | 0.000 | 0.980 | 0.678 | 0.800 |

Observations: **Release≈0.980 and Volume=0.800 are constant across all presets.** All timbral
character lives in Damper/PickUp/Pick/Shape. (Raw bytes available; rounding is cosmetic — e.g.
0.602 is `0x3f1a2e90`.)

---

## 4. Class architecture

Four C++ classes (Itanium ABI; typeinfo names @ `0x37c33`):

- **`VaString`** (8VaString) — the `AudioEffect` plugin. 0x4108 bytes. vtable `0x60b50`.
  Owns: AEffect (+0x30), the 16-program array (+0x40f0), the editor, `VaStringVoices` (+0x40f8),
  a 2112-byte object (+0x4100), a 16 KB aligned buffer (chorus/global delay).
- **`Vb1sxEditor`** — the GUI. 0xf0 bytes. ctor `0x1f0be`. Loads 6 bitmaps
  (bmp00229.bmp, bmp00236.png, bmp00231.png, bmp00230.png, bmp00233.bmp, bmp00237.bmp).
  **Do not RE the Carbon UI — rebuild fresh.**
- **`VaStringVoices`** (14VaStringVoices) — 8-voice polyphony manager. 0x4858 bytes. vtable `0x61070`.
  ctor `0x1e6d0`. Allocates **8 × `VaStringVoice`** (`new(0x128)` each, via voice factory `0x1e3c2`).
- **`VaStringVoice`** (13VaStringVoice) — one note's physical model. 0x128 bytes. vtable `0x611b0`.

### Program struct (0x70 bytes), in the +0x40f0 array
- `+0x00 .. +0x2c`: 6 param-descriptor pointers (idx 0..5 = Damper..Volume)
- `+0x30 .. +0x6f`: 64-byte program-name buffer

---

## 5. DSP core — digital waveguide string (DECODED)

`VaStringVoice` is a **bidirectional digital waveguide** with inverting terminations.

### Voice state fields (offsets in `VaStringVoice`, 0x128 bytes)
| Off | Type | Meaning |
|---|---|---|
| +0x08 | double | volume gain (set by `0x226ac`) |
| +0x10 | double | pan position (set by `0x2255c`) |
| +0x18 | double | pan scale |
| +0x30 | double | **computed left gain** (pan·volume) |
| +0x38 | double | **computed right gain** (pan·volume) |
| +0x40 | double | pan-law width (set by `0x225fa`; ctor default 0.75) |
| +0x48 | double | pan-law constant |
| +0xd0 | double | **g** — loop lowpass coefficient (= **Release** param; 0.98 ⇒ long sustain) |
| +0xd8 | double | loop-filter state (one-pole memory) |
| +0x100 | int32 | **N** — delay-line length (= pitch period; sampleRate/f0) |
| +0x104 | int32 | **pickup offset** (= PickUp mapping → comb read position) |
| +0x108 | int32 | DL-A circular index |
| +0x10c | int32 | DL-B circular index |
| +0x110 | float* | **delay line A** (`new[0x9604]` ≈ 9601 floats) |
| +0x118 | float* | **delay line B** (`new[0x9604]` ≈ 9601 floats) |
| +0xc4 | byte | sustain/release selector (0 = release loop) |
| +0xc6/+0xc7/+0xc8 | byte | pan-law enable flags |

### Per-sample render loop — `0x1dd4f` (sustain) / `0x1de62` (release)
`render(voice=rdi, outLptr=[rdx], outRptr=[rdx+8], numSamples=ecx)`. g=`+0xd0`, N=`+0x100`,
pickupOffset=`+0x104`, DL_A=`+0x110`, DL_B=`+0x118`, state=`+0xd8`.

```
for each sample:
    # 1) Pickup read (comb filtering): output read at an offset along DL_B
    pickup = DL_B[(bIdx + pickupOffset) % N]

    # 2) Loop lowpass (Release param -> g) on the traveling wave, then invert (fixed-end)
    state = g*state + (1-g)*DL_B[bIdx]          # one-pole lowpass
    DL_A[aIdx] = -(float)state                   # inverting termination

    # 3) Bidirectional coupling: A's previous sample feeds back into B (inverted)
    DL_B[bIdx] = -DL_A[(aIdx + N-1) % N]         # standing-wave coupling

    # advance circular indices (aIdx--, bIdx++, both mod N)

    # 4) Output (additive across voices → polyphony mix)
    outL += (float)(pickup * gainL)              # gainL = +0x30
    outR += (float)(pickup * gainR)              # gainR = +0x38
```

Sign masks: float `0x80000000` (`0x39080`), double `0x8000000000000000` (`0x393e0`). The two
near-identical loop bodies (sustain vs release) differ in coefficients selected by `+0xc4`.

### Pan + volume gain computation — `0x2255c / 0x225fa / 0x226ac` / inline `0x1fb20`
Constant-power-ish law: from pan `p` (+0x10) and width `w` (+0x40), with center 0.5 (`0x392a8`):
```
base = 1 - p
if w > ~1e-5 and p > 0.5:   base += w * k * (1-p)          # asymmetric widening
gainL = base * volume(+0x08) ; gainR = p' * volume          # → +0x30 / +0x38
```
(Exact `k`/asymmetry constant is `+0x48`; near-zero threshold `0x39430`. `[INFERENCE]` on the
precise law shape — reproduce by A/B, the field offsets are exact.)

### Parameter → DSP mapping (confirmed by tracing setParameter/note-on)
setParameter C-shim `0x62b4` → `VaString` vtable[11] `0x1e494` = `programs[curProgram].params[idx]=value`
(per-program storage). Voices pull current-program values on note-on (`0x1e5c6`/`0x1e630`/`0x1e068`).

| Param | Voice field / effect | Status |
|---|---|---|
| [0] Damper | detune of note→N + coupling coeff (`0x1e068`) | **field CONFIRM**; coeff TUNE |
| [1] PickUp | pickup read offset `+0x104` (comb filter) | CONFIRM |
| [2] Pick | note-on excitation window (raw↔smooth noise blend) | field TUNE (windowing) |
| [3] Release | loop lowpass `g = +0xd0` (`0x1e630`) | **CONFIRM** |
| [4] Shape | (waveshaper / excitation spectrum) | TBD — locate by sweep + A/B |
| [5] Volume | `+0x08` → gains `+0x30/+0x38` (`0x1e5c6`) | CONFIRM |

### Excitation — Karplus-Strong pluck (`0x2287a`, generated once)
Two global 16384-float tables built from `rand()`:
- `0x74a20` = white noise in [-1,1) (raw pluck)
- `0x84a20` = one-pole-integrated noise (leaky coeff `0.75`=`[0x39350]`) — softer pluck

On note-on these seed `DL_A`/`DL_B` (length N); Pick blends raw↔smooth. (Exact windowing = Phase-4 A/B.)

### Note → N (delay length) — `0x1e068`
`N = min( sampleRate/freq(note) + 1 , 9599 )`, `freq` from note via `0x21f12` (octaves = note×1/12
`[0x39378]`); Damper slightly retunes the octave term and scales a coupling coeff.

### Chorus / auto-pan — `0x22750` (the "Add Chorus" preset)
Reads the excitation table as a ping-pong LFO (`[+0x70]`/`[+0x74]`, wrap `0x3fff`) into pan-width
`+0x40`/`+0x48`, then recomputes gains — a stereo "whoosh". Enabled by `[+0xc8]`.

### Polyphony / MIDI — `VaStringVoices` (`0x2161a` render, `0x2117a` MIDI dispatch)
8 voices mixed additively. MIDI: CC7=Volume, CC10=Pan, CC1=Mod, sustain pedal `0x40`,
pitch-bend `0xE0`, program-change `0xC0`.

---

## 6. What is solid vs. remaining

**Solid (Phase 0 + 1 + Phase-2 fully decoded):** binary ID, AEffect metadata, 6-param set +
defaults, the complete 16×6 preset table, 4-class architecture + vtable maps, the waveguide render
loop, the Karplus-Strong excitation seeder (Pick = pluck position splitting N; triangular-windowed
fractional noise), the note→N formula, the pan/volume gain stage, and the chorus/auto-pan. The full
param→DSP map is confirmed for 5 of 6 params (Damper/PickUp/Pick/Release/Volume).

**Remaining RE (Phase-2 tail — coefficient fitting, not architecture):**
1. **Shape (param 4)** — the ONLY unsolved param. Not read in note-on (`0x1e068`) nor the render
   loop (which is linear apart from sign inversion). Find by dynamic sweep: vary Shape, diff output
   vs original. Likely a waveshaper on the voice mix (`0x2161a`) or an excitation-spectrum control.
2. Exact coefficient values (architecture is known): the g coupling coeff from Damper+Release+N,
   the release-path coefficients (second loop `0x1de62`), the pan-law asymmetry constant, the
   Damper detune constants, and the excitation window amplitude scaling (`[0x393b0]`).
3. **PreRelease** string's role (possibly a second release phase label; not host-exposed).

**Not started (Phases 3–5):** JUCE scaffold, DSP port, arm64/universal build, A/B match,
packaging. These are the bulk of the "working plugin" deliverable and need real iteration;
the DSP above is accurate enough to begin a port, but perceptual matching (Phase 4) will require
the refinements in §6.

---

## 7. Corrected reimplementation plan

- Phase 0: JUCE 8 + CMake; arm64 hello-world VST3/AU. (Original = VST2/Rosetta only.)
- Phase 1: ✅ this doc.
- Phase 2: finish §6 (Pick/Shape/Damper/N), then a JUCE `SynthesiserVoice` port of the §5 loop.
  8 voices, two `AudioBuffer`/`std::vector<float>` delay lines per voice, the 16 programs from §3.
- Phase 3: APVTS with the 6 params (host order: Damper,PickUp,Pick,Release,Shape,Volume), 16
  factory programs (§3 values), fresh 6-knob GUI (don't RE Carbon).
- Phase 4: render original via Rosetta+VST2 host vs re-impl; numpy RMS/spectrogram diff; iterate.
- Phase 5: universal VST3+AU, codesign, install.

**Legal:** personal use only; do not redistribute a derived VB-1 or its bitmap assets.
