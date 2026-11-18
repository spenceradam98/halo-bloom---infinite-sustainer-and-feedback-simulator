// Engine tests: run with ctest or directly. No framework, exits non-zero on failure.
#include <cmath>
#include <cstdio>
#include <vector>

#include "Engine.h"
#include "Presets.h"

using namespace eb;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// a guitar-ish note: 8 harmonics, exponential decay (tau seconds, 0 = no decay)
static std::vector<float> pluck (double hz, double sr, int n, float amp = 0.5f, double tau = 0.0)
{
    std::vector<float> v ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double env = tau > 0 ? std::exp (-i / (tau * sr)) : 1.0;
        double s = 0;
        for (int h = 1; h <= 8; ++h) s += (1.0 / h) * std::sin (2 * kPi * hz * h * i / sr);
        v[(size_t) i] = (float) (amp * 0.6 * env * s);
    }
    return v;
}

struct Run { std::vector<float> l, r; float loopPeak = 0; bool finite = true; };

static Run run (const Params& p, const std::vector<float>& x, double sr = 48000, int block = 256, Engine* external = nullptr)
{
    Engine local;
    Engine& e = external ? *external : local;
    e.setParams (p);
    e.prepare (sr, block);
    Run out;
    out.l.resize (x.size()); out.r.resize (x.size());
    std::vector<float> inL (x), inR (x);
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, x.size() - pos);
        const float* in[2] = { inL.data() + pos, inR.data() + pos };
        float* o[2] = { out.l.data() + pos, out.r.data() + pos };
        e.process (in, 2, o, 2, n);
        out.loopPeak = std::max (out.loopPeak, e.getStatus().loopPeak.exchange (0.0f));
    }
    for (size_t i = 0; i < x.size(); ++i)
        if (! std::isfinite (out.l[i]) || ! std::isfinite (out.r[i])) out.finite = false;
    return out;
}

static float peak (const std::vector<float>& v, size_t a = 0, size_t b = 0)
{
    if (b == 0 || b > v.size()) b = v.size();
    float m = 0; for (size_t i = a; i < b; ++i) m = std::max (m, std::abs (v[i])); return m;
}
static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    b = std::min (b, v.size()); double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / std::max<size_t> (1, b - a));
}
static double db (double g) { return 20.0 * std::log10 (std::max (g, 1e-12)); }

// Goertzel magnitude of one frequency over a range
static double tone (const std::vector<float>& v, size_t a, size_t b, double hz, double sr)
{
    const double w = 2 * kPi * hz / sr, c = 2 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i) { const double s = v[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (double) (b - a);
}

static float detectPitch (const std::vector<float>& v, size_t a, double sr)
{
    PitchDetector d; d.prepare (sr); d.setGateDb (-80);
    float f = 0;
    for (size_t i = a; i < v.size(); ++i) if (d.push (v[i]) && d.isVoiced()) f = d.getFrequency();
    return f;
}

static Params maxed()
{
    Params p;
    p.sustain = 1; p.swellMs = 0; p.releaseMs = 4000; p.bloomTime = 0.2f; p.harmonic = Harmonic::Octave;
    p.bloomAmount = 1; p.wobble = 1; p.ampDistance = 0.1f; p.tone = 1; p.body = 1; p.drive = 1;
    p.gateDb = -80; p.mix = 1; p.outputDb = 12; p.limiter = true; p.hold = true; p.bloomNow = true;
    return p;
}

static void testSilence()
{
    const std::vector<float> zeros (48000 * 3, 0.0f);
    for (auto mode : { Mode::Mono, Mode::Poly })
    {
        Params p = maxed(); p.mode = mode;
        auto r = run (p, zeros);
        CHECK (peak (r.l) == 0.0f && peak (r.r) == 0.0f, "silence in gave %g out (mode %d)", peak (r.l), (int) mode);
    }
    for (auto& pr : factoryPresets())
    {
        auto r = run (pr.p, std::vector<float> (48000, 0.0f));
        CHECK (peak (r.l) == 0.0f, "preset %s: silence in gave %g out", pr.name, peak (r.l));
    }
}

static void testNeverClips()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto mode : { Mode::Mono, Mode::Poly })
            for (double hz : { 82.41, 196.0, 659.25 })
            {
                Params p = maxed(); p.mode = mode;
                auto x = pluck (hz, sr, (int) (sr * 3), 1.0f);       // full-scale input
                for (size_t i = x.size() / 2; i < x.size(); ++i) x[i] = 0.0f;   // then silence: loop runs on its own
                auto r = run (p, x, sr);
                CHECK (r.finite, "non-finite output sr %.0f mode %d hz %.0f", sr, (int) mode, hz);
                CHECK (peak (r.l) <= 1.0f && peak (r.r) <= 1.0f, "output peak %.3f > 0 dBFS (sr %.0f mode %d hz %.0f)", peak (r.l), sr, (int) mode, hz);
                CHECK (r.loopPeak <= 1.0f, "loop peak %.3f > 0 dBFS", r.loopPeak);
                CHECK (rms (r.l, x.size() * 3 / 4, x.size()) > 1e-3, "maxed latch went quiet (sr %.0f mode %d hz %.0f)", sr, (int) mode, hz);

                p.limiter = false; p.outputDb = 0;
                auto r2 = run (p, x, sr);
                CHECK (r2.finite && r2.loopPeak <= 1.0f, "limiter off: loop peak %.3f", r2.loopPeak);
            }
}

