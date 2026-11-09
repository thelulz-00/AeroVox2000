#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

// Automatic resonance suppressor ("smart EQ").
// Every ~10 ms it analyses the spectrum (STFT, 75% overlap), estimates the smooth
// spectral envelope, finds narrow peaks that stick out above it (resonances) and
// turns them down with a dynamic, per-frequency gain. Stereo-linked.
class ResonanceSuppressor
{
public:
    static constexpr int displayPoints = 128;

    // Shared with the editor (written by the audio thread, read by the GUI).
    std::array<std::atomic<float>, displayPoints> displaySpectrumDb {};
    std::array<std::atomic<float>, displayPoints> displayReductionDb {};

    int getLatency() const noexcept { return fftSize; }

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        order = sampleRate > 60000.0 ? 12 : 11;
        fftSize = 1 << order;
        hop = fftSize / 4;
        numBins = fftSize / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);

        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i) // sqrt-Hann (periodic) used for analysis and synthesis
            window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / fftSize));
        // sum of hann^2 overlap at 75% is 2.0 -> divide by 2
        olaScale = 0.5f;

        channels.clear();
        channels.resize ((size_t) juce::jmax (1, numChannels));
        for (auto& c : channels)
        {
            c.in.assign ((size_t) fftSize, 0.0f);
            c.out.assign ((size_t) fftSize, 0.0f);
            c.spec.assign ((size_t) fftSize * 2, 0.0f);
        }

        magDb.assign ((size_t) numBins, -120.0f);
        envDb.assign ((size_t) numBins, -120.0f);
        prefix.assign ((size_t) numBins + 1, 0.0);
        target.assign ((size_t) numBins, 0.0f);
        reduction.assign ((size_t) numBins, 0.0f);
        tmp.assign ((size_t) numBins, 0.0f);
        gains.assign ((size_t) numBins, 1.0f);
        smoothPow.assign ((size_t) numBins, 0.0f);
        pos = 0;
        hopCounter = 0;

        for (int i = 0; i < displayPoints; ++i) { displaySpectrumDb[(size_t) i] = -100.0f; displayReductionDb[(size_t) i] = 0.0f; }

        // display bin ranges (log-spaced 20 Hz .. 20 kHz)
        for (int i = 0; i <= displayPoints; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / displayPoints);
            displayEdges[(size_t) i] = juce::jlimit (1, numBins - 1, (int) std::round (f * fftSize / fs));
        }
    }

    void setParameters (bool on, float depth01, float sensitivity01, float speed01)
    {
        enabled = on;
        depth = depth01;
        sensitivity = sensitivity01;
        speed = speed01;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
        const int n = buffer.getNumSamples();

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& c = channels[(size_t) ch];
                auto* d = buffer.getWritePointer (ch);
                c.in[(size_t) pos] = d[i];
                d[i] = c.out[(size_t) pos];
                c.out[(size_t) pos] = 0.0f;
            }
            pos = (pos + 1) & (fftSize - 1);

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                processFrame (numCh);
            }
        }
    }

