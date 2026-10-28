#pragma once
// Small real-time helpers used by the engine's audio callback (audio thread only, no allocation
// after construction).

#include <algorithm>
#include <cmath>
#include <vector>

namespace vox
{
/** Jitter buffer for live input monitoring. Input arrives in uneven chunks (0 frames in one
    callback, twice as many in the next); the output side needs exactly one block per callback.
    The buffer holds a small cushion so playback of the monitored voice never cuts out, and
    drops the excess if the cushion grows (keeps monitoring latency bounded). */
class MonitorFifo
{
public:
    explicit MonitorFifo (int capacity = 16384) : buf ((size_t) capacity, 0.0f) {}

    void reset() noexcept { fill = 0; readPos = 0; primed = false; }

    void push (const float* x, int n) noexcept
    {
        const int cap = (int) buf.size();
        for (int i = 0; i < n; ++i)
        {
            if (fill >= cap) { readPos = (readPos + 1) % cap; --fill; }   // overflow: drop oldest
            buf[(size_t) ((readPos + fill) % cap)] = x[i];
            ++fill;
        }
    }

    void pull (float* out, int n) noexcept
    {
        const int cap = (int) buf.size();
        const int cushion = std::max (256, n);   // ~5 ms at 48 kHz
        if (! primed && fill >= n + cushion) primed = true;
        if (primed && fill > n + 4 * cushion)
        {
            const int drop = fill - (n + cushion);   // latency crept up: catch up
            readPos = (readPos + drop) % cap;
            fill -= drop;
        }
        int i = 0;
        if (primed)
        {
            const int take = std::min (n, fill);
            for (; i < take; ++i) out[i] = buf[(size_t) ((readPos + i) % cap)];
            readPos = (readPos + take) % cap;
            fill -= take;
            if (take < n) primed = false;   // underflow: rebuild the cushion
        }
        for (; i < n; ++i) out[i] = 0.0f;
    }

    int available() const noexcept { return fill; }

private:
    std::vector<float> buf;
    int fill = 0, readPos = 0;
    bool primed = false;
};

/** 2nd-order Butterworth high-pass (RBJ biquad). */
struct HighPass
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void design (double sampleRate, double freq)
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double cs = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 + cs) / 2.0 / a0;
        b1 = -(1.0 + cs) / a0;
        b2 = b0;
        a1 = -2.0 * cs / a0;
        a2 = (1.0 - alpha) / a0;
        z1 = z2 = 0;
    }
    inline float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

} // namespace vox
