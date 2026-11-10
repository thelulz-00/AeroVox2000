#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

// One room resonance found in the room recording.
struct RoomResonance
{
    float freq = 0.0f;    // Hz
    float q = 1.0f;       // width (centre / -3 dB bandwidth)
    float boostDb = 0.0f; // how much the room lifts this frequency above its neighbours
};

// "Fingerprint" of a room, learned from a recording of the room alone.
struct RoomProfile
{
    static constexpr int curvePoints = 256;   // display curve, log-spaced 20 Hz .. 20 kHz
    static constexpr int maxCandidates = 32;

    bool valid = false;
    juce::String source;
    double seconds = 0.0;
    juce::String error;
    std::vector<RoomResonance> resonances;            // strongest first
    std::array<float, curvePoints> curveDb {};        // measured room spectrum (relative dB)
    std::array<float, curvePoints> envelopeDb {};     // its smooth shape (what "flat" means for this room)

    static float curveFreq (int i) { return 20.0f * std::pow (1000.0f, (float) i / (curvePoints - 1)); }

    //==========================================================================
    // Analyse a mono recording of the room.
    static RoomProfile analyse (const float* x, int n, double fs, const juce::String& sourceName)
    {
        RoomProfile p;
        p.source = sourceName;
        p.seconds = n / fs;

        if (n < (int) fs)
        {
            p.error = "Recording too short (need at least 1 s, 10-30 s is best)";
            return p;
        }

        double rmsSum = 0;
        for (int i = 0; i < n; ++i) rmsSum += (double) x[i] * x[i];
        if (std::sqrt (rmsSum / n) < 1.0e-5)
        {
            p.error = "Recording is silent";
            return p;
        }

        // 1) long FFT -> ~1.5 Hz resolution, averaged over the whole file (Welch method)
        int order = 12;
        while ((1 << (order + 1)) <= fs / 1.4 && order < 16) ++order;
        while ((1 << order) > n && order > 12) --order;
        const int N = 1 << order, hop = N / 4, bins = N / 2 + 1;
        const double binHz = fs / N;

        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) N), buf ((size_t) N * 2);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / N);

        std::vector<double> power ((size_t) bins, 0.0);
        int frames = 0;
        for (int start = 0; start + N <= n; start += hop)
        {
            for (int i = 0; i < N; ++i) buf[(size_t) i] = x[start + i] * win[(size_t) i];
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k < bins; ++k) power[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
            ++frames;
        }

        std::vector<float> db ((size_t) bins);
        for (int k = 0; k < bins; ++k) db[(size_t) k] = (float) (10.0 * std::log10 (power[(size_t) k] / frames + 1e-20));

        // 2) light smoothing (~1/40 octave) to remove measurement noise
        std::vector<float> fine ((size_t) bins);
        boxSmooth (db, fine, [] (int k) { return std::max (1, (int) (k * 0.012f)); });

        // 3) smooth envelope (~0.5 octave, at least +-30 Hz). Iterated "clipping" pushes it
        //    under the peaks, so a resonance can't lift the reference it is measured against.
        std::vector<float> env ((size_t) bins), clipped ((size_t) bins);
        auto envWidth = [binHz] (int k) { return std::max ((int) std::ceil (30.0 / binHz), (int) (k * 0.25f)); };
        boxSmooth (fine, env, envWidth);
        for (int pass = 0; pass < 5; ++pass)
        {
            for (int k = 0; k < bins; ++k) clipped[(size_t) k] = std::min (fine[(size_t) k], env[(size_t) k] + 0.5f);
            boxSmooth (clipped, env, envWidth);
        }

        // 4) peaks that stick out of the envelope = resonances
        std::vector<float> d ((size_t) bins);
        for (int k = 0; k < bins; ++k) d[(size_t) k] = fine[(size_t) k] - env[(size_t) k];

        const int kMin = std::max (2, (int) (25.0 / binHz)), kMax = std::min (bins - 3, (int) (std::min (18000.0, fs * 0.45) / binHz));
        std::vector<RoomResonance> found;
        std::vector<std::pair<float, float>> spans;

        for (int k = kMin; k <= kMax; ++k)
        {
            const float v = d[(size_t) k];
            if (v < 1.5f) continue;
            const int r = std::max (2, (int) (k * 0.01f));
            bool isMax = true;
            for (int j = std::max (0, k - r); j <= std::min (bins - 1, k + r) && isMax; ++j)
                if (d[(size_t) j] > v || (d[(size_t) j] == v && j < k)) isMax = false;
            if (! isMax) continue;

            // width at half the peak height in dB (the same definition the correction bell uses)
            const float edge = v * 0.5f;
            int lo = k, hi = k;
            while (lo > 1 && d[(size_t) lo - 1] > edge) --lo;
            while (hi < bins - 2 && d[(size_t) hi + 1] > edge) ++hi;
            auto interp = [&] (int a, int b) { // fractional bin where d crosses 'edge' between a (inside) and b (outside)
                const float da = d[(size_t) a], dbv = d[(size_t) b];
                return (float) a + (da - edge) / std::max (1.0e-6f, da - dbv) * (float) (b - a);
            };
            const float fLo = interp (lo, lo - 1) * (float) binHz, fHi = interp (hi, hi + 1) * (float) binHz;

            // refine centre with a parabola through the 3 top points
            const float y0 = fine[(size_t) k - 1], y1 = fine[(size_t) k], y2 = fine[(size_t) k + 1];
            const float denom = y0 - 2 * y1 + y2;
            const float offset = std::abs (denom) > 1e-6f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (y0 - y2) / denom) : 0.0f;
            const float f = ((float) k + offset) * (float) binHz;

            RoomResonance res;
            res.freq = f;
            res.q = juce::jlimit (1.5f, 60.0f, f / std::max ((float) binHz, fHi - fLo));
            res.boostDb = v;
            found.push_back (res);
            spans.push_back ({ fLo, fHi });
        }

        // strongest first, drop peaks that sit inside a stronger one
        std::vector<int> idx (found.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int) i;
        std::sort (idx.begin(), idx.end(), [&] (int a, int b) { return found[(size_t) a].boostDb > found[(size_t) b].boostDb; });
        for (int i : idx)
        {
            bool inside = false;
            for (auto& kept : p.resonances)
            {
                const float bw = kept.freq / kept.q;
                if (std::abs (found[(size_t) i].freq - kept.freq) < std::max (bw, 2.0f * (float) binHz)) { inside = true; break; }
            }
            if (! inside) p.resonances.push_back (found[(size_t) i]);
            if ((int) p.resonances.size() >= maxCandidates) break;
        }
        juce::ignoreUnused (spans);

        // 5) display curves, relative to the median level of the envelope
        std::vector<float> mids;
        for (int k = (int) (100 / binHz); k < std::min (bins, (int) (10000 / binHz)); ++k) mids.push_back (env[(size_t) k]);
        std::nth_element (mids.begin(), mids.begin() + (long) mids.size() / 2, mids.end());
        const float ref = mids.empty() ? 0.0f : mids[mids.size() / 2];
        for (int i = 0; i < curvePoints; ++i)
        {
            const double f0 = curveFreq (i) / std::pow (1000.0, 0.5 / (curvePoints - 1));
            const double f1 = curveFreq (i) * std::pow (1000.0, 0.5 / (curvePoints - 1));
            int a = juce::jlimit (1, bins - 1, (int) std::floor (f0 / binHz)), b = juce::jlimit (1, bins - 1, (int) std::ceil (f1 / binHz));
            float m = -200.0f, e = 0.0f;
            for (int k = a; k <= b; ++k) { m = std::max (m, fine[(size_t) k]); e += env[(size_t) k]; }
            p.curveDb[(size_t) i] = m - ref;
            p.envelopeDb[(size_t) i] = e / (float) (b - a + 1) - ref;
        }

        p.valid = true;
        return p;
    }

    //==========================================================================
    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("RoomProfile");
        t.setProperty ("valid", valid, nullptr);
        t.setProperty ("source", source, nullptr);
        t.setProperty ("seconds", seconds, nullptr);
        juce::StringArray res, curve, envl;
        for (auto& r : resonances) res.add (juce::String (r.freq, 3) + ":" + juce::String (r.q, 3) + ":" + juce::String (r.boostDb, 3));
        for (int i = 0; i < curvePoints; ++i) { curve.add (juce::String (curveDb[(size_t) i], 2)); envl.add (juce::String (envelopeDb[(size_t) i], 2)); }
        t.setProperty ("resonances", res.joinIntoString (" "), nullptr);
        t.setProperty ("curve", curve.joinIntoString (" "), nullptr);
        t.setProperty ("envelope", envl.joinIntoString (" "), nullptr);
        return t;
    }

    static RoomProfile fromValueTree (const juce::ValueTree& t)
    {
        RoomProfile p;
        if (! t.isValid()) return p;
        p.valid = t.getProperty ("valid", false);
        p.source = t.getProperty ("source", "").toString();
        p.seconds = t.getProperty ("seconds", 0.0);
        for (auto& item : juce::StringArray::fromTokens (t.getProperty ("resonances", "").toString(), " ", ""))
        {
            auto parts = juce::StringArray::fromTokens (item, ":", "");
            if (parts.size() == 3)
                p.resonances.push_back ({ parts[0].getFloatValue(), parts[1].getFloatValue(), parts[2].getFloatValue() });
        }
        auto curve = juce::StringArray::fromTokens (t.getProperty ("curve", "").toString(), " ", "");
        auto envl = juce::StringArray::fromTokens (t.getProperty ("envelope", "").toString(), " ", "");
        for (int i = 0; i < curvePoints && i < curve.size() && i < envl.size(); ++i)
        {
            p.curveDb[(size_t) i] = curve[i].getFloatValue();
            p.envelopeDb[(size_t) i] = envl[i].getFloatValue();
        }
        p.valid = p.valid && curve.size() == curvePoints;
        return p;
    }

private:
    template <typename WidthFn>
    static void boxSmooth (const std::vector<float>& src, std::vector<float>& dst, WidthFn width)
    {
        const int n = (int) src.size();
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int k = 0; k < n; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + src[(size_t) k];
        for (int k = 0; k < n; ++k)
        {
            const int w = width (k);
            const int a = std::max (0, k - w), b = std::min (n - 1, k + w);
            dst[(size_t) k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (b - a + 1));
        }
    }
};
