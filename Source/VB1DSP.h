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
        { "Bassic Bass",  { 0.100f, 0.750f, 0.333f, 0.980f, 0.000f, 0.800f } },
        { "Sustain Bass", { 0.602f, 0.913f, 0.540f, 0.980f, 0.000f, 0.800f } },
        { "Round Bass",   { 0.352f, 0.957f, 1.000f, 0.980f, 0.000f, 0.800f } },
        { "Fretless",     { 1.000f, 0.000f, 1.000f, 0.980f, 0.000f, 0.800f } },
        { "Synthi Bass",  { 0.114f, 0.290f, 0.000f, 0.980f, 0.000f, 0.800f } },
        { "Clavinet",     { 0.625f, 0.304f, 0.000f, 0.980f, 1.000f, 0.800f } },
        { "DX Bass",      { 0.636f, 0.522f, 0.405f, 0.980f, 0.798f, 0.800f } },
        { "Hollow Bass",  { 0.716f, 0.391f, 0.349f, 0.980f, 0.244f, 0.800f } },
        { "Sequenz Bass", { 0.136f, 0.000f, 1.000f, 0.980f, 0.229f, 0.800f } },
        { "Warm Bass",    { 0.807f, 0.000f, 0.405f, 0.980f, 0.161f, 0.800f } },
        { "Slap Frets",   { 0.239f, 0.000f, 0.000f, 0.980f, 1.000f, 0.800f } },
        { "Buzz Bass",    { 1.000f, 0.551f, 0.209f, 0.980f, 0.419f, 0.800f } },
        { "Add Chorus",   { 0.670f, 0.000f, 0.451f, 0.980f, 0.833f, 0.800f } },
        { "Synth Bass 2", { 1.000f, 0.304f, 1.000f, 0.980f, 0.387f, 0.800f } },
        { "Dark Click",   { 0.886f, 0.188f, 0.386f, 0.980f, 0.119f, 0.800f } },
        { "Synth Bass 3", { 0.148f, 1.000f, 0.000f, 0.980f, 0.678f, 0.800f } },
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
        else                sustain_ = false;   // -> release loop [0x1de62]
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
        const double relS = g_tune.relS;       // [TUNE] per-SAMPLE release decay (CMA-ES-fitted)
        for (int s = 0; s < n; ++s)
        {
            // (1) pickup read at offset -> comb filtering
            int pi = idxB_ + pk;  if (pi >= N) pi -= N;
            double pick = dlB_[pi];

            // (2) loop lowpass (Release->g) + inverting termination
            filt_ = g * filt_ + om * (double) dlB_[idxB_];
            dlA_[idxA_] = -(float) filt_;

            // (3) bidirectional coupling (inverted)
            int ap = idxA_ + (N - 1);  if (ap >= N) ap -= N;
            dlB_[idxB_] = -dlA_[ap];

            if (--idxA_ < 0) idxA_ += N;
            if (++idxB_ >= N) idxB_ -= N;

            // (4) output (additive across voices); avoid double-add when mono (R==L)
            L[start + s] += (float) (pick * gainL_);
            if (R != L) R[start + s] += (float) (pick * gainR_);
            // release: decay per sample; cut voice when inaudible
            if (! sustain_) { gainL_ *= relS; gainR_ *= relS; if (gainL_ < 1e-5 && gainR_ < 1e-5) { active_ = false; clearCurrentNote(); return; } }
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
