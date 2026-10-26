#pragma once

#include "DspMath.h"

namespace msfx::dsp
{
/** Linear ramp towards a target over a fixed time. Allocation-free. */
class LinearSmoother
{
public:
    void prepare (double sampleRate, float rampSeconds) noexcept
    {
        rampSamples = std::max (1, int (sampleRate * rampSeconds));
    }
    void reset (float value) noexcept { current = target = value; remaining = 0; step = 0.0f; }
    void setTarget (float t) noexcept
    {
        if (! (t < target || t > target)) return; // unchanged target
        target = t;
        remaining = rampSamples;
        step = (target - current) / float (remaining);
    }
    float next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0) current = target;
        }
        return current;
    }
    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return remaining > 0; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0, rampSamples = 1;
};

/** One-pole exponential smoother (good for modulation targets and gains). */
class OnePoleSmoother
{
public:
    void prepare (double sampleRate, float timeSeconds) noexcept
    {
        const double t = std::max (1.0e-4, double (timeSeconds));
        coeff = float (std::exp (-1.0 / (t * sampleRate)));
    }
    void reset (float value) noexcept { state = target = value; }
    void setTarget (float t) noexcept { target = t; }
    float next() noexcept
    {
        state = target + coeff * (state - target);
        return state;
    }
    float getCurrent() const noexcept { return state; }

private:
    float state = 0.0f, target = 0.0f, coeff = 0.0f;
};
} // namespace msfx::dsp
