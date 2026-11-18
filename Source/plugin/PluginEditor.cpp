#include "PluginEditor.h"

using namespace juce;

namespace
{
constexpr float kStart = MathConstants<float>::pi * -0.75f, kEnd = MathConstants<float>::pi * 0.75f;
constexpr float kGuitarLen = 330.0f, kAmpW = 150.0f;
const char* kPartials[4] = { "1x  FUND", "2x  OCTAVE", "3x  12TH", "4x  2 OCT" };
}

// ============================================================================ helpers
String BloomEditor::noteName (double hz)
{
    if (hz <= 0) return "--";
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - nearest) * 100.0);
    String s = String (eb::noteName (nearest)) + String (nearest / 12 - 1);
    if (cents != 0) s << (cents > 0 ? " +" : " ") << cents << "c";
    return s;
}

// ============================================================================ setup
BloomEditor::BloomEditor (BloomProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    lnf.setTheme (themeByName (proc.getThemeName()));

    // ---- header
    rebuildPresetBox();
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    prevPreset.onClick = [this] {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n);
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    };
    nextPreset.onClick = [this] {
        proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms());
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    };
    for (int i = 0; i < (int) allThemes().size(); ++i) themeBox.addItem (allThemes()[(size_t) i].name, i + 1);
    themeBox.setText (lnf.theme().name, dontSendNotification);
    themeBox.onChange = [this] { applyTheme (themeBox.getText()); };
    themeBox.setTooltip ("Theme (saved with the session)");
    for (auto* c : { (Component*) &presetBox, (Component*) &prevPreset, (Component*) &nextPreset, (Component*) &themeBox })
        addAndMakeVisible (c);

    // ---- sustain
    addKnob ("sustain", "SUSTAIN", "How far a fading note is lifted back up. At Infinite the note keeps ringing after the string dies:\n"
                                   "pick a new note to move on, or tap the muted strings to stop it.");
    addKnob ("swell", "SWELL", "Fade-in after each note. Removes the pick attack for bowed, violin-like entries.");
    addKnob ("release", "RELEASE", "Fade-out after you mute the strings or the gate closes.");

    // ---- bloom
    harmonicBox.addItemList ({ "Fundamental", "Octave", "12th", "2 Octaves", "Auto" }, 1);
    harmonicAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "harmonic", harmonicBox);
    harmonicBox.setTooltip ("The partial the feedback blooms into. Auto picks whichever partial your playing already favours.");
    addAndMakeVisible (harmonicBox);
    addKnob ("bloomTime", "BLOOM TIME", "How long a held note takes before the harmonic takes over (shorter when the guitar is closer to the amp).");
    addKnob ("bloomAmt", "AMOUNT", "How much of the note the harmonic takes over.");
    addKnob ("wobble", "WOBBLE", "Unstable feedback: pitch and level flutter that grows as the note blooms.");

    // ---- amp
    addKnob ("tone", "TONE", "Amp and feedback-loop brightness.");
    addKnob ("body", "BODY", "Guitar body resonance (low-mid wood).");
    addKnob ("drive", "DRIVE", "Amp saturation. More drive, more scream.");

    // ---- mode
    monoButton.setRadioGroupId (101);
    polyButton.setRadioGroupId (101);
    monoButton.setTooltip ("Mono: tracks the single note you play and tunes the feedback loop to it. Best for lead lines.");
    polyButton.setTooltip ("Poly: no tracking. A comb bank tuned to the key and scale rings out with whole chords.");
    auto setMode = [this] (int m) {
        if (auto* prm = proc.apvts.getParameter ("mode")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) m));
    };
    monoButton.onClick = [this, setMode] { if (monoButton.getToggleState()) setMode (0); };
    polyButton.onClick = [this, setMode] { if (polyButton.getToggleState()) setMode (1); };
    for (int i = 0; i < 12; ++i) keyBox.addItem (eb::noteName (i), i + 1);
    {
        const auto& sc = eb::builtinScales();
        String lastCat;
        for (int i = 0; i < (int) sc.size(); ++i)
        {
            const String cat = eb::builtinCategory (i);
            if (cat != lastCat) { scaleBox.addSectionHeading (cat); lastCat = cat; }
            scaleBox.addItem (sc[(size_t) i].getName(), i + 1);
        }
    }
    keyAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "key", keyBox);
    scaleAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scale", scaleBox);
    keyBox.setTooltip ("Poly mode: root of the comb bank");
    scaleBox.setTooltip ("Poly mode: scale the comb bank is tuned to (the same list as Comma)");
    holdButton.setClickingTogglesState (true);
    holdAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "hold", holdButton);
    holdButton.setTooltip ("Latch the current note forever, whatever Sustain says. MIDI CC64 (sustain pedal) does the same.");
    bloomNow.setTooltip ("Hold to jump straight into the harmonic. MIDI CC67 (soft pedal) does the same.");
    bloomNow.onStateChange = [this] {
        const bool down = bloomNow.isDown();
        if (auto* prm = proc.apvts.getParameter ("bloomNow"))
            if ((prm->getValue() > 0.5f) != down)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (down ? 1.0f : 0.0f);
                prm->endChangeGesture();
            }
    };
    for (auto* c : { (Component*) &monoButton, (Component*) &polyButton, (Component*) &keyBox, (Component*) &scaleBox,
                     (Component*) &holdButton, (Component*) &bloomNow })
        addAndMakeVisible (c);

    // ---- global
    addKnob ("gate", "GATE", "Input below this ends the note (or, at Infinite sustain, lets it latch). Raise it if hum keeps notes alive.");
    addKnob ("mix", "MIX", "Dry guitar against the sustained, blooming signal.");
    addKnob ("output", "OUTPUT", "Output level.", true);
    limiterButton.setClickingTogglesState (true);
    limiterAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "limiter", limiterButton);
    limiterButton.setTooltip ("Output limiter at -0.5 dBFS. The feedback loop has its own limiter that is always on.");
    addAndMakeVisible (limiterButton);

    grainTexture = Image (Image::ARGB, W, H, true);
    {
        Graphics tg (grainTexture);
        Random rnd (7);
        for (int i = 0; i < 1400; ++i)
        {
            tg.setColour (Colours::black.withAlpha (0.025f + 0.03f * rnd.nextFloat()));
            const float y = (float) rnd.nextInt (H);
            tg.drawLine (0, y, (float) W, y + rnd.nextFloat() * 6.0f - 3.0f, 0.6f + rnd.nextFloat());
        }
    }

    applyTheme (lnf.theme().name);
    setSize (W, H);
    timerCallback();
    startTimerHz (30);
}

