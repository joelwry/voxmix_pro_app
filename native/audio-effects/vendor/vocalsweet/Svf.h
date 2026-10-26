#pragma once
// Topology-preserving-transform state-variable filter (A. Simper / Cytomic, "SvfLinearTrapOptimised2").
// Chosen because it stays stable and click-free while its coefficients are modulated,
// which is essential for macro-controlled EQ. Framework independent (no JUCE).

#include <algorithm>
#include <cmath>
#include <complex>

namespace vs::dsp
{
constexpr double kPi = 3.14159265358979323846;

enum class SvfType
{
    HighPass,
    LowPass,
    BandPass,   // constant 0 dB peak gain
    Bell,
    LowShelf,
    HighShelf
};

struct SvfCoeffs
{
    double g = 0.0, k = 1.0;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
};

inline SvfCoeffs designSvf (SvfType type, double sampleRate, double freq, double q, double gainDb = 0.0)
{
    SvfCoeffs c;
    freq = std::clamp (freq, 10.0, 0.48 * sampleRate);
    q = std::max (q, 0.05);
    const double A = std::pow (10.0, gainDb / 40.0);
    double g = std::tan (kPi * freq / sampleRate);
    double k = 1.0 / q;
    double m0 = 0, m1 = 0, m2 = 0;

    switch (type)
    {
        case SvfType::HighPass:  m0 = 1.0; m1 = -k; m2 = -1.0; break;
        case SvfType::LowPass:   m0 = 0.0; m1 = 0.0; m2 = 1.0; break;
        case SvfType::BandPass:  m0 = 0.0; m1 = k;   m2 = 0.0; break;
        case SvfType::Bell:      k = 1.0 / (q * A); m0 = 1.0; m1 = k * (A * A - 1.0); m2 = 0.0; break;
        case SvfType::LowShelf:  g = g / std::sqrt (A); m0 = 1.0; m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
        case SvfType::HighShelf: g = g * std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
    }

    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    c.g = g; c.k = k;
    c.a1 = (float) a1; c.a2 = (float) a2; c.a3 = (float) a3;
    c.m0 = (float) m0; c.m1 = (float) m1; c.m2 = (float) m2;
    return c;
}

/** Exact complex response of the discrete filter at frequency f (the trapezoidal SVF is the
    bilinear transform of the analog prototype, so H(e^jw) = H_analog(j*tan(w/2)/g)). */
inline std::complex<double> svfResponse (const SvfCoeffs& c, double sampleRate, double f)
{
    const double w = std::tan (kPi * std::min (f, 0.4999 * sampleRate) / sampleRate) / c.g;
    const std::complex<double> s (0.0, w);
    const std::complex<double> den = s * s + c.k * s + 1.0;
    const std::complex<double> bp = s / den;
    const std::complex<double> lp = 1.0 / den;
    return (double) c.m0 + (double) c.m1 * bp + (double) c.m2 * lp;
}

inline double svfMagnitudeDb (const SvfCoeffs& c, double sampleRate, double f)
{
    return 20.0 * std::log10 (std::max (std::abs (svfResponse (c, sampleRate, f)), 1.0e-9));
}

struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;

    inline float process (const SvfCoeffs& c, float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }
    bool isFinite() const noexcept { return std::isfinite (ic1) && std::isfinite (ic2); }
};

/** Parameter smoother: one-pole (exponential) by default, or a linear ramp of fixed duration
    (used for on/off crossfades so they finish exactly, e.g. bit-exact bypass after 20 ms). */
struct Smoother
{
    float current = 0.0f, target = 0.0f, coeff = 0.0f, step = 0.0f;
    bool linear = false;

    void prepare (double rate, double timeMs) noexcept
    {
        coeff = (float) std::exp (-1.0 / (std::max (timeMs, 0.01) * 0.001 * rate));
        linear = false;
    }
    void prepareLinear (double rate, double rampMs) noexcept
    {
        step = (float) (1.0 / std::max (1.0, rampMs * 0.001 * rate));   // full 0..1 swing per ramp
        linear = true;
    }
    void snap (float v) noexcept { current = target = v; }
    void setTarget (float v) noexcept { target = v; }
    inline float next() noexcept
    {
        if (linear)
        {
            if (current < target) current = std::min (target, current + step);
            else if (current > target) current = std::max (target, current - step);
            return current;
        }
        current = target + coeff * (current - target);
        if (std::abs (current - target) < 1.0e-6f) current = target;
        return current;
    }
    /** Advance by n samples at once (used for block-rate coefficient smoothing). */
    inline float skip (int n) noexcept
    {
        current = target + std::pow (coeff, (float) n) * (current - target);
        if (std::abs (current - target) < 1.0e-6f) current = target;
        return current;
    }
};

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

} // namespace vs::dsp
