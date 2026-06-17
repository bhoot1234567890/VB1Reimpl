// =============================================================================
// VB-1 DSP Reference — reverse-engineered from vb1.vst (x86_64 slice)
// Digital waveguide string, 8-voice polyphony. Annotated, re-code-ready.
// Field offsets are the verified VaStringVoice (0x128 B) / VaString layout.
// Sections marked [CONFIRM] are decoded from the binary; [TUNE] need A/B fit.
// =============================================================================

#include <cmath>
#include <cstdint>
#include <vector>
#include <array>

static constexpr int   kMaxVoices = 8;
static constexpr int   kMaxDelay  = 9599;          // N clamp (binary: 0x257f)
static constexpr int   kExcLen    = 16384;         // 0x4000 excitation/LFO table
static constexpr double kSr       = 44100.0;       // runtime; host can change

// ---- Param indices (host order, from AEffect numParams=6) ----
enum { pDamper = 0, pPickUp, pPick, pRelease, pShape, pVolume };

// =============================================================================
// Excitation tables (binary 0x2287a, generated once, guarded by global flag).
// Classic Karplus-Strong pluck: a noise burst, plus a one-pole-smoothed copy.
// Two 16384-float globals at 0x74a20 (raw) and 0x84a20 (integrated, coeff 0.75).
// =============================================================================
struct Excitation {
    std::array<float, kExcLen> raw{};        // white noise in [-1,1)
    std::array<float, kExcLen> smooth{};     // one-pole integrated noise
    void generate() {
        float state = 0.f;
        for (int i = 0; i < kExcLen; ++i) {
            float n = (float)((double)rand() / RAND_MAX * 2.0 - 1.0); // [0x39480] scale, -1.0
            raw[i] = n;
            n *= 1.0f;                                   // [0x39488] scale (≈1.0)  [TUNE]
            state = 0.75f * state + n;                   // [0x39350] = 0.75 leaky integrator
            smooth[i] = state;
        }
    }
};

// =============================================================================
// One VaStringVoice (binary 0x128 bytes, vtable 0x611b0).
// render() is the decoded loop at 0x1dd4f (sustain) / 0x1de62 (release).
// =============================================================================
struct Voice {
    // output stage
    double volume   = 0.8;   // +0x08  (Volume param)
    double pan      = 0.5;   // +0x10
    double panScale = 1.0;   // +0x18
    double gainL    = 0.0;   // +0x30  (computed)
    double gainR    = 0.0;   // +0x38  (computed)
    double panWidth = 0.75;  // +0x40  (chorus modulates this via 0x22750)
    double panK     = 1.0;   // +0x48  (pan-law constant; written by chorus LFO)

    // waveguide
    double g        = 0.98;  // +0xd0  = Release param  [CONFIRM]
    double filt     = 0.0;   // +0xd8  one-pole loop-filter state
    double pick     = 0.0;   // +0xe0  = Pick*0.5  (excitation split point r8)
    double pickOff  = 0.0;   // +0xe8  = f(PickUp) -> pickup read offset +0x104
    double invN     = 0.0;   // +0xf8  = 1/N

    int    N        = 256;   // +0x100 delay length (pitch period) [CONFIRM]
    int    pickup   = 0;     // +0x104 pickup read offset  = PickUp param
    int    idxA     = 0;     // +0x108 DL-A circular index
    int    idxB     = 0;     // +0x10c DL-B circular index

    std::vector<float> dlA;  // +0x110 traveling wave A  (size ~kMaxDelay)
    std::vector<float> dlB;  // +0x118 traveling wave B

    bool   sustain  = true;  // +0xc4  (0 => release loop)
    bool   active   = false; // +0xc5

    Voice() : dlA(kMaxDelay + 8, 0.f), dlB(kMaxDelay + 8, 0.f) {}

    // ---- Pan + volume -> gainL/gainR (binary 0x2255c/0x225fa/0x226ac/0x1fb20) ----
    // Constant-power-ish law with asymmetric widening when panWidth>~1e-5 & pan>0.5.
    void recomputeGains() {                                  // [CONFIRM structure, TUNE law]
        double other = 1.0 - pan;                            // base for the far channel
        if (panWidth > 1e-5 && pan > 0.5) {                  // [0x39430] threshold, [0x392a8]=0.5
            double d = (pan - 1.0);                          // negated below
            d = -d;
            other = pan + d * panWidth * panK;               // widening term
            other = 1.0 - other;
        }
        if (/*panScale flag*/ true) { other *= panScale; pan *= panScale; }
        gainL = other * volume;
        gainR = pan    * volume;
    }

