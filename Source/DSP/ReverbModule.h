#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"
#include <array>
#include <vector>

// Stereo plate/room reverb with pre-delay and a low cut on the wet signal.
class ReverbModule
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        reverb.setSampleRate (fs);
        reverb.reset();
        chunk = juce::jmax (64, maxBlock);
        wet.setSize (2, chunk);
        delaySize = (int) (fs * 0.26) + 1;
        for (auto& d : delay) d.assign ((size_t) delaySize, 0.0f);
        writePos = 0;
        for (auto& f : lowCut) { f.setHighPass (fs, 180.0, 0.707); f.reset(); }
        mix.reset (fs, 0.05);
        mix.setCurrentAndTargetValue (0.0f);
    }

    void setParameters (bool on, float size01, float damp01, float preDelayMs, float mix01)
    {
        mix.setTargetValue (on ? mix01 : 0.0f);
        juce::Reverb::Parameters p;
        p.roomSize = 0.3f + 0.69f * size01;
        p.damping = damp01;
        p.width = 1.0f;
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        reverb.setParameters (p);
        preDelay = juce::jlimit (0, delaySize - 1, (int) (preDelayMs * 0.001 * fs));
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! mix.isSmoothing() && mix.getTargetValue() <= 0.0f)
            return;

        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();

        for (int start = 0; start < n; start += chunk)
        {
            const int len = std::min (chunk, n - start);

            // pre-delay + low cut into the wet buffer
            for (int i = 0; i < len; ++i)
            {
                const int rp = (writePos - preDelay + delaySize) % delaySize;
                for (int c = 0; c < numCh; ++c)
                {
                    delay[(size_t) c][(size_t) writePos] = buffer.getSample (c, start + i);
                    wet.setSample (c, i, lowCut[(size_t) c].process (delay[(size_t) c][(size_t) rp]));
                }
                writePos = (writePos + 1) % delaySize;
            }

            if (numCh == 2) reverb.processStereo (wet.getWritePointer (0), wet.getWritePointer (1), len);
            else            reverb.processMono (wet.getWritePointer (0), len);

            for (int i = 0; i < len; ++i)
            {
                const float m = mix.getNextValue();
                const float dryG = std::cos (m * juce::MathConstants<float>::halfPi);
                const float wetG = std::sin (m * juce::MathConstants<float>::halfPi) * 0.6f;
                for (int c = 0; c < numCh; ++c)
                    buffer.setSample (c, start + i, buffer.getSample (c, start + i) * dryG + wet.getSample (c, i) * wetG);
            }
        }
    }

private:
    double fs = 44100.0;
    juce::Reverb reverb;
    juce::AudioBuffer<float> wet;
    int chunk = 512;
    std::array<std::vector<float>, 2> delay;
    int delaySize = 1, writePos = 0, preDelay = 0;
    std::array<Biquad, 2> lowCut;
    juce::SmoothedValue<float> mix;
};
