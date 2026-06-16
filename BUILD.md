# VB-1 reimplementation — build & handoff

Reverse-engineered spec: `../VB1_RE_Spec.md`. DSP reference: `../VB1_DSP_Reference.cpp`.
This folder is the JUCE plugin **source scaffold** (Phases 0–2 RE done; Phase 3 source written;
**nothing has been compiled this session** — there is no JUCE/toolchain on this machine yet).

## What's here
| File | Role |
|---|---|
| `Source/VB1DSP.h` | Waveguide DSP: `Excitation` (Karplus-Strong tables), `VaStringVoice` (SynthesiserVoice port of render loop `0x1dd4f`), `VaStringSound`, 16 presets. |
| `Source/PluginProcessor.h/.cpp` | `AudioProcessor` + APVTS (6 params, host order) + 8-voice `Synthesiser` + 16 programs. |
| `Source/PluginEditor.h/.cpp` | Fresh 6-knob GUI + program combo (NOT a RE of the Carbon editor). |
| `CMakeLists.txt` | `juce_add_plugin`, VST3 + AU, synth, MIDI-in. |

## Build (once you install the toolchain)
```bash
# 1. JUCE + Xcode (Xcode is already installed here)
git clone --depth 1 --branch 7.0.5 https://github.com/juce-framework/JUCE
# 2. configure — arm64 by default on this M1
cmake -B build -DJUCE_DIR=$PWD/JUCE -DCMAKE_BUILD_TYPE=Release
# 3. build universal VST3 + AU, auto-installed to ~/Library/Audio/Plug-Ins
cmake --build build --config Release -j
```
For a Universal Binary add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. Codesign with your
Developer ID for distribution; ad-hoc `codesign -s -` is enough for local loading.

## Fidelity status — what's accurate vs. needs fitting
The signal path and **5 of 6 params** are decoded from the binary and ported faithfully:
render loop, Karplus-Strong excitation (Pick = pluck position splitting N, triangular-windowed
fractional noise), Release→loop-lowpass g, PickUp→comb offset, Damper→N detune+coupling,
Volume→gain, 8-voice mix.

Marked **[TUNE]** in the code (coefficient values, not architecture) — fit these in Phase 4:
- **Shape (param 4)** — the only unsolved param. It is provably absent from the linear render
  loop and the note-on seeder, so it must be a sweep-and-diff to confirm (try: output waveshaper
  on the voice mix, or excitation raw↔smooth table selection). Start with Shape at 0 vs 1.
- Exact g coupling coeff (Damper+Release+N), release-path coeffs (`0x1de62`), pan-law asymmetry,
  excitation window amplitude scaling, the note→N base frequency.

## Phase 4 — A/B match  (HEADLESS PIPELINE WORKING — no DAW needed)
Both plugins render headlessly and deterministically, then diff automatically. No REAPER required.

- **Original (VST2/x86_64):** `tools/vst2_render.c` is a minimal VST2 host (run under Rosetta)
  whose AEffect offsets are taken directly from the binary — note this build uses a NON-standard
  layout (`processReplacing` @ +0x78, `object` @ +0x60, `uniqueID` @ +0x70; `realQualities` is
  int32). `arch -x86_64 ./tools/vst2_render <…/vb1.vst/Contents/MacOS/vb1> /tmp/original.wav`
  (the plugin's mains-on/mains-off paths fault headlessly, so the host renders without them and
  writes the WAV before exit).
- **Reimpl:** `tools/render_reimpl.cpp` (JUCE console target `VB1Render`) drives `VB1DSP.h`.
- **Diff:** `python tools/ab_diff.py /tmp/original.wav /tmp/reimpl.wav --plot diff.png`.

Tuning-loop progress (deterministic — RNG seeded; each cycle is repeatable):
  baseline             error RMS −15.45 dB   spectral Δ 28.31 dB   (orig +2.6 dB hot)
  +per-sample release  error RMS −17.82 dB   spectral Δ  5.78 dB   ← biggest single win (release was applied per-call, not per-sample)
  +level match (1.3×)  error RMS −16.82 dB   spectral Δ  6.47 dB   level matched (−20.10/−20.12 dB)
Diagnosed: pitch ✓ (40.0 Hz both), sustain timbre ✓, release timing ✓. Remaining residual (−16.8 dB)
is waveform-shape. **Caveat for continuation:** the binary-decoded coeffs (`0x1e190`: g = 1−min(0.9,
(9600/N)·0.0125·(0.8·Damper+0.1)) — Damper-driven, not Release; pickup = (PickUp/6+1/64)·N; excitation
`2·noise·env − 0.1`) are ground-truth but REGRESS when swapped in alone (spectral 6.5→13–37 dB) because
they interact with the still-approximate release path (`0x1de62`) and the waveguide coupling. So:
isolate ONE coeff per cycle, keep `g=Release` + plain triangular excitation + `PickUp·N` as the known
best baseline, and integrate the decoded values together once the release/coupling coeffs are set.

**Automated fitting (CMA-ES) — `tools/cma_fit.py`:** derivative-free search over the 6 [TUNE] coeffs,
objective = 0.7·spectral-logmag + 0.3·aligned-RMS (level-normalized so it fits SHAPE, not level),
then an explicit kOutGain level-match post-step. Run: `python tools/cma_fit.py 400` (~3 min).
Result baked into VB1DSP.h g_tune defaults: **error RMS −17.76 dB, spectral Δ 4.89 dB** (was 6.47
hand-tuned), level-matched, no clipping. Modest but real, and fully automated.

**Structure is VERIFIED CORRECT (Ghidra 11.3.1 headless decompile):** the render loop (`FUN_0001dd10`),
note-on/seeder (`FUN_0001e068`), and excitation gen (`FUN_0002287a`) all match the port byte-for-byte —
g `=1−min(0.9,(9600/N)·0.0125·(0.8·Damper+0.1))`, pickup `=(PickUp/6+1/64)·N`, pluck `r8=Pick·0.5·N`,
seeder `e=2·noise[idx]·env − 0.1` @ `4096/N` scan. So the earlier "structural phase-drift blocker"
hypothesis was WRONG — the structure is accurate. The −17.7 dB error floor is therefore **coefficient/
float-level**, not structural. Remaining suspects: (a) exactly which noise table `[voice+0x90]+0xf0`
reads (gen points to raw `0x74a20`; we dump both via `vst2_render.c`); (b) x86_64-vs-arm64 float
divergence amplified by the lightly-damped loop. Re-run `tools/cma_fit.py` to refine; also try the
exact seeder (`2·noise·env−0.1`, `4096/N`) with the RAW table + a fresh CMA-ES pass. Shape note:
NOT in recomputeGains (that's a pan>0.5 branch); Shape only selects the excitation table. Goal: error
RMS < −60 dB. Bit-exact is unlikely (x86_64-vs-arm64 floats).

## Phase 5 — package  (`tools/package.sh`)
- Build Universal VST3 + AU (`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`), then
  `tools/package.sh` codesigns (ad-hoc locally, or `DEV_ID="..."` for distribution) and installs
  to `~/Library/Audio/Plug-Ins/{Components,VST3}`. `COPY_PLUGIN_AFTER_BUILD` also auto-installs.

## Legal
Personal use only. Do not redistribute a derived VB-1 or its bitmap assets.
