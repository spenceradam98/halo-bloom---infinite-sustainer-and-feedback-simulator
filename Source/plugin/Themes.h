// UI themes. Each theme changes colours and a few rendering choices (knob style, wheel glow, panel texture).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct Theme
{
    juce::String name;
    juce::Colour bg, bg2, panel, edge, text, dim, accent, accent2, knob, track;
    int knobStyle = 0;     // 0 = arc ring, 1 = chicken-head pointer, 2 = flat dot, 3 = LED ring
    bool glow = false;     // neon glow on the wheel and arcs
    bool grain = false;    // wood/paper grain texture on the background
    juce::Colour voiceColour (int v) const;
    juce::Colour onBg2() const;      // readable text colour on bg2 (combo boxes, buttons)
    juce::Colour onBg() const;       // readable text colour on the window background (header)
};

const std::vector<Theme>& allThemes();
const Theme& themeByName (const juce::String& name);

class BloomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BloomLookAndFeel();
    void setTheme (const Theme& t);
    const Theme& theme() const { return *th; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

private:
    const Theme* th = nullptr;
};
