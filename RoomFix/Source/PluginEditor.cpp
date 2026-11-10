#include "PluginEditor.h"

namespace
{
    constexpr int editorW = 900, editorH = 560, knobW = 78, knobH = 104;
    const juce::Colour cutColour (0xffff7a2f);
    const juce::Colour roomColour (0xffd9e3e8);

    float xForFreq (float f, juce::Rectangle<float> r) { return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
    juce::String freqText (float f) { return f < 100.0f ? juce::String (f, 1) : f < 1000.0f ? juce::String (juce::roundToInt (f)) : juce::String (f / 1000.0f, 2) + "k"; }
}

//==============================================================================
RoomDisplay::RoomDisplay (RoomFixProcessor& p) : proc (p)
{
    liveDb.fill (-120.0f);
    startTimerHz (30);
}

void RoomDisplay::timerCallback()
{
    // pull new input samples from the audio thread
    auto& fifo = proc.analyserFifo;
    int s1, z1, s2, z2;
    const int ready = fifo.getNumReady();
    fifo.prepareToRead (ready, s1, z1, s2, z2);
    auto take = [&] (int start, int count) {
        for (int i = 0; i < count; ++i) { ring[(size_t) ringPos] = proc.analyserData[(size_t) (start + i)]; ringPos = (ringPos + 1) % fftSize; }
    };
    take (s1, z1); take (s2, z2);
    fifo.finishedRead (z1 + z2);

    for (int i = 0; i < fftSize; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / fftSize);
        work[(size_t) i] = ring[(size_t) ((ringPos + i) % fftSize)] * w;
    }
    std::fill (work.begin() + fftSize, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (work.data(), true);

    const double fs = proc.getSampleRateNow();
    const float norm = 20.0f * std::log10 (fftSize * 0.25f);
    for (int i = 0; i < livePoints; ++i)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) i / livePoints), f1 = 20.0 * std::pow (1000.0, (double) (i + 1) / livePoints);
        const int a = juce::jlimit (1, fftSize / 2, (int) (f0 * fftSize / fs)), b = juce::jlimit (a, fftSize / 2, (int) (f1 * fftSize / fs));
        float m = 0; for (int k = a; k <= b; ++k) m = std::max (m, work[(size_t) k]);
        const float dbv = 20.0f * std::log10 (m + 1e-9f) - norm;
        liveDb[(size_t) i] = dbv > liveDb[(size_t) i] ? dbv : liveDb[(size_t) i] - 1.2f;
    }

    if (proc.profileVersion.load() != profileStamp)
    {
        profileStamp = proc.profileVersion.load();
        cachedProfile = proc.getProfile();
    }
    repaint();
}

