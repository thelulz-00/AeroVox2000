#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Grey, early-2000s "tech hardware" look: brushed metal, bevels, LCDs and LEDs.
namespace Theme
{
    const juce::Colour bgTop       { 0xffc9ccd0 };
    const juce::Colour bgBottom    { 0xffa3a8ae };
    const juce::Colour panel       { 0xffbcc0c5 };
    const juce::Colour panelDark   { 0xff8d9298 };
    const juce::Colour titleTop    { 0xff5d636b };
    const juce::Colour titleBottom { 0xff3a3f46 };
    const juce::Colour textDark    { 0xff2a2e33 };
    const juce::Colour textLight   { 0xffeef1f4 };
    const juce::Colour accent      { 0xff2d8cff };
    const juce::Colour lcdBg       { 0xff0e181d };
    const juce::Colour lcdText     { 0xff86e6ff };
    const juce::Colour ledOn       { 0xff59ff4a };
    const juce::Colour ledOff      { 0xff21361f };

    inline juce::Font font (float size, bool bold = false)
    {
        // Verdana ships with every Mac and Windows; fall back to the system sans font otherwise
        static const juce::String name = juce::Font::findAllTypefaceNames().contains ("Verdana")
                                             ? juce::String ("Verdana") : juce::Font::getDefaultSansSerifFontName();
        return juce::Font (juce::FontOptions (name, size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // raised (out) or sunken (in) bevel around a rectangle
    inline void bevel (juce::Graphics& g, juce::Rectangle<float> r, bool sunken, float corner = 3.0f)
    {
        const auto light = juce::Colours::white.withAlpha (0.75f), dark = juce::Colours::black.withAlpha (0.45f);
        g.setColour (sunken ? dark : light);
        g.drawLine (r.getX() + corner, r.getY() + 0.5f, r.getRight() - corner, r.getY() + 0.5f, 1.0f);
        g.drawLine (r.getX() + 0.5f, r.getY() + corner, r.getX() + 0.5f, r.getBottom() - corner, 1.0f);
        g.setColour (sunken ? light : dark);
        g.drawLine (r.getX() + corner, r.getBottom() - 0.5f, r.getRight() - corner, r.getBottom() - 0.5f, 1.0f);
        g.drawLine (r.getRight() - 0.5f, r.getY() + corner, r.getRight() - 0.5f, r.getBottom() - corner, 1.0f);
    }

    inline void screw (juce::Graphics& g, juce::Point<float> c)
    {
        const float r = 4.5f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9ecef), c.x - r, c.y - r, juce::Colour (0xff6c7178), c.x + r, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 0.8f);
        g.drawLine (c.x - r * 0.6f, c.y + r * 0.6f, c.x + r * 0.6f, c.y - r * 0.6f, 1.2f);
    }
}

class AeroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AeroLookAndFeel()
    {
        setColour (juce::Slider::textBoxBackgroundColourId, Theme::lcdBg);
        setColour (juce::Slider::textBoxTextColourId, Theme::lcdText);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::black.withAlpha (0.6f));
        setColour (juce::Slider::textBoxHighlightColourId, Theme::accent.withAlpha (0.5f));
        setColour (juce::ComboBox::backgroundColourId, Theme::lcdBg);
        setColour (juce::ComboBox::textColourId, Theme::lcdText);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::black.withAlpha (0.6f));
        setColour (juce::ComboBox::arrowColourId, Theme::lcdText);
        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::textColourId, Theme::textDark);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextEditor::backgroundColourId, Theme::lcdBg);
        setColour (juce::TextEditor::textColourId, Theme::lcdText);
        setColour (juce::Label::textColourId, Theme::lcdText);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // tick marks
        g.setColour (Theme::textDark.withAlpha (0.55f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            const auto p1 = c.getPointOnCircumference (radius - 1.0f, a);
            const auto p2 = c.getPointOnCircumference (radius - (i % 5 == 0 ? 5.0f : 3.5f), a);
            g.drawLine ({ p1, p2 }, 1.0f);
        }

        // value arc (blue LED ring)
        const float arcR = radius - 8.0f;
        juce::Path track, value;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (track, juce::PathStrokeType (3.0f));
        g.setColour (Theme::accent.withAlpha (0.35f));
        g.strokePath (value, juce::PathStrokeType (5.0f));
        g.setColour (Theme::accent.brighter (0.3f));
        g.strokePath (value, juce::PathStrokeType (2.2f));

        // knob body: metal cap with knurled edge
        const float kr = arcR - 5.0f;
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (c.x - kr + 1.5f, c.y - kr + 2.5f, kr * 2, kr * 2); // drop shadow
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff70757c), c.x, c.y - kr, juce::Colour (0xff3d4147), c.x, c.y + kr, false));
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
        const float cr = kr - 3.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f4f6), c.x - cr * 0.6f, c.y - cr, juce::Colour (0xff8e939a), c.x + cr * 0.4f, c.y + cr, false));
        g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 0.8f);

        // pointer
        const auto p1 = c.getPointOnCircumference (cr * 0.25f, angle);
        const auto p2 = c.getPointOnCircumference (cr - 2.0f, angle);
        g.setColour (Theme::textDark);
        g.drawLine ({ p1, p2 }, 2.6f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (down ? juce::Colour (0xff9da2a8) : juce::Colour (0xffe4e7ea), 0, r.getY(),
                                                 down ? juce::Colour (0xffc3c7cc) : juce::Colour (0xffa9aeb4), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.75f : 0.55f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        Theme::bevel (g, r.reduced (1.0f), down);

        const bool on = b.getToggleState();
        const float d = r.getHeight() * 0.45f;
        const juce::Rectangle<float> led (r.getX() + 6.0f, r.getCentreY() - d * 0.5f, d, d);
        if (on)
        {
            g.setColour (Theme::ledOn.withAlpha (0.35f));
            g.fillEllipse (led.expanded (2.5f));
        }
        g.setGradientFill (juce::ColourGradient ((on ? Theme::ledOn : Theme::ledOff).brighter (0.6f), led.getX(), led.getY(),
                                                 on ? Theme::ledOn.darker (0.3f) : Theme::ledOff, led.getRight(), led.getBottom(), false));
        g.fillEllipse (led);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (led, 0.8f);

        g.setColour (Theme::textDark);
        g.setFont (Theme::font (10.0f, true));
        g.drawText (on ? "ON" : "OFF", r.withTrimmedLeft (d + 8.0f), juce::Justification::centred);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (Theme::lcdBg);
        g.fillRoundedRectangle (r, 2.0f);
        Theme::bevel (g, r, true, 2.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (Theme::lcdText);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (11.0f); }
    juce::Font getPopupMenuFont() override { return Theme::font (12.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (Theme::font (10.5f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }
};
