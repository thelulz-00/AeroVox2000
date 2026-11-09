#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Biquad.h"
#include <array>

// Classic channel vocoder.
// Modulator = the plugin's main input (your voice).
// Carrier   = internal synth played by MIDI (or a drone at "Pitch" when no notes are held),
//             white noise, or the sidechain input.
class Vocoder
{
public:
    enum Carrier { synth = 0, noise = 1, sidechain = 2 };

    static constexpr int numBands = 20;
    static constexpr int numVoices = 8;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double fMin = 120.0, fMax = std::min (7500.0, fs * 0.4);
        const double ratio = std::pow (fMax / fMin, 1.0 / (numBands - 1));
        const double q = std::sqrt (ratio) / (ratio - 1.0) * 1.2;

        for (int b = 0; b < numBands; ++b)
        {
            const double fc = fMin * std::pow (ratio, b);
            for (auto* f : { &modA[b], &modB[b], &carA[b], &carB[b] })
            {
                f->setBandPass (fs, fc, q);
                f->reset();
            }
            modEnv[b] = carEnv[b] = 0.0f;
        }

        for (auto* f : { &sibA, &sibB }) { f->setHighPass (fs, std::min (7000.0, fs * 0.42), 0.707); f->reset(); }

        attackCoef = coef (0.002);
        carAttack = coef (0.004);
        carRelease = coef (0.025);
        mixSmooth.reset (fs, 0.05);
        mixSmooth.setCurrentAndTargetValue (0.0f);
        droneGain.reset (fs, 0.03);
        droneGain.setCurrentAndTargetValue (1.0f);

        for (auto& v : voices) v = {};
        droneVoice = {};
    }

    void setParameters (bool on, float mix01, int carrierType, int droneNote, float releaseMs)
    {
        mixSmooth.setTargetValue (on ? mix01 : 0.0f);
        carrier = carrierType;
        drone = droneNote;
        releaseCoef = coef (releaseMs * 0.001);
    }

    void process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* side, const juce::MidiBuffer& midi)
    {
        const int n = main.getNumSamples();
        const bool active = mixSmooth.isSmoothing() || mixSmooth.getTargetValue() > 0.0f;

        int pos = 0;
        for (const auto meta : midi)
        {
            const int evPos = juce::jlimit (0, n, meta.samplePosition);
            if (active) render (main, side, pos, evPos);
            pos = evPos;
            handleMidi (meta.getMessage());
        }
        if (active) render (main, side, pos, n);
    }

private:
    struct Voice
    {
        int note = -1;
        bool held = false;
        float level = 0.0f;
        float ph1 = 0.0f, ph2 = 0.37f, inc1 = 0.0f, inc2 = 0.0f;
    };

    float coef (double seconds) const { return (float) (1.0 - std::exp (-1.0 / (std::max (seconds, 1.0e-4) * fs))); }

    void setPitch (Voice& v, int note) const
    {
        const double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        v.inc1 = (float) (hz * std::pow (2.0, 7.0 / 1200.0) / fs);  // +7 cents
        v.inc2 = (float) (hz * std::pow (2.0, -7.0 / 1200.0) / fs); // -7 cents
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            Voice* target = nullptr;
            for (auto& v : voices) if (v.note == m.getNoteNumber()) target = &v;
            if (target == nullptr) for (auto& v : voices) if (! v.held && v.level <= 0.0f) { target = &v; break; }
            if (target == nullptr) target = &voices[(size_t) (rrIndex++ % numVoices)];
            target->note = m.getNoteNumber();
            target->held = true;
            setPitch (*target, target->note);
        }
        else if (m.isNoteOff())
        {
            for (auto& v : voices) if (v.note == m.getNoteNumber()) v.held = false;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices) v.held = false;
        }
    }

    static inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    static inline float saw (float& ph, float inc) noexcept
    {
        float v = 2.0f * ph - 1.0f - polyBlep (ph, inc);
        ph += inc;
        if (ph >= 1.0f) ph -= 1.0f;
        return v;
    }

    inline float nextNoise() noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        return (float) (int32_t) rng * (1.0f / 2147483648.0f);
    }

    inline float nextSynth() noexcept
    {
        const float step = (float) (1.0 / (0.005 * fs)); // 5 ms note fade in/out
        float out = 0.0f;
        bool anyHeld = false;

        for (auto& v : voices)
        {
            v.level = v.held ? std::min (1.0f, v.level + step) : std::max (0.0f, v.level - step);
            anyHeld |= v.held;
            if (v.level > 0.0f)
                out += v.level * (saw (v.ph1, v.inc1) + saw (v.ph2, v.inc2));
        }

        droneGain.setTargetValue (anyHeld ? 0.0f : 1.0f);
        const float dg = droneGain.getNextValue();
        if (dg > 0.0f)
        {
            if (droneVoice.note != drone) { droneVoice.note = drone; setPitch (droneVoice, drone); }
            out += dg * (saw (droneVoice.ph1, droneVoice.inc1) + saw (droneVoice.ph2, droneVoice.inc2));
        }
        return out * 0.5f;
    }

    void render (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* side, int start, int end)
    {
        const int numCh = main.getNumChannels();
        const bool haveSide = side != nullptr && side->getNumChannels() > 0;
        const int scCh = haveSide ? side->getNumChannels() : 0;
        const int mode = (carrier == sidechain && ! haveSide) ? synth : carrier;

        for (int i = start; i < end; ++i)
        {
            float mod = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) mod += main.getSample (ch, i);
            mod /= (float) juce::jmax (1, numCh);

            float car;
            if (mode == noise)
                car = nextNoise();
            else if (mode == sidechain)
            {
                car = 0.0f;
                for (int ch = 0; ch < scCh; ++ch) car += side->getSample (ch, i);
                car /= (float) scCh;
            }
            else
                car = nextSynth() + 0.03f * nextNoise(); // a little noise helps consonants

            float voc = 0.0f;
            for (int b = 0; b < numBands; ++b)
            {
                const float m = std::abs (modB[b].process (modA[b].process (mod)));
                modEnv[b] += (m > modEnv[b] ? attackCoef : releaseCoef) * (m - modEnv[b]);

                const float c = carB[b].process (carA[b].process (car));
                const float ca = std::abs (c);
                carEnv[b] += (ca > carEnv[b] ? carAttack : carRelease) * (ca - carEnv[b]);

                // normalise each carrier band so any carrier gives a balanced spectrum
                voc += c * modEnv[b] / (carEnv[b] + 1.0e-3f);
            }

            voc = voc * 1.0f + 0.6f * sibB.process (sibA.process (mod)); // keep "s"/"t" sounds
            voc = juce::jlimit (-4.0f, 4.0f, voc);

            const float m = mixSmooth.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = main.getSample (ch, i);
                main.setSample (ch, i, dry * (1.0f - m) + voc * m);
            }
        }
    }

    double fs = 44100.0;
    std::array<Biquad, numBands> modA, modB, carA, carB;
    std::array<float, numBands> modEnv {}, carEnv {};
    Biquad sibA, sibB;
    float attackCoef = 0, releaseCoef = 0, carAttack = 0, carRelease = 0;

    std::array<Voice, numVoices> voices;
    Voice droneVoice;
    int drone = 48, carrier = synth;
    unsigned rrIndex = 0;
    uint32_t rng = 22222u;

    juce::SmoothedValue<float> mixSmooth, droneGain;
};