void RoomDisplay::paint (juce::Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (Theme::lcdBg);
    g.fillRoundedRectangle (outer, 3.0f);
    Theme::bevel (g, outer, true);

    auto header = outer.reduced (8.0f, 4.0f).removeFromTop (14.0f);
    auto r = outer.reduced (8.0f, 6.0f).withTrimmedTop (16.0f).withTrimmedBottom (13.0f);

    // grid + frequency labels
    g.setFont (Theme::font (9.0f));
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f, r);
        g.setColour (Theme::lcdText.withAlpha (0.10f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (Theme::lcdText.withAlpha (0.45f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 15, r.getBottom() + 1, 30, 11), juce::Justification::centred);
    }

    // correction curve area: 0 dB line near the top, -24 dB at 60% height
    const float zeroY = r.getY() + r.getHeight() * 0.10f;
    auto cutToY = [&] (float cutDb) { return zeroY + juce::jlimit (0.0f, 24.0f, cutDb) / 24.0f * r.getHeight() * 0.6f; };
    g.setColour (cutColour.withAlpha (0.25f));
    g.drawHorizontalLine ((int) zeroY, r.getX(), r.getRight());
    g.setFont (Theme::font (8.5f));
    for (float d : { 0.0f, 6.0f, 12.0f, 18.0f })
    {
        g.setColour (cutColour.withAlpha (0.55f));
        g.drawText (d == 0.0f ? "0 dB" : "-" + juce::String ((int) d), juce::Rectangle<float> (r.getRight() - 30, cutToY (d) - 6, 28, 11), juce::Justification::centredRight);
        if (d > 0) { g.setColour (cutColour.withAlpha (0.08f)); g.drawHorizontalLine ((int) cutToY (d), r.getX(), r.getRight() - 32); }
    }

    // live input spectrum (dBFS)
    {
        juce::Path live;
        for (int i = 0; i < (int) liveDb.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * ((float) i + 0.5f) / (float) liveDb.size();
            const float y = juce::jmap (juce::jlimit (-110.0f, -10.0f, liveDb[(size_t) i]), -110.0f, -10.0f, r.getBottom(), r.getY() + r.getHeight() * 0.25f);
            if (i == 0) live.startNewSubPath (r.getX(), y); else live.lineTo (x, y);
        }
        juce::Path fill (live);
        fill.lineTo (r.getRight(), r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
        g.setColour (Theme::lcdText.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (Theme::lcdText.withAlpha (0.45f));
        g.strokePath (live, juce::PathStrokeType (1.0f));
    }

    const auto& prof = cachedProfile;
    g.setFont (Theme::font (10.0f, true));
    if (! prof.valid)
    {
        g.setColour (Theme::lcdText);
        g.drawText ("NO ROOM PROFILE", r.withTrimmedBottom (r.getHeight() * 0.3f), juce::Justification::centred);
        g.setFont (Theme::font (10.0f));
        g.setColour (Theme::lcdText.withAlpha (0.7f));
        g.drawText ("Load a recording of the empty room, or press LEARN while the room-only part plays",
                    r.withTrimmedTop (r.getHeight() * 0.25f), juce::Justification::centred);
        g.setFont (Theme::font (10.0f, true));
        g.setColour (Theme::lcdText);
        g.drawText ("ROOM ANALYSER", header, juce::Justification::centredLeft);
        return;
    }

    // room fingerprint (relative dB)
    {
        juce::Path room;
        for (int i = 0; i < RoomProfile::curvePoints; ++i)
        {
            const float x = xForFreq (RoomProfile::curveFreq (i), r);
            const float y = juce::jmap (juce::jlimit (-24.0f, 12.0f, prof.curveDb[(size_t) i]), -24.0f, 12.0f, r.getBottom(), r.getY() + r.getHeight() * 0.2f);
            if (i == 0) room.startNewSubPath (x, y); else room.lineTo (x, y);
        }
        g.setColour (roomColour.withAlpha (0.85f));
        g.strokePath (room, juce::PathStrokeType (1.4f));
    }

    // active bands: markers + combined correction curve
    const int nb = proc.eq.shared.numBands.load();
    const double fs = proc.getSampleRateNow();
    std::vector<Biquad> bells ((size_t) nb);
    float totalMax = 0;
    for (int b = 0; b < nb; ++b)
    {
        const float f = proc.eq.shared.freq[(size_t) b].load(), q = proc.eq.shared.q[(size_t) b].load(), cut = proc.eq.shared.cutDb[(size_t) b].load();
        bells[(size_t) b].setPeak (fs, f, q, -cut);
        totalMax = std::max (totalMax, cut);

        const float x = xForFreq (f, r);
        g.setColour (cutColour.withAlpha (0.35f));
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (x, zeroY, x, r.getBottom()), dashes, 2, 1.0f);
        g.setFont (Theme::font (8.5f, true));
        g.setColour (cutColour);
        const float ly = r.getBottom() - 12.0f - (float) (b % 3) * 11.0f;
        g.drawText (freqText (f), juce::Rectangle<float> (x - 22, ly, 44, 10), juce::Justification::centred);
    }

    juce::Path cutPath;
    const int pts = 300;
    for (int i = 0; i < pts; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / (pts - 1));
        double total = 0;
        for (auto& bq : bells) total += bq.magnitudeDb (fs, f);
        const float x = xForFreq (f, r), y = cutToY ((float) -total);
        if (i == 0) cutPath.startNewSubPath (x, y); else cutPath.lineTo (x, y);
    }
    juce::Path cutFill (cutPath);
    cutFill.lineTo (r.getRight(), zeroY); cutFill.lineTo (r.getX(), zeroY); cutFill.closeSubPath();
    g.setColour (cutColour.withAlpha (0.25f));
    g.fillPath (cutFill);
    g.setColour (cutColour);
    g.strokePath (cutPath, juce::PathStrokeType (1.6f));

    // header line
    g.setFont (Theme::font (10.0f, true));
    g.setColour (Theme::lcdText);
    g.drawText ("ROOM: " + prof.source.toUpperCase() + "  (" + juce::String (prof.seconds, 1) + " s)", header, juce::Justification::centredLeft);
    g.setColour (nb > 0 ? cutColour : Theme::lcdText.withAlpha (0.6f));
    g.drawText (juce::String (nb) + (nb == 1 ? " ROOM RESONANCE" : " ROOM RESONANCES") + "   NOW -" + juce::String (totalMax, 1) + " dB",
                header, juce::Justification::centredRight);

    // legend
    auto leg = juce::Rectangle<float> (r.getRight() - 250, r.getBottom() - 12, 250, 11);
    g.setFont (Theme::font (8.5f, true));
    g.setColour (roomColour); g.drawText ("- ROOM PRINT", leg, juce::Justification::centredLeft);
    g.setColour (Theme::lcdText.withAlpha (0.7f)); g.drawText ("- INPUT", leg.withTrimmedLeft (85), juce::Justification::centredLeft);
    g.setColour (cutColour); g.drawText ("- CORRECTION", leg.withTrimmedLeft (140), juce::Justification::centredLeft);
}