private:
    struct Channel { std::vector<float> in, out, spec; };

    void processFrame (int numCh)
    {
        // 1) analysis
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = channels[(size_t) ch];
            for (int i = 0; i < fftSize; ++i)
                c.spec[(size_t) i] = c.in[(size_t) ((pos + i) & (fftSize - 1))] * window[(size_t) i];
            std::fill (c.spec.begin() + fftSize, c.spec.end(), 0.0f);
            fft->performRealOnlyForwardTransform (c.spec.data(), true);
        }

        // 2) stereo-linked power, averaged over ~30 ms so we chase stable resonances,
        //    not the random peaks every noisy signal has, then converted to dB
        const float avg = (float) (1.0 - std::exp (-(hop / fs) / 0.03));
        for (int k = 0; k < numBins; ++k)
        {
            float p = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float re = channels[(size_t) ch].spec[(size_t) (2 * k)];
                const float im = channels[(size_t) ch].spec[(size_t) (2 * k + 1)];
                p = std::max (p, re * re + im * im);
            }
            smoothPow[(size_t) k] += avg * (p - smoothPow[(size_t) k]);
            magDb[(size_t) k] = 10.0f * std::log10 (smoothPow[(size_t) k] + 1.0e-12f);
        }

        // 3) smooth spectral envelope (~1/2 octave wide), computed twice:
        //    the second pass ignores the peaks so they don't lift the envelope
        smoothEnvelope (magDb, envDb);
        for (int k = 0; k < numBins; ++k)
            tmp[(size_t) k] = std::min (magDb[(size_t) k], envDb[(size_t) k] + 2.0f);
        smoothEnvelope (tmp, envDb);

        // 4) how far each bin sticks out above the envelope -> wanted reduction
        const float threshold = 12.0f - 9.0f * sensitivity;  // 12 dB .. 3 dB
        const float maxCut = 18.0f * depth;
        const float lowBin = (float) (80.0 * fftSize / fs), highBin = (float) (17000.0 * fftSize / fs);

        for (int k = 0; k < numBins; ++k)
        {
            float r = 0.0f;
            if (enabled && magDb[(size_t) k] > -80.0f && k > lowBin && k < highBin)
            {
                const float excess = magDb[(size_t) k] - envDb[(size_t) k] - threshold;
                if (excess > 0.0f)
                    r = std::min (maxCut, excess * (0.4f + 0.6f * depth) * depth * 1.6f);
            }
            target[(size_t) k] = r;
        }

        // 5) blur a little across frequency so cuts are smooth bells, not needles
        for (int pass = 0; pass < 2; ++pass)
        {
            tmp[0] = target[0];
            tmp[(size_t) numBins - 1] = target[(size_t) numBins - 1];
            for (int k = 1; k < numBins - 1; ++k)
                tmp[(size_t) k] = std::max (target[(size_t) k], 0.25f * target[(size_t) k - 1] + 0.5f * target[(size_t) k] + 0.25f * target[(size_t) k + 1]);
            std::swap (tmp, target);
        }

        // 6) smooth over time: fast attack, release set by "Speed"
        const double frameSec = hop / fs;
        const double releaseSec = 0.30 - 0.27 * speed;  // 300 ms .. 30 ms
        const double attackSec = 0.012 - 0.008 * speed; //  12 ms .. 4 ms
        const float att = (float) (1.0 - std::exp (-frameSec / attackSec));
        const float rel = (float) (1.0 - std::exp (-frameSec / releaseSec));

        for (int k = 0; k < numBins; ++k)
        {
            auto& r = reduction[(size_t) k];
            r += (target[(size_t) k] > r ? att : rel) * (target[(size_t) k] - r);
            gains[(size_t) k] = std::pow (10.0f, -r / 20.0f);
        }

        // 7) apply gains, resynthesise, overlap-add
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = channels[(size_t) ch];
            for (int k = 0; k < numBins; ++k)
            {
                c.spec[(size_t) (2 * k)] *= gains[(size_t) k];
                c.spec[(size_t) (2 * k + 1)] *= gains[(size_t) k];
            }
            fft->performRealOnlyInverseTransform (c.spec.data());
            for (int i = 0; i < fftSize; ++i)
                c.out[(size_t) ((pos + i) & (fftSize - 1))] += c.spec[(size_t) i] * window[(size_t) i] * olaScale;
        }

        updateDisplay();
    }

    void smoothEnvelope (const std::vector<float>& src, std::vector<float>& dst)
    {
        prefix[0] = 0.0;
        for (int k = 0; k < numBins; ++k)
            prefix[(size_t) k + 1] = prefix[(size_t) k] + src[(size_t) k];

        for (int k = 0; k < numBins; ++k)
        {
            const int w = std::max (3, (int) (k * 0.18f));
            const int a = std::max (0, k - w), b = std::min (numBins - 1, k + w);
            dst[(size_t) k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (b - a + 1));
        }
    }

    void updateDisplay()
    {
        // convert to a level that roughly reads as dBFS for a sine
        const float norm = 20.0f * std::log10 ((float) fftSize * 0.25f);
        for (int i = 0; i < displayPoints; ++i)
        {
            const int a = displayEdges[(size_t) i], b = std::max (a, displayEdges[(size_t) i + 1] - 1);
            float m = -200.0f, r = 0.0f;
            for (int k = a; k <= b; ++k)
            {
                m = std::max (m, magDb[(size_t) k]);
                r = std::max (r, reduction[(size_t) k]);
            }
            const float old = displaySpectrumDb[(size_t) i].load (std::memory_order_relaxed);
            const float nowDb = m - norm;
            displaySpectrumDb[(size_t) i].store (nowDb > old ? nowDb : old + 0.25f * (nowDb - old), std::memory_order_relaxed);
            displayReductionDb[(size_t) i].store (r, std::memory_order_relaxed);
        }
    }

    double fs = 44100.0;
    int order = 11, fftSize = 2048, hop = 512, numBins = 1025;
    int pos = 0, hopCounter = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    float olaScale = 0.5f;
    std::vector<Channel> channels;
    std::vector<float> smoothPow, magDb, envDb, target, reduction, tmp, gains;
    std::vector<double> prefix;
    std::array<int, displayPoints + 1> displayEdges {};

    std::atomic<bool> enabled { true };
    std::atomic<float> depth { 0.5f }, sensitivity { 0.5f }, speed { 0.5f };
};
