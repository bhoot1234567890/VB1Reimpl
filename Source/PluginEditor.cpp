// =============================================================================
// PluginEditor.cpp — Custom dark/amber UI for VB-1 reimpl
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─── Constructor ─────────────────────────────────────────────────────────────

VaStringReimplAudioProcessorEditor::VaStringReimplAudioProcessorEditor (VaStringReimplAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    static const char* names[6] = { "Damper", "PickUp", "Pick", "Release", "Shape", "Volume" };
    static const char* ids  [6] = { "damper", "pickup", "pick", "release", "shape", "volume" };

    for (int i = 0; i < 6; ++i)
    {
        knobs[i].slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        knobs[i].slider.setNumDecimalPlacesToDisplay (2);
        knobs[i].slider.setLookAndFeel (&lnf);
        knobs[i].slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xFFC8C8CE));
        knobs[i].slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knobs[i].slider.setColour (juce::Slider::textBoxHighlightColourId,
                                   juce::Colour (0xFFE8A547).withAlpha (0.25f));
        knobs[i].attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                              (proc.getAPVTS(), ids[i], knobs[i].slider);

        knobs[i].label.setText (names[i], juce::dontSendNotification);
        knobs[i].label.setJustificationType (juce::Justification::centred);
        knobs[i].label.setLookAndFeel (&lnf);
        knobs[i].label.setFont (juce::Font (juce::FontOptions (12.0f)));

        addAndMakeVisible (knobs[i].slider);
        addAndMakeVisible (knobs[i].label);
    }

    programLabel.setText ("PROGRAM", juce::dontSendNotification);
    programLabel.setJustificationType (juce::Justification::centredLeft);
    programLabel.setLookAndFeel (&lnf);
    programLabel.setFont (juce::Font (juce::FontOptions (9.0f)));
    programLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF6A6A72));
    addAndMakeVisible (programLabel);

    juce::StringArray items;
    for (const auto& pr : vb1::presets()) items.add (pr.name);
    programBox.addItemList (items, 1);
    programBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
    programBox.setLookAndFeel (&lnf);
    programBox.onChange = [this] { proc.setCurrentProgram (programBox.getSelectedItemIndex()); };
    addAndMakeVisible (programBox);

    // On-screen keyboard (clickable piano at bottom — also responds to computer QWERTY)
    keyboard.setOctaveForMiddleC (4);
    keyboard.setLowestVisibleKey (0x30);   // C2
    keyboard.setKeyWidth (26.0f);
    keyboard.setWantsKeyboardFocus (true);
    setWantsKeyboardFocus (true);
    // Map QWERTY keys to notes (bottom row = white keys, top row = black keys)
    using KP = juce::KeyPress;
    keyboard.setKeyPressForNote (KP ('z'), 0);   keyboard.setKeyPressForNote (KP ('s'), 1);
    keyboard.setKeyPressForNote (KP ('x'), 2);   keyboard.setKeyPressForNote (KP ('d'), 3);
    keyboard.setKeyPressForNote (KP ('c'), 4);
    keyboard.setKeyPressForNote (KP ('v'), 5);   keyboard.setKeyPressForNote (KP ('g'), 6);
    keyboard.setKeyPressForNote (KP ('b'), 7);   keyboard.setKeyPressForNote (KP ('h'), 8);
    keyboard.setKeyPressForNote (KP ('n'), 9);   keyboard.setKeyPressForNote (KP ('j'), 10);
    keyboard.setKeyPressForNote (KP ('m'), 11);
    keyboard.setKeyPressForNote (KP ('q'), 12);  keyboard.setKeyPressForNote (KP ('2'), 13);
    keyboard.setKeyPressForNote (KP ('w'), 14);  keyboard.setKeyPressForNote (KP ('3'), 15);
    keyboard.setKeyPressForNote (KP ('e'), 16);
    keyboard.setKeyPressForNote (KP ('r'), 17);  keyboard.setKeyPressForNote (KP ('5'), 18);
    keyboard.setKeyPressForNote (KP ('t'), 19);  keyboard.setKeyPressForNote (KP ('6'), 20);
    keyboard.setKeyPressForNote (KP ('y'), 21);  keyboard.setKeyPressForNote (KP ('7'), 22);
    keyboard.setKeyPressForNote (KP ('u'), 23);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId,    juce::Colour (0xFF2A2A30));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId,    juce::Colour (0xFF15151A));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xFF38383E));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour (0xFFE8A547).withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,      juce::Colour (0xFFE8A547).withAlpha (0.45f));
    addAndMakeVisible (keyboard);

    setSize (560, 360);
}

VaStringReimplAudioProcessorEditor::~VaStringReimplAudioProcessorEditor()
{
    for (int i = 0; i < 6; ++i)
    {
        knobs[i].slider.setLookAndFeel (nullptr);
        knobs[i].label.setLookAndFeel (nullptr);
    }
    programBox.setLookAndFeel (nullptr);
    programLabel.setLookAndFeel (nullptr);
}

// ─── Layout ─────────────────────────────────────────────────────────────────

void VaStringReimplAudioProcessorEditor::resized()
{
    // Program selector (top-right)
    programLabel.setBounds (getWidth() - 240, 6, 80, 12);
    programBox  .setBounds (getWidth() - 240, 18, 230, 22);

    // Knob grid: 3 groups of 2 — [Damper Shape] [PickUp Pick] [Release Volume]
    const int ks = 72;   // knob size
    const int kg = 14;   // gap between knobs within a group
    const int gg = 36;   // gap between groups
    const int tw = 6 * ks + 4 * kg + 2 * gg;  // total width

    int startX = juce::jmax (12, (getWidth() - tw) / 2);
    int startY = 60;

    for (int i = 0; i < 6; ++i)
    {
        int group = i / 2;   // 0, 0, 1, 1, 2, 2
        int slot  = i % 2;   // 0, 1, 0, 1, 0, 1

        int x = startX + group * (2 * ks + kg + gg) + slot * (ks + kg);
        knobs[i].slider.setBounds (x, startY, ks, ks);
        knobs[i].label.setBounds  (x - 4, startY + ks + 4, ks + 8, 16);
    }

    keyboard.setBounds (12, getHeight() - 92, getWidth() - 24, 80);
}

