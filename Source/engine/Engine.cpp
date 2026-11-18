#include "Engine.h"

namespace eb
{
namespace
{
constexpr float kTarget = 0.25f;          // sustainer target level (about -12 dBFS peak)
constexpr float kMaxLiftDb = 60.0f;       // most the sustainer will ever lift a fading string
constexpr float kCeiling = 0.944f;        // output limiter ceiling (-0.5 dBFS)
constexpr float kLowGuitarHz = 82.41f;    // E2: poly combs are folded into [E2, E3)

float proximity (float metres) { return std::clamp (1.0f - (metres - 0.1f) / 3.9f, 0.0f, 1.0f); }
float smoothstep (float x) { x = std::clamp (x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

// Phase delay (samples) of the loop's one-pole damping filter at angular frequency w.
float onePoleDelay (float a, float w)
{
    const float b = 1.0f - a;
    return w > 1.0e-4f ? std::atan2 (b * std::sin (w), 1.0f - b * std::cos (w)) / w : b / std::max (a, 1.0e-4f);
}
} // namespace

int harmonicNumber (Harmonic h)
{
    switch (h)
    {
        case Harmonic::Fundamental: return 1;
        case Harmonic::Octave:      return 2;
        case Harmonic::Twelfth:     return 3;
        case Harmonic::TwoOctaves:  return 4;
        case Harmonic::Auto:        break;
    }
    return 2;
}

int Engine::polyFrequencies (const Params& p, std::array<float, kMaxCombs>& out)
{
    const auto& scales = builtinScales();
    const Scale& sc = scales[(size_t) std::clamp (p.scaleIndex, 0, (int) scales.size() - 1)];
    const double root = p.refHz * std::pow (2.0, (p.key - 9) / 12.0);   // root in octave 4
    // pick up to kMaxCombs degrees spread evenly over one period
    const int n = std::max (1, sc.n);
    const int count = std::min (n, kMaxCombs);
    for (int i = 0; i < count; ++i)
    {
        const int degree = (int) ((long) i * n / count);
        double hz = root * std::pow (2.0, sc.degreeCents (degree) / 1200.0);
        while (hz >= 2.0 * kLowGuitarHz) hz *= 0.5;
        while (hz < kLowGuitarHz) hz *= 2.0;
        out[(size_t) i] = (float) hz;
    }
    return count;
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    detector.prepare (sr);
    envIn.setup (sr, 1.0, 40.0);
    envFast.setup (sr, 0.3, 10.0);
    envSlow.setup (sr, 10.0, 200.0);
    envSus.setup (sr, 5.0, 300.0);
    envLoop.setup (sr, 5.0, 300.0);
    envHarm.setup (sr, 5.0, 300.0);
    envOut.setup (sr, 0.5, 120.0);
    for (auto& e : envAnalysis) e.setup (sr, 5.0, 100.0);

    const int maxPeriod = (int) std::ceil (sr / 40.0) + 8;
    loop.delay.prepare (maxPeriod);
    loop.dc.setup (sr);
    for (int i = 0; i < kMaxCombs; ++i)
    {
        combA[(size_t) i].delay.prepare (maxPeriod);
        combB[(size_t) i].delay.prepare (maxPeriod);
        combA[(size_t) i].dc.setup (sr);
        combB[(size_t) i].dc.setup (sr);
    }
    harmDc.setup (sr);
    hp.setHighpass (sr, 70.0);
    limRel = (float) (1.0 - std::exp (-1.0 / (0.08 * sr)));
    polyKey = polyScale = -1;
    reset();
}

void Engine::reset()
{
    detector.reset();
    for (auto* e : { &envIn, &envFast, &envSlow, &envSus, &envLoop, &envHarm, &envOut }) e->value = 0;
    for (auto& e : envAnalysis) e.value = 0;
    for (auto& b : analysis) b.reset();
    loop.reset();
    for (auto& c : combA) c.reset();
    for (auto& c : combB) c.reset();
    for (auto* b : { &harmBand1, &harmBand2, &hp, &toneLp, &body1, &body2 }) b->reset();
    harmDc.reset();
    noteActive = releasing = latched = havePitch = autoChosen = notePitched = endsIfUnpitched = false;
    agcGain = 1; noteGain = 0; swellPos = 1;
    period = targetPeriod = pendingPeriod = 0; pendingCount = 0;
    bloom = bloomShaped = timeSinceOnset = refractory = belowGateTime = 0;
    refLevel = loopNorm = harmNorm = 0;
    fb = loopMix = wob = wobRand = wobRandTarget = lfoPhase = 0;
    inGainSmoothed = 1;
    limGain = 1; curOut = dbToGain (params.outputDb); curMix = params.mix;
    loopPeak = inPeak = outPeak = 0;
    controlCount = 0;
    harmonicN = harmonicNumber (params.harmonic);
    retuneFilters();
}

// ------------------------------------------------------------------------------------ note life
void Engine::startNote()
{
    const bool wasLatched = latched;
    noteActive = true;
    releasing = false;
    latched = false;
    swellPos = params.swellMs > 1.0f ? 0.0f : 1.0f;
    if (params.swellMs > 1.0f) noteGain = 0.0f;
    bloom = 0;
    timeSinceOnset = 0;
    refractory = 0.08f;
    belowGateTime = 0;
    pendingCount = 0;
    autoChosen = false;
    notePitched = false;
    endsIfUnpitched = wasLatched;   // tapping muted strings stops a latched note
    harmonicN = harmonicNumber (params.harmonic);
}

void Engine::startRelease()
{
    if (! noteActive || releasing) return;
    releasing = true;
    releaseStep = 1.0f / (float) (std::max (10.0f, params.releaseMs) * 0.001 * sr);
}

void Engine::endNote()
{
    noteActive = releasing = latched = false;
    noteGain = 0;
    bloom = bloomShaped = 0;
    loop.reset();
    for (auto& c : combA) c.reset();
    for (auto& c : combB) c.reset();
    harmBand1.reset(); harmBand2.reset(); harmDc.reset();
    envLoop.value = envHarm.value = 0;
    loopNorm = harmNorm = 0;
}

void Engine::retuneFilters()
{
    const float w = 1.0f / (float) sr;
    // amp stage
    toneLp.setLowpass (sr, 1200.0 * std::pow (10.0, params.tone * 1.05), 0.6);
    body1.setPeak (sr, 105.0, 1.1, params.body * 8.0);
    body2.setPeak (sr, 235.0, 1.8, params.body * 5.0);

    // loop damping: darker tone = fewer partials survive in the loop
    const double dampHz = 1500.0 + params.tone * 7500.0;
    loop.damp.setHz (sr, dampHz);
    for (auto& c : combA) c.damp.setHz (sr, dampHz);
    for (auto& c : combB) c.damp.setHz (sr, dampHz);

    if (period > 1.0f)
    {
        const float f0 = (float) sr / period;
        const float wobRatio = std::pow (2.0f, wob * 0.25f * bloomShaped / 12.0f);
        harmBand1.setBandpass (sr, f0 * (float) harmonicN * wobRatio, 8.0);
        harmBand2.setBandpass (sr, f0 * (float) harmonicN * wobRatio, 8.0);
        for (int i = 0; i < 3; ++i) analysis[(size_t) i].setBandpass (sr, f0 * (float) (i + 2), 10.0);
        lpDelay = onePoleDelay (loop.damp.a, 2.0f * (float) kPi * f0 * w);
    }
}

// ------------------------------------------------------------------------------------ control rate
void Engine::control()
{
    const auto& p = params;
    const float dt = (float) kControlInterval / (float) sr;
    const float gate = dbToGain (p.gateDb);
    const float prox = proximity (p.ampDistance);
    const bool infinite = p.sustain >= 0.995f || p.hold;
    const bool poly = p.mode == Mode::Poly;

    // ---- poly comb tuning (only when the key/scale changes)
    if (poly && (p.key != polyKey || p.scaleIndex != polyScale || p.refHz != polyRef))
    {
        polyKey = p.key; polyScale = p.scaleIndex; polyRef = p.refHz;
        numCombs = polyFrequencies (p, polyHz);
    }

    // ---- note detection: onsets, picks, mutes, gate
    refractory = std::max (0.0f, refractory - dt);
    if (noteActive) timeSinceOnset += dt;
    const bool aboveGate = envIn.value > gate;
    const bool pick = envFast.value > gate && envFast.value > 2.5f * envSlow.value && refractory <= 0.0f;
    const bool mute = noteActive && ! releasing && ! latched && envSlow.value > gate
                      && envFast.value < 0.08f * envSlow.value && timeSinceOnset > 0.05f;

    if (! noteActive || releasing)
    {
        if (aboveGate && (pick || ! noteActive)) startNote();
    }
    else if (pick)
        startNote();
    else if (mute && ! p.hold)
        startRelease();
    else if (! aboveGate && ! latched)
    {
        belowGateTime += dt;
        if (belowGateTime > 0.03f)
        {
            if (infinite && (havePitch || poly)) latched = true;
            else startRelease();
        }
    }
    else
        belowGateTime = 0;

    if (latched && ! infinite) startRelease();   // Hold footswitch let go, or Sustain turned down
    if (noteActive && endsIfUnpitched && ! poly && ! notePitched && timeSinceOnset > 0.15f)
    {
        endsIfUnpitched = false;
        startRelease();
    }
    if (notePitched) endsIfUnpitched = false;

    // ---- bloom progress
    if (noteActive && (notePitched || poly || latched))
    {
        const float effTime = std::max (0.2f, p.bloomTime) * (0.5f + 1.5f * (1.0f - prox));
        bloom += dt / effTime;
        if (p.bloomNow) bloom += dt / 0.25f;
        bloom = std::min (1.0f, bloom);
    }
    bloomShaped = smoothstep (bloom);

    // ---- Auto harmonic: the strongest of the 2nd, 3rd and 4th partial in the playing wins,
    // like real feedback locking onto whatever the room and strings favour.
    if (p.harmonic == Harmonic::Auto)
    {
        if (poly) harmonicN = 2;
        else if (notePitched && ! autoChosen && timeSinceOnset > 0.15f)
        {
            static const float weight[3] = { 1.0f, 0.8f, 0.6f };
            int best = 0;
            for (int i = 1; i < 3; ++i)
                if (envAnalysis[(size_t) i].value * weight[i] > envAnalysis[(size_t) best].value * weight[best]) best = i;
            harmonicN = 2 + best;
            autoChosen = true;
        }
    }
    else
        harmonicN = harmonicNumber (p.harmonic);

    // ---- sustainer gain (an upward compressor: Sustain is the fraction of the way to the target)
    {
        const float desired = kTarget / std::max (envIn.value, 1.0e-6f);
        const float s = p.hold ? 1.0f : std::clamp (p.sustain, 0.0f, 1.0f);   // the Hold footswitch is infinite sustain
        const float g = std::clamp (std::pow (desired, s), dbToGain (-30.0f), dbToGain (kMaxLiftDb * s));
        agcGain += (g < agcGain ? 0.5f : 0.035f) * (g - agcGain);
    }
    inGainSmoothed += 0.15f * ((latched ? 0.0f : 1.0f) - inGainSmoothed);

    // ---- feedback loop gain and blend
    const float drive = bloomShaped * (0.5f + 0.5f * prox);
    const float fbMax = infinite ? 1.1f : 0.97f;
    const float fbTarget = latched ? 1.25f : 0.35f + (fbMax - 0.35f) * drive;
    fb += 0.08f * (fbTarget - fb);
    const float mixTarget = latched ? 1.0f : (havePitch || poly ? 0.15f + 0.75f * drive : 0.0f);
    loopMix += 0.08f * (mixTarget - loopMix);
    if (! latched) refLevel = envSus.value;

    // ---- wobble: sine plus smoothed random, grows with the bloom
    lfoPhase += dt * 5.3f;
    if (lfoPhase >= 1.0f) { lfoPhase -= 1.0f; wobRandTarget = rng.next(); }
    wobRand += 0.02f * (wobRandTarget - wobRand);
    wob = p.wobble * (0.6f * std::sin (2.0f * (float) kPi * lfoPhase) + 0.8f * wobRand);

    // ---- pitch glide and loop tuning
    if (targetPeriod > 1.0f)
    {
        if (period <= 1.0f) period = targetPeriod;
        period += 0.25f * (targetPeriod - period);
    }
    const float wobCents = wob * 15.0f * (0.3f + 0.7f * bloomShaped);
    loop.period = period * std::pow (2.0f, -wobCents / 1200.0f);
    const float wobRatio = std::pow (2.0f, wobCents / 1200.0f);
    for (int i = 0; i < numCombs; ++i)
    {
        combA[(size_t) i].period = (float) sr / (polyHz[(size_t) i] * wobRatio);
        combB[(size_t) i].period = (float) sr / (polyHz[(size_t) i] * (float) harmonicN * wobRatio);
    }
    retuneFilters();

    // ---- status for the UI
    status.detectedHz.store (poly ? 0.0f : (havePitch && period > 1.0f ? (float) sr / period : 0.0f));
    status.noteActive.store (noteActive);
    status.latched.store (latched);
    status.bloom.store (noteActive ? bloomShaped : 0.0f);
    status.harmonic.store (harmonicN);
    status.sustainGainDb.store (noteActive && ! latched ? gainToDb (agcGain) : 0.0f);
    {
        const float level = noteActive ? noteGain * std::min (1.0f, refLevel * 4.0f) : 0.0f;
        const float k = p.bloomAmount * bloomShaped;
        for (int i = 0; i < 4; ++i)
        {
            float v = (i == 0 ? (1.0f - k) : 0.0f) + (i == harmonicN - 1 ? k : 0.0f);
            if (! poly && notePitched && i > 0 && i != harmonicN - 1) v += 0.15f * std::min (1.0f, envAnalysis[(size_t) i - 1].value * 4.0f);
            status.ladder[(size_t) i].store (std::min (1.0f, v * level));
        }
    }
}

// ------------------------------------------------------------------------------------ audio
void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples)
{
    const auto& p = params;
    const bool poly = p.mode == Mode::Poly;
    const float prox = proximity (p.ampDistance);
    const float driveAmt = 1.0f + p.drive * 24.0f;
    const float driveNorm = 1.0f / std::sqrt (driveAmt);
    const float ampGain = dbToGain (-4.0f + 7.0f * prox);
    const float outTarget = dbToGain (p.outputDb);
    const float mixTarget = std::clamp (p.mix, 0.0f, 1.0f);
    const float swellStep = p.swellMs > 1.0f ? 1.0f / (float) (p.swellMs * 0.001 * sr) : 1.0f;
    const float combIn = numCombs > 0 ? 1.0f / std::sqrt ((float) numCombs) : 0.0f;
    const int chans = std::max (1, numOut);

    for (int n = 0; n < numSamples; ++n)
    {
        if (controlCount-- <= 0) { controlCount = kControlInterval - 1; control(); }

        float dryL = numIn > 0 ? in[0][n] : 0.0f;
        float dryR = numIn > 1 ? in[1][n] : dryL;
        const float x = numIn > 1 ? 0.5f * (dryL + dryR) : dryL;
        inPeak = std::max (inPeak, std::abs (x));

        envIn.process (x); envFast.process (x); envSlow.process (x);

        // ---- pitch tracking (mono mode, only while a fresh note is being played)
        if (detector.push (x) && ! poly && noteActive && ! latched && ! releasing)
        {
            if (detector.isVoiced() && detector.getClarity() > 0.75f)
            {
                const float pNew = (float) (sr / detector.getFrequency());
                if (! notePitched)
                {
                    targetPeriod = pNew;
                    if (! havePitch) period = pNew;
                    notePitched = havePitch = true;
                    pendingCount = 0;
                }
                else
                {
                    const float cents = 1200.0f * std::log2 (targetPeriod / pNew);
                    if (std::abs (cents) > 40.0f)
                    {
                        // a new note without a pick (hammer-on, slide): accept after two agreeing frames
                        if (pendingCount > 0 && std::abs (1200.0f * std::log2 (pendingPeriod / pNew)) < 30.0f)
                        {
                            if (++pendingCount >= 2) { targetPeriod = pNew; bloom *= 0.5f; pendingCount = 0; autoChosen = false; timeSinceOnset = 0.0f; }
                        }
                        else { pendingPeriod = pNew; pendingCount = 1; }
                    }
                    else { targetPeriod = pNew; pendingCount = 0; }   // vibrato and bends
                }
            }
        }
        for (int i = 0; i < 3; ++i) envAnalysis[(size_t) i].process (analysis[(size_t) i].process (x));

        // ---- note envelope (swell in, release out)
        if (noteActive)
        {
            if (releasing)
            {
                noteGain -= releaseStep;
                if (noteGain <= 0.0f) endNote();
            }
            else if (swellPos < 1.0f)
            {
                swellPos = std::min (1.0f, swellPos + swellStep);
                noteGain = swellPos * swellPos;   // slow start, like a bow
            }
            else
                noteGain = std::min (1.0f, noteGain + 0.005f);
        }

        float wet = 0.0f;
        if (noteActive)
        {
            // ---- sustainer
            const float sus = x * agcGain * inGainSmoothed;
            envSus.process (sus);

            // ---- feedback: a tuned loop (Mono) or a comb bank on the scale (Poly)
            float fbOut = 0.0f, harm = 0.0f;
            if (! poly)
            {
                if (period > 1.0f) fbOut = loop.process (sus * 0.5f, fb, lpDelay, loopPeak);
            }
            else
            {
                for (int i = 0; i < numCombs; ++i)
                {
                    auto& c = combA[(size_t) i];
                    const float cd = onePoleDelay (c.damp.a, 2.0f * (float) kPi / std::max (2.0f, c.period));
                    fbOut += c.process (sus * 0.5f * combIn, fb, cd, loopPeak);
                }
                fbOut *= combIn;
            }
            envLoop.process (fbOut);
            const float ln = refLevel / std::max (envLoop.value, 1.0e-5f);
            loopNorm += 0.002f * (std::min (ln, 8.0f) - loopNorm);
            const float core = sus * (1.0f - loopMix) + fbOut * loopNorm * loopMix;

            // ---- bloom: the chosen partial grows out of the note
            float src = core;
            if (harmonicN == 2 || harmonicN == 4) src += 0.8f * std::abs (core);   // rectifying adds even partials
            if (harmonicN >= 3) src += 0.5f * softClip (4.0f * core);               // saturation adds odd partials
            src = harmDc.process (src);
            if (! poly)
                harm = harmBand2.process (harmBand1.process (src));
            else
            {
                for (int i = 0; i < numCombs; ++i)
                {
                    auto& c = combB[(size_t) i];
                    const float cd = onePoleDelay (c.damp.a, 2.0f * (float) kPi / std::max (2.0f, c.period));
                    harm += c.process (src * 0.5f * combIn, std::min (fb, 0.99f), cd, loopPeak);
                }
                harm *= combIn;
            }
            envHarm.process (harm);
            const float hn = refLevel / std::max (envHarm.value, 1.0e-5f);
            harmNorm += 0.002f * (std::min (hn, 30.0f) - harmNorm);
            const float k = p.bloomAmount * bloomShaped;
            wet = core * (1.0f - k) + harm * harmNorm * k * (1.0f + 0.35f * wob * bloomShaped);

            // ---- amp: drive, tone, guitar body, loudness by distance
            wet = softClip (wet * driveAmt) * driveNorm;
            wet = body2.process (body1.process (toneLp.process (hp.process (wet))));
            wet *= ampGain * noteGain;
        }
        if (! std::isfinite (wet)) { wet = 0.0f; endNote(); }

        // ---- mix, output, limiter
        curMix += 0.002f * (mixTarget - curMix);
        curOut += 0.002f * (outTarget - curOut);
        float l = (dryL * (1.0f - curMix) + wet * curMix) * curOut;
        float r = (dryR * (1.0f - curMix) + wet * curMix) * curOut;
        if (p.limiter)
        {
            const float pk = std::max (std::abs (l), std::abs (r));
            if (pk * limGain > kCeiling) limGain = kCeiling / pk;
            else limGain += limRel * (1.0f - limGain);
            l = std::clamp (l * limGain, -kCeiling, kCeiling);
            r = std::clamp (r * limGain, -kCeiling, kCeiling);
        }
        else
            limGain = 1.0f;
        envOut.process (std::max (std::abs (l), std::abs (r)));
        outPeak = std::max (outPeak, std::max (std::abs (l), std::abs (r)));

        out[0][n] = l;
        if (chans > 1) out[1][n] = r;
        for (int c = 2; c < numOut; ++c) out[c][n] = l;
    }

    status.inLevel.store (inPeak);
    status.outLevel.store (outPeak);
    status.limiterDb.store (gainToDb (limGain));
    status.loopPeak.store (std::max (status.loopPeak.load(), loopPeak));
    inPeak = outPeak = loopPeak = 0;
}
} // namespace eb
