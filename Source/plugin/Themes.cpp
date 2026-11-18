#include "Themes.h"

using namespace juce;

Colour Theme::voiceColour (int v) const
{
    static const float hues[] = { 0.0f, 0.18f, 0.42f, 0.64f };
    const float base = accent2.getHue();
    return Colour::fromHSV (std::fmod (base + hues[v & 3], 1.0f), jlimit (0.35f, 0.9f, accent2.getSaturation()),
                            jlimit (0.55f, 1.0f, accent2.getBrightness()), 1.0f);
}

static bool isDark (Colour c) { return c.getPerceivedBrightness() < 0.5f; }

Colour Theme::onBg2() const { return isDark (bg2) == isDark (text) ? bg2.contrasting (0.85f) : text; }
Colour Theme::onBg() const { return isDark (bg) == isDark (text) ? bg.contrasting (0.85f) : text; }

const std::vector<Theme>& allThemes()
{
    static const std::vector<Theme> t = [] {
        std::vector<Theme> v;
        auto add = [&] (const char* name, uint32 bg, uint32 bg2, uint32 panel, uint32 edge, uint32 text, uint32 dim,
                        uint32 accent, uint32 accent2, uint32 knob, uint32 track, int style, bool glow, bool grain)
        {
            v.push_back ({ name, Colour (bg), Colour (bg2), Colour (panel), Colour (edge), Colour (text), Colour (dim),
                           Colour (accent), Colour (accent2), Colour (knob), Colour (track), style, glow, grain });
        };
        //   name            bg          bg2         panel       edge        text        dim         accent      accent2     knob        track
        add ("Midnight",     0xff0e1420, 0xff151d2d, 0xff18223a, 0xff2a3a5c, 0xffe6ecf5, 0xff7d8ba6, 0xff4fd1c5, 0xffb48cff, 0xff222e48, 0xff2a3550, 0, false, false);
        add ("Sunburst",     0xff2a1408, 0xff4a2410, 0xfff1e4c8, 0xffb88a50, 0xff2a1a0c, 0xff7a5a38, 0xffd2601a, 0xffb02a20, 0xff1c1c1c, 0xffd8c6a4, 1, false, true);
        add ("Neon Fret",    0xff07060c, 0xff120a1c, 0xff100c18, 0xff3a1f55, 0xfff4eaff, 0xff8a78a8, 0xffff2fa8, 0xff27e3ff, 0xff18122a, 0xff241a3a, 3, true,  false);
        add ("Stompbox",     0xff1d3a2a, 0xff245036, 0xff2c5a3e, 0xff8fbf9f, 0xfff3ead2, 0xffa8c2ae, 0xfff2c14e, 0xffff7b54, 0xfff3ead2, 0xff1a3324, 2, false, false);
        add ("Studio Grey",  0xff26282b, 0xff303337, 0xff383b40, 0xff55595f, 0xffeeeeee, 0xff9aa0a6, 0xffff9f1c, 0xff6ec1ff, 0xff4a4e54, 0xff2a2c30, 0, false, false);
        add ("Manuscript",   0xffefe9dc, 0xffe3dccb, 0xfffaf6ec, 0xffc9bea4, 0xff222018, 0xff7a7262, 0xff2f5d8a, 0xffa33b3b, 0xff2b2b2b, 0xffe2dac6, 2, false, true);
        return v;
    }();
    return t;
}

const Theme& themeByName (const String& name)
{
    for (auto& t : allThemes()) if (t.name == name) return t;
    return allThemes().front();
}

BloomLookAndFeel::BloomLookAndFeel() { setTheme (allThemes().front()); }

void BloomLookAndFeel::setTheme (const Theme& t)
{
    th = &t;
    setColour (ComboBox::backgroundColourId, t.bg2);
    setColour (ComboBox::textColourId, t.onBg2());
    setColour (ComboBox::outlineColourId, t.edge);
    setColour (ComboBox::arrowColourId, t.dim);
    setColour (PopupMenu::backgroundColourId, t.bg2);
    setColour (PopupMenu::textColourId, t.onBg2());
    setColour (PopupMenu::headerTextColourId, t.accent);
    setColour (PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.35f));
    setColour (PopupMenu::highlightedTextColourId, t.onBg2());
    setColour (TextButton::textColourOffId, t.onBg2());
    setColour (TextButton::textColourOnId, t.onBg2());
    setColour (TextButton::buttonColourId, t.bg2);
    setColour (Label::textColourId, t.text);
    setColour (TooltipWindow::backgroundColourId, t.bg2);
    setColour (TooltipWindow::textColourId, t.onBg2());
    setColour (BubbleComponent::backgroundColourId, t.bg2);
    setColour (BubbleComponent::outlineColourId, t.edge);
    setColour (Slider::textBoxTextColourId, t.text);
    setColour (AlertWindow::backgroundColourId, t.bg2);
    setColour (AlertWindow::textColourId, t.text);
}

Font BloomLookAndFeel::getComboBoxFont (ComboBox&) { return Font (FontOptions (13.0f)); }
Font BloomLookAndFeel::getPopupMenuFont() { return Font (FontOptions (14.0f)); }

void BloomLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto& t = *th;
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float d = jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float r = d * 0.5f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties()["bipolar"];
    const float from = bipolar ? (start + end) * 0.5f : start;
    const Colour col = s.getProperties().contains ("colour") ? Colour ((uint32) (int64) s.getProperties()["colour"]) : t.accent;
    const bool enabled = s.isEnabled();

    auto arc = [&] (float a0, float a1, float radius, float thick, Colour cc)
    {
        Path p;
        p.addCentredArc (c.x, c.y, radius, radius, 0, jmin (a0, a1), jmax (a0, a1), true);
        g.setColour (cc);
        g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
    };

    switch (t.knobStyle)
    {
        case 1:   // chicken-head pointer on a cream plate (Sunburst)
        {
            g.setColour (Colours::black.withAlpha (0.25f));
            g.fillEllipse (Rectangle<float> (d * 0.82f, d * 0.82f).withCentre (c.translated (1.5f, 2.5f)));
            for (int i = 0; i <= 10; ++i)
            {
                const float a = start + (end - start) * i / 10.0f;
                g.setColour (t.dim);
                g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.86f, a), c.getPointOnCircumference (r * 0.98f, a)), 1.2f);
            }
            g.setColour (t.knob);
            g.fillEllipse (Rectangle<float> (d * 0.58f, d * 0.58f).withCentre (c));
            Path ptr;
            ptr.addTriangle (-r * 0.16f, 0, r * 0.16f, 0, 0, -r * 0.80f);
            ptr.addEllipse (-r * 0.22f, -r * 0.22f, r * 0.44f, r * 0.44f);
            g.fillPath (ptr, AffineTransform::rotation (angle).translated (c));
            g.setColour (Colour (0xfff1e4c8));
            g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.25f, angle), c.getPointOnCircumference (r * 0.72f, angle)), 2.0f);
            break;
        }
        case 2:   // flat dot knob (Stompbox / Manuscript)
        {
            arc (start, end, r * 0.9f, 2.0f, t.dim.withAlpha (0.4f));
            arc (from, angle, r * 0.9f, 2.5f, enabled ? col : t.dim);
            g.setColour (t.knob);
            g.fillEllipse (Rectangle<float> (d * 0.66f, d * 0.66f).withCentre (c));
            g.setColour (t.knob.contrasting (0.8f));
            g.fillEllipse (Rectangle<float> (d * 0.1f, d * 0.1f).withCentre (c.getPointOnCircumference (r * 0.48f, angle)));
            break;
        }
        case 3:   // LED ring (Neon Fret)
        {
            const int leds = 21;
            for (int i = 0; i < leds; ++i)
            {
                const float a = start + (end - start) * i / (float) (leds - 1);
                const bool lit = (a >= jmin (from, angle) - 0.001f && a <= jmax (from, angle) + 0.001f);
                const auto p = c.getPointOnCircumference (r * 0.88f, a);
                if (lit && enabled)
                {
                    g.setColour (col.withAlpha (0.25f));
                    g.fillEllipse (Rectangle<float> (d * 0.13f, d * 0.13f).withCentre (p));
                }
                g.setColour (lit && enabled ? col : t.track);
                g.fillEllipse (Rectangle<float> (d * 0.065f, d * 0.065f).withCentre (p));
            }
            g.setColour (t.knob);
            g.fillEllipse (Rectangle<float> (d * 0.6f, d * 0.6f).withCentre (c));
            g.setColour (t.edge);
            g.drawEllipse (Rectangle<float> (d * 0.6f, d * 0.6f).withCentre (c), 1.0f);
            g.setColour (enabled ? col : t.dim);
            g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.1f, angle), c.getPointOnCircumference (r * 0.55f, angle)), 2.0f);
            break;
        }
        default:  // arc ring (Midnight / Studio Grey)
        {
            arc (start, end, r * 0.86f, 4.0f, t.track);
            arc (from, angle, r * 0.86f, 4.0f, enabled ? col : t.dim);
            auto body = Rectangle<float> (d * 0.62f, d * 0.62f).withCentre (c);
            g.setGradientFill (ColourGradient (t.knob.brighter (0.25f), body.getX(), body.getY(), t.knob.darker (0.3f), body.getRight(), body.getBottom(), false));
            g.fillEllipse (body);
            g.setColour (t.edge);
            g.drawEllipse (body, 1.0f);
            g.setColour (t.text);
            g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.12f, angle), c.getPointOnCircumference (r * 0.5f, angle)), 2.0f);
            break;
        }
    }
}

void BloomLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto& t = *th;
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const Colour col = b.getProperties().contains ("colour") ? Colour ((uint32) (int64) b.getProperties()["colour"]) : t.accent;
    g.setColour (on ? col.withAlpha (0.9f) : t.bg2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (highlighted ? col : t.edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (on ? col.contrasting (0.85f) : t.onBg2().withAlpha (0.7f));
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (b.getButtonText(), r, Justification::centred);
}

void BloomLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    const auto& t = *th;
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? t.accent.withAlpha (0.4f) : t.bg2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (highlighted ? t.accent : t.edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void BloomLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    const auto& t = *th;
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (t.bg2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? t.accent : t.edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (t.dim);
    g.fillPath (arrow);
}
