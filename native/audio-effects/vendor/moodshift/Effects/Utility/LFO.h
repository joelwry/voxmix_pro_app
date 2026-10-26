#pragma once

#include "DspMath.h"

namespace msfx::dsp
{
enum class LfoShape
{
    sine,
    triangle,
    sawUp,
    sawDown,
    square,       // band-limited-ish (smoothed edges)
    smoothRandom  // deterministic smoothed sample & hold
};

/** Low-frequency oscillator with phase control for tempo locking. */
class LFO
{
public:
    void prepare (double sr) noexcept { sampleRate = float (sr); }
    void reset (float phase0 = 0.0f) noexcept
    {
        phase = phase0 - std::floor (phase0);
        rng.setSeed (seed);
        prevRand = rng.nextBipolar();
        nextRand = rng.nextBipolar();
    }
    void setSeed (uint32_t s) noexcept { seed = s; }
    void setShape (LfoShape s) noexcept { shape = s; }
    void setFrequency (float hz) noexcept { inc = clamp (hz, 0.0f, 0.5f * sampleRate) / sampleRate; }
    void setPhase (float p) noexcept { phase = p - std::floor (p); }
    float getPhase() const noexcept { return phase; }

    /** Returns a value in [-1, 1] and advances. */
    float next() noexcept
    {
        const float v = valueAt (phase);
        phase += inc;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            prevRand = nextRand;
            nextRand = rng.nextBipolar();
        }
        return v;
    }

    /** Value of the current shape at an arbitrary phase (no state change, except random uses current segment). */
    float valueAt (float p) const noexcept
    {
        p -= std::floor (p);
        switch (shape)
        {
            case LfoShape::sine:     return std::sin (kTwoPi * p);
            case LfoShape::triangle: return 1.0f - 4.0f * std::abs (p - 0.5f) ;
            case LfoShape::sawUp:    return 2.0f * p - 1.0f;
            case LfoShape::sawDown:  return 1.0f - 2.0f * p;
            case LfoShape::square:
            {
                // Square with ~4% raised-cosine edges to avoid clicks.
                const float e = 0.04f;
                if (p < e)          return -std::cos (kPi * p / e);
                if (p < 0.5f)       return 1.0f;
                if (p < 0.5f + e)   return std::cos (kPi * (p - 0.5f) / e);
                return -1.0f;
            }
            case LfoShape::smoothRandom:
            {
                const float t = 0.5f - 0.5f * std::cos (kPi * p);
                return lerp (prevRand, nextRand, t);
            }
        }
        return 0.0f;
    }

private:
    float sampleRate = 44100.0f, phase = 0.0f, inc = 0.0f;
    LfoShape shape = LfoShape::sine;
    Random rng;
    uint32_t seed = 0x1234567u;
    float prevRand = 0.0f, nextRand = 0.0f;
};
} // namespace msfx::dsp