// ─── Computer keyboard → MIDI ─────────────────────────────────────────────

static const struct { int key; int note; } keyMap[] = {
    { 'a', 48 }, { 'w', 49 }, { 's', 50 }, { 'e', 51 }, { 'd', 52 },
    { 'f', 53 }, { 't', 54 }, { 'g', 55 }, { 'y', 56 }, { 'h', 57 },
    { 'u', 58 }, { 'j', 59 }, { 'k', 60 }, { 'o', 61 }, { 'l', 62 },
    { 'p', 63 }, { ';', 64 },
    { 'z', 36 }, { 'x', 37 }, { 'c', 38 }, { 'v', 39 }, { 'b', 40 },
    { 'n', 41 }, { 'm', 42 },
};

bool VaStringReimplAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    auto chr = key.getTextCharacter();
    for (const auto& m : keyMap) {
        if (chr == m.key) {
            proc.getKeyboardState().noteOn (1, m.note, 0.8f);
            return true;
        }
    }
    return false;
}

bool VaStringReimplAudioProcessorEditor::keyStateChanged (bool)
{
    // Release notes when keys are released
    for (const auto& m : keyMap) {
        if (! juce::KeyPress::isKeyCurrentlyDown (m.key)) {
            // Only turn off if the key is actually up (state changed)
            if (proc.getKeyboardState().isNoteOn (1, m.note)) {
                proc.getKeyboardState().noteOff (1, m.note, 0.0f);
            }
        }
    }
    return true;
}

// ─── Paint ───────────────────────────────────────────────────────────────────

void VaStringReimplAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto w = bounds.getWidth();
    auto h = bounds.getHeight();

    // ─── Background: multi-stop gradient ─────────────────────────────────
    juce::ColourGradient bg (
        juce::Colour (0xFF202028), 0, 0,
        juce::Colour (0xFF0A0A0E), 0, h, false);
    bg.addColour (0.3, juce::Colour (0xFF181820));
    bg.addColour (0.7, juce::Colour (0xFF0F0F14));
    g.setGradientFill (bg);
    g.fillAll();

    // ─── Top glow bar (amber accent line) ──────────────────────────────────
    juce::ColourGradient glow (
        juce::Colour (0xFFE8A547).withAlpha (0.25f), 0, 0,
        juce::Colour (0xFFE8A547).withAlpha (0.0f),  0, 4, false);
    g.setGradientFill (glow);
    g.fillRect (juce::Rectangle<float> (0, 0, w, 4));

    // ─── Glass-edge sheen ──────────────────────────────────────────────────
    juce::ColourGradient sheen (
        juce::Colours::white.withAlpha (0.03f), 0, 0,
        juce::Colours::transparentWhite, 0, 45, false);
    g.setGradientFill (sheen);
    g.fillRect (juce::Rectangle<float> (0, 0, w, 45));

    // ─── Vignette (darker corners) ─────────────────────────────────────────
    juce::ColourGradient vig (
        juce::Colours::transparentBlack, w * 0.3f, h * 0.3f,
        juce::Colours::black.withAlpha (0.25f), 0, 0, true);
    g.setGradientFill (vig);
    g.fillAll();

    // ─── Outer border ──────────────────────────────────────────────────────
    g.setColour (juce::Colour (0xFF383840));
    g.drawRect (bounds.reduced (0.5f), 1.0f);

    // ─── Title with glow ───────────────────────────────────────────────────
    g.setColour (juce::Colour (0xFFE8A547).withAlpha (0.15f));
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText ("VB-1", 15, 9, 56, 20, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xFFE8A547));
    g.drawText ("VB-1", 14, 8, 56, 20, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xFF5A5A62));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("reimpl", 14, 27, 56, 12, juce::Justification::centredLeft);

    // ─── Section labels + dividers ─────────────────────────────────────────
    const int ks = 72, kg = 14, gg = 36;
    const int tw = 6 * ks + 4 * kg + 2 * gg;
    int startX = juce::jmax (12, (getWidth() - tw) / 2);
    int labelY = 60 + ks + 24;
    const char* sections[] = { "TONE", "GEOMETRY", "ENVELOPE" };

    for (int grp = 0; grp < 3; ++grp)
    {
        int groupStart = startX + grp * (2 * ks + kg + gg);
        int groupWidth = 2 * ks + kg;

        g.setColour (juce::Colour (0xFF48484E));
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        g.drawText (sections[grp], groupStart, labelY, groupWidth, 12, juce::Justification::centred);

        if (grp < 2)
        {
            int sepX = groupStart + groupWidth + gg / 2;
            // Gradient divider (fades at top/bottom)
            juce::ColourGradient dg (
                juce::Colour (0xFF28282E).withAlpha (0.0f), (float) sepX, 60.0f,
                juce::Colour (0xFF28282E),                  (float) sepX, 66.0f, false);
            dg.addColour (0.5, juce::Colour (0xFF2C2C34));
            dg.addColour (1.0, juce::Colour (0xFF28282E).withAlpha (0.0f));
            g.setGradientFill (dg);
            g.drawVerticalLine (sepX, 66.0f, (float) (60 + ks + 4));
        }
    }
}
