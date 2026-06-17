// =============================================================================
// PluginProcessor.cpp
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
std::unique_ptr<juce::AudioParameterFloat> p (const juce::String& id, const juce::String& name,
                                               float def, const juce::String& label = {})
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
             juce::NormalisableRange<float> (0.0f, 1.0f), def, label);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout VaStringReimplAudioProcessor::makeLayout()
{
    using namespace vb1;
    // host param order: Damper, PickUp, Pick, Release, Shape, Volume
    return { p ("damper",  "Damper",  presets()[0].v[pDamper]),
             p ("pickup",  "PickUp",  presets()[0].v[pPickUp]),
             p ("pick",    "Pick",    presets()[0].v[pPick]),
             p ("release", "Release", presets()[0].v[pRelease]),
             p ("shape",   "Shape",   presets()[0].v[pShape]),
             p ("volume",  "Volume",  presets()[0].v[pVolume], "dB") };
}

VaStringReimplAudioProcessor::VaStringReimplAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", makeLayout())
{
    for (int i = 0; i < vb1::kNumVoices; ++i)
        synth.addVoice (new vb1::VaStringVoice (excitation));
    synth.addSound (new vb1::VaStringSound());
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i)))
            v->setExcitationTable (vb1::excitationTable (0));

    for (const auto id : { "damper", "pickup", "pick", "release", "shape", "volume" })
        apvts.addParameterListener (id, this);
}

void VaStringReimplAudioProcessor::parameterChanged (const juce::String&, float)
{
    pushParamsToVoices();   // any knob/program change re-applies all 6 params to voices
}

void VaStringReimplAudioProcessor::pushParamsToVoices()
{
    float pv[vb1::nParams] = { apvts.getRawParameterValue ("damper")->load(),
                               apvts.getRawParameterValue ("pickup")->load(),
                               apvts.getRawParameterValue ("pick")->load(),
                               apvts.getRawParameterValue ("release")->load(),
                               apvts.getRawParameterValue ("shape")->load(),
                               apvts.getRawParameterValue ("volume")->load() };
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i)))
            voice->setParams (pv);
}

void VaStringReimplAudioProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i)))
            voice->setSampleRate (sampleRate);
    pushParamsToVoices();
}

bool VaStringReimplAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void VaStringReimplAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                 juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}

void VaStringReimplAudioProcessor::setCurrentProgram (int idx)
{
    currentProgram_ = juce::jlimit (0, getNumPrograms() - 1, idx);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<vb1::VaStringVoice*> (synth.getVoice (i)))
            v->setExcitationTable (vb1::excitationTable (currentProgram_));
    const auto& pr = vb1::presets()[currentProgram_];
    const juce::StringArray ids { "damper", "pickup", "pick", "release", "shape", "volume" };
    for (int i = 0; i < vb1::nParams; ++i)
        if (auto* param = apvts.getParameter (ids[i]))
            param->setValueNotifyingHost (pr.v[i]);
}

void VaStringReimplAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, dest);
}

void VaStringReimplAudioProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VaStringReimplAudioProcessor::createEditor()
{
    return new VaStringReimplAudioProcessorEditor (*this);
}

// This creates the plugin instances.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VaStringReimplAudioProcessor();
}