    // ---- Per-sample render (decoded loop 0x1dd4f) ----
    void render(float* outL, float* outR, int n) {
        if (!active) return;
        const int  Nn     = N;
        const double gg   = g;
        const double oneM = 1.0 - g;
        const int  pk     = pickup;
        for (int s = 0; s < n; ++s) {
            // (1) pickup read at offset -> comb filtering (the phasey tone)
            int pickIdx = idxB + pk; if (pickIdx >= Nn) pickIdx -= Nn;
            double pickupSamp = dlB[pickIdx];

            // (2) loop lowpass (Release-controlled) + inverting termination
            filt = gg * filt + oneM * (double)dlB[idxB];
            dlA[idxA] = -(float)filt;

            // (3) bidirectional coupling: A's previous feeds B (inverted)
            int aPrev = idxA + (Nn - 1); if (aPrev >= Nn) aPrev -= Nn;
            dlB[idxB] = -dlA[aPrev];

            // advance indices (mod N)
            if (--idxA < 0) idxA += Nn;
            if (++idxB >= Nn) idxB -= Nn;

            // (4) output (additive across voices)
            outL[s] += (float)(pickupSamp * gainL);
            outR[s] += (float)(pickupSamp * gainR);
        }
        // release path (sustain==false) is the near-identical loop at 0x1de62
        // with different coefficients selected by [+0xc4].           [TUNE coeffs]
    }
};

// =============================================================================
// Note -> N (delay length).  Binary 0x1e068.
//   freq(note) via 0x21f12 (octave = (note + offset) * 1/12 [0x39378]); Damper
//   slightly retunes.  N = min( sampleRate/freq + 1 , 9599 ).            [CONFIRM]
// =============================================================================
inline int noteToN(int midiNote, double damper, double sampleRate) {
    double octaves = (midiNote + 0.0 /*offset [0x39160]*/ + 0.0 /*+0x20*2*/) / 12.0;
    if ((1.0 - damper) > 0.0 /*[0x3909c]*/)
        octaves += (1.0 - damper - 0.5) * (1.0 / 12.0) * 1.0 /*[0x39380]*/;  // Damper detune [TUNE]
    double freq = /*0x21f12(octaves)*/ 440.0 * std::pow(2.0, (midiNote - 69) / 12.0); // [TUNE base]
    double n = sampleRate / freq + 1.0;
    int N = (int)n + 1;
    return (N < kMaxDelay) ? N : kMaxDelay;            // cmovl vs 0x2580/0x257f
}

// =============================================================================
// Note-on: seed excitation + apply current-program params (binary 0x1e5c6/0x1e630
// + voice note-on via Voices[vtable+0x88]).
//   g (+0xd0) = Release;  N (+0x100) = noteToN;  pickup (+0x104) = PickUp*N;
//   gains *= Volume;  DL_A/DL_B seeded from the Karplus-Strong noise table
//   windowed by Pick position.                                            [TUNE windowing]
// =============================================================================
struct Program {                                  // 0x70-byte program struct
    float damper, pickup, pick, release, shape, volume; // slots 0..5
    char name[64];
};

