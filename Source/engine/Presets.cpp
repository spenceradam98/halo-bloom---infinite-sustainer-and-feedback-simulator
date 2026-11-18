#include "Presets.h"

namespace eb
{
namespace
{
// Compact builder so each preset reads as one line of settings.
struct P
{
    Params p;
    P& sustain (float s, float swellMs, float releaseMs) { p.sustain = s; p.swellMs = swellMs; p.releaseMs = releaseMs; return *this; }
    P& bloom (float seconds, Harmonic h, float amount, float wobble) { p.bloomTime = seconds; p.harmonic = h; p.bloomAmount = amount; p.wobble = wobble; return *this; }
    P& amp (float metres, float tone, float body, float drive) { p.ampDistance = metres; p.tone = tone; p.body = body; p.drive = drive; return *this; }
    P& poly (int key, int scale) { p.mode = Mode::Poly; p.key = key; p.scaleIndex = scale; return *this; }
    P& mix (float m, float outDb = 0.0f) { p.mix = m; p.outputDb = outDb; return *this; }
    P& gate (float db) { p.gateDb = db; return *this; }
};

// builtinScales() indices used below
constexpr int kMajor = 0, kMinor = 1, kDorian = 2, kMinPent = 11, kJustMajor = 15;
constexpr int E = 4, A = 9, D = 2, G = 7, C = 0;
} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = [] {
        using H = Harmonic;
        std::vector<Preset> v;
        auto add = [&] (const char* name, const char* cat, const P& b) { v.push_back ({ name, cat, b.p }); };

        // ---- Sustain: hold the note, little or no bloom
        add ("Infinite Hold",      "Sustain",  P().sustain (1.00f,    0,  600).bloom (6.0f, H::Fundamental, 0.25f, 0.05f).amp (1.5f, 0.55f, 0.40f, 0.15f));
        add ("Bow Swell",          "Sustain",  P().sustain (0.95f,  650,  900).bloom (5.0f, H::Fundamental, 0.30f, 0.08f).amp (1.8f, 0.50f, 0.50f, 0.10f));
        add ("Clean Cello",        "Sustain",  P().sustain (0.92f,  420, 1200).bloom (8.0f, H::Fundamental, 0.20f, 0.04f).amp (2.5f, 0.38f, 0.75f, 0.05f));
        add ("Violin Lead",        "Sustain",  P().sustain (0.96f,  280,  500).bloom (4.0f, H::Octave,      0.25f, 0.15f).amp (1.2f, 0.72f, 0.35f, 0.35f));
        add ("Long Natural",       "Sustain",  P().sustain (0.60f,    0,  800).bloom (9.0f, H::Fundamental, 0.15f, 0.02f).amp (3.0f, 0.60f, 0.40f, 0.10f));
        add ("Sostenuto Pad",      "Sustain",  P().sustain (1.00f, 1400, 2500).bloom (7.0f, H::Fundamental, 0.35f, 0.06f).amp (2.2f, 0.42f, 0.55f, 0.05f).mix (0.85f));

        // ---- Bloom: the note turns into a harmonic
        add ("Octave Bloom",       "Bloom",    P().sustain (0.95f,    0,  600).bloom (3.0f, H::Octave,      0.75f, 0.18f).amp (1.0f, 0.60f, 0.40f, 0.30f));
        add ("Slow Fifth",         "Bloom",    P().sustain (0.95f,  300,  900).bloom (6.0f, H::Twelfth,     0.70f, 0.12f).amp (1.4f, 0.55f, 0.45f, 0.25f));
        add ("Two Octaves Up",     "Bloom",    P().sustain (0.97f,    0,  700).bloom (4.0f, H::TwoOctaves,  0.65f, 0.20f).amp (0.8f, 0.70f, 0.30f, 0.30f));
        add ("Auto Harmonic",      "Bloom",    P().sustain (0.96f,    0,  700).bloom (3.5f, H::Auto,        0.70f, 0.22f).amp (1.0f, 0.60f, 0.40f, 0.35f));
        add ("Quick Flip",         "Bloom",    P().sustain (0.95f,    0,  400).bloom (0.8f, H::Octave,      0.90f, 0.10f).amp (0.6f, 0.65f, 0.35f, 0.40f));
        add ("Whistle Bloom",      "Bloom",    P().sustain (1.00f,  200, 1000).bloom (5.0f, H::TwoOctaves,  0.95f, 0.30f).amp (1.2f, 0.80f, 0.20f, 0.15f));

        // ---- Feedback: close to a loud amp
        add ("Feedback Scream",    "Feedback", P().sustain (1.00f,    0,  500).bloom (1.5f, H::Octave,      0.85f, 0.45f).amp (0.2f, 0.75f, 0.30f, 0.85f));
        add ("Shoegaze Sustain",   "Feedback", P().sustain (1.00f,  500, 2000).bloom (4.0f, H::Auto,        0.60f, 0.40f).amp (0.7f, 0.45f, 0.55f, 0.65f).mix (0.9f));
        add ("Amp Stack Howl",     "Feedback", P().sustain (0.98f,    0,  600).bloom (2.0f, H::Twelfth,     0.80f, 0.35f).amp (0.3f, 0.62f, 0.45f, 0.75f));
        add ("Unstable Valve",     "Feedback", P().sustain (0.97f,    0,  500).bloom (2.5f, H::Octave,      0.70f, 0.85f).amp (0.5f, 0.55f, 0.40f, 0.60f));
        add ("Distant Room",       "Feedback", P().sustain (0.90f,  150, 1500).bloom (9.0f, H::Octave,      0.50f, 0.25f).amp (3.6f, 0.40f, 0.60f, 0.40f));

        // ---- Poly: comb bank on the key, for chords
        add ("Drone Chord (Poly)",     "Poly", P().sustain (1.00f,  600, 2500).bloom (6.0f, H::Fundamental, 0.40f, 0.10f).amp (1.5f, 0.45f, 0.50f, 0.20f).poly (E, kMinor));
        add ("Major Shimmer (Poly)",   "Poly", P().sustain (0.95f,  400, 2000).bloom (4.0f, H::Octave,      0.60f, 0.15f).amp (1.2f, 0.65f, 0.40f, 0.15f).poly (G, kMajor));
        add ("Dorian Haze (Poly)",     "Poly", P().sustain (1.00f,  900, 3000).bloom (7.0f, H::Twelfth,     0.45f, 0.20f).amp (2.0f, 0.40f, 0.55f, 0.30f).poly (D, kDorian).mix (0.85f));
        add ("Pentatonic Ring (Poly)", "Poly", P().sustain (0.90f,    0, 1500).bloom (3.0f, H::Octave,      0.55f, 0.10f).amp (1.0f, 0.60f, 0.40f, 0.25f).poly (A, kMinPent));
        add ("Just Organ (Poly)",      "Poly", P().sustain (1.00f,  300, 1800).bloom (5.0f, H::Fundamental, 0.30f, 0.05f).amp (1.8f, 0.50f, 0.60f, 0.10f).poly (C, kJustMajor));
        return v;
    }();
    return list;
}
} // namespace eb
