#pragma once

#include "../Utility/DspMath.h"
#include <array>

namespace msfx::dsp
{
/** Waveshaper with several characters. Drive in dB; output is roughly level compensated.

    The smooth shapes use first-order antiderivative anti-aliasing (ADAA):
    y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]) where F is the antiderivative of
    the shaping function. This strongly reduces aliasing at only half a sample of
    delay, so the saturated wet path stays phase-aligned with the dry signal. The
    "fold" shape is deliberately left un-antialiased (digital character). */
class Saturator
{
public:
    enum class Type { soft, tape, tube, fold, hard };

    void reset() noexcept { prevX = 0.0; prevF = antiderivative (0.0); }
    void set (Type t, float driveDb, float asymmetry = 0.0f) noexcept
    {
        const bool changed = t != type || asymmetry != bias;
        type = t;
        drive = dbToGain (clamp (driveDb, 0.0f, 48.0f));
        bias = clamp (asymmetry, -0.5f, 0.5f);
        tanhBias = std::tanh (double (bias));
        makeup = 1.0f / std::pow (drive, 0.62f);
        if (changed) prevF = antiderivative (prevX);
    }
    float process (float x) noexcept
    {
        const double s = double (clamp (x * drive, -60.0f, 60.0f));
        if (type == Type::fold) return shape (s) * makeup;
        const double F = antiderivative (s);
        const double dx = s - prevX;
        double y;
        if (std::abs (dx) > 1.0e-5)
            y = (F - prevF) / dx;
        else
            y = double (shape (0.5 * (s + prevX)));
        prevX = s;
        prevF = F;
        return sanitize (float (y)) * makeup;
    }

private:
    static double logCosh (double x) noexcept
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
    }
    float shape (double s) const noexcept
    {
        switch (type)
        {
            case Type::soft: return float (std::tanh (s));
            case Type::tape: return float (std::tanh (s + bias) - tanhBias);
            case Type::tube: return float (s >= 0.0 ? std::tanh (s) : (0.8 / 0.6) * std::tanh (0.6 * s));
            case Type::fold:
            {
                const double t = s * 0.25 + 0.25;
                const double frac = t - std::floor (t);
                return float (4.0 * std::abs (frac - 0.5) - 1.0);
            }
            case Type::hard: return float (clamp (s, -1.0, 1.0));
        }
        return float (s);
    }
    double antiderivative (double s) const noexcept
    {
        switch (type)
        {
            case Type::soft: return logCosh (s);
            case Type::tape: return logCosh (s + bias) - s * tanhBias;
            case Type::tube: return s >= 0.0 ? logCosh (s) : (0.8 / 0.36) * logCosh (0.6 * s);
            case Type::hard: return std::abs (s) <= 1.0 ? 0.5 * s * s : std::abs (s) - 0.5;
            case Type::fold: return 0.0;
        }
        return 0.0;
    }

    Type type = Type::soft;
    float drive = 1.0f, bias = 0.0f, makeup = 1.0f;
    double tanhBias = 0.0, prevX = 0.0, prevF = 0.0;
};

/** Bit-depth and sample-rate reduction with fractional hold rate. */
class BitCrusher
{
public:
    void prepare (double sr) noexcept { sampleRate = float (sr); }
    void reset() noexcept { phase = 1.0f; held = 0.0f; }
    /** bits 1..24 (fractional allowed), targetRate in Hz. */
    void set (float bits, float targetRateHz) noexcept
    {
        levels = std::exp2 (clamp (bits, 1.0f, 24.0f) - 1.0f);
        step = clamp (targetRateHz, 100.0f, sampleRate) / sampleRate;
    }
    float process (float x) noexcept
    {
        phase += step;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            held = std::round (clamp (x, -1.0f, 1.0f) * levels) / levels;
        }
        return held;
    }

private:
    float sampleRate = 44100.0f, levels = 128.0f, step = 1.0f, phase = 1.0f, held = 0.0f;
};
} // namespace msfx::dsp
