#!/usr/bin/env python3
"""
ab_diff.py — VB-1 original vs reimpl A/B comparison (Phase 4).

Usage:
    python ab_diff.py original.wav reimpl.wav [--plot out.png] [--block 1024]

Loads both renders (identical MIDI through original VB-1 [Rosetta/VST2] and the
reimpl), sample-aligns them via cross-correlation (compensates for plugin latency),
and reports time-domain (RMS / max) and spectral (per-block FFT) differences plus
a verdict. Bit-exact is unlikely across x86_64-vs-arm64 floats; aim for the diff
to be inaudible (RMS <-60 dB, spectral delta near the noise floor).

Requires: numpy, soundfile, scipy, matplotlib (pip install numpy soundfile scipy matplotlib).
"""
import argparse, sys
import numpy as np

try:
    import soundfile as sf
except ImportError:
    sys.exit("pip install soundfile")

def load_mono(path):
    x, sr = sf.read(path, always_2d=False, dtype="float32")
    if x.ndim == 2:                 # mix to mono for comparison
        x = x.mean(axis=1)
    return x, sr

def align(a, b):
    """Cross-correlate to find the integer sample offset that best aligns b to a."""
    n = min(len(a), len(b))
    a0, b0 = a[:n], b[:n]
    # use scipy if available (fast), else numpy
    try:
        from scipy.signal import correlate
        corr = correlate(a0, b0, mode="full")
    except ImportError:
        corr = np.correlate(a0, b0, mode="full")
    lag = int(np.argmax(np.abs(corr))) - (len(b0) - 1)
    if lag >= 0:
        return a[lag:], b[:len(a)-lag] if lag < len(a) else b[:0]
    else:
        return a, b[-lag:]

def rms_db(x):
    m = np.sqrt(np.mean(x**2))
    return -np.inf if m == 0 else 20 * np.log10(m)

def spectral_diff(a, b, block, hop, sr):
    """Average per-block magnitude-FFT difference in dB."""
    from numpy.fft import rfft
    win = np.hanning(block)
    n = min(len(a), len(b))
    deltas, ref = [], []
    for i in range(0, n - block, hop):
        A = np.abs(rfft(a[i:i+block] * win))
        B = np.abs(rfft(b[i:i+block] * win))
        eps = 1e-12
        deltas.append(20*np.log10((np.abs(A-B) + eps) / (A + eps) + eps))
        ref.append(20*np.log10(A + eps))
    return np.mean(deltas), np.array(ref).mean(axis=0), np.arange(block//2+1) * (sr/block)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("reimpl")
    ap.add_argument("--plot", help="save spectrogram/diff plot here")
    ap.add_argument("--block", type=int, default=1024)
    ap.add_argument("--hop",   type=int, default=512)
    args = ap.parse_args()

    a, sra = load_mono(args.original)
    b, srb = load_mono(args.reimpl)
    if sra != srb:
        sys.exit(f"sample-rate mismatch: {sra} vs {srb} (render both at the same rate)")

    a, b = align(a, b)
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    diff = a - b

    print(f"length        : {n} samples ({n/sra:.2f}s @ {sra} Hz)")
    print(f"signal RMS    : orig {rms_db(a):6.2f} dB   reimpl {rms_db(b):6.2f} dB")
    print(f"error RMS     : {rms_db(diff):6.2f} dB   (target: < -60 dB = inaudible)")
    print(f"peak error    : {20*np.log10(np.max(np.abs(diff))/(np.max(np.abs(a))+1e-12)):6.2f} dB")

    sd, refspec, freqs = spectral_diff(a, b, args.block, args.hop, sra)
    print(f"spectral delta: {sd:6.2f} dB mean per-band ratio (target: < -40 dB)")

    verdict = "INAUDIBLE / matched" if rms_db(diff) < -60 else "AUDIBLE — keep tuning [TUNE] coeffs"
    print(f"\nVERDICT: {verdict}")

    if args.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(3, 1, figsize=(10, 8), sharex=False)
        ax[0].plot(np.linspace(0, n/sra, n), a, label="original", alpha=.7)
        ax[0].plot(np.linspace(0, n/sra, n), b, label="reimpl",  alpha=.7)
        ax[0].set_title("waveforms"); ax[0].legend()
        ax[1].plot(np.linspace(0, n/sra, n), diff, color="r", alpha=.7)
        ax[1].set_title("difference (original - reimpl)")
        ax[2].semilogx(freqs, refspec, label="original spectrum (dB)")
        ax[2].set_title("original magnitude spectrum"); ax[2].set_xlabel("Hz")
        fig.tight_layout(); fig.savefig(args.plot, dpi=110)
        print(f"plot saved: {args.plot}")

if __name__ == "__main__":
    main()