static void testSustain()
{
    const double sr = 48000;
    auto x = pluck (196.0, sr, (int) (sr * 4), 0.5f, 0.6);   // decays 29 dB over 4 s
    Params p; p.bloomAmount = 0; p.gateDb = -70; p.ampDistance = 4; p.drive = 0;

    p.sustain = 0.97f;
    auto held = run (p, x, sr);
    const double early = rms (held.l, (size_t) (0.3 * sr), (size_t) (0.6 * sr)), late = rms (held.l, (size_t) (3.4 * sr), (size_t) (3.7 * sr));
    CHECK (db (late / early) > -6.0, "sustain 97%%: level fell %.1f dB", db (late / early));

    p.sustain = 0.0f;
    auto natural = run (p, x, sr);
    const double e2 = rms (natural.l, (size_t) (0.3 * sr), (size_t) (0.6 * sr)), l2 = rms (natural.l, (size_t) (3.4 * sr), (size_t) (3.7 * sr));
    CHECK (db (l2 / e2) < -18.0, "sustain 0: note should decay naturally, fell only %.1f dB", db (l2 / e2));
}

static void testInfiniteLatch()
{
    const double sr = 48000;
    auto x = pluck (147.0, sr, (int) (sr * 6), 0.5f, 0.3);   // the string rings out and fades below the gate
    Params p; p.bloomAmount = 0; p.gateDb = -60; p.releaseMs = 200;

    p.sustain = 1.0f;
    auto r = run (p, x, sr);
    const double tail = rms (r.l, (size_t) (5.5 * sr), (size_t) (6 * sr));
    CHECK (db (tail) > -30.0, "infinite sustain: note died (%.1f dBFS after the string faded out)", db (tail));
    const float f = detectPitch (r.l, (size_t) (4.5 * sr), sr);
    CHECK (f > 0 && std::abs (1200.0 * std::log2 (f / 147.0)) < 15.0, "latched note drifted to %.2f Hz (played 147)", f);

    p.sustain = 0.8f;
    auto r2 = run (p, x, sr);
    CHECK (peak (r2.l, (size_t) (5.5 * sr), x.size()) < 1e-4f, "sustain 80%%: note should end when the string fades out (%.5f)", peak (r2.l, (size_t) (5.5 * sr), x.size()));

    // Hold (footswitch) latches even at a low Sustain setting
    p.sustain = 0.3f; p.hold = true;
    auto r3 = run (p, x, sr);
    CHECK (db (rms (r3.l, (size_t) (5.5 * sr), (size_t) (6 * sr))) > -30.0, "Hold did not latch the note");
}

static void testMuteStops()
{
    const double sr = 48000;
    auto x = pluck (110.0, sr, (int) (sr * 3));
    for (size_t i = (size_t) (1.5 * sr); i < x.size(); ++i) x[i] = 0.0f;   // abrupt mute (palm on the strings)
    Params p; p.sustain = 1.0f; p.bloomAmount = 0; p.releaseMs = 100; p.gateDb = -60;
    auto r = run (p, x, sr);
    CHECK (peak (r.l, (size_t) (2.2 * sr), x.size()) < 1e-4f, "a fast mute should stop even an infinite note (%.5f)", peak (r.l, (size_t) (2.2 * sr), x.size()));
}

static void testBloom()
{
    const double sr = 48000;
    const double hz = 110.0;
    auto x = pluck (hz, sr, (int) (sr * 4));
    for (auto h : { Harmonic::Octave, Harmonic::Twelfth, Harmonic::TwoOctaves })
    {
        Params p; p.sustain = 0.97f; p.bloomTime = 1.0f; p.ampDistance = 0.1f; p.bloomAmount = 1.0f; p.wobble = 0;
        p.harmonic = h; p.drive = 0; p.gateDb = -60;
        auto r = run (p, x, sr);
        const int n = harmonicNumber (h);
        const size_t a0 = (size_t) (0.1 * sr), a1 = (size_t) (0.3 * sr), b0 = (size_t) (3.0 * sr), b1 = (size_t) (3.8 * sr);
        const double fundEarly = tone (r.l, a0, a1, hz, sr), harmEarly = tone (r.l, a0, a1, hz * n, sr);
        const double fundLate = tone (r.l, b0, b1, hz, sr), harmLate = tone (r.l, b0, b1, hz * n, sr);
        CHECK (fundEarly > harmEarly, "harmonic %d: before the bloom the fundamental should lead (%.4f vs %.4f)", n, fundEarly, harmEarly);
        CHECK (harmLate > 3.0 * fundLate, "harmonic %d: after the bloom it should dominate (%.4f vs fundamental %.4f)", n, harmLate, fundLate);
    }
}

