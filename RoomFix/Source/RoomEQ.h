#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "DSP/Biquad.h"
#include "RoomProfile.h"
#include <array>
#include <atomic>

// Dynamic correction EQ: one narrow bell per learned room resonance.
//
// Rule: a band never cuts more than the room adds at that frequency
// (learned boost x Amount). With "Dynamic" up, it cuts only while the incoming
// signal actually piles up energy at that frequency (i.e. the room is ringing),
// so the voice is left alone the rest of the time. Zero latency.
class RoomEQ
{
public:
    static constexpr int maxBands = 16;

    struct Shared // read by the GUI
    {
        std::atomic<int> numBands { 0 };
        std::array<std::atomic<float>, maxBands> freq {}, q {}, cutDb {};
    } shared;

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        numCh = juce::jlimit (1, 2, numChannels);
        powerCoef = coef (0.025);
        attack = coefBlock (0.010);
        release = coefBlock (0.150);
        bandsDirty = true;
        for (auto& b : bands) b.reset();
    }

    // called from the audio thread with a copy of the profile's resonances
    void setResonances (const RoomResonance* r, int count)
    {
        numCandidates = std::min (count, RoomProfile::maxCandidates);
        for (int i = 0; i < numCandidates; ++i) candidates[(size_t) i] = r[i];
        bandsDirty = true;
    }

    void setParameters (float amount01, float thresholdDb, float dynamic01, float lowHz, float highHz)
    {
        amount = amount01;
        dynamic = dynamic01;
        if (thresholdDb != threshold || lowHz != lowF || highHz != highF)
        {
            threshold = thresholdDb; lowF = lowHz; highF = highHz;
            bandsDirty = true;
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (bandsDirty) rebuildBands();

        const int n = buffer.getNumSamples();
        const int chans = std::min (numCh, buffer.getNumChannels());
        constexpr int sub = 32;

        for (int start = 0; start < n; start += sub)
        {
            const int len = std::min (sub, n - start);

            // 1) detection on the mono sum
            for (int i = 0; i < len; ++i)
            {
                float m = 0.0f;
                for (int c = 0; c < chans; ++c) m += buffer.getSample (c, start + i);
                m /= (float) chans;
                for (int b = 0; b < activeBands; ++b)
                {
                    auto& band = bands[(size_t) b];
                    const float nv = band.narrow.process (m), wv = band.wide.process (m);
                    band.pNarrow += powerCoef * (nv * nv - band.pNarrow);
                    band.pWide += powerCoef * (wv * wv - band.pWide);
                }
            }

            // 2) decide each band's cut and update its filter
            for (int b = 0; b < activeBands; ++b)
            {
                auto& band = bands[(size_t) b];
                float factor = 0.0f;
                if (band.pWide > 1.0e-10f)
                {
                    // how much this narrow band sticks out compared with a flat spectrum
                    const float excess = 10.0f * std::log10 ((band.pNarrow + 1.0e-20f) / band.pWide) + band.flatOffsetDb;
                    factor = juce::jlimit (0.0f, 1.0f, excess / band.res.boostDb);
                }
                const float target = std::min (24.0f, amount * band.res.boostDb * ((1.0f - dynamic) + dynamic * factor));
                band.cut += (target > band.cut ? attack : release) * (target - band.cut);
                if (target <= 0.0f && band.cut < 0.02f) band.cut = 0.0f;

                if (std::abs (band.cut - band.coefCut) > 0.02f || (band.cut == 0.0f && band.coefCut != 0.0f))
                {
                    band.coefCut = band.cut;
                    for (int c = 0; c < numCh; ++c) band.eq[(size_t) c].setPeak (fs, band.res.freq, band.res.q, -band.cut);
                }
                shared.cutDb[(size_t) b].store (band.cut, std::memory_order_relaxed);
            }

            // 3) apply the bells
            for (int c = 0; c < chans; ++c)
            {
                auto* d = buffer.getWritePointer (c, start);
                for (int b = 0; b < activeBands; ++b)
                {
                    auto& f = bands[(size_t) b].eq[(size_t) c];
                    if (bands[(size_t) b].coefCut < 0.01f) continue; // 0 dB bell = pass-through, skip
                    for (int i = 0; i < len; ++i) d[i] = f.process (d[i]);
                }
            }
        }
    }

private:
    struct Band
    {
        RoomResonance res;
        Biquad narrow, wide;
        std::array<Biquad, 2> eq;
        float pNarrow = 0, pWide = 0, cut = 0, coefCut = 0, flatOffsetDb = 0;
        void reset()
        {
            narrow.reset(); wide.reset(); for (auto& e : eq) { e.reset(); e.setPeak (48000.0, 1000.0, 1.0, 0.0); }
            pNarrow = pWide = cut = coefCut = 0;
        }
    };

    float coef (double sec) const { return (float) (1.0 - std::exp (-1.0 / (sec * fs))); }
    float coefBlock (double sec) const { return (float) (1.0 - std::exp (-32.0 / (sec * fs))); }

    void rebuildBands()
    {
        bandsDirty = false;
        // choose: strong enough, inside the range, strongest first (candidates are already sorted)
        std::array<RoomResonance, maxBands> chosen;
        int count = 0;
        for (int i = 0; i < numCandidates && count < maxBands; ++i)
        {
            const auto& r = candidates[(size_t) i];
            if (r.boostDb >= threshold && r.freq >= lowF && r.freq <= highF && r.freq < fs * 0.45)
                chosen[(size_t) count++] = r;
        }

        // keep filter state of bands that didn't change (no clicks while turning knobs)
        std::array<Band, maxBands> next;
        for (int i = 0; i < count; ++i)
        {
            bool reused = false;
            for (int j = 0; j < activeBands; ++j)
                if (std::abs (bands[(size_t) j].res.freq - chosen[(size_t) i].freq) < 0.01f)
                {
                    next[(size_t) i] = bands[(size_t) j];
                    reused = true;
                    break;
                }
            if (! reused)
            {
                next[(size_t) i].reset();
                for (int c = 0; c < 2; ++c) next[(size_t) i].eq[(size_t) c].setPeak (fs, chosen[(size_t) i].freq, chosen[(size_t) i].q, 0.0);
            }
            auto& b = next[(size_t) i];
            b.res = chosen[(size_t) i];
            b.narrow.setBandPass (fs, b.res.freq, b.res.q);
            b.wide.setBandPass (fs, b.res.freq, wideQ);
            // for white noise, power ratio narrow/wide = wideQ/q -> offset makes a flat spectrum read 0 dB
            b.flatOffsetDb = 10.0f * std::log10 (b.res.q / wideQ);
        }
        bands = next;
        activeBands = count;

        shared.numBands.store (count);
        for (int i = 0; i < count; ++i)
        {
            shared.freq[(size_t) i].store (bands[(size_t) i].res.freq);
            shared.q[(size_t) i].store (bands[(size_t) i].res.q);
        }
    }

    static constexpr float wideQ = 0.7f;
    double fs = 48000.0;
    int numCh = 2;
    float powerCoef = 0, attack = 0, release = 0;
    float amount = 0.8f, dynamic = 0.5f, threshold = 3.0f, lowF = 30.0f, highF = 8000.0f;

    std::array<RoomResonance, RoomProfile::maxCandidates> candidates {};
    int numCandidates = 0;
    std::array<Band, maxBands> bands;
    int activeBands = 0;
    bool bandsDirty = true;
};
