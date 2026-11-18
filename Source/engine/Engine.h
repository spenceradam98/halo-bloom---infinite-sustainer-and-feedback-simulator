// Halo Bloom engine: infinite sustainer and amp-feedback simulator for guitar.
// No JUCE dependency so it can be unit-tested on its own.
#pragma once

#include <array>
#include <atomic>

#include "Dsp.h"
#include "Scales.h"

namespace eb
{
enum class Harmonic { Fundamental = 0, Octave = 1, Twelfth = 2, TwoOctaves = 3, Auto = 4 };
enum class Mode { Mono = 0, Poly = 1 };

// Harmonic number (1, 2, 3 or 4) for a fixed choice; Auto returns 2 (the engine picks at run time).
int harmonicNumber (Harmonic h);

struct Params
{
    // Sustain
    float sustain = 0.85f;        // 0..1. 0 = natural decay, 1 = infinite (the note latches and holds)
    float swellMs = 0.0f;         // 0..2000, fade-in after each note (removes the pick)
    float releaseMs = 400.0f;     // 10..4000, fade-out after a mute or when the gate closes

    // Bloom
    float bloomTime = 3.0f;       // seconds before the harmonic has fully taken over (0.2..15)
    Harmonic harmonic = Harmonic::Octave;
    float bloomAmount = 0.7f;     // 0..1, how much of the note the harmonic takes over
    float wobble = 0.2f;          // 0..1, unstable feedback flutter

    // Amp
    float ampDistance = 1.0f;     // metres from the amp, 0.1..4 (closer = faster, louder bloom)
    float tone = 0.6f;            // 0..1, dark to bright
    float body = 0.4f;            // 0..1, guitar body resonance
    float drive = 0.3f;           // 0..1, amp saturation

    // Mode
    Mode mode = Mode::Mono;
    int key = 4;                  // root pitch class (0 = C) for Poly mode
    int scaleIndex = 0;           // builtinScales() index
    float refHz = 440.0f;

    // Global
    float gateDb = -52.0f;        // input below this ends (or, at infinite sustain, latches) the note
    float mix = 1.0f;             // 0..1 dry/wet
    float outputDb = 0.0f;
    bool limiter = true;

    // Performance (footswitch / MIDI)
    bool hold = false;            // latch the current note forever, whatever Sustain says
    bool bloomNow = false;        // jump straight into the harmonic
};

// What the UI shows. Written by the audio thread, read by the editor.
struct Status
{
    std::atomic<float> inLevel { 0 }, outLevel { 0 };
    std::atomic<float> detectedHz { 0 };      // the pitch the feedback loop is tuned to (0 = none)
    std::atomic<bool> noteActive { false }, latched { false };
    std::atomic<float> bloom { 0 };            // 0..1 bloom progress (shaped)
    std::atomic<int> harmonic { 1 };           // harmonic number currently blooming (1..4)
    std::array<std::atomic<float>, 4> ladder {};   // energy on partials 1x..4x, 0..1
    std::atomic<float> sustainGainDb { 0 };    // how much the sustainer is lifting the input
    std::atomic<float> limiterDb { 0 };        // output limiter gain reduction (<= 0)
    std::atomic<float> loopPeak { 0 };         // peak inside the feedback loop since last read (diagnostics/tests)
};

class Engine
{
public:
    static constexpr int kMaxCombs = 12;
    static constexpr int kControlInterval = 32;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const Params& p) { params = p; }
    const Params& getParams() const { return params; }

    // in/out are channel arrays; mono or stereo. In-place is allowed.
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);

    const Status& getStatus() const { return status; }
    Status& getStatus() { return status; }

    // Poly-mode comb frequencies (Hz) for a key/scale, folded into the lowest guitar octave.
    static int polyFrequencies (const Params& p, std::array<float, kMaxCombs>& out);

private:
    struct Loop   // saturated, damped feedback loop (Karplus-style) with a DC blocker inside
    {
        DelayLine delay;
        OnePole damp;
        DcBlocker dc;
        float period = 0, y = 0;
        void reset() { delay.reset(); damp.z = 0; dc.reset(); y = 0; }
        float process (float in, float fb, float lpDelay, float& peak)
        {
            float r = delay.read (std::max (2.0f, period - lpDelay));
            r = dc.process (damp.process (r));
            float v = softClip (in + fb * r);
            v = std::clamp (v, -0.98f, 0.98f);   // hard ceiling inside the loop: it can never run away
            peak = std::max (peak, std::abs (v));
            delay.write (v);
            y = v;
            return v;
        }
    };

    void control();
    void startNote();
    void startRelease();
    void endNote();
    void retuneFilters();

    double sr = 48000;
    Params params;
    Status status;

    PitchDetector detector;
    Envelope envIn, envFast, envSlow, envSus, envLoop, envHarm, envOut;
    std::array<Envelope, 3> envAnalysis;
    std::array<Biquad, 3> analysis;   // input energy at 2x, 3x, 4x for Auto
    Loop loop;
    std::array<Loop, kMaxCombs> combA, combB;
    std::array<float, kMaxCombs> polyHz {};
    int numCombs = 0, polyKey = -1, polyScale = -1;
    float polyRef = 0;
    Biquad harmBand1, harmBand2, hp, toneLp, body1, body2;
    DcBlocker harmDc;

    // state
    bool noteActive = false, releasing = false, latched = false, havePitch = false, autoChosen = false;
    bool notePitched = false, endsIfUnpitched = false;
    float agcGain = 1, noteGain = 0, swellPos = 1, releaseStep = 0;
    float period = 0, targetPeriod = 0, pendingPeriod = 0;
    int pendingCount = 0;
    float bloom = 0, bloomShaped = 0, timeSinceOnset = 0, refractory = 0, belowGateTime = 0;
    int harmonicN = 2;
    float refLevel = 0, loopNorm = 0, harmNorm = 0;
    float fb = 0, loopMix = 0, wob = 0, wobRand = 0, wobRandTarget = 0, lfoPhase = 0;
    float lpDelay = 0, inGainSmoothed = 0;
    float limGain = 1, limRel = 0.001f, curOut = 1, curMix = 1;
    float loopPeak = 0, inPeak = 0, outPeak = 0;
    int controlCount = 0;
    Rng rng;
};
} // namespace eb
