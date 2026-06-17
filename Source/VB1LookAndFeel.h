// =============================================================================
// VB1LookAndFeel.h — Custom FabFilter-style LookAndFeel for VB-1 reimpl
// Dark charcoal + warm amber/copper accents. All vector-drawn, no image assets.
// =============================================================================
#pragma once
#include <JuceHeader.h>

class VB1LookAndFeel : public juce::LookAndFeel_V4
{
public:
    VB1LookAndFeel()
    {
        // Slider
        setColour (juce::Slider::textBoxTextColourId,        juce::Colour (0xFFC8C8CE));
        setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,   juce::Colour (0xFFE8A547).withAlpha (0.25f));
        setColour (juce::Slider::textBoxBackgroundColourId,  juce::Colours::transparentBlack);

        // ComboBox
        setColour (juce::ComboBox::backgroundColourId,       juce::Colour (0xFF222228));
        setColour (juce::ComboBox::outlineColourId,          juce::Colour (0xFF38383E));
        setColour (juce::ComboBox::textColourId,             juce::Colour (0xFFD0D0D6));
        setColour (juce::ComboBox::arrowColourId,            juce::Colour (0xFFE8A547));
        setColour (juce::ComboBox::buttonColourId,           juce::Colour (0xFF2C2C32));

        // Popup menu
        setColour (juce::PopupMenu::backgroundColourId,      juce::Colour (0xFF1C1C22));
        setColour (juce::PopupMenu::textColourId,            juce::Colour (0xFFD0D0D6));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xFFE8A547).withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xFFE8A547));

        // Label
        setColour (juce::Label::textColourId,                juce::Colour (0xFF8A8A92));
    }

    // ─── Knob ───────────────────────────────────────────────────────────────

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        auto bounds   = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
        auto centre   = bounds.getCentre();
        auto radius   = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.42f;
        auto angle    = startAngle + pos * (endAngle - startAngle);
        auto stroke   = juce::jmax (2.5f, radius * 0.14f);

        // --- Track ring (dark background arc) ---
        juce::Path trackArc;
        trackArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xFF26262C));
        g.strokePath (trackArc, { stroke, juce::PathStrokeType::curved, juce::PathStrokeType::butt });

        // --- Value ring (amber/copper gradient arc) ---
        if (pos > 0.001f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);

            juce::ColourGradient arcGrad (
                juce::Colour (0xFFC2803C),
                centre.x - radius, centre.y - radius * 0.3f,
                juce::Colour (0xFFE8A547),
                centre.x + radius, centre.y + radius * 0.3f, false);
            g.setGradientFill (arcGrad);
            g.strokePath (valueArc, { stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // --- Knob body (radial gradient sphere) ---
        auto bodyR = radius * 0.72f;
        juce::ColourGradient bodyGrad (
            juce::Colour (0xFF3C3C44), centre.x, centre.y - bodyR * 0.5f,
            juce::Colour (0xFF1A1A20), centre.x, centre.y + bodyR * 0.6f, true);
        g.setGradientFill (bodyGrad);
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        // --- Knob rim (subtle highlight) ---
        g.setColour (juce::Colour (0xFF48484E));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // --- Indicator notch ---
        auto  notchLen = bodyR * 0.72f;
        auto  notchW   = juce::jmax (1.5f, radius * 0.06f);
        float px       = centre.x + std::sin (angle) * notchLen;
        float py       = centre.y - std::cos (angle) * notchLen;

        g.setColour (juce::Colour (0xFFE8A547));
        juce::Line<float> line (centre.x, centre.y, px, py);
        g.drawLine (line, notchW);

        // --- Pointer dot at the tip ---
        g.setColour (juce::Colour (0xFFFFD08A));
        g.fillEllipse (px - notchW * 0.7f, py - notchW * 0.7f, notchW * 1.4f, notchW * 1.4f);
    }

    // ─── ComboBox ───────────────────────────────────────────────────────────

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);

        // Background
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 4.0f);

        // Outline
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        // Arrow
        auto arrowArea = bounds.removeFromRight (height * 0.7f).reduced (height * 0.25f);
        juce::Path arrow;
        arrow.startNewSubPath (arrowArea.getX(), arrowArea.getCentreY() - arrowArea.getHeight() * 0.3f);
        arrow.lineTo (arrowArea.getCentreX(), arrowArea.getCentreY() + arrowArea.getHeight() * 0.3f);
        arrow.lineTo (arrowArea.getRight(), arrowArea.getCentreY() - arrowArea.getHeight() * 0.3f);
        arrow.closeSubPath();
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - box.getHeight(), box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions (13.0f));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font (juce::FontOptions (13.0f));
    }

    // drawPopupMenuItem uses JUCE 8 defaults — our colour overrides above handle the look
};
