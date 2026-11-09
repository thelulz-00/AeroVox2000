// Renders the editor to a PNG (with some test audio flowing so the analyser shows data).
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    AeroVoxProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    juce::Random rnd (3);
    double ph = 0;
    float b0 = 0;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    for (int blk = 0; blk < 400; ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float w = rnd.nextFloat() * 2 - 1;
            b0 = 0.98f * b0 + 0.1f * w;
            ph += 2 * juce::MathConstants<double>::pi * 3150.0 / 48000.0;
            const float s = 0.5f * b0 + 0.04f * w + 0.12f * (float) std::sin (ph) + 0.08f * (float) std::sin (ph * 0.2857);
            buf.setSample (0, i, s); buf.setSample (1, i, s); buf.setSample (2, i, 0); buf.setSample (3, i, 0);
        }
        proc.processBlock (buf, midi);
        if (blk % 10 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f);
    juce::File f (argv[1]); f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    return 0;
}
