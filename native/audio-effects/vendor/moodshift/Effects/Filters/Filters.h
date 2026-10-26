#pragma once

#include "../Utility/DspMath.h"

namespace msfx::dsp
{
/** Topology-preserving-transform state variable filter (Zavalishin / Simper).
    Unconditionally stable under fast cutoff modulation; one instance per channel. */
class SVF
{
public:
    enum class Mode { lowpass, bandpass, highpass, notch, peak, allpass, bandpassNormalized };

    void prepare (double sr) noexcept { sampleRate = float (sr); update(); }
    void reset() noexcept { ic1 = ic2 = 0.0f; }
    void setMode (Mode m) noexcept { mode = m; }

    /** q: resonance as Q (0.5 = gentle, 10+ = very resonant). */
    void setParams (float cutoffHz, float q) noexcept
    {
        cutoff = clamp (cutoffHz, 10.0f, 0.49f * sampleRate);
        Q = clamp (q, 0.05f, 40.0f);
        update();
    }
    void setCutoff (float cutoffHz) noexcept { setParams (cutoffHz, Q); }

    float process (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = sanitize (2.0f * v1 - ic1);
        ic2 = sanitize (2.0f * v2 - ic2);
        const float lp = v2, bp = v1, hp = x - k * v1 - v2;
        switch (mode)
        {
            case Mode::lowpass:            return lp;
            case Mode::bandpass:           return bp;
            case Mode::bandpassNormalized: return bp * k;
            case Mode::highpass:           return hp;
            case Mode::notch:              return lp + hp;
            case Mode::peak:               return lp - hp;
            case Mode::allpass:            return x - 2.0f * k * bp;
        }
        return lp;
    }

private:
    void update() noexcept
    {
        g = std::tan (kPi * cutoff / sampleRate);
        k = 1.0f / Q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float sampleRate = 44100.0f, cutoff = 1000.0f, Q = 0.707f;
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;
    Mode mode = Mode::lowpass;
};

/** One-pole low/high pass. */
class OnePole
{
public:
    void prepare (double sr) noexcept { sampleRate = float (sr); setCutoff (cutoff); }
    void reset (float v = 0.0f) noexcept { z = v; }
    void setCutoff (float hz) noexcept
    {
        cutoff = clamp (hz, 1.0f, 0.49f * sampleRate);
        b = 1.0f - std::exp (-kTwoPi * cutoff / sampleRate);
    }
    float lowpass (float x) noexcept { z += b * (x - z); z = sanitize (z); return z; }
    float highpass (float x) noexcept { return x - lowpass (x); }

private:
    float sampleRate = 44100.0f, cutoff = 1000.0f, b = 0.1f, z = 0.0f;
};

/** RBJ cookbook biquad (transposed direct form II). For static or slowly varying EQ. */
class Biquad
{
public:
    enum class Type { lowpass, highpass, bandpass, peak, lowShelf, highShelf };

    void prepare (double sr) noexcept { sampleRate = float (sr); }
    void reset() noexcept { s1 = s2 = 0.0f; }
    void set (Type type, float freq, float q, float gainDb = 0.0f) noexcept
    {
        const float f = clamp (freq, 10.0f, 0.49f * sampleRate);
        const float w = kTwoPi * f / sampleRate;
        const float cw = std::cos (w), sw = std::sin (w);
        const float alpha = sw / (2.0f * std::max (0.05f, q));
        const float A = std::pow (10.0f, gainDb / 40.0f);
        float b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type)
        {
            case Type::lowpass:  b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::highpass: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::bandpass: b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::peak:     b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case Type::lowShelf:
            {
                const float sq = 2.0f * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sq);
                a0 = (A + 1) + (A - 1) * cw + sq; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sq;
                break;
            }
            case Type::highShelf:
            {
                const float sq = 2.0f * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sq);
                a0 = (A + 1) - (A - 1) * cw + sq; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sq;
                break;
            }
        }
        nb0 = b0 / a0; nb1 = b1 / a0; nb2 = b2 / a0; na1 = a1 / a0; na2 = a2 / a0;
    }
    float process (float x) noexcept
    {
        const float y = nb0 * x + s1;
        s1 = sanitize (nb1 * x - na1 * y + s2);
        s2 = sanitize (nb2 * x - na2 * y);
        return y;
    }

private:
    float sampleRate = 44100.0f;
    float nb0 = 1, nb1 = 0, nb2 = 0, na1 = 0, na2 = 0, s1 = 0, s2 = 0;
};

/** First-order allpass with coefficient computed from a break frequency (for phasers). */
class FirstOrderAllpass
{
public:
    void reset() noexcept { z = 0.0f; }
    static float coefficientFor (float hz, float sampleRate) noexcept
    {
        const float t = std::tan (kPi * clamp (hz, 10.0f, 0.49f * sampleRate) / sampleRate);
        return (t - 1.0f) / (t + 1.0f);
    }
    float process (float x, float a) noexcept
    {
        const float y = a * x + z;
        z = sanitize (x - a * y);
        return y;
    }

private:
    float z = 0.0f;
};

/** DC blocking high-pass (~10 Hz). */
class DCBlocker
{
public:
    void prepare (double sr) noexcept { R = float (std::exp (-kTwoPi * 10.0 / sr)); }
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = sanitize (y);
        return y1;
    }

private:
    float R = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

/** Gentle spectral tilt around ~800 Hz: tone in [-1, 1], -1 darker, +1 brighter (about +-6 dB at the extremes). */
class TiltEQ
{
public:
    void prepare (double sr) noexcept { lp.prepare (sr); lp.setCutoff (800.0f); }
    void reset() noexcept { lp.reset(); }
    void setTone (float t) noexcept
    {
        t = clamp (t, -1.0f, 1.0f);
        lowGain = dbToGain (-6.0f * t);
        highGain = dbToGain (6.0f * t);
    }
    float process (float x) noexcept
    {
        const float low = lp.lowpass (x);
        return low * lowGain + (x - low) * highGain;
    }

private:
    OnePole lp;
    float lowGain = 1.0f, highGain = 1.0f;
};
} // namespace msfx::dsp