//==============================================================================
void RfMeter::paint (juce::Graphics& g)
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
RoomFixEditor::RoomFixEditor (RoomFixProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), inMeter (p.inputLevel, "IN"), outMeter (p.outputLevel, "OUT")
{
    setLookAndFeel (&lnf);

    analyser = { "ROOM ANALYSER  //  RESONANCE MAP", {}, {} };
    roomPanel = { "1. LEARN THE ROOM", {}, {} };
    correction = { "2. CORRECTION", {}, {} };
    range = { "RANGE", {}, {} };
    output = { "OUTPUT", "LISTEN CUT", {} };

    addKnob ("amount", "Amount", "How much of the room's boost to remove (100% = flatten it completely)");
    addKnob ("threshold", "Threshold", "Only correct room peaks at least this many dB above their neighbours");
    addKnob ("dynamic", "Dynamic", "0% = fixed EQ. 100% = cut only when the voice actually excites the room resonance");
    addKnob ("low", "Low", "Ignore room resonances below this frequency");
    addKnob ("high", "High", "Ignore room resonances above this frequency");
    addKnob ("out", "Gain", "Output level");

    loadButton.setTooltip ("Load a recording of the empty room (WAV, AIFF, FLAC, MP3). 10-30 s is ideal.");
    learnButton.setTooltip ("Press, play the part where only the room is audible (or feed it to the sidechain), press again to analyse");
    clearButton.setTooltip ("Forget the room profile");
    learnButton.setClickingTogglesState (true);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a recording of the room", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            setStatus ("Analysing " + file.getFileName() + "...");
            const auto err = proc.loadRoomFile (file);
            if (err.isNotEmpty()) setStatus (err, true);
            else refreshProfileStatus();
        });
    };
    learnButton.onClick = [this]
    {
        if (learnButton.getToggleState()) { proc.startLearning(); setStatus ("RECORDING THE ROOM... play the room-only part"); }
        else if (proc.isLearning() || wasLearning)
        {
            wasLearning = false;
            const auto err = proc.stopLearning();
            if (err.isNotEmpty()) setStatus (err, true); else refreshProfileStatus();
        }
    };
    clearButton.onClick = [this] { proc.setProfile ({}); refreshProfileStatus(); };

    listenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "listen", listenButton);
    listenButton.setTooltip ("Hear only what is being removed (the room resonances)");

    status.setColour (juce::Label::backgroundColourId, Theme::lcdBg);
    status.setColour (juce::Label::textColourId, Theme::lcdText);
    status.setFont (Theme::font (10.0f));
    status.setJustificationType (juce::Justification::centredLeft);
    status.setMinimumHorizontalScale (0.6f);

    for (juce::Component* c : { (juce::Component*) &loadButton, (juce::Component*) &learnButton, (juce::Component*) &clearButton,
                                (juce::Component*) &listenButton, (juce::Component*) &status, (juce::Component*) &display,
                                (juce::Component*) &inMeter, (juce::Component*) &outMeter })
        addAndMakeVisible (c);

    if (proc.isLearning()) { learnButton.setToggleState (true, juce::dontSendNotification); wasLearning = true; }
    refreshProfileStatus();
    setSize (editorW, editorH);
    startTimerHz (10);
}

