# VB-1 Reimplementation — Findings & Open Questions

Status of the reverse-engineering + match effort. Authoritative for the DSP-matching phase;
for class architecture / preset tables see `VB1_RE_Spec.md` and `VB1_DSP_Reference.cpp`.

## 1. What is SOLID (Ghidra-verified + runtime-dump-confirmed)

All DSP stages are now proven-correct via the headless voice-state dump technique:

- **Render loop** `FUN_0001dd10` (sustain) / `0x1de62` (release) — bidirectional waveguide.
  Python sim reproduces reimpl's state *exactly*; loop is byte-faithful.
- **note-on** `FUN_0001e068`:
  - `N = min( sampleRate/(2·freq_musical) + 1 , 9599 )` — **freq = 2× musical** (period = 2N).
  - `g = 1 − min(0.9, (9600/N)·0.0125·(0.8·Damper + 0.1))`
  - `pickup = (PickUp/6 + 1/64) · N`
  - `r8 (pluck split) = Pick · 0.5 · N`
- **Seeder** (inside note-on): reads a **CONSTANT-0.5 lookup table** at `[voice+0x90]+0xf0`
  (NOT noise!). Produces `e = 2·0.5·env − 0.1 = env − 0.1` — a deterministic triangular ramp.
  - Seg1 [0,r8): `e = (i/r8) − 0.1`
  - Seg2 [r8,N): `e = ((N−1−i)/(N−1−r8)) − 0.1`
  - **Seeded dlA/dlB now match the original byte-for-byte** (max abs diff = 0.000000).
- **Noise tables** (`FUN_0002287a`): raw @ `0x74a20`, smooth @ `0x84a20` — generated but **NOT used
  by the default seeder**. Likely used for the chorus/LFO modulation (`FUN_00022750`) or Shape≠0
  presets. (The entire noise-table investigation was a detour — the seeder is deterministic.)

## 2. Current state
- **error RMS: −28.6 dB** (was −17 dB before RCA). **spectral Δ: −4.2 dB**. Level matched.
- User verdict: **"very near now!"** — perceptually close.
- Remaining gap: **float-precision divergence** (x86_64-vs-arm64 doubles drifting over thousands of
  waveguide round trips). Structurally everything is proven-correct.

## 3. RCA execution results (3 root causes found and fixed)

| # | Root cause | How found | Fix | Impact |
|---|---|---|---|---|
| 1 | **noteToN 2× (octave-low pitch)** | Voice dump: original N=264, mine 539 | `freq *= 2` | Pitch matched |
| 2 | **Seeder used noise instead of constant-0.5 triangle** | Voice dump: seeded dlB peak 0.90 (mine 1.93); reverse-engineered = `(i/r8)−0.1` | Replaced noise with constant 0.5 | **Seeded state byte-identical** |
| 3 | **Level mismatch** | A/B: reimpl −27 dB vs original −20 dB | kOutGain calibrated to 0.816 | Level matched |

**The headless voice-state dump** (`vst2_render.c dumpvoices`) was the key method: walks
`AEffect.object → +0x40f8 → +0x2b8 (voice)`, reads `dlA/dlB/N/g/pickup` directly. This turned
"sounds different" into "seeded dlB differs by exactly 0.000000 (match)" or "N=264 not 539 (bug)".

## 4. Dead-ends & hard lessons (do not repeat)
1. **The spectral metric is gameable.** CMA-ES hit −0.73 dB spectral Δ by halving the pluck
   position → spectrum matched, instrument sounded broken. → Lock physical params; only level/release free.
2. **The noise-table investigation was a red herring.** The seeder reads a constant-0.5 lookup table,
   not noise. Hours spent on rand() replication / table dumps were wasted. → Trust the runtime state
   dump over the static decompile when they conflict.
3. **One-variable coefficient swaps regress** because params interact. → Prove per-sample state
   equality before tuning anything.
4. **`recomputeGains` pan/Shape is mono-invariant** (gainL+gainR≡1) — invisible in mono A/B.

## 5. Infra (reusable)
- `tools/vst2_render.c` — headless VST2 host (Rosetta); `dumpvoices` mode dumps voice state.
- `tools/render_reimpl.cpp` — headless reimpl renderer; `short` mode dumps voice state.
- `tools/ab_diff.py`, `tools/cma_fit.py`, `tools/generate_test_midi.py`, `tools/package.sh`.
- Ghidra reproducible via `analyzeHeadless` + `/tmp/vb1_decompile.py`.
