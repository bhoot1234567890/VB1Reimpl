#include "VB1ExcitationTables.h"
// =============================================================================
// VB1DSP.h — VaString digital waveguide, ported from the RE'd binary.
// See ../VB1_RE_Spec.md and ../VB1_DSP_Reference.cpp for provenance.
// Items tagged [TUNE] are decoded structurally; their exact coefficients must be
// fitted against the original via the Phase-4 A/B diff.
// =============================================================================
#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <array>

namespace vb1 {

constexpr int   kNumVoices = 8;
constexpr int   kMaxDelay  = 9599;        // N clamp (binary 0x257f)
constexpr int   kExcLen    = 16384;       // excitation/LFO table (0x4000)

// Host param order (AEffect numParams = 6). The 6th, Release, is GUI-hidden.
enum ParamID { pDamper = 0, pPickUp, pPick, pRelease, pShape, pVolume, nParams };

// Runtime-tunable coefficients (fitted by tools/cma_fit.py via CMA-ES on the A/B error).
struct Tune { double kOutGain=0.7795, gScale=1.0, pickupScale=1.0, pluckScale=1.0, excAmp=1.0, relS=0.997; };  // gainL=0.312×vel/127 (original +0x30 runtime-dumped)
inline Tune g_tune;

// ---- 16 factory programs (binary table @ 0x68100). Columns = host order. ----
struct Preset { const char* name; float v[nParams]; };

inline const std::array<Preset, 16>& presets()
{
    static const std::array<Preset, 16> p{{ // Damper, PickUp, Pick, Release, Shape, Volume
        { "Bassic Bass",  { 0.1f, 0.75f, 0.33329999f, 0.98000002f, 0.0f, 0.80000001f } },
        { "Sustain Bass", { 0.60227299f, 0.91304302f, 0.53953499f, 0.98000002f, 0.0f, 0.80000001f } },
        { "Round Bass",   { 0.35227299f, 0.95652199f, 1.0f, 0.98000002f, 0.0f, 0.80000001f } },
        { "Fretless",     { 1.0f, 0.0f, 1.0f, 0.98000002f, 0.0f, 0.80000001f } },
        { "Synthi Bass",  { 0.113636f, 0.289855f, 0.0f, 0.98000002f, 0.0f, 0.80000001f } },
        { "Clavinet",     { 0.625f, 0.30434799f, 0.0f, 0.98000002f, 1.0f, 0.80000001f } },
        { "DX Bass",      { 0.63636398f, 0.52173901f, 0.40465099f, 0.98000002f, 0.79828799f, 0.80000001f } },
        { "Hollow Bass",  { 0.715909f, 0.39130399f, 0.34883699f, 0.98000002f, 0.24428099f, 0.80000001f } },
        { "Sequenz Bass", { 0.136364f, 0.0f, 1.0f, 0.98000002f, 0.228516f, 0.80000001f } },
        { "Warm Bass",    { 0.80681801f, 0.0f, 0.40465099f, 0.98000002f, 0.160605f, 0.80000001f } },
        { "Slap Frets",   { 0.238636f, 0.0f, 0.0f, 0.98000002f, 1.0f, 0.80000001f } },
        { "Buzz Bass",    { 1.0f, 0.55072498f, 0.20930199f, 0.98000002f, 0.419047f, 0.80000001f } },
        { "Add Chorus",   { 0.67045498f, 0.0f, 0.45116299f, 0.98000002f, 0.83333302f, 0.80000001f } },
        { "Synth Bass 2", { 1.0f, 0.30434799f, 1.0f, 0.98000002f, 0.387485f, 0.80000001f } },
        { "Dark Click",   { 0.88636398f, 0.18840601f, 0.38604599f, 0.98000002f, 0.118538f, 0.80000001f } },
        { "Synth Bass 3", { 0.147727f, 1.0f, 0.0f, 0.98000002f, 0.67822999f, 0.80000001f } },
    }};
    return p;
}

// =============================================================================
// Excitation tables (binary 0x2287a, generated once): Karplus-Strong pluck.
// raw = white noise [-1,1); smooth = one-pole-integrated noise (leak 0.75).
// Shared across all voices (binary global tables @ 0x74a20 / 0x84a20).
// =============================================================================
class Excitation
{
public:
    Excitation() { generate(); }
    const float* raw()    const noexcept { return raw_.data(); }
    const float* smooth() const noexcept { return smooth_.data(); }

private:
    void generate()
    {
        // Prefer the ORIGINAL VB-1's exact noise tables (dumped by tools/vst2_render.c) so the
        // waveguide evolves sample-identically; fall back to a seeded RNG if the dumps are absent.
        auto load = [] (const char* path, std::array<float,kExcLen>& dst) -> bool {
            auto f = juce::File (path).createInputStream();
            if (! f) return false;
            return f->read (dst.data(), (ssize_t)(kExcLen * sizeof (float))) == kExcLen * (int)sizeof(float);
        };
        // RCA test: load the ORIGINAL's exact raw noise (dumped by vst2_render.c) if present,
        // so the excitation is sample-identical. Generates smooth from it (0.75·prev + raw·0.45).
        if (load ("/tmp/exc_raw.bin", raw_))
        {
            float state = 0.0f;
            for (int i = 0; i < kExcLen; ++i) { state = 0.75f * state + raw_[i] * 0.45f; smooth_[i] = state; }
            return;
        }
        juce::Random r (0x57623401);
        float state = 0.0f;
        for (int i = 0; i < kExcLen; ++i)
        {
            float n = (float) r.nextFloat() * 2.0f - 1.0f;
            raw_[i] = n; state = 0.75f * state + n * 0.45f; smooth_[i] = state;
        }
    }
    std::array<float, kExcLen> raw_{};
    std::array<float, kExcLen> smooth_{};
};

// =============================================================================
// VaStringVoice — one note's waveguide. Binary: 0x128 B, vtable 0x611b0,
// render loop @ 0x1dd4f (sustain) / 0x1de62 (release).
// =============================================================================
class VaStringVoice : public juce::SynthesiserVoice
{
public:
    explicit VaStringVoice (Excitation& e) : exc_ (e)
    {
        dlA_.assign (kMaxDelay + 8, 0.0f);
        dlB_.assign (kMaxDelay + 8, 0.0f);
    }
    // RCA: expose internal state for diffing against the original's dumped voice
    const float* dlAData() const { return dlA_.data(); }
    const float* dlBData() const { return dlB_.data(); }
    void setExcitationTable (const float* table) { excTable_ = table; }
    int getN() const { return N_; }
    // Per-sample state accessors (transient A/B analysis vs original's dumped voice).
    double getFilt() const { return filt_; }
    int    getIdxA() const { return idxA_; }
    int    getIdxB() const { return idxB_; }
    int    getPickup() const { return pickup_; }
    double getG() const { return g_; }
    double getGainL() const { return gainL_; }
    double getGainR() const { return gainR_; }

