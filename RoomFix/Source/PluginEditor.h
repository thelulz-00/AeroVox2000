#pragma once
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include <map>

class RfKnob : public juce::Component
{
public:
    RfKnob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& caption, const juce::String& tip)
        : text (caption), attachment (state, id, slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 16);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        if (auto* p = state.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (tip);
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

// LCD: room fingerprint, live input spectrum, detected resonances and the live correction curve
class RoomDisplay : public juce::Component, private juce::Timer
{
public:
    explicit RoomDisplay (RoomFixProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    RoomFixProcessor& proc;
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder, livePoints = 160;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> ring = std::vector<float> (fftSize, 0.0f), work = std::vector<float> (fftSize * 2, 0.0f);
    int ringPos = 0;
    std::array<float, livePoints> liveDb {};
    RoomProfile cachedProfile;
    int profileStamp = -1;
};

class RfMeter : public juce::Component, private juce::Timer
{
public:
    RfMeter (std::atomic<float>& src, juce::String name) : level (src), label (std::move (name)) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { shown = std::max (juce::Decibels::gainToDecibels (level.load(), -60.0f), shown - 1.5f); repaint(); }
    std::atomic<float>& level;
    juce::String label;
    float shown = -60.0f;
};

class RoomFixEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RoomFixEditor (RoomFixProcessor&);
    ~RoomFixEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setStatus (const juce::String& text, bool isError = false);
    void refreshProfileStatus();
    void addKnob (const char* id, const char* caption, const char* tip);

    struct Panel { juce::String title, hint; juce::Rectangle<int> bounds; };
    void drawPanel (juce::Graphics&, const Panel&);

    RoomFixProcessor& proc;
    AeroLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    Panel analyser, roomPanel, correction, range, output;
    std::vector<std::unique_ptr<RfKnob>> knobs;
    std::map<juce::String, RfKnob*> knobById;

    juce::TextButton loadButton { "LOAD ROOM RECORDING..." }, learnButton { "LEARN" }, clearButton { "CLEAR" };
    juce::ToggleButton listenButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;

    RoomDisplay display;
    RfMeter inMeter, outMeter;
    bool wasLearning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomFixEditor)
};
