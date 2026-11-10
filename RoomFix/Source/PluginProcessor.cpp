#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <optional>

namespace
{
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String hz (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String db (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }

    std::unique_ptr<juce::AudioParameterFloat> fp (const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                                                   juce::String (*txt) (float, int))
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction (txt));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout RoomFixProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (fp ("amount", "Amount", { 0.0f, 1.0f }, 0.8f, pct));
    l.add (fp ("threshold", "Threshold", { 1.0f, 12.0f, 0.1f }, 3.0f, [] (float v, int) { return juce::String (v, 1) + " dB"; }));
    l.add (fp ("dynamic", "Dynamic", { 0.0f, 1.0f }, 0.5f, pct));
    l.add (fp ("low", "Low Limit", { 20.0f, 1000.0f, 0.0f, 0.4f }, 30.0f, hz));
    l.add (fp ("high", "High Limit", { 200.0f, 20000.0f, 0.0f, 0.4f }, 8000.0f, hz));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "listen", 1 }, "Listen Removed", false));
    l.add (fp ("out", "Output", { -12.0f, 12.0f, 0.1f }, 0.0f, db));
    return l;
}

RoomFixProcessor::RoomFixProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Room Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "RoomFix", createLayout())
{
}

bool RoomFixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void RoomFixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    learning = false;
    sampleRateNow = sampleRate;
    learnBuffer.assign ((size_t) (sampleRate * maxLearnSeconds), 0.0f);
    learnPos = 0;
    eq.prepare (sampleRate, getMainBusNumOutputChannels());
    seenGeneration = -1; // re-send resonances to the EQ
    dryCopy.setSize (2, std::max (1, samplesPerBlock));
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("out")));
    setLatencySamples (0);
}

void RoomFixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const int n = main.getNumSamples(), chans = main.getNumChannels();
    for (int ch = getMainBusNumInputChannels(); ch < chans; ++ch) main.clear (ch, 0, n);

    // new room profile from the GUI?
    if (handoverGeneration.load() != seenGeneration)
    {
        const juce::SpinLock::ScopedTryLockType tl (handoverLock);
        if (tl.isLocked())
        {
            eq.setResonances (handover.data(), handoverCount);
            seenGeneration = handoverGeneration.load();
        }
    }

    // LEARN: record the room (sidechain if connected, otherwise the main input)
    if (learning.load())
    {
        std::optional<juce::AudioBuffer<float>> side;
        if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0)
            side.emplace (getBusBuffer (buffer, true, 1));
        const auto& src = side ? *side : main;
        int pos = learnPos.load();
        const int room = (int) learnBuffer.size() - pos, todo = std::min (room, n);
        for (int i = 0; i < todo; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < src.getNumChannels(); ++c) m += src.getSample (c, i);
            learnBuffer[(size_t) (pos + i)] = m / (float) std::max (1, src.getNumChannels());
        }
        learnPos = pos + todo;
        if (todo < n) learning = false; // buffer full
    }

    auto peak = [&main] { float p = 0; for (int c = 0; c < main.getNumChannels(); ++c) p = std::max (p, main.getMagnitude (c, 0, main.getNumSamples())); return p; };
    inputLevel.store (std::max (peak(), inputLevel.load() * 0.85f));

    // feed the analyser (input, mono)
    {
        int s1, z1, s2, z2;
        analyserFifo.prepareToWrite (n, s1, z1, s2, z2);
        auto put = [&] (int dst, int count, int offset) {
            for (int i = 0; i < count; ++i)
            {
                float m = 0; for (int c = 0; c < chans; ++c) m += main.getSample (c, offset + i);
                analyserData[(size_t) (dst + i)] = m / (float) std::max (1, chans);
            }
        };
        put (s1, z1, 0); put (s2, z2, z1);
        analyserFifo.finishedWrite (z1 + z2);
    }

    const bool listen = param ("listen") > 0.5f;
    if (listen)
    {
        dryCopy.setSize (2, n, false, false, true);
        for (int c = 0; c < std::min (2, chans); ++c) dryCopy.copyFrom (c, 0, main, c, 0, n);
    }

    eq.setParameters (param ("amount"), param ("threshold"), param ("dynamic"), param ("low"), param ("high"));
    eq.process (main);

    if (listen) // hear only what is being removed
        for (int c = 0; c < std::min (2, chans); ++c)
        {
            auto* d = main.getWritePointer (c);
            const auto* dry = dryCopy.getReadPointer (c);
            for (int i = 0; i < n; ++i) d[i] = dry[i] - d[i];
        }

    outGain.setTargetValue (juce::Decibels::decibelsToGain (param ("out")));
    if (outGain.isSmoothing()) outGain.applyGain (main, n);
    else main.applyGain (outGain.getTargetValue());

    outputLevel.store (std::max (peak(), outputLevel.load() * 0.85f));
}

//==============================================================================
void RoomFixProcessor::setProfile (const RoomProfile& p)
{
    {
        const juce::ScopedLock sl (profileLock);
        profile = p;
    }
    ++profileVersion;
    const juce::SpinLock::ScopedLockType l (handoverLock);
    handoverCount = p.valid ? std::min ((int) p.resonances.size(), RoomProfile::maxCandidates) : 0;
    for (int i = 0; i < handoverCount; ++i) handover[(size_t) i] = p.resonances[(size_t) i];
    ++handoverGeneration;
}

juce::String RoomFixProcessor::loadRoomFile (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) return "Can't read this file (use WAV, AIFF, FLAC, OGG or MP3)";

    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 120.0));
    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();

    auto p = RoomProfile::analyse (mono.data(), len, reader->sampleRate, file.getFileName());
    if (! p.valid) return p.error;
    setProfile (p);
    return {};
}

void RoomFixProcessor::startLearning()
{
    learnPos = 0;
    learning = true;
}

juce::String RoomFixProcessor::stopLearning()
{
    learning = false;
    const int n = learnPos.load();
    auto p = RoomProfile::analyse (learnBuffer.data(), n, sampleRateNow, "Learned from input");
    if (! p.valid) return p.error;
    setProfile (p);
    return {};
}

//==============================================================================
juce::AudioProcessorEditor* RoomFixProcessor::createEditor() { return new RoomFixEditor (*this); }

void RoomFixProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("RoomProfile"), nullptr);
    state.appendChild (getProfile().toValueTree(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void RoomFixProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto prof = tree.getChildWithName ("RoomProfile");
            setProfile (RoomProfile::fromValueTree (prof));
            tree.removeChild (prof, nullptr);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RoomFixProcessor(); }