    bool canPlaySound (juce::SynthesiserSound*) override { return true; }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int) override
    {
        const auto pr = params_;
        note_ = midiNoteNumber;
        level_ = velocity;

        // (1) Compute delay-line length N first — g, pickup, r8, and seeder all depend on it.
        N_   = noteToN (midiNoteNumber, (double) pr[pDamper], getSampleRate());
        invN_ = 1.0 / N_;

        // (2) g = GHIDRA-VERIFIED (FUN_0001e068 @ 0x1e190):
        //     g = 1.0 − min(0.9, (double)(float)((9600/N) × 0.0125 × (0.8×Damper + 0.1f)))
        //     DAT_00039294 = 0.1f (float constant); gcoeff is truncated to (float) then promoted.
        double gcoeff = (float)((9600.0 / N_) * 0.0125 * ((double) pr[pDamper] * 0.8 + 0.1f));
        g_ = 1.0 - std::min (0.9, gcoeff);

        // (3) pickup = GHIDRA-VERIFIED: (PickUp/6 + 1/64) × N
        pickup_ = juce::jlimit (0, N_ - 1, (int) (((double) pr[pPickUp] / 6.0 + 1.0 / 64.0) * N_));

        // (4) r8 (pluck split) = int(Pick × 0.5 × N), clamped ≥ 1
        int r8 = juce::jmax (1, (int) std::trunc ((double) pr[pPick] * 0.5 * N_));

        // (5) Seed excitation (makes the pluck burst), then set output gains.
        seedExcitation (r8);                       // Shape selects raw↔smooth table
        recomputeGains();
        filt_ = 0.0;
        idxA_ = idxB_ = 0;
        active_ = true;
        sustain_ = true;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (! allowTailOff) { active_ = false; clearCurrentNote(); }
        else {
            // GHIDRA-VERIFIED (FUN_0001e630 @ 0x1e630): at note-off, g switches to the Release
            // param, and a LINEAR envelope starts at 1.0 with per-sample decrement.
            sustain_ = false;
            g_ = (double) params_[pRelease];
            int releaseSamples = (int) (sampleRate_ * 2.5 * (1.0 - (double) params_[pRelease]));
            releaseSamples = juce::jmax (256, releaseSamples);
            releaseEnv_ = 1.0;
            releaseDec_ = 1.0 / (double) releaseSamples;
        }
    }