BloomEditor::~BloomEditor() { setLookAndFeel (nullptr); }

void BloomEditor::rebuildPresetBox()
{
    presetBox.clear (dontSendNotification);
    const auto& ps = eb::factoryPresets();
    String lastCat;
    for (int i = 0; i < (int) ps.size(); ++i)
    {
        if (lastCat != ps[(size_t) i].category) { presetBox.addSectionHeading (ps[(size_t) i].category); lastCat = ps[(size_t) i].category; }
        presetBox.addItem (ps[(size_t) i].name, i + 1);
    }
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
}

BloomEditor::Knob& BloomEditor::addKnob (const String& id, const String& label, const String& tip, bool bipolar)
{
    auto k = std::make_unique<Knob>();
    k->label = label;
    auto& s = k->slider;
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::TextBoxBelow, false, 78, 16);
    s.setRotaryParameters (kStart, kEnd, true);
    s.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    s.setTooltip (tip);
    s.setName (id);
    if (bipolar) s.getProperties().set ("bipolar", true);
    addAndMakeVisible (s);
    k->attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    if (auto* prm = proc.apvts.getParameter (id))
        s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void BloomEditor::applyTheme (const String& name)
{
    const auto& t = themeByName (name);
    lnf.setTheme (t);
    proc.setThemeName (t.name);
    for (auto& k : knobs)
    {
        k->slider.setColour (Slider::textBoxTextColourId, t.dim);
        const bool bloomish = k->slider.getName().startsWith ("bloom") || k->slider.getName() == "wobble";
        if (bloomish) k->slider.getProperties().set ("colour", (int64) t.accent2.getARGB());
        else k->slider.getProperties().remove ("colour");
    }
    holdButton.getProperties().set ("colour", (int64) t.accent2.getARGB());
    sendLookAndFeelChange();
    repaint();
}

