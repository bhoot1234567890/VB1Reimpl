// =============================================================================
// PluginEditor.cpp
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"

VaStringReimplAudioProcessorEditor::VaStringReimplAudioProcessorEditor (VaStringReimplAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    static const char* names[6] = { "Damper", "PickUp", "Pick", "Release", "Shape", "Volume" };
    static const char* ids[6]   = { "damper", "pickup", "pick", "release", "shape", "volume" };

    for (int i = 0; i < 6; ++i)
    {
        knobs[i].slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
        knobs[i].label.setText (names[i], juce::dontSendNotification);
        knobs[i].label.setJustificationType (juce::Justification::centred);
        knobs[i].attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                              (proc.getAPVTS(), ids[i], knobs[i].slider);
        addAndMakeVisible (knobs[i].slider);
        addAndMakeVisible (knobs[i].label);
    }

    // Programs are not an APVTS param — drive them via setCurrentProgram().
    programBox.addItemList ([&]
    {
        juce::StringArray items;
        for (const auto& pr : vb1::presets()) items.add (pr.name);
        return items;
    }(), 1);
    programBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
    programBox.onChange = [this]
    {
        proc.setCurrentProgram (programBox.getSelectedItemIndex());
    };
    addAndMakeVisible (programBox);

    setSize (520, 220);
}

void VaStringReimplAudioProcessorEditor::layoutKnob (juce::Slider& s, juce::Label& l, int x)
{
    l .setBounds (x,        70, 80, 18);
    s .setBounds (x + 10,   18, 60, 60);
}

void VaStringReimplAudioProcessorEditor::resized()
{
    programBox.setBounds (12, 12, 200, 22);
    for (int i = 0; i < 6; ++i)
        layoutKnob (knobs[i].slider, knobs[i].label, 12 + i * 84);
}

void VaStringReimplAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161c));
    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawText ("VB-1 (reimpl)", getLocalBounds().removeFromTop (16),
                juce::Justification::centred, true);
}
