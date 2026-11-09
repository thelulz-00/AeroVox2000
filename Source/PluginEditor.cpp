#include "PluginEditor.h"

namespace
{
    constexpr int editorW = 940, editorH = 590;
    constexpr int knobW = 78, knobH = 104;

    float freqToX (float f, juce::Rectangle<float> r)
    {
        return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
    }
}

//==============================================================================
void ResonanceDisplay::paint (juce::Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (Theme::lcdBg);
    g.fillRoundedRectangle (outer, 3.0f);
    Theme::bevel (g, outer, true);

    auto r = outer.reduced (8.0f, 6.0f).withTrimmedBottom (12.0f).withTrimmedTop (14.0f);
    const float minDb = -90.0f, maxDb = 0.0f;
    auto dbToY = [&] (float db) { return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, r.getBottom(), r.getY()); };

    // grid
    g.setFont (Theme::font (9.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f, r);
        g.setColour (Theme::lcdText.withAlpha (0.12f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (Theme::lcdText.withAlpha (0.45f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 15, r.getBottom() + 1, 30, 11), juce::Justification::centred);
    }
    for (float db = -80.0f; db < 0.0f; db += 20.0f)
    {
        g.setColour (Theme::lcdText.withAlpha (0.10f));
        g.drawHorizontalLine ((int) dbToY (db), r.getX(), r.getRight());
    }

    // spectrum
    const int n = ResonanceSuppressor::displayPoints;
    juce::Path spec, cut;
    spec.startNewSubPath (r.getX(), r.getBottom());
    cut.startNewSubPath (r.getX(), r.getY());
    int count = 0;
    float maxCut = 0.0f;
    bool inPeak = false;

    for (int i = 0; i < n; ++i)
    {
        const float x = r.getX() + r.getWidth() * ((float) i + 0.5f) / (float) n;
        spec.lineTo (x, dbToY (res.displaySpectrumDb[(size_t) i].load (std::memory_order_relaxed)));

        const float red = res.displayReductionDb[(size_t) i].load (std::memory_order_relaxed);
        cut.lineTo (x, r.getY() + juce::jlimit (0.0f, 1.0f, red / 18.0f) * r.getHeight() * 0.55f);
        maxCut = std::max (maxCut, red);
        if (red > 2.0f && ! inPeak) ++count;
        inPeak = red > 2.0f;
    }
    spec.lineTo (r.getRight(), r.getBottom());
    spec.closeSubPath();
    cut.lineTo (r.getRight(), r.getY());
    cut.closeSubPath();

    g.setGradientFill (juce::ColourGradient (Theme::lcdText.withAlpha (0.45f), 0, r.getY(), Theme::lcdText.withAlpha (0.05f), 0, r.getBottom(), false));
    g.fillPath (spec);
    g.setColour (Theme::lcdText.withAlpha (0.9f));
    g.strokePath (spec, juce::PathStrokeType (1.0f));

    const juce::Colour cutColour (0xffff7a2f);
    g.setColour (cutColour.withAlpha (0.35f));
    g.fillPath (cut);
    g.setColour (cutColour);
    g.strokePath (cut, juce::PathStrokeType (1.4f));

    // status line
    g.setFont (Theme::font (10.0f, true));
    g.setColour (Theme::lcdText);
    g.drawText ("AUTO-DETECT", outer.reduced (8.0f, 3.0f).removeFromTop (14.0f), juce::Justification::centredLeft);
    g.setColour (count > 0 ? cutColour : Theme::lcdText.withAlpha (0.6f));
    g.drawText (juce::String (count) + (count == 1 ? " RESONANCE" : " RESONANCES") + "   MAX -" + juce::String (maxCut, 1) + " dB",
                outer.reduced (8.0f, 3.0f).removeFromTop (14.0f), juce::Justification::centredRight);
}

//==============================================================================
void LedMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::textDark);
    g.setFont (Theme::font (9.0f, true));
    g.drawText (label, r.removeFromLeft (28.0f), juce::Justification::centredLeft);

    g.setColour (Theme::lcdBg);
    g.fillRoundedRectangle (r, 2.0f);
    Theme::bevel (g, r, true, 2.0f);

    constexpr int segments = 24;
    auto inner = r.reduced (3.0f);
    const float segW = inner.getWidth() / (float) segments;
    for (int i = 0; i < segments; ++i)
    {
        const float segDb = -60.0f + 63.0f * (float) (i + 1) / (float) segments;
        const juce::Colour c = segDb > -1.0f ? juce::Colour (0xffff3b30) : segDb > -12.0f ? juce::Colour (0xffffd23a) : Theme::ledOn;
        g.setColour (shown >= segDb - 63.0f / segments ? c : c.withAlpha (0.12f));
        g.fillRect (inner.getX() + i * segW + 0.5f, inner.getY(), segW - 1.5f, inner.getHeight());
    }
}