    void controllerMoved (int, int) override {}
    void pitchWheelMoved (int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int n) override
    {
        if (! active_) return;
        auto* L = out.getWritePointer (0);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : L;

        const int    N  = N_;
        const double g  = g_;
        const double om = 1.0 - g;
        const int    pk = pickup_;
        for (int s = 0; s < n; ++s)
        {
            // (1) pickup read at offset -> comb filtering
            int pi = idxB_ + pk;  if (pi >= N) pi -= N;
            double pick = dlB_[pi];

            // (2) loop lowpass (g from Damper in sustain, Release param in release)
            filt_ = g * filt_ + om * (double) dlB_[idxB_];
            dlA_[idxA_] = -(float) filt_;

            // (3) bidirectional coupling (inverted)
            int ap = idxA_ + (N - 1);  if (ap >= N) ap -= N;
            dlB_[idxB_] = -dlA_[ap];

            if (--idxA_ < 0) idxA_ += N;
            if (++idxB_ >= N) idxB_ -= N;

            // (4) output: sustain = pick × gainL; release = pick × env × gainL
            double env = sustain_ ? 1.0 : releaseEnv_;
            L[start + s] += (float) (pick * env * gainL_);
            if (R != L) R[start + s] += (float) (pick * env * gainR_);
            if (! sustain_) {
                releaseEnv_ -= releaseDec_;
                if (releaseEnv_ <= 0.0) { active_ = false; clearCurrentNote(); return; }
            }
        }
    }
    // Called by the processor whenever a param changes (and on program change).
    void setParams (const float p[nParams])
    {
        std::copy (p, p + nParams, params_);
        recomputeGains();
    }
    void setSampleRate (double sr) { sampleRate_ = sr; }

private:
    double getSampleRate() const { return sampleRate_; }

    static int noteToN (int midi, double damper, double sr)
    {
        // binary 0x1e068: N = min( sr/freq + 1 , 9599 ).  Frequency is 12-TET with a
        // minute Damper-driven detune (≤ 2.8 cents, only when Damper < 0.5).
        double octave = (midi - 69) / 12.0;
        if (damper < 0.5)
            octave += (0.5 - damper) * (1.0 / 12.0) * (2.0 / 3.0);  // ≈ (0.5−Damper)/18
        double freq = 2.0 * 440.0 * std::pow (2.0, octave);  // RCA: original uses 2x musical freq (N=half); was 1x -> octave low + wrong g/pickup/pluck
        int N = (int) (sr / freq + 1.0);
        return juce::jmin (N, kMaxDelay);
    }

    void recomputeGains()                          // binary 0x2255c/0x226ac/0x1fb20
    {
        // [TUNE] a flat output gain does NOT match the original — the waveguide's internal
        // excitation amplitude / level calibration differs. Calibrate seedExcitation() and the
        // [TUNE] DSP coeffs iteratively via tools/ab_diff.py (orig ≈ +18 dB vs this baseline).
        const double pan = 0.5;
        const double vol = (double) params_[pVolume];
        const double kOutGain = g_tune.kOutGain;     // [TUNE] CMA-ES-fitted
        gainL_ = (1.0 - pan) * vol * level_ * kOutGain;
        gainR_ =        pan  * vol * level_ * kOutGain;
    }

    void seedExcitation (int r8)
    {
        // GHIDRA-VERIFIED (FUN_0001e068): reads excitation table at [voice+0x90]+0xf0.
        // Phase steps by (float)(1.0/N × 4096). The table value is multiplied by the
        // triangular window envelope, cast to float, then 2× and −0.1f (all float arithmetic).
        // For Shape=0 the table is flat 0.5 → deterministic ramp. For Shape>0 the table
        // contains the Shape-dependent wavetable waveform (see VB1ExcitationTables.h).
        const float* table = excTable_;
        if (! table) {
            // Fallback before first program change: flat 0.5 (= original Shape=0 default)
            for (int i = 0; i < r8; ++i)
                dlA_[i] = dlB_[i] = (float) (2.0 * 0.5 * ((1.0/r8) * i) - 0.1);
            for (int i = r8; i < N_; ++i)
                dlA_[i] = dlB_[i] = (float) (2.0 * 0.5 * ((1.0/juce::jmax(1,N_-1-r8)) * (N_-1-i)) - 0.1);
            return;
        }
        const double rise = 1.0 / r8;
        const double fall = 1.0 / juce::jmax (1, N_ - 1 - r8);
        const float step = (float) (invN_ * 4096.0);
        float phase = 0.0f;
        for (int i = 0; i < r8; ++i) {
            float tv = table[(int) phase & 0xfff];
            float f = (float) ((double) tv * rise * (double) i);
            dlA_[i] = dlB_[i] = (f + f) - 0.1f;
            phase += step;
        }
        for (int i = r8; i < N_; ++i) {
            float tv = table[(int) phase & 0xfff];
            float f = (float) ((double) tv * fall * (double) (N_ - 1 - i));
            dlA_[i] = dlB_[i] = (f + f) - 0.1f;
            phase += step;
        }
    }

    Excitation& exc_;
    float  params_[nParams]{ 0.1f, 0.75f, 0.333f, 0.98f, 0.0f, 0.8f };
    double sampleRate_ = 44100.0;
    double level_ = 1.0;
    int    note_  = 60;
    bool   active_ = false, sustain_ = true;

    double g_ = 0.98, filt_ = 0.0, invN_ = 0.0;
    double gainL_ = 0.0, gainR_ = 0.0;
    double releaseEnv_ = 0.0, releaseDec_ = 0.0;  // linear release envelope (FUN_0001e630)
    int    N_ = 256, pickup_ = 0, idxA_ = 0, idxB_ = 0;
    const float* excTable_ = nullptr;  // Shape-dependent excitation table (4096 floats)
    std::vector<float> dlA_, dlB_;
};

// Trivial sound type.
struct VaStringSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

} // namespace vb1
