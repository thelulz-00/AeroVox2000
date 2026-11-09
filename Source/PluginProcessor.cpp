#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <optional>

namespace
{
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String hz (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String ms (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String db (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* id, const char* name, juce::NormalisableRange<float> range,
                                                           float def, juce::String (*toText) (float, int))
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText));
    }

    std::unique_ptr<juce::AudioParameterBool> onParam (const char* id, const char* name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AeroVoxProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const juce::NormalisableRange<float> unit (0.0f, 1.0f);

    // Vocoder
    l.add (onParam ("voc_on", "Vocoder On", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "voc_carrier", 1 }, "Vocoder Carrier",
                                                          juce::StringArray { "Synth (MIDI)", "Noise", "Sidechain" }, 0));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "voc_pitch", 1 }, "Vocoder Pitch", 24, 84, 48,
               juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return juce::MidiMessage::getMidiNoteName (v, true, true, 3); })));
    l.add (floatParam ("voc_release", "Vocoder Release", { 5.0f, 300.0f, 0.0f, 0.5f }, 40.0f, ms));
    l.add (floatParam ("voc_mix", "Vocoder Mix", unit, 1.0f, pct));

    // Resonance EQ
    l.add (onParam ("res_on", "Resonance EQ On", true));
    l.add (floatParam ("res_depth", "Resonance Depth", unit, 0.5f, pct));
    l.add (floatParam ("res_sens", "Resonance Sensitivity", unit, 0.5f, pct));
    l.add (floatParam ("res_speed", "Resonance Speed", unit, 0.5f, pct));

    // Fresh Air
    l.add (onParam ("air_on", "Fresh Air On", true));
    l.add (floatParam ("air_amount", "Air Amount", unit, 0.3f, pct));
    l.add (floatParam ("air_freq", "Air Frequency", { 3000.0f, 20000.0f, 0.0f, 0.5f }, 12000.0f, hz));

    // Reverb
    l.add (onParam ("rev_on", "Reverb On", true));
    l.add (floatParam ("rev_size", "Reverb Size", unit, 0.5f, pct));
    l.add (floatParam ("rev_damp", "Reverb Damping", unit, 0.4f, pct));
    l.add (floatParam ("rev_predelay", "Reverb Pre-Delay", { 0.0f, 250.0f, 0.0f, 0.6f }, 20.0f, ms));
    l.add (floatParam ("rev_mix", "Reverb Mix", unit, 0.2f, pct));

    // Output
    l.add (floatParam ("out_gain", "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, db));
    return l;
}

AeroVoxProcessor::AeroVoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "AeroVox", createLayout())
{
}

bool AeroVoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void AeroVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = getMainBusNumOutputChannels();
    vocoder.prepare (sampleRate);
    resonance.prepare (sampleRate, numCh);
    air.prepare (sampleRate);
    reverb.prepare (sampleRate, samplesPerBlock);
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("out_gain")));
    setLatencySamples (resonance.getLatency());
}

void AeroVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);
    const int n = main.getNumSamples();

    // clear output channels that have no matching input
    for (int ch = getMainBusNumInputChannels(); ch < main.getNumChannels(); ++ch)
        main.clear (ch, 0, n);

    // sidechain (optional carrier for the vocoder) - a non-allocating view into the host buffer
    std::optional<juce::AudioBuffer<float>> side;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0)
        side.emplace (getBusBuffer (buffer, true, 1));
    const juce::AudioBuffer<float>* sidePtr = side ? &*side : nullptr;

    auto peak = [&main] { float p = 0.0f; for (int c = 0; c < main.getNumChannels(); ++c) p = std::max (p, main.getMagnitude (c, 0, main.getNumSamples())); return p; };
    inputLevel.store (std::max (peak(), inputLevel.load() * 0.85f));

    // chain: Vocoder -> Resonance EQ -> Fresh Air -> Reverb -> Output
    vocoder.setParameters (param ("voc_on") > 0.5f, param ("voc_mix"), (int) param ("voc_carrier"), (int) param ("voc_pitch"), param ("voc_release"));
    vocoder.process (main, sidePtr, midi);

    resonance.setParameters (param ("res_on") > 0.5f, param ("res_depth"), param ("res_sens"), param ("res_speed"));
    resonance.process (main);

    air.setParameters (param ("air_on") > 0.5f, param ("air_amount"), param ("air_freq"));
    air.process (main);

    reverb.setParameters (param ("rev_on") > 0.5f, param ("rev_size"), param ("rev_damp"), param ("rev_predelay"), param ("rev_mix"));
    reverb.process (main);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (param ("out_gain")));
    if (outGain.isSmoothing())
        outGain.applyGain (main, n);
    else
        main.applyGain (outGain.getTargetValue());

    outputLevel.store (std::max (peak(), outputLevel.load() * 0.85f));
}

juce::AudioProcessorEditor* AeroVoxProcessor::createEditor() { return new AeroVoxEditor (*this); }

void AeroVoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AeroVoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AeroVoxProcessor(); }
