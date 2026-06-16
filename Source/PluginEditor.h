// =============================================================================
// PluginEditor.h — fresh GUI: 6 knobs (host-order params) + program selector.
// Deliberately NOT a reverse-engine of the Carbon Vb1sxEditor.
// =============================================================================
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class VaStringReimplAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit VaStringReimplAudioProcessorEditor (VaStringReimplAudioProcessor&);
    ~VaStringReimplAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void layoutKnob (juce::Slider&, juce::Label&, int x);

    VaStringReimplAudioProcessor& proc;

    struct Knob { juce::Slider slider; juce::Label label;
                  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach; };
    Knob knobs[6];

    juce::ComboBox programBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VaStringReimplAudioProcessorEditor)
};