// ============================================================================ layout
void BloomEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    header = r.removeFromTop (44);
    {
        auto h = header;
        h.removeFromLeft (318);
        themeBox.setBounds (h.removeFromRight (150).reduced (0, 8));
        h.removeFromRight (16);
        prevPreset.setBounds (h.removeFromLeft (30).reduced (0, 8));
        presetBox.setBounds (h.removeFromLeft (320).reduced (4, 8));
        nextPreset.setBounds (h.removeFromLeft (30).reduced (0, 8));
    }
    r.removeFromTop (8);

    auto top = r.removeFromTop (262);
    ladderPanel = top.removeFromRight (280);
    top.removeFromRight (10);
    sceneArea = top;
    {
        auto lp = ladderPanel.reduced (12);
        lp.removeFromTop (18);
        readoutArea = lp.removeFromTop (70);
        lp.removeFromTop (6);
        ladderArea = lp;
    }

    r.removeFromTop (10);
    auto row1 = r.removeFromTop (150);
    r.removeFromTop (10);
    auto row2 = r;

    auto knobsIn = [this] (Rectangle<int> area, std::initializer_list<const char*> ids)
    {
        const int n = (int) ids.size();
        const int kw = area.getWidth() / n;
        for (auto* id : ids)
            for (auto& k : knobs)
                if (k->slider.getName() == id)
                    k->slider.setBounds (area.removeFromLeft (kw).withTrimmedTop (14).withSizeKeepingCentre (jmin (kw, 92), area.getHeight() - 14));
    };

    sustainPanel = row1.removeFromLeft (300);
    row1.removeFromLeft (10);
    bloomPanel = row1.removeFromLeft (400);
    row1.removeFromLeft (10);
    ampPanel = row1;
    knobsIn (sustainPanel.reduced (10).withTrimmedTop (22).withHeight (104), { "sustain", "swell", "release" });
    {
        auto b = bloomPanel.reduced (10).withTrimmedTop (22);
        auto left = b.removeFromLeft (110);
        harmonicBox.setBounds (left.withSizeKeepingCentre (left.getWidth() - 6, 26).withY (left.getY() + 30));
        knobsIn (b.withHeight (104), { "bloomTime", "bloomAmt", "wobble" });
    }
    knobsIn (ampPanel.reduced (10).withTrimmedTop (22).withHeight (104), { "tone", "body", "drive" });

    modePanel = row2.removeFromLeft (500);
    row2.removeFromLeft (10);
    globalPanel = row2;
    {
        auto m = modePanel.reduced (12).withTrimmedTop (22);
        auto r1 = m.removeFromTop (30);
        monoButton.setBounds (r1.removeFromLeft (90));
        r1.removeFromLeft (6);
        polyButton.setBounds (r1.removeFromLeft (90));
        r1.removeFromLeft (14);
        keyBox.setBounds (r1.removeFromLeft (64));
        r1.removeFromLeft (6);
        scaleBox.setBounds (r1);
        m.removeFromTop (8);
        modeInfoArea = m.removeFromTop (34);
        m.removeFromTop (6);
        auto r2 = m.removeFromTop (34);
        holdButton.setBounds (r2.removeFromLeft (110));
        r2.removeFromLeft (8);
        bloomNow.setBounds (r2.removeFromLeft (130));
    }
    {
        auto g = globalPanel.reduced (10).withTrimmedTop (22);
        meterArea = g.removeFromRight (60);
        auto right = g.removeFromRight (96);
        limiterButton.setBounds (right.withSizeKeepingCentre (88, 28));
        knobsIn (g.withHeight (104), { "gate", "mix", "output" });
    }
}

