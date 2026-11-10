// Tests for RoomFix 2000: room analysis precision, correction, voice protection, state, learn, file load.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

using namespace juce;
static int failures = 0;
static void check (bool ok, const String& what) { std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8()); if (! ok) ++failures; }

static constexpr double fs = 48000.0;
struct TrueRes { float f, q, gain; };
static const TrueRes room[] = { { 87.0f, 10.0f, 10.0f }, { 163.0f, 8.0f, 8.0f }, { 1240.0f, 12.0f, 6.0f } };

static std::vector<float> noise (int n, int seed)
{
    Random r (seed); std::vector<float> v ((size_t) n); float lp = 0;
    for (auto& x : v) { const float w = r.nextFloat() * 2 - 1; lp = 0.995f * lp + 0.05f * w; x = 0.2f * w + lp; } // gently tilted noise
    return v;
}
static std::vector<float> throughRoom (std::vector<float> v)
{
    for (auto& r : room) { Biquad b; b.setPeak (fs, r.f, r.q, r.gain); for (auto& x : v) x = b.process (x); }
    return v;
}
static double bandDb (const std::vector<float>& v, double f, int skip = 0)
{
    const int order = 15, N = 1 << order; dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2); double acc = 0; int frames = 0;
    const int k = (int) std::round (f * N / fs);
    for (int s = skip; s + N <= (int) v.size(); s += N / 4)
    {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = v[(size_t) (s + i)] * (0.5f - 0.5f * std::cos (2 * MathConstants<float>::pi * i / N));
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        for (int j = k - 2; j <= k + 2; ++j) acc += (double) buf[(size_t) j] * buf[(size_t) j];
        ++frames;
    }
    return 10 * std::log10 (acc / frames + 1e-30);
}
static void setP (RoomFixProcessor& p, const char* id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
static std::vector<float> run (RoomFixProcessor& p, const std::vector<float>& in)
{
    std::vector<float> out (in.size()); AudioBuffer<float> buf (2, 512); MidiBuffer m;
    for (int pos = 0; pos < (int) in.size(); pos += 512)
    {
        const int n = std::min (512, (int) in.size() - pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) buf.setSample (c, i, in[(size_t) (pos + i)]);
        p.processBlock (buf, m);
        for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = buf.getSample (0, i);
    }
    return out;
}
static std::vector<float> sine (int n, double f, float a) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = a * (float) std::sin (2 * MathConstants<double>::pi * f * i / fs); return v; }
static double rmsDb (const std::vector<float>& v, int from) { double s = 0; for (size_t i = (size_t) from; i < v.size(); ++i) s += v[i] * v[i]; return 10 * std::log10 (s / (double) (v.size() - (size_t) from) + 1e-30); }

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;

    // A) analysis precision
    auto roomRec = throughRoom (noise ((int) fs * 20, 1));
    auto prof = RoomProfile::analyse (roomRec.data(), (int) roomRec.size(), fs, "synthetic room");
    check (prof.valid, "room analysed");
    for (auto& t : room)
    {
        const RoomResonance* best = nullptr;
        for (auto& r : prof.resonances) if (std::abs (r.freq - t.f) < t.f * 0.03f && (! best || r.boostDb > best->boostDb)) best = &r;
        check (best && std::abs (best->freq - t.f) < std::max (1.5f, t.f * 0.005f) && std::abs (best->boostDb - t.gain) < 3.0f,
               "found " + String (t.f) + " Hz (+" + String (t.gain) + " dB): " + (best ? String (best->freq, 2) + " Hz, +" + String (best->boostDb, 1) + " dB, Q " + String (best->q, 1) : String ("MISSING")));
    }
    int extra = 0;
    for (auto& r : prof.resonances)
    {
        bool known = false; for (auto& t : room) if (std::abs (r.freq - t.f) < t.f * 0.05f) known = true;
        if (! known && r.boostDb >= 3.0f) { ++extra; std::printf ("       false positive: %.1f Hz +%.1f dB\n", r.freq, r.boostDb); }
    }
    check (extra == 0, "no false resonances above 3 dB (" + String (extra) + ")");

    RoomFixProcessor p;
    p.prepareToPlay (fs, 512);
    p.setProfile (prof);
    const int N = (int) fs * 12, settle = (int) fs;

    // B) static correction flattens the room
    setP (p, "amount", 1.0f); setP (p, "dynamic", 0.0f); setP (p, "threshold", 3.0f);
    auto dry = noise (N, 7); auto wet = throughRoom (dry);
    auto out = run (p, wet);
    for (auto& t : room)
    {
        const double before = bandDb (wet, t.f, settle) - bandDb (dry, t.f, settle), after = bandDb (out, t.f, settle) - bandDb (dry, t.f, settle);
        check (std::abs (after) < 2.5, String (t.f) + " Hz: room adds +" + String (before, 1) + " dB -> after RoomFix " + String (after, 1) + " dB");
    }
    for (double f : { 400.0, 600.0, 3000.0, 8000.0 })
    {
        const double d = bandDb (out, f, settle) - bandDb (wet, f, settle);
        check (std::abs (d) < 0.5, "untouched at " + String ((int) f) + " Hz: " + String (d, 2) + " dB");
    }

    // C) voice protection: tones away from room resonances keep their level (dynamic 50%)
    setP (p, "dynamic", 0.5f);
    for (double f : { 220.0, 400.0, 660.0, 2000.0 })
    {
        auto s = sine (N, f, 0.3f); auto o = run (p, s);
        check (std::abs (rmsDb (o, settle) - rmsDb (s, settle)) < 0.3, "voice tone " + String ((int) f) + " Hz change: " + String (rmsDb (o, settle) - rmsDb (s, settle), 2) + " dB");
    }

    // D) dynamic 100%: a note that hits the room mode gets cut, others don't
    setP (p, "dynamic", 1.0f);
    {
        auto s = sine (N, 87.0, 0.3f); auto o = run (p, s);
        check (rmsDb (s, settle) - rmsDb (o, settle) > 6.0, "dynamic: tone on 87 Hz room mode cut by " + String (rmsDb (s, settle) - rmsDb (o, settle), 1) + " dB");
        auto s2 = sine (N, 300.0, 0.3f); auto o2 = run (p, s2);
        check (std::abs (rmsDb (o2, settle) - rmsDb (s2, settle)) < 0.2, "dynamic: tone at 300 Hz change " + String (rmsDb (o2, settle) - rmsDb (s2, settle), 2) + " dB");
    }

    // E) amount 0 = bit-exact pass-through, zero latency
    setP (p, "amount", 0.0f); run (p, wet);
    {
        auto o = run (p, wet); double e = 0; for (size_t i = 0; i < o.size(); ++i) e = std::max (e, (double) std::abs (o[i] - wet[i]));
        check (e < 1e-6, "amount 0 is transparent, no latency (max err " + String (e, 8) + ")");
    }

    // F) listen: hear only the removed part
    setP (p, "amount", 1.0f); setP (p, "dynamic", 0.0f); setP (p, "listen", 1.0f);
    {
        auto o = run (p, sine (N, 400.0, 0.3f)); auto o2 = run (p, sine (N, 87.0, 0.3f));
        // a 0.3 sine is -13.5 dB rms: the 400 Hz residue must be tiny, the 87 Hz one large
        check (rmsDb (o, settle) < -38.0 && rmsDb (o2, settle) > -20.0, "listen: 400 Hz residue " + String (rmsDb (o, settle), 1) + " dB, 87 Hz residue " + String (rmsDb (o2, settle), 1) + " dB");
    }
    setP (p, "listen", 0.0f);

    // G) profile is saved with the project
    {
        MemoryBlock mb; p.getStateInformation (mb);
        RoomFixProcessor p2; p2.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto r2 = p2.getProfile();
        check (r2.valid && r2.resonances.size() == prof.resonances.size() && std::abs (r2.resonances[0].freq - prof.resonances[0].freq) < 0.01f,
               "room profile restored from saved project (" + String ((int) mb.getSize()) + " bytes)");
    }

    // H) LEARN from the input
    {
        RoomFixProcessor p3; p3.prepareToPlay (fs, 512);
        p3.startLearning();
        run (p3, throughRoom (noise ((int) fs * 12, 3)));
        const auto err = p3.stopLearning();
        const auto pr = p3.getProfile();
        bool has87 = false; for (auto& r : pr.resonances) if (std::abs (r.freq - 87.0f) < 1.5f && r.boostDb > 6) has87 = true;
        check (err.isEmpty() && pr.valid && has87, "LEARN from input finds 87 Hz (" + String (pr.seconds, 1) + " s) " + err);
    }

    // I) load a WAV file
    {
        File f = File::getSpecialLocation (File::tempDirectory).getChildFile ("roomfix_test_room.wav"); f.deleteFile();
        {
            WavAudioFormat wav; auto os = f.createOutputStream();
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.release(), fs, 1, 24, {}, 0));
            AudioBuffer<float> b (1, (int) roomRec.size()); b.copyFrom (0, 0, roomRec.data(), (int) roomRec.size());
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        RoomFixProcessor p4; p4.prepareToPlay (fs, 512);
        const auto err = p4.loadRoomFile (f);
        check (err.isEmpty() && p4.getProfile().valid, "load room WAV file " + err);
        f.deleteFile();
        check (p4.loadRoomFile (File ("/nonexistent.wav")).isNotEmpty(), "bad file gives an error message");
        std::vector<float> shortRec ((size_t) 1000, 0.1f);
        check (! RoomProfile::analyse (shortRec.data(), 1000, fs, "x").valid, "too-short recording rejected");
    }

    // CPU
    {
        setP (p, "threshold", 1.0f); setP (p, "dynamic", 0.5f);
        auto t0 = Time::getMillisecondCounterHiRes(); run (p, wet);
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        std::printf ("       CPU: %.2f%% of one core (%d bands)\n", 100.0 * ms / (1000.0 * N / fs), p.eq.shared.numBands.load());
        setP (p, "threshold", 3.0f);
    }

    // snapshot
    if (argc > 1)
    {
        setP (p, "dynamic", 0.5f);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto voice = wet;
        for (int k = 0; k < 6; ++k) { run (p, std::vector<float> (voice.begin() + k * 48000, voice.begin() + (k + 1) * 48000)); MessageManager::getInstance()->runDispatchLoopUntil (60); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, argc > 2 ? String (argv[2]).getFloatValue() : 1.0f);
        File out2 (argv[1]); out2.deleteFile(); FileOutputStream os (out2); PNGImageFormat().writeImageToStream (img, os);
        ed.reset();
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
