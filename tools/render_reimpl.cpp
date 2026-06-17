// render_reimpl.cpp — headless render of the REIMPL VB-1 DSP (same MIDI sequence as
// vst2_render.c: 16 programs x notes 28/40/52, 0.5s sustain + 0.25s release) -> WAV.
// Built as a JUCE console app so it reuses the shipped VB1DSP.h voice exactly.
#include <JuceHeader.h>
#include "VB1DSP.h"
#include <juce_audio_formats/juce_audio_formats.h>

// Usage: VB1Render <out.wav> [coeffs.txt] [short]   (coeffs.txt = 6 floats; "short" = single note for fast opt)
int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "/tmp/reimpl.wav";
    bool shortMode = false, persampleMode = false;
    for (int a = 2; a < argc; ++a) { if (juce::String (argv[a]) == "short") shortMode = true;
        else if (juce::String (argv[a]) == "persample") persampleMode = true;
        else { // coeffs file: 6 floats -> g_tune
            auto ts = juce::File (argv[a]).loadFileAsString(); auto t = juce::StringArray::fromTokens (ts, true);
            double c[6] = {1.3,1.0,1.0,1.0,1.0,0.997};
            for (int i = 0; i < 6 && i < t.size(); ++i) c[i] = t[i].getDoubleValue();
            vb1::g_tune = { c[0], c[1], c[2], c[3], c[4], c[5] };
        }
    }
    const int SR = 44100;
    const int sustain = SR / 2, release = SR / 4;
    const int NOTES[3] = { 28, 40, 52 };
    if (persampleMode)
    {
        // Program 0, note 40, velocity 100, SUSTAIN ONLY (no release). Renders one sample at a
        // time through a single VaStringVoice (bypassing the synth for direct state access) and
        // dumps per-sample voice state to /tmp/reimpl_persample.bin for byte-comparison against
        // the original. Binary layout: int32 N, int32 nsamp, then per sample:
        //   float out | double filt | int32 idxA | int32 idxB | int32 pickup |
        //   double g | double gainL | double gainR | float dlA[N] | float dlB[N]
        using namespace vb1;
        Excitation excPs;
        VaStringVoice voice (excPs);
        voice.setSampleRate ((double) SR);
        float pv[nParams]; for (int i = 0; i < nParams; ++i) pv[i] = presets()[0].v[i];
        voice.setParams (pv);
        voice.setExcitationTable (excitationTable (0));
        const float vel = juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100).getFloatVelocity();
        voice.startNote (40, vel, nullptr, 0);
        const int NSAMP = 64, N = voice.getN();
        FILE* f = fopen ("/tmp/reimpl_persample.bin", "wb");
        int32_t hdr[2] = { N, NSAMP }; fwrite (hdr, 4, 2, f);
        juce::AudioBuffer<float> b1 (2, 1);
        for (int s = 0; s < NSAMP; ++s)
        {
            b1.clear(); voice.renderNextBlock (b1, 0, 1);
            float o = b1.getReadPointer (0)[0];
            double flt = voice.getFilt(); double g = voice.getG();
            double gL = voice.getGainL(), gR = voice.getGainR();
            int32_t a = voice.getIdxA(), bb = voice.getIdxB(), pk = voice.getPickup();
            fwrite (&o, 4, 1, f); fwrite (&flt, 8, 1, f);
            fwrite (&a, 4, 1, f); fwrite (&bb, 4, 1, f); fwrite (&pk, 4, 1, f);
            fwrite (&g, 8, 1, f); fwrite (&gL, 8, 1, f); fwrite (&gR, 8, 1, f);
            fwrite (voice.dlAData(), 4, N, f); fwrite (voice.dlBData(), 4, N, f);
            fprintf (stderr, "s=%2d out=%+.6e filt=%+.6e idxA=%d idxB=%d\n", s, o, flt, a, bb);
        }
        fclose (f);
        fprintf (stderr, "wrote /tmp/reimpl_persample.bin (N=%d nsamp=%d vel=%.7f gainL=%.7f g=%.7f pickup=%d)\n",
                 N, NSAMP, vel, voice.getGainL(), voice.getG(), voice.getPickup());
        return 0;
    }

    vb1::Excitation exc;
    juce::Synthesiser synth;
    for (int i = 0; i < vb1::kNumVoices; ++i) synth.addVoice (new vb1::VaStringVoice (exc));
    synth.addSound (new vb1::VaStringSound());
    synth.setCurrentPlaybackSampleRate (SR);

    std::vector<float> gL, gR;
    juce::AudioBuffer<float> buf (2, std::max (sustain, release));
    juce::MidiBuffer midi;

    auto runPhase = [&] (int n) {
        buf.clear();
        synth.renderNextBlock (buf, midi, 0, n);
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int i = 0; i < n; ++i) { gL.push_back (L[i]); gR.push_back (R[i]); }
    };

    auto playNote = [&] (int note, int sus, int rel) {
        midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);  runPhase (sus);
        midi.clear(); midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);       runPhase (rel);
    };
    if (shortMode)
    {
        float pv[vb1::nParams]; for (int i = 0; i < vb1::nParams; ++i) pv[i] = vb1::presets()[0].v[i];
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i))) {
                v->setParams (pv);
                v->setExcitationTable (vb1::excitationTable (0));
            }
        midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 0); runPhase (4095);   // pure seeded (vel=100 matches original)
        auto dumpVoice = [] (vb1::VaStringVoice* v, const char* pathA, const char* pathB) {
            int N = v->getN();
            FILE* fa = fopen (pathA, "wb"); fwrite (v->dlAData(), 4, N, fa); fclose (fa);
            FILE* fb = fopen (pathB, "wb"); fwrite (v->dlBData(), 4, N, fb); fclose (fb);
            float pk=0; for(int i=0;i<N;++i){float a=std::abs(v->dlBData()[i]); if(a>pk)pk=a;}
            fprintf (stderr, "  dump %s: N=%d dlB peak=%.4f\n", pathB, N, pk);
        };
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i)))
                if (v->getN() == 264) { dumpVoice (v, "/tmp/reimpl_dlA_s1.bin", "/tmp/reimpl_dlB_s1.bin"); break; }
        runPhase (4095);   // evolve to ~4096 total
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i)))
                if (v->getN() == 264) { dumpVoice (v, "/tmp/reimpl_dlA.bin", "/tmp/reimpl_dlB.bin"); break; }
    }
    else
    {
    for (int prog = 0; prog < 16; ++prog)
    {
        float pv[vb1::nParams];
        for (int i = 0; i < vb1::nParams; ++i) pv[i] = vb1::presets()[prog].v[i];
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i))) {
                v->setParams (pv);
                v->setExcitationTable (vb1::excitationTable (prog));
            }

        for (int ni = 0; ni < 3; ++ni)
        {
            midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, NOTES[ni], (juce::uint8) 100), 0);
            runPhase (sustain);
            midi.clear(); midi.addEvent (juce::MidiMessage::noteOff (1, NOTES[ni]), 0);
            runPhase (release);
        }
    }
    }
    midi.clear(); runPhase (SR / 4);   // final tail

    auto outFile = juce::File (out); outFile.deleteFile();   // ensure clean truncate

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (
        new juce::FileOutputStream (juce::File (out)), (double) SR, 2u, 32, {}, 0));
    juce::AudioBuffer<float> joined (2, (int) gL.size());
    joined.copyFrom (0, 0, gL.data(), (int) gL.size());
    joined.copyFrom (1, 0, gR.data(), (int) gR.size());
    w->writeFromAudioSampleBuffer (joined, 0, joined.getNumSamples());

    double peak = 0; for (float x : gL) { float a = x < 0 ? -x : x; if (a > peak) peak = a; }
    std::fprintf (stderr, "wrote %s (%zu frames, %zus) peakL=%.4f\n", out, gL.size(), gL.size()/SR, peak);
    return 0;
}
