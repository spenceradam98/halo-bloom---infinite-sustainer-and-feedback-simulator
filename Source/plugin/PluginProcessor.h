#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Presets.h"

class BloomProcessor : public juce::AudioProcessor
{
public:
    BloomProcessor();
    ~BloomProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- UI hooks (message thread) ----
    const eb::Status& getStatus() const { return engine.getStatus(); }
    eb::Params currentParams() const { return readParams(); }
    bool midiHoldDown() const { return midiHold.load(); }
    bool midiBloomDown() const { return midiBloom.load(); }

    juce::String getThemeName() const { return themeName; }
    void setThemeName (const juce::String& t) { themeName = t; }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // MIDI footswitch mapping: CC64 (sustain pedal) = Hold, CC67 (soft pedal) = Bloom Now.
    static constexpr int kHoldCC = 64, kBloomCC = 67;

private:
    eb::Params readParams() const;
    void handleMidi (const juce::MidiBuffer&);

    eb::Engine engine;
    std::atomic<bool> midiHold { false }, midiBloom { false };
    juce::String themeName { "Midnight" };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BloomProcessor)
};