RoomFixEditor::~RoomFixEditor() { setLookAndFeel (nullptr); }

void RoomFixEditor::addKnob (const char* id, const char* caption, const char* tip)
{
    knobs.push_back (std::make_unique<RfKnob> (proc.apvts, id, caption, tip));
    knobById[id] = knobs.back().get();
    addAndMakeVisible (*knobs.back());
}

void RoomFixEditor::setStatus (const juce::String& text, bool isError)
{
    status.setColour (juce::Label::textColourId, isError ? cutColour : Theme::lcdText);
    status.setText (" " + text, juce::dontSendNotification);
}

void RoomFixEditor::refreshProfileStatus()
{
    const auto p = proc.getProfile();
    if (! p.valid) { setStatus ("No room profile yet"); return; }
    int strong = 0;
    for (auto& r : p.resonances) if (r.boostDb >= 3.0f) ++strong;
    setStatus ("Room learned: " + juce::String (p.seconds, 1) + " s, " + juce::String (strong) + " resonances above 3 dB");
}

void RoomFixEditor::timerCallback()
{
    if (proc.isLearning())
    {
        wasLearning = true;
        setStatus ("REC  " + juce::String (proc.learnedSeconds(), 1) + " / " + juce::String ((int) RoomFixProcessor::maxLearnSeconds) + " s   - press LEARN again to analyse");
    }
    else if (wasLearning) // stopped by itself (30 s reached)
    {
        wasLearning = false;
        learnButton.setToggleState (false, juce::dontSendNotification);
        const auto err = proc.stopLearning();
        if (err.isNotEmpty()) setStatus (err, true); else refreshProfileStatus();
    }
}

