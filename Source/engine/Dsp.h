// DSP building blocks for Halo Bloom. Plain scalar C++ (no SIMD intrinsics, no -march).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eb
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float flushTiny (float x) { return std::abs (x) < 1.0e-15f ? 0.0f : x; }

// YIN monophonic pitch detector, tuned for guitar (about 60 Hz to 1.4 kHz).
// The input is low-passed and decimated to roughly 11-12 kHz, then analysed every hop.
class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        decim = std::max (1, (int) (sampleRate / 16000.0));
        fs = sampleRate / decim;
        maxLag = (int) std::ceil (fs / kMinHz) + 2;
        minLag = std::max (2, (int) std::floor (fs / kMaxHz));
        window = std::max (maxLag, (int) (fs * 0.024));
        bufLen = window + maxLag + 2;
        buf.assign ((size_t) bufLen, 0.0f);
        lin.assign ((size_t) bufLen, 0.0f);
        diff.assign ((size_t) maxLag + 2, 0.0f);
        hop = std::max (16, (int) (fs * 0.004));
        // two cascaded one-pole low-passes at ~1.8 kHz as anti-alias before decimating
        lpCoef = (float) (1.0 - std::exp (-2.0 * kPi * 1800.0 / sampleRate));
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        lp1 = lp2 = 0;
        decimCount = 0; writePos = 0; sinceHop = 0; filled = 0;
        freq = 0; voiced = false; level = 0; clarity = 0;
    }

    void setGateDb (float db) { gate = std::pow (10.0f, db / 20.0f); }

    // Feed one input sample; returns true when a new analysis frame was produced.
    bool push (float x)
    {
        lp1 += lpCoef * (x - lp1);
        lp2 += lpCoef * (lp1 - lp2);
        if (++decimCount < decim) return false;
        decimCount = 0;
        buf[(size_t) writePos] = lp2;
        writePos = (writePos + 1) % bufLen;
        if (filled < bufLen) ++filled;
        if (++sinceHop < hop || filled < bufLen) return false;
        sinceHop = 0;
        analyse();
        return true;
    }

    float getFrequency() const { return freq; }   // Hz, 0 if unvoiced
    bool isVoiced() const { return voiced; }
    float getLevel() const { return level; }      // RMS of the analysis window
    float getClarity() const { return clarity; }  // 1 - aperiodicity, 0..1

    static constexpr double kMinHz = 60.0, kMaxHz = 1400.0;

private:
    float at (int i) const { return buf[(size_t) ((writePos + i) % bufLen)]; }   // 0 = oldest

    void analyse()
    {
        for (int j = 0; j < bufLen; ++j) lin[(size_t) j] = at (j);
        const float* x = lin.data();
        double e = 0;
        for (int j = 0; j < window; ++j) e += x[j] * x[j];
        level = (float) std::sqrt (e / window);
        if (level < gate) { voiced = false; clarity = 0; return; }

        // difference function
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0;
            for (int j = 0; j < window; ++j)
            {
                const float a = x[j] - x[j + tau];
                d += a * a;
            }
            diff[(size_t) tau] = (float) d;
        }
        // cumulative mean normalised difference
        diff[0] = 1.0f;
        double running = 0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = running > 0 ? (float) (diff[(size_t) tau] * tau / running) : 1.0f;
        }
        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (diff[(size_t) tau] < kThreshold)
            {
                while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) { voiced = false; clarity = 0; return; }

        // parabolic interpolation
        double t = best;
        const double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-12) t += 0.5 * (a - c) / den;
        clarity = (float) std::clamp (1.0 - b, 0.0, 1.0);
        freq = (float) (fs / t);
        voiced = freq >= kMinHz && freq <= kMaxHz;
    }

    static constexpr float kThreshold = 0.15f;
    std::vector<float> buf, lin, diff;
    double fs = 11025;
    int decim = 4, maxLag = 200, minLag = 8, window = 260, bufLen = 462, hop = 44;
    int decimCount = 0, writePos = 0, sinceHop = 0, filled = 0;
    float lpCoef = 0.2f, lp1 = 0, lp2 = 0, gate = 0.001f;
    float freq = 0, level = 0, clarity = 0;
    bool voiced = false;
};

