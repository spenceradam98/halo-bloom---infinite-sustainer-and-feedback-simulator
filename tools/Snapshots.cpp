// Renders the editor in every theme to PNG files (for docs). Build with -DHALO_BUILD_SNAPSHOTS=ON;
// on Linux run under a display (e.g. xvfb-run). Usage: halo_snapshots <out-dir> [preset-index]
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/plugin/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory());
    dir.createDirectory();
    BloomProcessor proc;
    proc.setCurrentProgram (argc > 2 ? juce::String (argv[2]).getIntValue() : 6);   // Octave Bloom
    proc.prepareToPlay (48000, 512);

    // play a held A (110 Hz) for a few seconds so the bloom, ladder and meters show something
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const double hz = 110.0, sr = 48000.0;
    int64_t t = 0;
    for (int b = 0; b < 230; ++b)
    {
        for (int i = 0; i < 512; ++i, ++t)
        {
            double s = 0;
            for (int h = 1; h <= 8; ++h) s += (1.0 / h) * std::sin (2.0 * juce::MathConstants<double>::pi * hz * h * (double) t / sr);
            const float v = (float) (0.3 * s * std::exp (-(double) t / (1.2 * sr)));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
    }

    for (auto& th : allThemes())
    {
        proc.setThemeName (th.name);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        if (auto* be = dynamic_cast<BloomEditor*> (ed.get())) be->settle();
        ed->setSize (BloomEditor::W, BloomEditor::H);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f = dir.getChildFile (th.name.replace (" ", "-").toLowerCase() + ".png");
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}
