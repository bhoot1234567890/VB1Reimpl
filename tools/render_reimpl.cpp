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
    bool shortMode = false;
    for (int a = 2; a < argc; ++a) { if (juce::String (argv[a]) == "short") shortMode = true;
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
            if (auto* v = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i))) v->setParams (pv);
        playNote (28, SR, SR / 2);   // E1, 1s sustain + 0.5s release — fast optimization eval
    }
    else
    {
    for (int prog = 0; prog < 16; ++prog)
    {
        float pv[vb1::nParams];
        for (int i = 0; i < vb1::nParams; ++i) pv[i] = vb1::presets()[prog].v[i];
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i))) v->setParams (pv);

        for (int ni = 0; ni < 3; ++ni)
        {
            midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, NOTES[ni], 0.8f), 0);
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
