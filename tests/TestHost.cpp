// Offline test host: loads the AeroVox 2000 VST3 and checks its audio behaviour.
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static String nameFor (const String& id)
{
    static const std::map<String, String> m {
        { "voc_on", "Vocoder On" },
        { "voc_carrier", "Vocoder Carrier" },
        { "voc_pitch", "Vocoder Pitch" },
        { "voc_release", "Vocoder Release" },
        { "voc_mix", "Vocoder Mix" },
        { "res_on", "Resonance EQ On" },
        { "res_depth", "Resonance Depth" },
        { "res_sens", "Resonance Sensitivity" },
        { "res_speed", "Resonance Speed" },
        { "air_on", "Fresh Air On" },
        { "air_amount", "Air Amount" },
        { "air_freq", "Air Frequency" },
        { "rev_on", "Reverb On" },
        { "rev_size", "Reverb Size" },
        { "rev_damp", "Reverb Damping" },
        { "rev_predelay", "Reverb Pre-Delay" },
        { "rev_mix", "Reverb Mix" },
        { "out_gain", "Output" } };
    auto it = m.find (id); return it != m.end() ? it->second : id;
}

struct Rig
{
    std::unique_ptr<AudioPluginInstance> plugin;
    double sr = 48000.0;
    int block = 512;

    void set (const String& id, float plainValue)
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (100) == nameFor (id))
                {
                    p->setValueNotifyingHost (p->getValueForText (String (plainValue)));
                    return;
                }
        std::printf ("unknown param %s\n", id.toRawUTF8());
    }
    void setNorm (const String& id, float v)
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (100) == nameFor (id)) { p->setValueNotifyingHost (v); return; }
        std::printf ("unknown param %s\n", id.toRawUTF8());
    }

    AudioBuffer<float> run (const AudioBuffer<float>& in, MidiBuffer* midiAll = nullptr)
    {
        AudioBuffer<float> out (2, in.getNumSamples());
        const int chans = std::max (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
        AudioBuffer<float> buf (chans, block);
        for (int pos = 0; pos < in.getNumSamples(); pos += block)
        {
            const int n = std::min (block, in.getNumSamples() - pos);
            buf.setSize (chans, n, false, false, true);
            buf.clear();
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, pos, n);
            MidiBuffer m;
            if (midiAll) for (auto meta : *midiAll) if (meta.samplePosition >= pos && meta.samplePosition < pos + n) m.addEvent (meta.getMessage(), meta.samplePosition - pos);
            plugin->processBlock (buf, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
        }
        return out;
    }
};

