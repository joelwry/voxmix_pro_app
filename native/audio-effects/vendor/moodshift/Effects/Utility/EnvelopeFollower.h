#pragma once

#include "DspMath.h"

namespace msfx::dsp
{
/** Peak envelope follower with independent attack and release times. */
class EnvelopeFollower
{
public:
    void prepare (double sr) noexcept { sampleRate = sr; setTimes (attackMs, releaseMs); }
    void setTimes (float attackMilliseconds, float releaseMilliseconds) noexcept
    {
        attackMs = std::max (0.01f, attackMilliseconds);
        releaseMs = std::max (0.01f, releaseMilliseconds);
        attCoeff = float (std::exp (-1.0 / (0.001 * attackMs * sampleRate)));
        relCoeff = float (std::exp (-1.0 / (0.001 * releaseMs * sampleRate)));
    }
    void reset() noexcept { env = 0.0f; }
    float process (float x) noexcept
    {
        const float a = std::abs (x);
        const float c = a > env ? attCoeff : relCoeff;
        env = a + c * (env - a);
        env = sanitize (env);
        return env;
    }
    float get() const noexcept { return env; }

private:
    double sampleRate = 44100.0;
    float attackMs = 5.0f, releaseMs = 120.0f;
    float attCoeff = 0.0f, relCoeff = 0.0f, env = 0.0f;
};
} // namespace msfx::dsp
