#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"
#include <array>

// "Fresh Air": a wide, gentle high shelf plus a touch of harmonic excitement
// generated only from the top end, for open, breathy, expensive-sounding highs.
class AirExciter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        amount.reset (fs, 0.05);
        amount.setCurrentAndTargetValue (0.0f);
        lastAmount = -1.0f;
        lastFreq = -1.0f;
        for (auto& c : ch) { c.shelf.reset(); c.hp1.reset(); c.hp2.reset(); }
    }

    void setParameters (bool on, float amount01, float freqHz)
    {
        amount.setTargetValue (on ? amount01 : 0.0f);
        freq = freqHz;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), (int) ch.size());
        const int n = buffer.getNumSamples();

        if (! amount.isSmoothing() && amount.getTargetValue() <= 0.0f)
            return;

        constexpr int sub = 32;
        for (int start = 0; start < n; start += sub)
        {
            const int len = std::min (sub, n - start);
            const float a = amount.skip (len);
            updateCoefficients (a);

            const float excite = a * 0.7f;
            for (int c = 0; c < numCh; ++c)
            {
                auto& st = ch[(size_t) c];
                auto* d = buffer.getWritePointer (c, start);
                for (int i = 0; i < len; ++i)
                {
                    const float x = d[i];
                    const float h = st.hp1.process (x);
                    const float e = st.hp2.process (std::tanh (h * 4.0f) * 0.25f);
                    d[i] = st.shelf.process (x) + excite * e;
                }
            }
        }
    }

private:
    void updateCoefficients (float a)
    {
        if (std::abs (a - lastAmount) < 1.0e-4f && freq == lastFreq)
            return;
        lastAmount = a;
        lastFreq = freq;
        const double f = std::min ((double) freq, fs * 0.42);
        for (auto& c : ch)
        {
            c.shelf.setHighShelf (fs, f, 0.55, 10.0 * a);   // up to +10 dB
            c.hp1.setHighPass (fs, f * 0.7, 0.707);
            c.hp2.setHighPass (fs, f * 0.7, 0.707);
        }
    }

    struct State { Biquad shelf, hp1, hp2; };
    std::array<State, 2> ch;
    double fs = 44100.0;
    float freq = 12000.0f, lastAmount = -1.0f, lastFreq = -1.0f;
    juce::SmoothedValue<float> amount;
};