inline void noteOn(Voice& v, const Program& p, int midiNote,
                   const Excitation& exc, double sampleRate) {
    v.sustain = true;                             // +0xc4 = 1
    v.N       = noteToN(midiNote, p.damper, sampleRate);   // +0x100  [CONFIRM]
    v.invN    = 1.0 / v.N;                        // +0xf8
    // g (+0xd0): live param path sets g=Release (0x1e630); note-on (0x1e068)
    // refines it as 1-coeff(Damper,Release,N). Use Release as the base. [TUNE coeff]
    v.g       = p.release;
    v.volume  = p.volume;                         // +0x08
    v.recomputeGains();

    // PickUp (param 1) -> pickup read offset (+0x104).                     [CONFIRM]
    // +0xe8 = PickUp*kPuA + kPuB  ([0x393a0]/[0x393a8]); +0x104 = trunc(+0xe8 * N)
    v.pickOff = p.pickup;                         // [TUNE: affine coeffs]
    v.pickup  = (int)(v.pickOff * v.N);

    // Pick (param 2) -> PLUCK POSITION: splits N into two segments.       [CONFIRM]
    // r8 = trunc(Pick * 0.5 * N)  ([0x392a8]=0.5), clamped >= 1.
    int r8 = (int)std::trunc(p.pick * 0.5 * v.N); if (r8 <= 0) r8 = 1;
    v.pick = r8;

    // Seed DL_A & DL_B identically with a triangular-windowed, fractionally-
    // interpolated read of the noise table (binary loops 0x1e27f / 0x1e2d5).
    // Segment 1 [0,r8):  rising edge  (gain ~ i / r8).
    // Segment 2 [r8,N):  falling edge (gain ~ (r8-(i-r8)) / (N-1-r8)).
    // Exact amplitude scaling/offset ([0x393b0], the *2/-bias) = [TUNE].
    double step  = (1.0 / v.N);                   // fractional read increment
    double rise  = 1.0 / r8;
    double fall  = 1.0 / (v.N - 1 - r8);
    float acc = 0.f;
    for (int i = 0; i < r8; ++i) {
        int idx = (int)acc & (kExcLen - 1);       // read noise table (fractional)
        float e  = (float)(exc.raw[idx] * rise * i);
        v.dlA[i] = v.dlB[i] = e;
        acc += (float)step;
    }
    for (int i = r8; i < v.N; ++i) {
        int idx = (int)acc & (kExcLen - 1);
        float e  = (float)(exc.raw[idx] * fall * (r8 - (i - r8)));
        v.dlA[i] = v.dlB[i] = e;
        acc += (float)step;
    }
    v.filt  = 0.0;                                // +0xd8 reset
    v.idxA  = v.idxB = 0;                         // +0x108/+0x10c reset
    v.active = true;
}

// =============================================================================
// Chorus / auto-pan (binary 0x22750). Enabled by "Add Chorus"-style preset /
// [+0xc8] flag.  Reads the excitation table as an LFO with ping-pong index,
// writes into panWidth(+0x40)/panK(+0x48), then recomputes gains.
// =============================================================================
inline void chorusTick(Voice& v, const Excitation& exc, int& phase, bool& dir) {
    if (!dir) { v.panK = exc.raw[phase]; }
    else      { v.panK = exc.raw[kExcLen - 1 - phase]; }   // ping-pong (0x3fff - idx)
    if (--phase < 0) { phase = kExcLen - 1; dir = !dir; }  // +0x74 toggles
    v.recomputeGains();
}

// =============================================================================
// Polyphony (VaStringVoices, 8 voices) — processReplacing mixes all active
// voices additively into the stereo output (binary 0x2161a).
// =============================================================================
struct VB1 {
    std::array<Voice, kMaxVoices> voices;
    Excitation exc;
    VB1() { exc.generate(); }
    void process(float* outL, float* outR, int n) {
        for (auto& v : voices) v.render(outL, outR, n);
    }
};

/* =============================================================================
   PARAMETER -> DSP MAP (consolidated)
     [0] Damper  -> noteToN() detune + g coupling coeff      [0x1e068]  [CONFIRM]
     [1] PickUp  -> pickup read offset  (+0x104)             [0x1e1d5]  [CONFIRM]
     [2] Pick    -> PLUCK POSITION: splits N into r8,N-r8;   [0x1e208]  [CONFIRM]
                    triangular-windowed fractional-noise seed [0x1e27f]
     [3] Release -> loop lowpass g  (+0xd0)                  [0x1e630]  [CONFIRM]
     [4] Shape   -> target NOT in note-on/render (loop is linear) -> A/B sweep  [TBD]
     [5] Volume  -> output gain  (+0x08 -> gainL/R)          [0x1e5c6]  [CONFIRM]

   STILL TO NAIL BY DYNAMIC A/B (Phase 4) — coeff values, not architecture:
     - Shape's target (only true unknown; sweep Shape, diff output)
     - exact g coupling coeff (Damper+Release+N) and release-path coeffs (0x1de62)
     - exact pan-law asymmetry + Damper detune constants
     - excitation window amplitude scaling ([0x393b0], the *2/-bias)
   ============================================================================= */
