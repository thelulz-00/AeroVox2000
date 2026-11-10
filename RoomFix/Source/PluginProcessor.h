#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RoomEQ.h"

class RoomFixProcessor : public juce::AudioProcessor
{
public:
    RoomFixProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==== room profile (message thread) =======================================
    RoomProfile getProfile() const { const juce::ScopedLock sl (profileLock); return profile; }
    void setProfile (const RoomProfile&);
    juce::String loadRoomFile (const juce::File&);   // returns an error message, empty if ok

    // LEARN: record the room straight from the input (or the sidechain, if connected)
    static constexpr double maxLearnSeconds = 30.0;
    void startLearning();
    juce::String stopLearning();   // analyses what was recorded
    bool isLearning() const { return learning.load(); }
    double learnedSeconds() const { return learnPos.load() / std::max (1.0, sampleRateNow); }

    // for the GUI
    juce::AudioProcessorValueTreeState apvts;
    RoomEQ eq;
    std::atomic<float> inputLevel { 0.0f }, outputLevel { 0.0f };
    juce::AbstractFifo analyserFifo { 1 << 15 };
    std::vector<float> analyserData = std::vector<float> (1 << 15);
    double getSampleRateNow() const { return sampleRateNow; }
    std::atomic<int> profileVersion { 0 };

private:
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::CriticalSection profileLock;
    RoomProfile profile;

    // hand-over of resonances to the audio thread
    juce::SpinLock handoverLock;
    std::array<RoomResonance, RoomProfile::maxCandidates> handover {};
    int handoverCount = 0;
    std::atomic<int> handoverGeneration { 0 };
    int seenGeneration = -1;

    std::vector<float> learnBuffer;
    std::atomic<int> learnPos { 0 };
    std::atomic<bool> learning { false };
    double sampleRateNow = 48000.0;

    juce::AudioBuffer<float> dryCopy;
    juce::SmoothedValue<float> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomFixProcessor)
};