static double rms (const AudioBuffer<float>& b, int start, int len)
{
    double s = 0; for (int c = 0; c < b.getNumChannels(); ++c) for (int i = start; i < start + len; ++i) s += b.getSample (c, i) * b.getSample (c, i);
    return std::sqrt (s / (len * b.getNumChannels()));
}
static bool finite (const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}
static double toneLevelDb (const AudioBuffer<float>& b, int start, int len, double f, double sr)
{
    double re = 0, im = 0;
    for (int i = 0; i < len; ++i) { double w = 0.5 - 0.5 * std::cos (2 * MathConstants<double>::pi * i / len);
        double x = b.getSample (0, start + i) * w; re += x * std::cos (2 * MathConstants<double>::pi * f * i / sr); im += x * std::sin (2 * MathConstants<double>::pi * f * i / sr); }
    return 20 * std::log10 (std::sqrt (re * re + im * im) / (len * 0.25) + 1e-12);
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("usage: TestHost <plugin.vst3> [snapshot.png]\n"); return 2; }

    AudioPluginFormatManager fm; fm.addDefaultFormats();
    OwnedArray<PluginDescription> types;
    VST3PluginFormat vst3;
    vst3.findAllTypesForFile (types, argv[1]);
    check (types.size() == 1, "VST3 found: " + (types.isEmpty() ? String() : types[0]->name));
    if (types.isEmpty()) return 1;

    String err;
    Rig rig;
    if (argc > 3) rig.sr = String (argv[3]).getDoubleValue();
    std::printf ("       sample rate %.0f Hz\n", rig.sr);
    rig.plugin = fm.createPluginInstance (*types[0], rig.sr, rig.block, err);
    check (rig.plugin != nullptr, "plugin instantiated " + err);
    if (! rig.plugin) return 1;
    rig.plugin->enableAllBuses();
    rig.plugin->prepareToPlay (rig.sr, rig.block);
    const int lat = rig.plugin->getLatencySamples();
    check (lat == (rig.sr > 60000 ? 4096 : 2048), "latency reported: " + String (lat));
    check (rig.plugin->getParameters().size() >= 18, "parameters: " + String (rig.plugin->getParameters().size()));

    const int N = (int) rig.sr * 4;
    Random rnd (1);

    // 1) all modules off -> output must equal input delayed by latency (transparent)
    rig.setNorm ("voc_on", 0); rig.setNorm ("res_on", 0); rig.setNorm ("air_on", 0); rig.setNorm ("rev_on", 0);
    rig.set ("out_gain", 0.0f);
    AudioBuffer<float> noise (2, N);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < N; ++i) noise.setSample (c, i, (rnd.nextFloat() * 2 - 1) * 0.3f);
    rig.run (noise); // settle smoothers
    auto out = rig.run (noise);
    double errMax = 0;
    for (int i = lat + 4096; i < N; ++i) errMax = std::max (errMax, (double) std::abs (out.getSample (0, i) - noise.getSample (0, i - lat)));
    check (errMax < 1e-4, "all-off is transparent (max err " + String (errMax, 7) + ")");

    // 2) resonance EQ: pink-ish noise + strong resonance at 2.5 kHz -> peak reduced, broadband kept
    AudioBuffer<float> resIn (2, N);
    { float b0 = 0, b1 = 0; double ph = 0;
      for (int i = 0; i < N; ++i) { float w = rnd.nextFloat() * 2 - 1; b0 = 0.99f * b0 + 0.1f * w; b1 = b0 * 0.5f + w * 0.05f;
          ph += 2 * MathConstants<double>::pi * 2500.0 / rig.sr; float s = b1 + 0.25f * (float) std::sin (ph);
          resIn.setSample (0, i, s); resIn.setSample (1, i, s); } }
    rig.setNorm ("res_on", 1); rig.setNorm ("res_depth", 0.7f); rig.setNorm ("res_sens", 0.6f); rig.setNorm ("res_speed", 0.5f);
    out = rig.run (resIn);
    const int a = N / 2, len = 16384;
    const double before = toneLevelDb (resIn, a - lat, len, 2500, rig.sr), after = toneLevelDb (out, a, len, 2500, rig.sr);
    const double bbBefore = toneLevelDb (resIn, a - lat, len, 700, rig.sr), bbAfter = toneLevelDb (out, a, len, 700, rig.sr);
    check (before - after > 4.0, "resonance at 2.5 kHz reduced by " + String (before - after, 1) + " dB");
    check (std::abs (bbBefore - bbAfter) < 2.0, "broadband at 700 Hz changed by " + String (bbAfter - bbBefore, 2) + " dB");
    check (finite (out), "resonance output finite");

    // 3) fresh air raises highs, leaves lows
    rig.setNorm ("res_on", 0); rig.setNorm ("air_on", 1); rig.setNorm ("air_amount", 1.0f); rig.set ("air_freq", 10000.0f);
    AudioBuffer<float> two (2, N);
    for (int i = 0; i < N; ++i) { float s = 0.2f * (float) std::sin (2 * MathConstants<double>::pi * 200 * i / rig.sr) + 0.05f * (float) std::sin (2 * MathConstants<double>::pi * 15000 * i / rig.sr);
        two.setSample (0, i, s); two.setSample (1, i, s); }
    out = rig.run (two);
    const double hiGain = toneLevelDb (out, a, len, 15000, rig.sr) - toneLevelDb (two, a - lat, len, 15000, rig.sr);
    const double loGain = toneLevelDb (out, a, len, 200, rig.sr) - toneLevelDb (two, a - lat, len, 200, rig.sr);
    check (hiGain > 5.0 && std::abs (loGain) < 0.5, "air: 15 kHz " + String (hiGain, 1) + " dB, 200 Hz " + String (loGain, 2) + " dB");
    rig.setNorm ("air_on", 0);

    // 4) reverb: impulse produces a tail
    rig.setNorm ("rev_on", 1); rig.setNorm ("rev_mix", 0.5f); rig.setNorm ("rev_size", 0.7f);
    AudioBuffer<float> silence (2, N); silence.clear();
    rig.run (silence);
    AudioBuffer<float> imp (2, N); imp.clear(); imp.setSample (0, 1000, 1.0f); imp.setSample (1, 1000, 1.0f);
    out = rig.run (imp);
    const double tail = rms (out, 1000 + lat + (int) (0.5 * rig.sr), (int) (0.3 * rig.sr));
    check (tail > 1e-4 && finite (out), "reverb tail rms at 0.5 s: " + String (tail, 6));
    rig.setNorm ("rev_on", 0);

    // 5) vocoder: "voice" (pulse train through formant-ish filter, gated) with a MIDI chord
    rig.setNorm ("voc_on", 1); rig.setNorm ("voc_mix", 1.0f); rig.setNorm ("voc_carrier", 0.0f);
    AudioBuffer<float> voice (2, N);
    { double ph = 0; for (int i = 0; i < N; ++i) { ph += 140.0 / rig.sr; if (ph >= 1) ph -= 1;
        float s = (float) (ph < 0.05 ? 0.6 : 0.0) * ((i / 12000) % 2 == 0 ? 1.0f : 0.0f) + 0.01f * (rnd.nextFloat() - 0.5f);
        voice.setSample (0, i, s); voice.setSample (1, i, s); } }
    MidiBuffer midi;
    midi.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 100);
    midi.addEvent (MidiMessage::noteOn (1, 64, (uint8) 100), 100);
    midi.addEvent (MidiMessage::noteOn (1, 67, (uint8) 100), 100);
    midi.addEvent (MidiMessage::noteOff (1, 60), N - 20000);
    out = rig.run (voice, &midi);
    const double vIn = rms (voice, 0, N), vOut = rms (out, lat, N - lat);
    const double c4 = toneLevelDb (out, a, len, 261.63, rig.sr), g4 = toneLevelDb (out, a, len, 392.0, rig.sr), off = toneLevelDb (out, a, len, 300, rig.sr);
    check (finite (out) && vOut > vIn * 0.2 && vOut < vIn * 5.0, "vocoder level in " + String (vIn, 4) + " out " + String (vOut, 4));
    check (c4 > off + 6 && g4 > off + 6, "vocoder follows MIDI chord (C4 " + String (c4, 1) + " G4 " + String (g4, 1) + " vs 300Hz " + String (off, 1) + " dB)");

    // 6) everything on, CPU time
    for (auto id : { "voc_on", "res_on", "air_on", "rev_on" }) rig.setNorm (id, 1);
    const auto t0 = Time::getMillisecondCounterHiRes();
    out = rig.run (voice, &midi);
    const double ms = Time::getMillisecondCounterHiRes() - t0;
    check (finite (out), "all modules on: output finite");
    std::printf ("       CPU: %.1f ms to render %.1f s of audio (%.2f%% of one core)\n", ms, N / rig.sr, 100.0 * ms / (1000.0 * N / rig.sr));

    // 7) state save/restore
    MemoryBlock state; rig.plugin->getStateInformation (state);
    rig.setNorm ("rev_mix", 0.0f);
    rig.plugin->setStateInformation (state.getData(), (int) state.getSize());
    check (state.getSize() > 100, "state saved (" + String ((int) state.getSize()) + " bytes)");

    // editor snapshot
    if (argc > 2 && String (argv[2]) != "-")
    {
        rig.plugin->releaseResources(); rig.plugin->prepareToPlay (rig.sr, rig.block);
        for (auto id : { "voc_on", "res_on", "air_on", "rev_on" }) rig.setNorm (id, String (id) == "voc_on" ? 0.0f : 1.0f);
        rig.setNorm ("rev_mix", 0.2f);
        rig.run (resIn);
        std::unique_ptr<AudioProcessorEditor> ed (rig.plugin->createEditorIfNeeded());
        check (ed != nullptr, "editor created " + String (ed ? ed->getWidth() : 0) + "x" + String (ed ? ed->getHeight() : 0));
        if (ed)
        {
            ed->setVisible (true);
            for (int i = 0; i < 20; ++i) { MessageManager::getInstance()->runDispatchLoopUntil (50); rig.run (resIn); }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            File f (argv[2]); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
            std::printf ("       snapshot written: %s\n", argv[2]);
            ed.reset();
        }
    }
    rig.plugin->releaseResources();
    rig.plugin.reset();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