// ============================================================================ distance (dragging the guitar)
float BloomEditor::headstockX() const
{
    const auto s = sceneArea.toFloat();
    const float xMax = s.getRight() - kAmpW - 30.0f;            // guitar body right edge, closest
    const float xMin = s.getX() + 18.0f + kGuitarLen;            // farthest
    float norm = 0.3f;
    if (auto* prm = proc.apvts.getParameter ("ampDist")) norm = prm->getValue();
    return xMax - norm * (xMax - xMin);
}

void BloomEditor::setDistanceFromX (float x)
{
    const auto s = sceneArea.toFloat();
    const float xMax = s.getRight() - kAmpW - 30.0f, xMin = s.getX() + 18.0f + kGuitarLen;
    const float norm = jlimit (0.0f, 1.0f, (xMax - x) / (xMax - xMin));
    if (auto* prm = proc.apvts.getParameter ("ampDist")) prm->setValueNotifyingHost (norm);
}

void BloomEditor::mouseDown (const MouseEvent& e)
{
    if (sceneArea.contains (e.getPosition()))
    {
        draggingHeadstock = true;
        if (auto* prm = proc.apvts.getParameter ("ampDist")) prm->beginChangeGesture();
        setDistanceFromX ((float) e.x);
    }
}
void BloomEditor::mouseDrag (const MouseEvent& e) { if (draggingHeadstock) setDistanceFromX ((float) e.x); }
void BloomEditor::mouseUp (const MouseEvent&)
{
    if (draggingHeadstock)
        if (auto* prm = proc.apvts.getParameter ("ampDist")) prm->endChangeGesture();
    draggingHeadstock = false;
}
void BloomEditor::mouseDoubleClick (const MouseEvent& e)
{
    if (sceneArea.contains (e.getPosition()))
        if (auto* prm = proc.apvts.getParameter ("ampDist")) prm->setValueNotifyingHost (prm->getDefaultValue());
}
void BloomEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (sceneArea.contains (e.getPosition()))
        if (auto* prm = proc.apvts.getParameter ("ampDist"))
            prm->setValueNotifyingHost (jlimit (0.0f, 1.0f, prm->getValue() - w.deltaY * 0.15f));
}
void BloomEditor::mouseMove (const MouseEvent& e)
{
    const bool over = sceneArea.contains (e.getPosition());
    if (over != hoverHeadstock) { hoverHeadstock = over; repaint (sceneArea); }
    setMouseCursor (over ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

// ============================================================================ painting
void BloomEditor::drawPanel (Graphics& g, Rectangle<int> area, const String& title)
{
    const auto& t = lnf.theme();
    auto r = area.toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (t.edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (t.dim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (title, area.reduced (12, 8).removeFromTop (14), Justification::left);
    }
}

void BloomEditor::drawMeter (Graphics& g, Rectangle<float> r, float level, const String& label)
{
    const auto& t = lnf.theme();
    g.setColour (t.bg2);
    g.fillRoundedRectangle (r, 3.0f);
    const float db = level > 0 ? 20.0f * std::log10 (level) : -100.0f;
    const float frac = jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);
    auto fill = r.reduced (2).withTrimmedTop ((r.getHeight() - 4) * (1.0f - frac));
    g.setColour (db > -1.0f ? Colour (0xffff4040) : t.accent);
    g.fillRoundedRectangle (fill, 2.0f);
    g.setColour (t.dim);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText (label, r.withY (r.getBottom() + 2).withHeight (12).withSizeKeepingCentre (40, 12), Justification::centred);
}

void BloomEditor::drawScene (Graphics& g, Rectangle<float> s)
{
    const auto& t = lnf.theme();
    const auto& st = proc.getStatus();
    const float bloom = st.bloom.load();
    const bool active = st.noteActive.load();

    // backdrop
    g.setColour (t.panel);
    g.fillRoundedRectangle (s, 8.0f);
    g.saveState();
    g.reduceClipRegion (s.toNearestInt());
    const float floorY = s.getBottom() - 34.0f;
    g.setColour (t.panel.interpolatedWith (t.edge, 0.25f));
    g.fillRect (s.withTop (floorY));
    g.setColour (t.edge);
    g.drawHorizontalLine ((int) floorY, s.getX(), s.getRight());

    // ---- amp
    auto amp = Rectangle<float> (s.getRight() - kAmpW - 14.0f, s.getY() + 34.0f, kAmpW, floorY - s.getY() - 34.0f);
    const auto spk = Point<float> (amp.getCentreX(), amp.getY() + 34.0f + (amp.getHeight() - 34.0f) * 0.5f);
    const float spkR = jmin (amp.getWidth(), amp.getHeight() - 34.0f) * 0.38f;
    if (glow > 0.01f)
    {
        const auto gc = t.accent.withAlpha (jlimit (0.0f, 0.6f, glow * 0.6f));
        g.setGradientFill (ColourGradient (gc, spk.x, spk.y, gc.withAlpha (0.0f), spk.x - spkR * 3.2f, spk.y, true));
        g.fillEllipse (Rectangle<float> (spkR * 6.4f, spkR * 6.4f).withCentre (spk));
    }
    g.setColour (t.knob.darker (0.4f));
    g.fillRoundedRectangle (amp, 6.0f);
    g.setColour (t.edge);
    g.drawRoundedRectangle (amp, 6.0f, 1.2f);
    auto panel = amp.withHeight (30.0f).reduced (6.0f, 5.0f);
    g.setColour (t.bg2);
    g.fillRoundedRectangle (panel, 3.0f);
    for (int i = 0; i < 5; ++i)
    {
        g.setColour (t.dim);
        g.fillEllipse (Rectangle<float> (9, 9).withCentre ({ panel.getX() + 20.0f + i * 20.0f, panel.getCentreY() }));
    }
    g.setColour (glow > 0.05f ? t.accent : t.dim.darker());
    g.fillEllipse (Rectangle<float> (6, 6).withCentre ({ panel.getRight() - 12.0f, panel.getCentreY() }));
    auto grille = amp.withTrimmedTop (34.0f).reduced (8.0f);
    g.setColour (t.bg.interpolatedWith (t.knob, 0.3f));
    g.fillRoundedRectangle (grille, 4.0f);
    for (float y = grille.getY() + 4; y < grille.getBottom(); y += 5.0f)
    {
        g.setColour (t.edge.withAlpha (0.25f));
        g.drawHorizontalLine ((int) y, grille.getX() + 2, grille.getRight() - 2);
    }
    g.setColour (t.accent.withAlpha (0.12f + 0.5f * glow));
    g.drawEllipse (Rectangle<float> (spkR * 2, spkR * 2).withCentre (spk), 2.0f);
    g.setColour (t.accent.withAlpha (0.10f + 0.6f * glow));
    g.fillEllipse (Rectangle<float> (spkR * 0.6f, spkR * 0.6f).withCentre (spk));

    // ---- sound reaching back to the guitar
    const float bodyRight = headstockX();
    if (glow > 0.02f)
        for (int i = 0; i < 4; ++i)
        {
            const float ph = std::fmod (stringPhase * 0.08f + i * 0.25f, 1.0f);
            const float rr = spkR + ph * (spk.x - bodyRight);
            Path arc;
            arc.addCentredArc (spk.x, spk.y, rr, rr, 0, MathConstants<float>::pi * 1.32f, MathConstants<float>::pi * 1.68f, true);
            g.setColour (t.accent2.withAlpha (glow * (1.0f - ph) * 0.55f));
            g.strokePath (arc, PathStrokeType (1.6f));
        }

    // ---- guitar (headstock on the left, body nearest the amp)
    const float cy = floorY - 70.0f;
    const auto bodyCol = t.accent.interpolatedWith (t.knob, 0.55f);
    {
        Path body;
        body.addEllipse (bodyRight - 124.0f, cy - 60.0f, 124.0f, 120.0f);
        body.addEllipse (bodyRight - 210.0f, cy - 48.0f, 104.0f, 96.0f);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillPath (body, AffineTransform::translation (3, 5));
        g.setGradientFill (ColourGradient (bodyCol.brighter (0.3f), bodyRight - 150, cy - 60, bodyCol.darker (0.4f), bodyRight, cy + 60, false));
        g.fillPath (body);
        g.setColour (t.edge);
        g.strokePath (body, PathStrokeType (1.2f));
        // pickups and bridge
        g.setColour (t.bg.withAlpha (0.85f));
        g.fillRoundedRectangle (Rectangle<float> (14, 44).withCentre ({ bodyRight - 150.0f, cy }), 3.0f);
        g.fillRoundedRectangle (Rectangle<float> (14, 44).withCentre ({ bodyRight - 112.0f, cy }), 3.0f);
        g.setColour (t.dim);
        g.fillRoundedRectangle (Rectangle<float> (8, 40).withCentre ({ bodyRight - 70.0f, cy }), 2.0f);
    }
    const float nutX = bodyRight - kGuitarLen + 62.0f;
    {
        // neck and fretboard
        auto neck = Rectangle<float> (nutX, cy - 11.0f, bodyRight - 190.0f - nutX, 22.0f);
        g.setColour (t.knob.brighter (0.15f));
        g.fillRect (neck);
        g.setColour (t.dim.withAlpha (0.6f));
        for (int f = 1; f <= 14; ++f)
        {
            const float x = nutX + neck.getWidth() * (1.0f - std::pow (2.0f, -f / 12.0f)) * 1.8f;
            if (x < neck.getRight()) g.drawVerticalLine ((int) x, neck.getY(), neck.getBottom());
        }
        // headstock
        Path head;
        head.startNewSubPath (nutX, cy - 13.0f);
        head.lineTo (nutX - 62.0f, cy - 20.0f);
        head.quadraticTo (nutX - 70.0f, cy, nutX - 62.0f, cy + 20.0f);
        head.lineTo (nutX, cy + 13.0f);
        head.closeSubPath();
        g.setColour (bodyCol.darker (0.2f));
        g.fillPath (head);
        g.setColour (t.edge);
        g.strokePath (head, PathStrokeType (1.2f));
        g.setColour (t.text.withAlpha (0.8f));
        for (int i = 0; i < 3; ++i)
        {
            const float x = nutX - 14.0f - i * 16.0f;
            g.fillEllipse (Rectangle<float> (6, 6).withCentre ({ x, cy - 22.0f - i * 1.2f }));
            g.fillEllipse (Rectangle<float> (6, 6).withCentre ({ x, cy + 22.0f + i * 1.2f }));
        }
        g.setColour (t.text.withAlpha (0.9f));
        g.fillRect (nutX - 2.0f, cy - 12.0f, 3.0f, 24.0f);
    }
    // strings: vibrate with the output level, light up as the note blooms
    {
        const float x0 = nutX, x1 = bodyRight - 70.0f;
        const auto col = t.dim.interpolatedWith (t.accent2, jlimit (0.0f, 1.0f, 0.25f + bloom * 0.9f));
        for (int i = 0; i < 6; ++i)
        {
            const float y0 = cy - 8.5f + i * 3.4f, y1 = cy - 13.0f + i * 5.2f;
            Path str;
            const int segs = 40;
            for (int k = 0; k <= segs; ++k)
            {
                const float u = (float) k / segs;
                const float disp = stringAmp * (1.2f + 0.3f * i) * std::sin (MathConstants<float>::pi * u)
                                   * std::sin (stringPhase * (1.0f + 0.13f * i) + i);
                const Point<float> pt (x0 + (x1 - x0) * u, y0 + (y1 - y0) * u + disp);
                if (k == 0) str.startNewSubPath (pt); else str.lineTo (pt);
            }
            if (t.glow && bloom > 0.05f)
            {
                g.setColour (col.withAlpha (0.25f * bloom));
                g.strokePath (str, PathStrokeType (4.0f));
            }
            g.setColour (active ? col : t.dim.withAlpha (0.7f));
            g.strokePath (str, PathStrokeType (0.8f + 0.25f * (5 - i)));
        }
    }

    // ---- distance ruler on the floor
    {
        const float y = floorY + 14.0f;
        g.setColour (hoverHeadstock || draggingHeadstock ? t.accent : t.dim);
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine (Line<float> (bodyRight, y, amp.getX(), y), dashes, 2, 1.2f);
        g.drawVerticalLine ((int) bodyRight, y - 5, y + 5);
        g.drawVerticalLine ((int) amp.getX(), y - 5, y + 5);
        g.setFont (FontOptions (12.0f, Font::bold));
        const float m = proc.apvts.getRawParameterValue ("ampDist")->load();
        g.drawText ("AMP DISTANCE  " + String (m, 2) + " m", Rectangle<float> (s.getX() + 12, y - 8, bodyRight - s.getX() - 22, 16),
                    Justification::centredRight);
    }
    g.setColour (t.dim);
    g.setFont (FontOptions (11.0f));
    g.drawText ("Drag the guitar toward the amp for faster, louder feedback", s.reduced (12, 10).removeFromTop (14), Justification::left);
    g.restoreState();
    g.setColour (t.edge);
    g.drawRoundedRectangle (s.reduced (0.5f), 8.0f, 1.0f);
}

void BloomEditor::drawLadder (Graphics& g, Rectangle<float> area)
{
    const auto& t = lnf.theme();
    const auto& st = proc.getStatus();
    const int chosen = st.harmonic.load();
    const float rungH = area.getHeight() / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        auto rung = Rectangle<float> (area.getX(), area.getBottom() - (i + 1) * rungH, area.getWidth(), rungH).reduced (0, 4);
        const bool isChosen = (i + 1) == chosen && i > 0;
        g.setColour (isChosen ? t.accent2 : t.dim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (kPartials[i], rung.removeFromLeft (84), Justification::centredLeft);
        g.setColour (t.bg2);
        g.fillRoundedRectangle (rung, 4.0f);
        const float v = ladder[(size_t) i];
        const auto col = i == 0 ? t.accent : (isChosen ? t.accent2 : t.dim);
        if (v > 0.005f)
        {
            auto fill = rung.reduced (2).withWidth ((rung.getWidth() - 4) * jlimit (0.0f, 1.0f, v));
            if (t.glow)
            {
                g.setColour (col.withAlpha (0.25f));
                g.fillRoundedRectangle (fill.expanded (2), 4.0f);
            }
            g.setColour (col);
            g.fillRoundedRectangle (fill, 3.0f);
        }
        g.setColour (t.edge);
        g.drawRoundedRectangle (rung, 4.0f, 1.0f);
    }
}

void BloomEditor::paint (Graphics& g)
{
    const auto& t = lnf.theme();
    g.setGradientFill (ColourGradient (t.bg2, 0, 0, t.bg, 0, (float) H, false));
    g.fillAll();
    if (t.grain) g.drawImageAt (grainTexture, 0, 0);

    // wordmark
    {
        auto h = header;
        g.setColour (t.onBg());
        g.setFont (FontOptions (27.0f, Font::bold));
        g.drawText ("HALO BLOOM", h.removeFromLeft (196), Justification::centredLeft);
        g.setColour (t.accent);
        g.setFont (FontOptions (11.0f));
        g.drawFittedText ("infinite sustain\nfeedback bloom", h.removeFromLeft (110), Justification::centredLeft, 2);
    }

    drawScene (g, sceneArea.toFloat());
    drawPanel (g, ladderPanel, "HARMONIC LADDER");
    drawPanel (g, sustainPanel, "SUSTAIN");
    drawPanel (g, bloomPanel, "BLOOM");
    drawPanel (g, ampPanel, "AMP");
    drawPanel (g, modePanel, "MODE");
    drawPanel (g, globalPanel, "GLOBAL");

    // readout
    {
        const auto& st = proc.getStatus();
        const auto params = proc.currentParams();
        const bool poly = params.mode == eb::Mode::Poly;
        auto r = readoutArea.toFloat();
        const float hz = st.detectedHz.load();
        g.setColour (st.noteActive.load() ? t.text : t.dim);
        g.setFont (FontOptions (26.0f, Font::bold));
        g.drawText (poly ? String (eb::noteName (params.key)) + " comb" : noteName (hz), r.removeFromTop (32), Justification::centredLeft);
        String state;
        if (! st.noteActive.load()) state = poly ? "Poly: play a chord" : "Play a note";
        else if (st.latched.load()) state = params.hold ? "Held (footswitch)" : "Latched: infinite";
        else state = "Sustaining  +" + String (roundToInt (jmax (0.0f, st.sustainGainDb.load()))) + " dB";
        g.setFont (FontOptions (12.0f));
        g.setColour (st.latched.load() ? t.accent2 : t.dim);
        g.drawText (state, r.removeFromTop (16), Justification::centredLeft);
        r.removeFromTop (6);
        auto bar = r.removeFromTop (10);
        g.setColour (t.bg2);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (t.accent2);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, st.bloom.load())), 3.0f);
        g.setColour (t.dim);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText ("BLOOM", readoutArea.toFloat().removeFromTop (32), Justification::topRight);
    }
    drawLadder (g, ladderArea.toFloat());
    g.setColour (t.dim);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText ("HARMONIC", harmonicBox.getBounds().translated (0, -16).withHeight (14), Justification::centred);

    // mode info
    {
        const bool poly = proc.currentParams().mode == eb::Mode::Poly;
        g.setColour (t.dim);
        g.setFont (FontOptions (11.5f));
        g.drawFittedText (poly ? "POLY: chords ring out on a comb bank tuned to the key and scale. No pitch tracking."
                               : "MONO: tracks the one note you play and tunes the feedback to it. Play single notes.",
                          modeInfoArea, Justification::centredLeft, 2);
        auto hint = modePanel.reduced (12).removeFromBottom (16);
        hint.removeFromLeft (256);
        g.drawText ("MIDI: CC64 = Hold, CC67 = Bloom", hint, Justification::centredRight);
    }

    // meters
    {
        auto m = meterArea.toFloat().withTrimmedBottom (14).reduced (4, 4);
        const float w = 14.0f;
        drawMeter (g, Rectangle<float> (m.getX() + 4, m.getY(), w, m.getHeight()), inMeter, "IN");
        drawMeter (g, Rectangle<float> (m.getRight() - w - 4, m.getY(), w, m.getHeight()), outMeter, "OUT");
    }

    // knob labels
    g.setFont (FontOptions (10.0f, Font::bold));
    for (auto& k : knobs)
    {
        g.setColour (t.dim);
        g.drawText (k->label, k->slider.getBounds().withY (k->slider.getY() - 14).withHeight (14), Justification::centred);
    }
}

