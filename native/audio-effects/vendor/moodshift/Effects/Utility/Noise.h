#pragma once

#include "DspMath.h"

namespace msfx::dsp
{
/** Deterministic noise sources: white, pink (Paul Kellet economy filter) and vinyl-style crackle. */
class NoiseGenerator
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = float (sr);
        crackleDecay = float (std::exp (-1.0 / (0.00025 * sr))); // ~0.25 ms ticks
    }
    void reset (uint32_t seed) noexcept
    {
        rng.setSeed (seed);
        b0 = b1 = b2 = 0.0f;
        crackleEnv = 0.0f;
        crackleSign = 1.0f;
    }
    float white() noexcept { return rng.nextBipolar(); }
    float pink() noexcept
    {
        const float w = rng.nextBipolar();
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.2f;
    }
    /** Sparse decaying clicks; density = expected clicks per second. */
    float crackle (float density) noexcept
    {
        const float p = density / sampleRate;
        if (rng.nextFloat() < p)
        {
            crackleEnv = 0.3f + 0.7f * rng.nextFloat();
            crackleSign = rng.nextFloat() < 0.5f ? -1.0f : 1.0f;
        }
        const float out = crackleEnv * crackleSign * (0.6f + 0.4f * rng.nextBipolar());
        crackleEnv *= crackleDecay;
        return out;
    }

private:
    float sampleRate = 44100.0f;
    Random rng;
    float b0 = 0, b1 = 0, b2 = 0;
    float crackleEnv = 0, crackleSign = 1, crackleDecay = 0.9f;
};
} // namespace msfx::dsp
