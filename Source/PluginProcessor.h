#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Vocoder.h"
#include "DSP/ResonanceSuppressor.h"
#include "DSP/AirExciter.h"
#include "DSP/ReverbModule.h"

class AeroVoxProcessor : public juce::AudioProcessor
{
public:
    AeroVoxProcessor();
    ~AeroVoxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    ResonanceSuppressor resonance;
    std::atomic<float> inputLevel { 0.0f }, outputLevel { 0.0f };

private:
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    Vocoder vocoder;
    AirExciter air;
    ReverbModule reverb;
    juce::SmoothedValue<float> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeroVoxProcessor)
};