//==============================================================================
AeroVoxEditor::AeroVoxEditor (AeroVoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      display (p.resonance),
      inMeter (p.inputLevel, "IN"), outMeter (p.outputLevel, "OUT")
{
    setLookAndFeel (&lnf);

    addSection (vocoder, "VOCODER", "CARRIER", "voc_on");
    addSection (resonance, "RESONANCE EQ  //  AUTO", {}, "res_on");
    addSection (air, "FRESH AIR", "HIGH SHELF + EXCITER", "air_on");
    addSection (reverb, "REVERB", "STEREO SPACE", "rev_on");
    addSection (output, "OUTPUT", "MASTER", nullptr);

    addKnob ("voc_pitch", "Pitch");
    addKnob ("voc_release", "Release");
    addKnob ("voc_mix", "Mix");
    addKnob ("res_depth", "Depth");
    addKnob ("res_sens", "Sensitivity");
    addKnob ("res_speed", "Speed");
    addKnob ("air_amount", "Amount");
    addKnob ("air_freq", "Freq");
    addKnob ("rev_size", "Size");
    addKnob ("rev_damp", "Damping");
    addKnob ("rev_predelay", "Pre-Delay");
    addKnob ("rev_mix", "Mix");
    addKnob ("out_gain", "Gain");

    carrierBox.addItemList ({ "Synth (MIDI)", "Noise", "Sidechain" }, 1);
    carrierBox.setTooltip ("Carrier: Synth = play MIDI notes (or uses Pitch with no notes), Noise = whisper, Sidechain = external signal");
    carrierAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "voc_carrier", carrierBox);
    addAndMakeVisible (carrierBox);

    addAndMakeVisible (display);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    setSize (editorW, editorH);
}

AeroVoxEditor::~AeroVoxEditor() { setLookAndFeel (nullptr); }

void AeroVoxEditor::addSection (Section& s, const juce::String& title, const juce::String& hint, const char* powerParam)
{
    s.title = title;
    s.hint = hint;
    if (powerParam != nullptr)
    {
        s.power = std::make_unique<juce::ToggleButton>();
        s.power->setTooltip ("Switch " + title.upToFirstOccurrenceOf (" ", false, false).toLowerCase() + " on/off");
        s.powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, powerParam, *s.power);
        addAndMakeVisible (*s.power);
    }
}

Knob& AeroVoxEditor::addKnob (const char* id, const char* caption)
{
    knobs.push_back (std::make_unique<Knob> (proc.apvts, id, caption));
    knobById[id] = knobs.back().get();
    addAndMakeVisible (*knobs.back());
    return *knobs.back();
}

//==============================================================================
void AeroVoxEditor::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // brushed-metal background
    g.setGradientFill (juce::ColourGradient (Theme::bgTop, 0, 0, Theme::bgBottom, 0, area.getBottom(), false));
    g.fillAll();
    juce::Random rng (2000);
    for (int y = 0; y < getHeight(); y += 2)
    {
        g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.025f + 0.02f * rng.nextFloat()));
        g.drawHorizontalLine (y, 0.0f, area.getRight());
    }

    // header
    auto header = area.removeFromTop (56.0f).reduced (14.0f, 8.0f);
    g.setFont (Theme::font (26.0f, true));
    const juce::String brand ("AEROVOX");
    const float brandW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), brand);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawText (brand, header.translated (1, 1), juce::Justification::centredLeft);
    g.setColour (Theme::textDark);
    g.drawText (brand, header, juce::Justification::centredLeft);
    g.setColour (Theme::accent);
    g.drawText ("2000", header.withTrimmedLeft (brandW + 8.0f), juce::Justification::centredLeft);
    g.setFont (Theme::font (9.5f, true));
    g.setColour (Theme::textDark.withAlpha (0.7f));
    g.drawText ("VOCAL PROCESSING UNIT   //   VOCODER - AUTO RESONANCE EQ - FRESH AIR - REVERB",
                header.withTrimmedLeft (brandW + 82.0f), juce::Justification::centredLeft);

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawHorizontalLine (56, 10.0f, area.getRight() - 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawHorizontalLine (57, 10.0f, area.getRight() - 10.0f);

    for (auto* s : { &vocoder, &resonance, &air, &reverb, &output })
        drawPanel (g, *s);

    // footer
    g.setFont (Theme::font (9.5f));
    g.setColour (Theme::textDark.withAlpha (0.65f));
    g.drawText ("DRAG KNOBS UP/DOWN  -  DOUBLE-CLICK TO RESET  -  SEND MIDI NOTES TO PLAY THE VOCODER",
                getLocalBounds().removeFromBottom (26).reduced (16, 0), juce::Justification::centredLeft);
    g.drawText ("v1.0", getLocalBounds().removeFromBottom (26).reduced (16, 0), juce::Justification::centredRight);
}

