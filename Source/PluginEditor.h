#pragma once
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include <map>

// Rotary knob with its caption above and an LCD value box below.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption)
        : text (caption), attachment (state, paramId, slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 16);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        if (auto* p = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (caption + " - double-click to reset");
        addAndMakeVisible (slider);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::textDark);
        g.setFont (Theme::font (10.0f, true));
        g.drawText (text.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centred);
    }

    void resized() override { slider.setBounds (getLocalBounds().withTrimmedTop (14)); }

private:
    juce::String text;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// Spectrum + detected-resonance display ("LCD").
class ResonanceDisplay : public juce::Component, private juce::Timer
{
public:
    explicit ResonanceDisplay (ResonanceSuppressor& r) : res (r) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    ResonanceSuppressor& res;
};

// Segmented LED level meter.
class LedMeter : public juce::Component, private juce::Timer
{
public:
    LedMeter (std::atomic<float>& src, juce::String name) : level (src), label (std::move (name)) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override
    {
        const float db = juce::Decibels::gainToDecibels (level.load(), -60.0f);
        shown = std::max (db, shown - 1.5f);
        repaint();
    }
    std::atomic<float>& level;
    juce::String label;
    float shown = -60.0f;
};

class AeroVoxEditor : public juce::AudioProcessorEditor
{
public:
    explicit AeroVoxEditor (AeroVoxProcessor&);
    ~AeroVoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title, hint;
        juce::Rectangle<int> bounds;
        std::unique_ptr<juce::ToggleButton> power;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    };

    void addSection (Section& s, const juce::String& title, const juce::String& hint, const char* powerParam);
    void drawPanel (juce::Graphics&, const Section&);
    Knob& addKnob (const char* id, const char* caption);

    AeroVoxProcessor& proc;
    AeroLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    Section vocoder, resonance, air, reverb, output;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::map<juce::String, Knob*> knobById;

    juce::ComboBox carrierBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> carrierAttachment;

    ResonanceDisplay display;
    LedMeter inMeter, outMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeroVoxEditor)
};