// ---------------------------------------------------------------------------------------
// RBJ biquad. Only the shapes this plugin needs.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    // Band-pass with 0 dB peak gain at the centre.
    void setBandpass (double sr, double hz, double q)
    {
        hz = std::clamp (hz, 10.0, sr * 0.45);
        const double w = 2 * kPi * hz / sr, alpha = std::sin (w) / (2 * q), a0 = 1 + alpha;
        b0 = (float) (alpha / a0); b1 = 0; b2 = (float) (-alpha / a0);
        a1 = (float) (-2 * std::cos (w) / a0); a2 = (float) ((1 - alpha) / a0);
    }
    void setPeak (double sr, double hz, double q, double db)
    {
        hz = std::clamp (hz, 10.0, sr * 0.45);
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * hz / sr, alpha = std::sin (w) / (2 * q);
        const double a0 = 1 + alpha / A;
        b0 = (float) ((1 + alpha * A) / a0); b1 = (float) (-2 * std::cos (w) / a0); b2 = (float) ((1 - alpha * A) / a0);
        a1 = b1; a2 = (float) ((1 - alpha / A) / a0);
    }
    void setLowpass (double sr, double hz, double q = 0.7071)
    {
        hz = std::clamp (hz, 10.0, sr * 0.45);
        const double w = 2 * kPi * hz / sr, alpha = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + alpha;
        b0 = (float) ((1 - c) * 0.5 / a0); b1 = (float) ((1 - c) / a0); b2 = b0;
        a1 = (float) (-2 * c / a0); a2 = (float) ((1 - alpha) / a0);
    }
    void setHighpass (double sr, double hz, double q = 0.7071)
    {
        hz = std::clamp (hz, 10.0, sr * 0.45);
        const double w = 2 * kPi * hz / sr, alpha = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + alpha;
        b0 = (float) ((1 + c) * 0.5 / a0); b1 = (float) (-(1 + c) / a0); b2 = b0;
        a1 = (float) (-2 * c / a0); a2 = (float) ((1 - alpha) / a0);
    }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = flushTiny (b1 * x - a1 * y + z2);
        z2 = flushTiny (b2 * x - a2 * y);
        return y;
    }
};

// One-pole low-pass.
struct OnePole
{
    float a = 1, z = 0;
    void setHz (double sr, double hz) { a = (float) (1.0 - std::exp (-2.0 * kPi * std::min (hz, sr * 0.49) / sr)); }
    float process (float x) { z = flushTiny (z + a * (x - z)); return z; }
};

// DC blocker (one-zero, one-pole high-pass at ~10 Hz).
struct DcBlocker
{
    float r = 0.9987f, x1 = 0, y1 = 0;
    void setup (double sr) { r = (float) std::exp (-2.0 * kPi * 10.0 / sr); }
    void reset() { x1 = y1 = 0; }
    float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = flushTiny (y); return y1; }
};

// Peak envelope follower with separate attack and release times.
struct Envelope
{
    float att = 0.1f, rel = 0.001f, value = 0;
    void setup (double sr, double attackMs, double releaseMs)
    {
        att = (float) (1.0 - std::exp (-1.0 / (std::max (0.01, attackMs) * 0.001 * sr)));
        rel = (float) (1.0 - std::exp (-1.0 / (std::max (0.01, releaseMs) * 0.001 * sr)));
    }
    float process (float x)
    {
        const float a = std::abs (x);
        value += (a > value ? att : rel) * (a - value);
        value = flushTiny (value);
        return value;
    }
};

// Circular delay line with a fractional 4-point Hermite read.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void write (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // d = delay in samples measured from the most recent write (d >= 1)
    float read (float d) const
    {
        d = std::clamp (d, 1.0f, (float) (mask - 3));
        float pos = (float) w - d;
        while (pos < 0) pos += (float) (mask + 1);
        const int i = (int) pos;
        const float f = pos - (float) i;
        const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)],
                    x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// Smooth soft clipper that never exceeds +-1.
inline float softClip (float x) { return std::tanh (x); }

// Small deterministic random generator (for the feedback wobble).
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / (float) 0xffffff * 2.0f - 1.0f; }
};
} // namespace eb