void AeroVoxEditor::drawPanel (juce::Graphics& g, const Section& s)
{
    const auto r = s.bounds.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (r.translated (1.5f, 2.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (Theme::panel.brighter (0.12f), 0, r.getY(), Theme::panel.darker (0.08f), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    Theme::bevel (g, r.reduced (1.0f), false, 4.0f);

    auto title = r.reduced (5.0f).removeFromTop (22.0f);
    g.setGradientFill (juce::ColourGradient (Theme::titleTop, 0, title.getY(), Theme::titleBottom, 0, title.getBottom(), false));
    g.fillRoundedRectangle (title, 2.0f);
    g.setColour (Theme::textLight);
    g.setFont (Theme::font (11.0f, true));
    g.drawText (s.title, title.reduced (16.0f, 0), juce::Justification::centredLeft);
    g.setColour (Theme::accent);
    g.fillRect (title.getX() + 6.0f, title.getCentreY() - 3.0f, 4.0f, 6.0f);

    if (s.hint.isNotEmpty())
    {
        g.setColour (Theme::textDark.withAlpha (0.7f));
        g.setFont (Theme::font (9.5f, true));
        g.drawText (s.hint, s.bounds.reduced (10).withTrimmedTop (24).removeFromTop (34).withTrimmedLeft (8).toFloat(),
                    juce::Justification::centredLeft);
    }

    for (auto c : { juce::Point<float> (r.getX() + 9, r.getBottom() - 9), juce::Point<float> (r.getRight() - 9, r.getBottom() - 9) })
        Theme::screw (g, c);
}

void AeroVoxEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    auto header = area.removeFromTop (56);
    auto meters = header.removeFromRight (230).reduced (0, 12);
    inMeter.setBounds (meters.removeFromTop (14));
    meters.removeFromTop (4);
    outMeter.setBounds (meters.removeFromTop (14));

    area.removeFromTop (8);
    area.removeFromBottom (26);

    auto placeRow = [this] (juce::Rectangle<int> r, std::initializer_list<const char*> ids)
    {
        const int total = (int) ids.size() * knobW;
        int x = r.getX() + (r.getWidth() - total) / 2;
        for (auto* id : ids)
        {
            knobById[id]->setBounds (x, r.getY() + (r.getHeight() - knobH) / 2, knobW, knobH);
            x += knobW;
        }
    };
    auto placePower = [] (Section& s)
    {
        if (s.power != nullptr)
            s.power->setBounds (s.bounds.getRight() - 62, s.bounds.getY() + 7, 52, 18);
    };
    auto content = [] (const Section& s) { return s.bounds.reduced (10).withTrimmedTop (24).withTrimmedBottom (6); };

    // top row: resonance EQ with its analyser
    resonance.bounds = area.removeFromTop (262);
    {
        auto c = content (resonance);
        auto knobsArea = c.removeFromRight (3 * knobW + 16);
        display.setBounds (c.reduced (2, 4));
        placeRow (knobsArea, { "res_depth", "res_sens", "res_speed" });
    }
    area.removeFromTop (8);

    // bottom row
    auto row = area.removeFromTop (226);
    vocoder.bounds = row.removeFromLeft (290); row.removeFromLeft (8);
    air.bounds     = row.removeFromLeft (180); row.removeFromLeft (8);
    output.bounds  = row.removeFromRight (100); row.removeFromRight (8);
    reverb.bounds  = row;

    {
        auto c = content (vocoder);
        auto top = c.removeFromTop (34);
        carrierBox.setBounds (top.reduced (10, 5).withTrimmedLeft (64));
        placeRow (c, { "voc_pitch", "voc_release", "voc_mix" });
    }
    placeRow (content (air).withTrimmedTop (34), { "air_amount", "air_freq" });
    placeRow (content (reverb).withTrimmedTop (34), { "rev_size", "rev_damp", "rev_predelay", "rev_mix" });
    placeRow (content (output).withTrimmedTop (34), { "out_gain" });

    for (auto* s : { &vocoder, &resonance, &air, &reverb })
        placePower (*s);
}
