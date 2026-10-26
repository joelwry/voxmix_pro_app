#pragma once

#include "../Utility/DspMath.h"
#include <cstdint>
#include <vector>

namespace msfx::dsp
{
/** Circular capture buffer addressed by absolute sample index.
    Reads outside the retained history return 0 (never out of bounds). */
class CaptureBuffer
{
public:
    void prepare (double sampleRate, float seconds)
    {
        const int needed = int (std::ceil (sampleRate * double (seconds))) + 8;
        int size = 16;
        while (size < needed) size <<= 1;
        buffer.assign (size_t (size), 0.0f);
        mask = uint64_t (size - 1);
        written = 0;
    }
    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); written = 0; }
    void push (float x) noexcept { buffer[size_t (written & mask)] = sanitize (x); ++written; }
    uint64_t getWriteIndex() const noexcept { return written; }
    uint64_t capacity() const noexcept { return mask + 1; }
    bool isAvailable (uint64_t index) const noexcept { return index < written && written - index <= mask; }
    float read (uint64_t index) const noexcept { return isAvailable (index) ? buffer[size_t (index & mask)] : 0.0f; }
    /** Fractional read with linear interpolation; index measured in absolute samples. */
    float readFrac (double index) const noexcept
    {
        if (index < 0.0) return 0.0f;
        const auto i = uint64_t (index);
        const float f = float (index - double (i));
        return read (i) + f * (read (i + 1) - read (i));
    }

private:
    std::vector<float> buffer;
    uint64_t mask = 0, written = 0;
};

/** Genuine reverse playback: two Hann-windowed read heads, offset by half a chunk,
    each play the most recent `chunk` samples backwards. Output is continuous and
    click-free; it is inherently delayed by up to two chunk lengths. */
class ReverseDelay
{
public:
    void prepare (double sampleRate, float maxChunkSeconds)
    {
        maxChunk = int (sampleRate * double (maxChunkSeconds));
        buffer.prepare (sampleRate, maxChunkSeconds * 2.0f + 0.05f);
        reset();
    }
    void reset()
    {
        buffer.reset();
        heads[0] = { 0, chunk };
        heads[1] = { chunk / 2, chunk };
        started = false;
    }
    void setChunkSamples (int n) noexcept { chunk = clamp (n, 64, maxChunk); }

    float process (float x) noexcept
    {
        buffer.push (x);
        const uint64_t now = buffer.getWriteIndex(); // index of next sample to be written
        if (! started)
        {
            heads[0] = { 0, chunk };
            heads[1] = { chunk / 2, chunk };
            started = true;
        }
        float out = 0.0f;
        for (auto& h : heads)
        {
            // Output x[t0 - 1 - c] at time t0 + c -> delay of 2c + 1 behind "now - 1".
            const uint64_t delay = uint64_t (2 * h.counter + 1);
            const float w = std::sin (kPi * float (h.counter) / float (h.length));
            if (now >= delay + 1) out += buffer.read (now - 1 - delay) * w * w;
            if (++h.counter >= h.length)
            {
                h.counter = 0;
                h.length = chunk; // latch new chunk size at grain boundaries
            }
        }
        return out;
    }

private:
    struct Head { int counter = 0, length = 4096; };
    CaptureBuffer buffer;
    Head heads[2];
    int chunk = 4096, maxChunk = 4096;
    bool started = false;
};

/** Buffer-repeat engine for stutters and glitches.
    While engaged it loops a captured slice (forward or reversed, optionally at a
    different playback speed) with short fades at slice edges; engaging and
    releasing crossfade against the live input. */
class SliceRepeater
{
public:
    void prepare (double sr, float maxSeconds = 4.0f)
    {
        sampleRate = float (sr);
        capture.prepare (sr, maxSeconds);
        fadeSamples = std::max (8.0f, 0.003f * sampleRate);
        xfadeStep = 1.0f / std::max (8.0f, 0.004f * sampleRate);
        reset();
    }
    void reset()
    {
        capture.reset();
        engaged = false;
        mixToRepeat = 0.0f;
        pos = 0.0;
    }

    /** Begin looping the most recent `sliceSamples` of input. `speed` changes the
        playback rate of the loop (a deliberate tape-style varispeed glitch), and
        `gateFraction` silences the tail of each repeat for a gated stutter. */
    void engage (int sliceSamples, bool reversed, float speed = 1.0f, float gateFraction = 1.0f) noexcept
    {
        slice = clamp (float (sliceSamples), 32.0f, float (capture.capacity() / 2));
        const uint64_t now = capture.getWriteIndex();
        sliceStart = now > uint64_t (slice) ? double (now - uint64_t (slice)) : 0.0;
        reverse = reversed;
        rate = clamp (speed, 0.25f, 4.0f);
        gate = clamp (gateFraction, 0.1f, 1.0f);
        pos = 0.0;
        engaged = true;
    }
    void release() noexcept { engaged = false; }
    bool isEngaged() const noexcept { return engaged; }

    float process (float x) noexcept
    {
        capture.push (x);
        mixToRepeat = clamp (mixToRepeat + (engaged ? xfadeStep : -xfadeStep), 0.0f, 1.0f);
        if (mixToRepeat <= 0.0f) return x;

        // Loop the slice; length in source samples is `slice`, traversed at `rate`.
        const double sliceLen = double (slice);
        const double gatedLen = sliceLen * double (gate);
        float rep = 0.0f;
        if (pos < gatedLen)
        {
            const double srcOffset = reverse ? (sliceLen - 1.0 - pos) : pos;
            rep = capture.readFrac (sliceStart + srcOffset);
            const float edge = float (std::min (pos, gatedLen - pos));
            rep *= clamp (edge / fadeSamples, 0.0f, 1.0f);
        }
        pos += double (rate);
        if (pos >= sliceLen) pos -= sliceLen;
        return lerp (x, rep, mixToRepeat);
    }

private:
    CaptureBuffer capture;
    float sampleRate = 44100.0f, slice = 4096.0f, fadeSamples = 128.0f, xfadeStep = 0.01f;
    float mixToRepeat = 0.0f, rate = 1.0f, gate = 1.0f;
    double sliceStart = 0.0, pos = 0.0;
    bool engaged = false, reverse = false;
};
} // namespace msfx::dsp