static void testSwell()
{
    const double sr = 48000;
    auto x = pluck (196.0, sr, (int) (sr * 1.5));
    Params p; p.bloomAmount = 0; p.swellMs = 800;
    auto slow = run (p, x, sr);
    p.swellMs = 0;
    auto fast = run (p, x, sr);
    const double s = rms (slow.l, 0, (size_t) (0.1 * sr)), f = rms (fast.l, 0, (size_t) (0.1 * sr));
    CHECK (db (s / f) < -20.0, "swell should remove the pick attack (first 100 ms only %.1f dB quieter)", db (s / f));
    CHECK (rms (slow.l, (size_t) (1.2 * sr), (size_t) (1.5 * sr)) > 0.5 * rms (fast.l, (size_t) (1.2 * sr), (size_t) (1.5 * sr)), "swell never reached full level");
}

static void testTracking()
{
    for (double hz : { 82.41, 146.83, 329.63, 880.0 })
    {
        Engine e;
        Params p; p.gateDb = -60;
        run (p, pluck (hz, 48000, 48000), 48000, 256, &e);
        const float f = e.getStatus().detectedHz.load();
        CHECK (std::abs (1200.0 * std::log2 (f / hz)) < 10.0, "loop tuned to %.2f Hz for %.2f Hz", f, hz);
    }
}

static void testPoly()
{
    std::array<float, Engine::kMaxCombs> f {};
    for (int s = 0; s < (int) builtinScales().size(); ++s)
    {
        Params p; p.key = 9; p.scaleIndex = s;
        const int n = Engine::polyFrequencies (p, f);
        CHECK (n >= 1 && n <= Engine::kMaxCombs, "scale %d: %d combs", s, n);
        for (int i = 0; i < n; ++i)
            CHECK (f[(size_t) i] >= 82.0f && f[(size_t) i] < 165.0f, "scale %d comb %d at %.2f Hz is outside E2..E3", s, i, f[(size_t) i]);
    }
    Params p; p.key = 9; p.scaleIndex = 1;   // A minor: A comb at 110 Hz
    Engine::polyFrequencies (p, f);
    CHECK (std::abs (f[0] - 110.0f) < 0.01f, "A minor root comb at %.2f Hz", f[0]);

    // a chord rings on in Poly mode
    const double sr = 48000;
    std::vector<float> chord ((size_t) (sr * 4), 0.0f);
    for (double hz : { 110.0, 164.81, 220.0 })
    {
        auto n = pluck (hz, sr, (int) chord.size(), 0.25f, 0.4);
        for (size_t i = 0; i < chord.size(); ++i) chord[i] += n[i];
    }
    p.mode = Mode::Poly; p.sustain = 1.0f; p.bloomAmount = 0.3f; p.gateDb = -50;
    auto r = run (p, chord, sr);
    CHECK (r.finite && db (rms (r.l, (size_t) (3.5 * sr), chord.size())) > -30.0, "poly chord did not sustain (%.1f dBFS)", db (rms (r.l, (size_t) (3.5 * sr), chord.size())));
}

static void testPresets()
{
    const auto& ps = factoryPresets();
    CHECK (ps.size() >= 20, "only %zu presets", ps.size());
    const double sr = 48000;
    auto x = pluck (196.0, sr, (int) (sr * 3), 0.8f, 0.8);
    for (auto& pr : ps)
    {
        auto r = run (pr.p, x, sr);
        CHECK (r.finite, "preset %s: non-finite output", pr.name);
        CHECK (peak (r.l) <= 1.0f, "preset %s: output peak %.3f", pr.name, peak (r.l));
        CHECK (rms (r.l, (size_t) (1.0 * sr), (size_t) (2.0 * sr)) > 1e-3, "preset %s: no sound", pr.name);
    }
}

int main()
{
    testSilence();
    testNeverClips();
    testSustain();
    testInfiniteLatch();
    testMuteStops();
    testBloom();
    testSwell();
    testTracking();
    testPoly();
    testPresets();
    if (failures == 0) std::printf ("All engine tests passed\n");
    else std::printf ("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
