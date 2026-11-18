#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout BloomProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pct = [] (float v, int) { return String (v, 0) + "%"; };
    auto ms = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, 0) + " ms"; };
    auto db = [] (float v, int) { return String (v, 1) + " dB"; };
    auto attrs = [] (auto fn) { return AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto add = [&] (const char* id, const char* name, NormalisableRange<float> range, float def, auto fn)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, attrs (fn)));
    };

    // Sustain
    add ("sustain", "Sustain", NormalisableRange<float> (0, 100, 0.1f), 85.0f,
         [] (float v, int) { return v >= 99.5f ? String ("Infinite") : String (v, 0) + "%"; });
    add ("swell", "Swell", NormalisableRange<float> (0, 2000, 1, 0.5f), 0.0f, ms);
    add ("release", "Release", NormalisableRange<float> (10, 4000, 1, 0.4f), 400.0f, ms);
    // Bloom
    add ("bloomTime", "Bloom Time", NormalisableRange<float> (0.2f, 15, 0.01f, 0.45f), 3.0f,
         [] (float v, int) { return String (v, v < 10 ? 1 : 0) + " s"; });
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "harmonic", 1 }, "Harmonic",
                                                         StringArray { "Fundamental", "Octave", "12th", "2 Octaves", "Auto" }, 1));
    add ("bloomAmt", "Bloom Amount", NormalisableRange<float> (0, 100, 0.1f), 70.0f, pct);
    add ("wobble", "Wobble", NormalisableRange<float> (0, 100, 0.1f), 20.0f, pct);
    // Amp
    add ("ampDist", "Amp Distance", NormalisableRange<float> (0.1f, 4, 0.01f, 0.6f), 1.0f,
         [] (float v, int) { return String (v, 2) + " m"; });
    add ("tone", "Tone", NormalisableRange<float> (0, 100, 0.1f), 60.0f, pct);
    add ("body", "Body", NormalisableRange<float> (0, 100, 0.1f), 40.0f, pct);
    add ("drive", "Drive", NormalisableRange<float> (0, 100, 0.1f), 30.0f, pct);
    // Mode
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                         StringArray { "Mono (tracks one note)", "Poly (chords, key + scale)" }, 0));
    StringArray keys;
    for (int i = 0; i < 12; ++i) keys.add (eb::noteName (i));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keys, 4));
    StringArray scales;
    for (auto& s : eb::builtinScales()) scales.add (s.getName());
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scales, 0));
    // Global
    add ("gate", "Gate", NormalisableRange<float> (-80, -20, 0.1f), -52.0f, db);
    add ("mix", "Mix", NormalisableRange<float> (0, 100, 0.1f), 100.0f, pct);
    add ("output", "Output", NormalisableRange<float> (-24, 12, 0.1f), 0.0f, db);
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "limiter", 1 }, "Limiter", true));
    // Performance
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "hold", 1 }, "Hold", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "bloomNow", 1 }, "Bloom Now", false));
    return { p.begin(), p.end() };
}

BloomProcessor::BloomProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HaloBloom", createLayout())
{
    setCurrentProgram (0);   // start on the first factory preset
}

bool BloomProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    const bool okOut = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    const bool okIn = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    return okOut && okIn;
}

void BloomProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, samplesPerBlock);
}

eb::Params BloomProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    eb::Params p;
    p.sustain = v ("sustain") / 100.0f;
    p.swellMs = v ("swell");
    p.releaseMs = v ("release");
    p.bloomTime = v ("bloomTime");
    p.harmonic = (eb::Harmonic) (int) v ("harmonic");
    p.bloomAmount = v ("bloomAmt") / 100.0f;
    p.wobble = v ("wobble") / 100.0f;
    p.ampDistance = v ("ampDist");
    p.tone = v ("tone") / 100.0f;
    p.body = v ("body") / 100.0f;
    p.drive = v ("drive") / 100.0f;
    p.mode = (eb::Mode) (int) v ("mode");
    p.key = (int) v ("key");
    p.scaleIndex = (int) v ("scale");
    p.gateDb = v ("gate");
    p.mix = v ("mix") / 100.0f;
    p.outputDb = v ("output");
    p.limiter = v ("limiter") > 0.5f;
    p.hold = v ("hold") > 0.5f || midiHold.load();
    p.bloomNow = v ("bloomNow") > 0.5f || midiBloom.load();
    return p;
}

void BloomProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isController())
        {
            if (msg.getControllerNumber() == kHoldCC) midiHold.store (msg.getControllerValue() >= 64);
            if (msg.getControllerNumber() == kBloomCC) midiBloom.store (msg.getControllerValue() >= 64);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff() || msg.isResetAllControllers())
        {
            midiHold.store (false);
            midiBloom.store (false);
        }
    }
}

void BloomProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, buffer.getNumSamples());
    engine.setParams (readParams());
    engine.process (buffer.getArrayOfReadPointers(), inCh, buffer.getArrayOfWritePointers(), outCh, buffer.getNumSamples());
}

int BloomProcessor::getNumPrograms() { return (int) eb::factoryPresets().size(); }

const juce::String BloomProcessor::getProgramName (int index)
{
    const auto& ps = eb::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) ps.size()) ? juce::String (ps[(size_t) index].name) : juce::String();
}

void BloomProcessor::setCurrentProgram (int index)
{
    const auto& ps = eb::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) ps.size())) return;
    currentProgram = index;
    const auto& p = ps[(size_t) index].p;
    auto set = [this] (const char* id, float value)
    {
        if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set ("sustain", p.sustain * 100.0f);
    set ("swell", p.swellMs);
    set ("release", p.releaseMs);
    set ("bloomTime", p.bloomTime);
    set ("harmonic", (float) (int) p.harmonic);
    set ("bloomAmt", p.bloomAmount * 100.0f);
    set ("wobble", p.wobble * 100.0f);
    set ("ampDist", p.ampDistance);
    set ("tone", p.tone * 100.0f);
    set ("body", p.body * 100.0f);
    set ("drive", p.drive * 100.0f);
    set ("mode", (float) (int) p.mode);
    set ("key", (float) p.key);
    set ("scale", (float) p.scaleIndex);
    set ("gate", p.gateDb);
    set ("mix", p.mix * 100.0f);
    set ("output", p.outputDb);
    // limiter, hold and bloom-now are performance/safety settings: presets leave them alone
}

void BloomProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("theme", themeName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void BloomProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            themeName = tree.getProperty ("theme", "Midnight").toString();
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* BloomProcessor::createEditor() { return new BloomEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BloomProcessor(); }