void BloomEditor::timerCallback()
{
    const auto& st = proc.getStatus();
    const auto p = proc.currentParams();
    inMeter = jmax (st.inLevel.load(), inMeter * 0.85f);
    outMeter = jmax (st.outLevel.load(), outMeter * 0.85f);
    const float prox = jlimit (0.0f, 1.0f, 1.0f - (p.ampDistance - 0.1f) / 3.9f);
    glow += 0.25f * (jlimit (0.0f, 1.0f, outMeter * 2.5f * (0.5f + 0.5f * prox)) - glow);
    stringAmp += 0.3f * (jlimit (0.0f, 1.0f, outMeter * 4.0f) * 2.2f - stringAmp);
    stringPhase += 1.9f;
    if (stringPhase > 1000.0f) stringPhase -= 1000.0f;
    for (int i = 0; i < 4; ++i) ladder[(size_t) i] += 0.35f * (st.ladder[(size_t) i].load() - ladder[(size_t) i]);

    const bool poly = p.mode == eb::Mode::Poly;
    monoButton.setToggleState (! poly, dontSendNotification);
    polyButton.setToggleState (poly, dontSendNotification);
    keyBox.setEnabled (poly);
    scaleBox.setEnabled (poly);
    keyBox.setAlpha (poly ? 1.0f : 0.45f);
    scaleBox.setAlpha (poly ? 1.0f : 0.45f);
    holdButton.setButtonText (proc.midiHoldDown() ? "HOLD (CC64)" : "HOLD");
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    repaint();
}
