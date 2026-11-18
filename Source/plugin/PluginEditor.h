#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Themes.h"

class BloomEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BloomEditor (BloomProcessor&);
    ~BloomEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr int W = 1040, H = 660;

    // Runs the animation smoothing to a steady state (used by the screenshot tool).
    void settle() { for (int i = 0; i < 30; ++i) timerCallback(); }

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    Knob& addKnob (const juce::String& id, const juce::String& label, const juce::String& tip, bool bipolar = false);
    void applyTheme (const juce::String& name);
    void rebuildPresetBox();
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);
    void drawScene (juce::Graphics&, juce::Rectangle<float>);
    void drawLadder (juce::Graphics&, juce::Rectangle<float>);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, const juce::String& label);
    float headstockX() const;                  // x of the headstock nut in the scene for the current distance
    void setDistanceFromX (float x);
    static juce::String noteName (double hz);

    BloomProcessor& proc;
    BloomLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ComboBox presetBox, themeBox, harmonicBox, keyBox, scaleBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, bloomNow { "BLOOM NOW" };
    juce::ToggleButton monoButton { "MONO" }, polyButton { "POLY" }, holdButton { "HOLD" }, limiterButton { "LIMITER" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> harmonicAttach, keyAttach, scaleAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttach, limiterAttach;

    juce::Rectangle<int> header, sceneArea, ladderPanel, ladderArea, readoutArea, sustainPanel, bloomPanel, ampPanel,
                         modePanel, globalPanel, meterArea, modeInfoArea;
    juce::Image grainTexture;
    float inMeter = 0, outMeter = 0, glow = 0, stringPhase = 0, stringAmp = 0;
    std::array<float, 4> ladder {};
    bool draggingHeadstock = false, hoverHeadstock = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BloomEditor)
};
