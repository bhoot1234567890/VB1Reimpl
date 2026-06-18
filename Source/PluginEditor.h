// =============================================================================
// PluginEditor.h — Custom dark/amber UI for VB-1 reimpl
// FabFilter-style: gradient background, arc knobs, section labels
// =============================================================================
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "VB1LookAndFeel.h"

class VaStringReimplAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit VaStringReimplAudioProcessorEditor (VaStringReimplAudioProcessor&);
    ~VaStringReimplAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;

private:
    VaStringReimplAudioProcessor& proc;
    VB1LookAndFeel lnf;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    Knob knobs[6];
    juce::MidiKeyboardComponent keyboard;

    juce::ComboBox programBox;
    juce::Label    programLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VaStringReimplAudioProcessorEditor)
};