//==============================================================================
void RoomFixEditor::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (Theme::bgTop, 0, 0, Theme::bgBottom, 0, area.getBottom(), false));
    g.fillAll();
    juce::Random rng (2000);
    for (int y = 0; y < getHeight(); y += 2)
    {
        g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.025f + 0.02f * rng.nextFloat()));
        g.drawHorizontalLine (y, 0.0f, area.getRight());
    }

    auto header = area.removeFromTop (56.0f).reduced (14.0f, 8.0f);
    g.setFont (Theme::font (26.0f, true));
    const juce::String brand ("ROOMFIX");
    const float brandW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), brand);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawText (brand, header.translated (1, 1), juce::Justification::centredLeft);
    g.setColour (Theme::textDark);
    g.drawText (brand, header, juce::Justification::centredLeft);
    g.setColour (Theme::accent);
    g.drawText ("2000", header.withTrimmedLeft (brandW + 8.0f), juce::Justification::centredLeft);
    g.setFont (Theme::font (9.5f, true));
    g.setColour (Theme::textDark.withAlpha (0.7f));
    g.drawText ("ROOM RESONANCE REMOVER   //   LEARNS YOUR ROOM, LEAVES YOUR VOICE", header.withTrimmedLeft (brandW + 82.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawHorizontalLine (56, 10.0f, area.getRight() - 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawHorizontalLine (57, 10.0f, area.getRight() - 10.0f);

    for (auto* pnl : { &analyser, &roomPanel, &correction, &range, &output })
        drawPanel (g, *pnl);

    g.setFont (Theme::font (9.5f));
    g.setColour (Theme::textDark.withAlpha (0.65f));
    g.drawText ("TIP: RECORD 10-30 s OF THE ROOM WITH THE SAME MIC AND POSITION - A FEW CLAPS OR SOME NOISE PLAYED IN THE ROOM GIVE THE BEST RESULT",
                getLocalBounds().removeFromBottom (26).reduced (16, 0), juce::Justification::centredLeft);
    g.drawText ("v1.0", getLocalBounds().removeFromBottom (26).reduced (16, 0), juce::Justification::centredRight);
}

void RoomFixEditor::drawPanel (juce::Graphics& g, const Panel& s)
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
        g.setColour (Theme::textDark);
        g.setFont (Theme::font (10.0f, true));
        g.drawText (s.hint, s.bounds.reduced (6).withTrimmedTop (28).removeFromTop (14).toFloat(), juce::Justification::centred);
    }
    for (auto c : { juce::Point<float> (r.getX() + 9, r.getBottom() - 9), juce::Point<float> (r.getRight() - 9, r.getBottom() - 9) })
        Theme::screw (g, c);
}

void RoomFixEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    auto header = area.removeFromTop (56);
    auto meters = header.removeFromRight (230).reduced (0, 12);
    inMeter.setBounds (meters.removeFromTop (14));
    meters.removeFromTop (4);
    outMeter.setBounds (meters.removeFromTop (14));
    area.removeFromTop (8);
    area.removeFromBottom (26);

    analyser.bounds = area.removeFromTop (282);
    display.setBounds (analyser.bounds.reduced (12).withTrimmedTop (22).withTrimmedBottom (4));
    area.removeFromTop (8);

    auto row = area;
    roomPanel.bounds = row.removeFromLeft (300); row.removeFromLeft (8);
    correction.bounds = row.removeFromLeft (3 * knobW + 30); row.removeFromLeft (8);
    range.bounds = row.removeFromLeft (2 * knobW + 30); row.removeFromLeft (8);
    output.bounds = row;

    auto place = [this] (juce::Rectangle<int> r, std::initializer_list<const char*> ids)
    {
        int x = r.getX() + (r.getWidth() - (int) ids.size() * knobW) / 2;
        for (auto* id : ids) { knobById[id]->setBounds (x, r.getY() + (r.getHeight() - knobH) / 2, knobW, knobH); x += knobW; }
    };
    auto content = [] (const Panel& p) { return p.bounds.reduced (10).withTrimmedTop (24).withTrimmedBottom (8); };

    {
        auto c = content (roomPanel).reduced (4, 2);
        loadButton.setBounds (c.removeFromTop (28));
        c.removeFromTop (8);
        auto buttons = c.removeFromTop (28);
        learnButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
        buttons.removeFromLeft (8);
        clearButton.setBounds (buttons);
        c.removeFromTop (10);
        status.setBounds (c.removeFromTop (34));
    }
    place (content (correction), { "amount", "threshold", "dynamic" });
    place (content (range), { "low", "high" });
    {
        auto c = content (output);
        c.removeFromTop (12);
        listenButton.setBounds (c.removeFromTop (20).withSizeKeepingCentre (56, 18));
        place (c.withTrimmedTop (6), { "out" });
    }
}
