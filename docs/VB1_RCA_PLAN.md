# VB-1 — First-Principles RCA Plan

Goal: make the reimpl **perceptually indistinguishable** from the original by understanding *why* it
differs, not by tuning-and-hoping. Core principle: **observe the original's actual signal flow,
isolate each stage, prove per-sample equality stage-by-stage, then (and only then) tune residuals.**

Anti-patterns banned by this plan (see `VB1_FINDINGS.md` §5): naked spectral/RMS optimization,
one-variable coefficient swaps, trusting unverified dumps, chasing mono-invisible pan/Shape.

---

## Stage 0 — Pin the runtime ground truth (the original's real state)

The single highest-leverage move: the original is **runnable** via `tools/vst2_render.c`. Instrument
it to dump its **internal state** at exact checkpoints, then diff against the reimpl's state. This
turns "sounds different" into "diverges at sample k of stage X".

**0.1 — Resolve the noise-table address correctly.** Recompute the ASLR slide robustly (use a known
data symbol, not a code pointer) and **verify the dump against the generator formula**
(`raw[i]=rand()·2⁻³⁰−1`; `smooth[i]=0.75·prev+raw·0.45`). Accept the dump only if its spectrum/
stats match the formula. Dump: the raw table, the smooth table, and `[voice+0x90]+0xf0`'s target
(to settle raw-vs-smooth, §findings Q1).

**0.2 — Dump the seeded delay lines.** After the original's note-on, dump `dlA[0..N]` and `dlB[0..N]`
(the exact excitation as actually written). Compare to the reimpl's seeded arrays **sample-for-sample**.
- If they match → the seeder is correct; the divergence is in the loop/output. (Resolves the attack
  paradox §findings §3.)
- If they differ → the seeder (table choice / scan rate / window / amplitude) is wrong; fix it here
  and the attack paradox may vanish.

**0.3 — Dump one note's full evolution.** For a single sustained note, capture the original's
`dlA/dlB` + output every `N` samples (one per round trip). Diff against the reimpl at the same
checkpoints. The first checkpoint that diverges names the guilty stage.

> Deliverable: a `tools/state_diff.py` that prints, per checkpoint, the sample index and magnitude of
> the first divergence. This is the compass for every later step.

---

## Stage 1 — Black-box system identification (controlled experiments)

Treat the original as a black box; drive it with surgical inputs and read the exact response. This
characterizes each behavior independently of the decompilation.

- **1.1 Impulse/step response per param.** Hold all params at a preset, inject a single-sample
  impulse (or a known short window) instead of the noise pluck, capture the full decay. Estimate the
  effective **loop transfer function** (poles/zero → confirms `g`, the inverting terminations, and
  whether there's an *additional* filter the decompile missed).
- **1.2 One-param sweeps.** Sweep each of the 6 params over [0,1] holding others fixed; record
  pitch, decay-time (T60), and spectral centroid vs the param value. This **empirically maps each
  param→effect** and will expose if `g` really tracks the Damper+N formula or something else.
- **1.3 Pickup comb measurement.** With a fixed periodic input, measure the comb spacing vs PickUp →
  confirms `pickup = (PickUp/6+1/64)·N` (and explains the "phasey" tone) independently of the binary.
- **1.4 Noise-floor vs time.** Measure the original's broadband floor decay (2–8 kHz RMS in 5 ms
  windows) → the true high-freq damping rate my port must reproduce.

> Deliverable: an empirical spec of each stage's behavior, cross-checked against the Ghidra formulas.
> Any disagreement = a real bug or a missed stage (e.g., the attack lowpass).

---

## Stage 2 — Per-stage isolation & unit verification

Prove each DSP stage correct **in isolation** before composing them.

- **2.1 Seeder unit test.** Feed identical noise tables into the original's seeder (via the dumped
  state) and the reimpl's; assert `dlA == dlA_orig` to 1e-6. (Depends on 0.2.) This is the direct
  test of the attack-paradox suspect.
- **2.2 Loop unit test.** Seed both with the *same* known buffer; step the loop k samples; assert
  state equality to 1e-6. Isolates the loop filter/coupling from the excitation.
- **2.3 Pickup/output unit test.** With a known buffer state, compare the pickup-read output sample.
- **2.4 rand() replication.** Reverse-engineer Apple's macOS libc `rand()` (the LCG constants/state)
  and verify the regenerated noise matches the dumped raw table **exactly** — removes the last source
  of sample-level divergence (findings §4).

> Deliverable: green per-stage equality tests. Once all pass, any residual difference is bounded and
> tunable without breaking physics.

---

## Stage 3 — Resolve the named mysteries

Each maps to a Stage-0/1/2 observation:
- **Attack paradox** → Stage 0.2/2.1 (is the seeded state byte-equal? if not, the table/scan/window
  is wrong; if yes, an output filter is missing → hunt it in the decompile around the voice-mix
  `0x2161a` and the processReplacing path).
- **Which table (`[voice+0x90]+0xf0`)** → Stage 0.1 (dump the pointer target).
- **Missing output/attack filter** → Stage 1.1 (impulse response will show an extra pole/rolloff) +
  targeted Ghidra re-read of the output path.
- **Sample-level divergence** → Stage 2.4 (rand replication) + 0.3 (per-round-trip diff).

---

## Stage 4 — Perceptual re-validation (metrics demoted to advisory)

- **A/B blind listening** is the acceptance gate (`tools/...` → narrated A/B WAV). Two notes must be
  confusable.
- Metrics (RMS, spectral Δ) are **diagnostic only**, never the objective — they're gameable (lesson §5.1).
- Tune *only* level + release once all stages are state-equal; never touch pluck/g/pickup/seeder
  (those are physics, locked to verified values).

---

## Sequencing & success criteria

| Stage | Order | Gate to proceed |
|---|---|---|
| 0.1 dump+verify tables | 1st | dumped spectrum matches generator formula |
| 0.2 dump seeded dlA/dlB | 2nd | seeder state equality resolved (match or localized bug) |
| 0.3 per-round-trip diff | 3rd | first-divergence checkpoint identified |
| 1 black-box ID | parallel | empirical per-param maps cross-check Ghidra |
| 2 per-stage unit tests | after 0 | all stages byte-equal to 1e-6 |
| 3 mysteries | as data arrives | each mystery has an observed cause |
| 4 perceptual | last | blind A/B confusable |

**Stop condition:** seeded-state equality + per-round-trip divergence below −60 dB AND blind A/B
confusable. Until then, do not optimize coefficients.

## Why this beats what we did
We spent the matching phase optimizing a gameable metric over wrong-but-interacting parameters and
got a spectrally-"matched" instrument that sounded broken. This plan replaces guessing with
**observation of the original's actual internal state** — the divergence gets localized to an exact
stage/sample, and each fix is provable instead of hopeful.
