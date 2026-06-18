// =============================================================================
// VB1LookAndFeel.h — Professional dark/amber LookAndFeel for VB-1 reimpl
// Multi-layer vector knobs with glow, gradient body, rim highlight.
// All code-drawn, no image assets, DPI-scalable.
// =============================================================================
#pragma once
#include <JuceHeader.h>

class VB1LookAndFeel : public juce::LookAndFeel_V4
{
public:
    VB1LookAndFeel()
    {
        auto am = juce::Colour (0xFFE8A547);   // amber
        auto co = juce::Colour (0xFFB87333);   // copper
        auto dg = juce::Colour (0xFF1A1A20);   // dark gunmetal
        auto md = juce::Colour (0xFF2A2A30);   // medium dark

        setColour (juce::Slider::textBoxTextColourId,         juce::Colour (0xFFD8D8DE));
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,    am.withAlpha (0.20f));
        setColour (juce::ComboBox::backgroundColourId,        md);
        setColour (juce::ComboBox::outlineColourId,           juce::Colour (0xFF38383E));
        setColour (juce::ComboBox::textColourId,              juce::Colour (0xFFD0D0D6));
        setColour (juce::ComboBox::arrowColourId,             am);
        setColour (juce::PopupMenu::backgroundColourId,       dg);
        setColour (juce::PopupMenu::textColourId,             juce::Colour (0xFFD0D0D6));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, am.withAlpha (0.15f));
        setColour (juce::PopupMenu::highlightedTextColourId,  am);
        setColour (juce::Label::textColourId,                 juce::Colour (0xFF8A8A92));
    }

    // ─── Knob: 5-layer composite ─────────────────────────────────────────────

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();
        auto r  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.42f;
        auto angle = startAngle + pos * (endAngle - startAngle);
        auto sw    = juce::jmax (2.5f, r * 0.12f);
        // LAYER 1: Subtle shadow below knob
        auto bodyR = r * 0.74f;
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (cx - bodyR + 1, cy + bodyR - 3, bodyR * 1.8f, 6);

        // LAYER 2: Track ring (dark background arc — 270° sweep)
        {
            juce::Path track;
            track.addCentredArc (cx, cy, r, r, 0.0f, startAngle, endAngle, true);
            g.setColour (juce::Colour (0xFF202026));
            g.strokePath (track, { sw + 1.5f, juce::PathStrokeType::curved });
        }

        // LAYER 3: Value arc (amber→copper gradient, glow underneath)
        if (pos > 0.003f)
        {
            // Glow
            juce::Path glow;
            glow.addCentredArc (cx, cy, r, r, 0.0f, startAngle, angle, true);
            g.setColour (juce::Colour (0xFFE8A547).withAlpha (0.15f));
            g.strokePath (glow, { sw + 5.0f, juce::PathStrokeType::curved });

            // Gradient arc
            juce::Path val;
            val.addCentredArc (cx, cy, r, r, 0.0f, startAngle, angle, true);
            juce::ColourGradient ag (
                juce::Colour (0xFFC2803C),  cx - r, cy,
                juce::Colour (0xFFFFD08A),  cx + r, cy, false);
            g.setGradientFill (ag);
            g.strokePath (val, { sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // LAYER 4: Knob body (radial gradient sphere with depth)
        {
            // Outer rim shadow
            g.setColour (juce::Colour (0xFF0A0A0E));
            g.fillEllipse (cx - bodyR - 1, cy - bodyR - 1, bodyR * 2 + 2, bodyR * 2 + 2);

            // Body gradient (top-left light source)
            juce::ColourGradient bg (
                juce::Colour (0xFF44444C),  cx - bodyR * 0.4f, cy - bodyR * 0.5f,
                juce::Colour (0xFF16161C),  cx + bodyR * 0.3f, cy + bodyR * 0.6f, true);
            g.setGradientFill (bg);
            g.fillEllipse (cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);

            // Rim highlight (top arc)
            juce::Path rim;
            rim.addCentredArc (cx, cy, bodyR, bodyR, 0.0f,
                               juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 1.75f, true);
            g.setColour (juce::Colour (0xFF6A6A74));
            g.strokePath (rim, { 1.5f, juce::PathStrokeType::curved });

            // Inner subtle ring
            g.setColour (juce::Colour (0xFF3A3A42));
            g.drawEllipse (cx - bodyR + 2, cy - bodyR + 2, (bodyR - 2) * 2, (bodyR - 2) * 2, 0.5f);
        }

        // LAYER 5: Indicator (pointing line + glowing tip)
        {
            auto len = bodyR * 0.70f;
            auto wid = juce::jmax (2.0f, r * 0.07f);
            float px = cx + std::sin (angle) * len;
            float py = cy - std::cos (angle) * len;

            // Indicator line
            juce::Line<float> line (cx, cy, px, py);
            g.setColour (juce::Colour (0xFFE8A547));
            g.drawLine (line, wid);

            // Glowing tip dot
            g.setColour (juce::Colour (0xFFFFD08A));
            g.fillEllipse (px - wid * 0.8f, py - wid * 0.8f, wid * 1.6f, wid * 1.6f);

            // Center cap
            g.setColour (juce::Colour (0xFF2A2A30));
            g.fillEllipse (cx - 2, cy - 2, 4, 4);
        }
    }

    // ─── ComboBox ─────────────────────────────────────────────────────────────

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);

        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (b, 5.0f);

        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (b, 5.0f, 1.0f);

        // Amber arrow
        auto a = b.removeFromRight (h * 0.7f).reduced (h * 0.25f);
        juce::Path arrow;
        arrow.startNewSubPath (a.getX(), a.getCentreY() - a.getHeight() * 0.3f);
        arrow.lineTo (a.getCentreX(), a.getCentreY() + a.getHeight() * 0.3f);
        arrow.lineTo (a.getRight(), a.getCentreY() - a.getHeight() * 0.3f);
        arrow.closeSubPath();
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& l) override
    {
        l.setBounds (10, 1, box.getWidth() - box.getHeight(), box.getHeight() - 2);
        l.setFont (juce::Font (juce::FontOptions (13.0f)));
        l.setJustificationType (juce::Justification::centredLeft);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override       { return { juce::FontOptions (13.0f) }; }
    juce::Font getPopupMenuFont() override                       { return { juce::FontOptions (13.0f) }; }

    // drawPopupMenuItem uses JUCE 8 defaults with colour overrides above
};
