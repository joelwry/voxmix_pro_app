#pragma once

#include "../Utility/DspMath.h"
#include <vector>

namespace msfx::dsp
{
/** Single-channel circular delay line. Memory is allocated only in prepare().
    Reads are always bounds-safe: delays are clamped to the allocated capacity. */
class DelayLine
{
public:
    void prepare (double sampleRate, float maxDelaySeconds)
    {
        const int needed = int (std::ceil (sampleRate * double (maxDelaySeconds))) + 8;
        int size = 16;
        while (size < needed) size <<= 1;
        buffer.assign (size_t (size), 0.0f);
        mask = size - 1;
        maxDelay = float (size - 4);
        writePos = 0;
    }
    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }
    bool isPrepared() const noexcept { return ! buffer.empty(); }
    float getMaxDelaySamples() const noexcept { return maxDelay; }
    int getSize() const noexcept { return mask + 1; }

    void push (float x) noexcept
    {
        buffer[size_t (writePos)] = sanitize (x);
        writePos = (writePos + 1) & mask;
    }

    /** Integer-sample read; delay 0 is the most recently pushed sample. */
    float readInt (int delay) const noexcept
    {
        delay = clamp (delay, 0, mask - 1);
        return buffer[size_t ((writePos - 1 - delay) & mask)];
    }

    /** Linear interpolated read (cheap, used for heavily modulated taps). */
    float readLinear (float delay) const noexcept
    {
        delay = clamp (delay, 0.0f, maxDelay);
        const int i = int (delay);
        const float f = delay - float (i);
        const float a = buffer[size_t ((writePos - 1 - i) & mask)];
        const float b = buffer[size_t ((writePos - 2 - i) & mask)];
        return a + f * (b - a);
    }

    /** 4-point Hermite interpolated read (higher quality for pitch shifting). */
    float readHermite (float delay) const noexcept
    {
        delay = clamp (delay, 1.0f, maxDelay);
        const int i = int (delay);
        const float f = delay - float (i);
        const float xm1 = buffer[size_t ((writePos - i) & mask)];
        const float x0 = buffer[size_t ((writePos - 1 - i) & mask)];
        const float x1 = buffer[size_t ((writePos - 2 - i) & mask)];
        const float x2 = buffer[size_t ((writePos - 3 - i) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buffer;
    int mask = 0, writePos = 0;
    float maxDelay = 0.0f;
};

/** Schroeder allpass built on a delay line (used for diffusion). */
class AllpassDelay
{
public:
    void prepare (double sr, float maxSeconds) { line.prepare (sr, maxSeconds); }
    void reset() noexcept { line.reset(); }
    float process (float x, float delaySamples, float g) noexcept
    {
        const float d = line.readLinear (delaySamples - 1.0f);
        const float v = x + g * d;
        line.push (v);
        return d - g * v;
    }

private:
    DelayLine line;
};
} // namespace msfx::dsp
