#!/usr/bin/env python3
"""
cma_fit.py — fit VB1DSP.h's [TUNE] coefficients to the original VB-1 via CMA-ES
(research agent's #1 recommendation; skopt is dead, so NOT used here).

Objective = 0.7 * spectral-logmag + 0.3 * cross-correlation-aligned RMS
(pure RMS is dominated by waveguide loop-phase drift, per the audio-loss literature).

Each eval: write coeffs -> run VB1Render (short single-note) -> diff vs original_short.wav.
Run:  python tools/cma_fit.py            (defaults: ~150 evals, ~2-3 min)
      python tools/cma_fit.py 600        (more evals)
"""
import sys, subprocess, numpy as np
import cma
from scipy.signal import stft, fftconvolve
import soundfile as sf
RENDER = "build/VB1Render_artefacts/Release/VB1Render"
ORIG_WAV = "/tmp/original.wav"   # FULL 16-program render (generalizes; short note overfits)
OUT_WAV  = "/tmp/r_opt.wav"
COEFFS   = "/tmp/c.txt"

# ---- coefficient vector: [kOutGain, gScale, pickupScale, pluckScale, excAmp, relS] ----
X0   = np.array([1.3, 1.0, 1.0, 1.0, 1.0, 0.997])
LO   = np.array([0.3, 0.95, 0.1, 0.5, 0.3, 0.950])
HI   = np.array([4.0, 1.02, 2.0, 2.0, 3.0, 0.9999])
NAMES = ["kOutGain","gScale","pickupScale","pluckScale","excAmp","relS"]

o, sr = sf.read(ORIG_WAV);  o = o[:,0] if o.ndim > 1 else o
evals = [0]; best = [1e9, None]

def metric(a, b):
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    # normalize to equal RMS so the optimizer fits spectral SHAPE, not absolute level
    # (log1p is compressive -> otherwise it rewards making the reimpl quiet). kOutGain is
    # then set by an explicit level-match step after the search (see end of script).
    a = a / (np.sqrt(np.mean(a*a)) + 1e-12);  b = b / (np.sqrt(np.mean(b*b)) + 1e-12)
    # spectral log-magnitude (multi-resolution), shift/phase-robust
    spec = 0.0
    for nfft in (512, 2048, 8192):
        _, _, A = stft(a, nperseg=min(nfft, n), noverlap=min(nfft,n)//2)
        _, _, B = stft(b, nperseg=min(nfft, n), noverlap=min(nfft,n)//2)
        spec += np.mean((np.log1p(np.abs(A)) - np.log1p(np.abs(B)))**2)
    spec /= 3.0
    # aligned time-domain RMS
    c = fftconvolve(a, b[::-1], mode="full"); lag = np.argmax(np.abs(c)) - (len(b)-1)
    bb = np.roll(b, lag); rms = float(np.sqrt(np.mean((a - bb)**2)) + 1e-12)
    return 0.7 * spec + 0.3 * rms

def objective(x):
    x = np.clip(x, LO, HI)
    np.savetxt(COEFFS, x)
    try:
        subprocess.run([RENDER, OUT_WAV, COEFFS], check=True, capture_output=True, cwd=".")
        r, _ = sr2 = sf.read(OUT_WAV); r = r[:,0] if r.ndim > 1 else r
        f = metric(o, r)
    except Exception as e:
        if evals[0] < 3: import traceback; print("OBJ EXC:", repr(e)); traceback.print_exc()
        f = 1e6
    evals[0] += 1
    if f < best[0]:
        best[0] = f; best[1] = x.copy()
        print(f"  [{evals[0]:3d}] f={f:.5f}  " + "  ".join(f"{n}={v:.3f}" for n,v in zip(NAMES,x)), flush=True)
    return f

if __name__ == "__main__":
    budget = int(sys.argv[1]) if len(sys.argv) > 1 else 150
    sigma0 = 0.25
    es = cma.CMAEvolutionStrategy(X0.tolist(), sigma0,
            {"bounds": [LO.tolist(), HI.tolist()], "maxfevals": budget,
             "verbose": -9, "seed": 42})
    es.optimize(objective)
    # --- post-search: level-match kOutGain to the original (objective is level-invariant) ---
    np.savetxt(COEFFS, best[1])
    subprocess.run([RENDER, OUT_WAV, COEFFS], check=True, capture_output=True)
    rr, _ = sf.read(OUT_WAV); rr = rr[:,0] if rr.ndim > 1 else rr
    kmatch = float(best[1][0] * np.sqrt(np.sum(o**2) / (np.sum(rr**2) + 1e-12)))
    final = best[1].copy(); final[0] = kmatch
    print("\n=== BEST (shape) ===  f =", round(best[0],5))
    for n, v in zip(NAMES, best[1]): print(f"  {n} = {v:.4f}")
    print(f"\n=== FINAL (kOutGain level-matched to original) ===")
    for n, v in zip(NAMES, final): print(f"  {n} = {v:.4f}")
    np.savetxt("/tmp/vb1_best_coeffs.txt", final, fmt="%.6f")
    print("\nsaved -> /tmp/vb1_best_coeffs.txt ; validate: ab_diff.py /tmp/original.wav <render>")
