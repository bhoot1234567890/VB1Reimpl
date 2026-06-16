// =============================================================================
// PluginProcessor.h — VaString reimpl AudioProcessor: APVTS (6 params, host
// order), 8-voice Synthesiser, 16 factory programs. See VB1_RE_Spec.md.
// =============================================================================
#pragma once
#include <JuceHeader.h>
#include "VB1DSP.h"

class VaStringReimplAudioProcessor  : public juce::AudioProcessor,
                                      public juce::AudioProcessorValueTreeState::Listener
{
public:
    VaStringReimplAudioProcessor();
    ~VaStringReimplAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // --- 16 programs (binary numPrograms) ---
    int getNumPrograms() override { return (int) vb1::presets().size(); }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int idx) override;
    const juce::String getProgramName (int idx) override { return vb1::presets()[idx].name; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void pushParamsToVoices();
    void parameterChanged (const juce::String&, float) override;

    juce::AudioProcessorValueTreeState apvts;
    vb1::Excitation excitation;
    juce::Synthesiser synth;
    int currentProgram_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VaStringReimplAudioProcessor)
};
