#pragma once

#include "../Delays/DelayLine.h"
#include <array>

namespace msfx::dsp
{
/** Time-domain granular pitch shifter with waveform-similarity grain alignment
    (a WSOLA-style refinement of the classic rotating-tap delay shifter).

    Two read taps traverse a delay line at speed `ratio`, each faded with a
    Hann window over its grain, offset by half a grain so the windows sum to
    one. When a tap restarts, its new read position is chosen within a small
    search region by maximising cross-correlation with the signal the other
    tap is currently playing. This keeps overlapping grains phase-coherent,
    which removes most of the cancellation / sideband artefacts of the plain
    rotating-tap method on voiced, monophonic material. Polyphonic material
    is handled without pitch detection, with mild granular smearing.

    Pitch is changed independently of duration (genuine pitch shifting, not
    varispeed playback). The wet output is delayed by roughly half a grain. */
class GranularPitchShifter
{
public:
    void prepare (double sr, float maxWindowMs = 120.0f)
    {
        sampleRate = float (sr);
        searchRange = std::max (16, int (0.006f * sampleRate));
        corrLength = std::max (16, int (0.006f * sampleRate));
        maxWindow = maxWindowMs * 0.001f * sampleRate;
        line.prepare (sr, (maxWindowMs * 4.0f + 30.0f) * 0.001f);
        setWindowMs (60.0f);
        reset();
    }
    void reset()
    {
        line.reset();
        taps[0] = { window * 0.5f, 0.0f, window };
        taps[1] = { window * 0.5f, window * 0.5f, window };
    }
    void setWindowMs (float ms) noexcept { window = clamp (ms * 0.001f * sampleRate, 64.0f, maxWindow); }

    /** ratio: frequency ratio (2 = octave up, 0.5 = octave down), 0.25 .. 4. */
    float process (float x, float ratio) noexcept
    {
        line.push (x);
        ratio = clamp (ratio, 0.25f, 4.0f);
        const float drift = 1.0f - ratio; // delay change per output sample
        float out = 0.0f, wsum = 0.0f;
        for (int t = 0; t < 2; ++t)
        {
            Tap& tap = taps[t];
            if (tap.age >= tap.length)
                restart (tap, taps[1 - t], ratio);
            const float w = std::sin (kPi * tap.age / tap.length);
            const float w2 = w * w;
            out += line.readHermite (tap.delay) * w2;
            wsum += w2;
            tap.delay = clamp (tap.delay + drift, 2.0f, line.getMaxDelaySamples() - 4.0f);
            tap.age += 1.0f;
        }
        // Windows sum to 1 at a constant ratio; normalising keeps the level steady
        // while the ratio is being modulated.
        return out / std::max (wsum, 0.5f);
    }

private:
    struct Tap { float delay, age, length; };

    void restart (Tap& tap, const Tap& other, float ratio) noexcept
    {
        const float absDrift = std::abs (1.0f - ratio);
        // Grain length so that a grain drifts by at most one window; bounded for ratios near 1.
        const float length = clamp (absDrift > 1.0e-4f ? window / absDrift : 4.0f * window, window * 0.5f, window * 4.0f);
        const float travel = absDrift * length;
        float lo, hi;
        if (ratio > 1.0f) { lo = 2.0f + travel; hi = lo + float (searchRange); }
        else if (ratio < 1.0f) { lo = 2.0f; hi = lo + float (searchRange); }
        else { lo = window * 0.5f; hi = lo + float (searchRange); }
        hi = std::min (hi, line.getMaxDelaySamples() - travel - float (corrLength) - 8.0f);
        lo = std::min (lo, hi);

        // Coarse search (step 2), correlating every second sample, then refine.
        const float ref = other.delay;
        float best = lo, bestScore = -1.0e30f;
        for (float d = lo; d <= hi; d += 2.0f)
        {
            const float s = score (ref, d);
            if (s > bestScore) { bestScore = s; best = d; }
        }
        for (float d : { best - 1.0f, best + 1.0f })
            if (d >= lo && d <= hi)
            {
                const float s = score (ref, d);
                if (s > bestScore) { bestScore = s; best = d; }
            }
        tap.delay = best;
        tap.age = 0.0f;
        tap.length = length;
    }

    float score (float refDelay, float candDelay) const noexcept
    {
        float xy = 0.0f, yy = 1.0e-9f;
        const int ri = int (refDelay), ci = int (candDelay);
        for (int k = 0; k < corrLength; k += 2)
        {
            const float a = line.readInt (ri + k), b = line.readInt (ci + k);
            xy += a * b;
            yy += b * b;
        }
        return xy / std::sqrt (yy);
    }

    DelayLine line;
    float sampleRate = 44100.0f, window = 2048.0f, maxWindow = 4096.0f;
    int searchRange = 256, corrLength = 256;
    Tap taps[2] { { 1024.0f, 0.0f, 2048.0f }, { 1024.0f, 1024.0f, 2048.0f } };
};
} // namespace msfx::dsp
