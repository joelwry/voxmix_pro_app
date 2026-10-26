#pragma once

#include "../Delays/DelayLine.h"
#include "../Filters/Filters.h"
#include "../Utility/LFO.h"
#include <array>

namespace msfx::dsp
{
/** 8-line feedback delay network reverb.
    - Pre-delay and 4-stage allpass input diffusion per channel
    - Householder feedback matrix (orthogonal, energy preserving)
    - Per-line gain derived from RT60 so the decay time is accurate and always < 1 (stable)
    - In-loop one-pole damping, modulated delay taps for a lush, chorused tail
    Returns the wet signal only. */
class FDNReverb
{
public:
    static constexpr int N = 8;

    struct Params
    {
        float decaySeconds = 2.5f;   // RT60 at low/mid frequencies
        float size = 1.0f;           // 0.3 .. 2.0 delay length scale
        float dampingHz = 6000.0f;   // in-loop lowpass cutoff
        float lowCutHz = 80.0f;      // input highpass
        float diffusion = 0.7f;      // 0..1 input allpass gain scaling
        float modDepth = 0.3f;       // 0..1
        float modRate = 0.4f;        // Hz
        float preDelayMs = 10.0f;
        float width = 1.0f;          // 0..2
    };

    void prepare (double sr)
    {
        sampleRate = float (sr);
        for (auto& l : lines) l.prepare (sr, 0.25f);
        for (auto& ch : diffusers) for (auto& d : ch) d.prepare (sr, 0.03f);
        for (auto& p : preDelay) p.prepare (sr, 0.3f);
        for (auto& f : damp) f.prepare (sr);
        for (auto& f : inputHp) f.prepare (sr);
        for (int i = 0; i < N; ++i)
        {
            lfos[size_t (i)].prepare (sr);
            lfos[size_t (i)].setSeed (0xBADA55u + uint32_t (i) * 104729u);
            lfos[size_t (i)].setShape (LfoShape::smoothRandom);
        }
        setParams (params);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.reset();
        for (auto& ch : diffusers) for (auto& d : ch) d.reset();
        for (auto& p : preDelay) p.reset();
        for (auto& f : damp) f.reset();
        for (auto& f : inputHp) f.reset();
        for (int i = 0; i < N; ++i) lfos[size_t (i)].reset (float (i) * 0.125f);
        feedback.fill (0.0f);
    }

    void setParams (const Params& p)
    {
        params = p;
        static constexpr std::array<float, N> baseMs { 29.7f, 37.1f, 41.1f, 43.7f, 53.1f, 59.3f, 67.9f, 73.1f };
        const float size = clamp (p.size, 0.25f, 2.5f);
        const float rt = clamp (p.decaySeconds, 0.1f, 60.0f);
        for (int i = 0; i < N; ++i)
        {
            lengths[size_t (i)] = std::min (baseMs[size_t (i)] * size * 0.001f * sampleRate, lines[size_t (i)].getMaxDelaySamples() - 200.0f);
            // RT60: gain per pass so that after rt seconds the level has dropped 60 dB.
            gains[size_t (i)] = std::pow (10.0f, -3.0f * lengths[size_t (i)] / (rt * sampleRate));
            damp[size_t (i)].setCutoff (p.dampingHz);
            lfos[size_t (i)].setFrequency (p.modRate * (0.8f + 0.05f * float (i)));
        }
        modSamples = clamp (p.modDepth, 0.0f, 1.0f) * 0.0025f * sampleRate; // up to 2.5 ms
        for (auto& f : inputHp) f.setCutoff (p.lowCutHz);
        diffGain = 0.45f + 0.3f * clamp (p.diffusion, 0.0f, 1.0f);
        preDelaySamples = clamp (p.preDelayMs, 0.0f, 290.0f) * 0.001f * sampleRate;
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        static constexpr std::array<float, 4> diffMs { 4.77f, 3.59f, 12.73f, 9.31f };
        float x[2] = { inL, inR };
        for (int c = 0; c < 2; ++c)
        {
            preDelay[size_t (c)].push (x[c]);
            float s = preDelaySamples > 0.5f ? preDelay[size_t (c)].readLinear (preDelaySamples) : x[c];
            s = inputHp[size_t (c)].highpass (s);
            for (int d = 0; d < 4; ++d)
                s = diffusers[size_t (c)][size_t (d)].process (s, diffMs[size_t (d)] * (1.0f + 0.13f * float (c)) * 0.001f * sampleRate, diffGain);
            x[c] = s;
        }

        // Read delay outputs
        std::array<float, N> y {};
        for (int i = 0; i < N; ++i)
        {
            const float mod = modSamples * lfos[size_t (i)].next();
            y[size_t (i)] = lines[size_t (i)].readLinear (lengths[size_t (i)] + mod);
        }

        // Householder reflection: y - (2/N) * sum(y)
        float sum = 0.0f;
        for (auto v : y) sum += v;
        sum *= 2.0f / float (N);

        for (int i = 0; i < N; ++i)
        {
            float v = (y[size_t (i)] - sum) * gains[size_t (i)];
            v = damp[size_t (i)].lowpass (v);
            const float in = (i & 1) ? x[1] : x[0];
            const float sign = ((i >> 1) & 1) ? -1.0f : 1.0f;
            lines[size_t (i)].push (v + in * sign * 0.5f);
        }

        // Decorrelated stereo outputs
        const float l = (y[0] - y[2] + y[4] - y[6] + y[1] * 0.5f + y[5] * 0.5f) * 0.45f;
        const float r = (y[1] - y[3] + y[5] - y[7] + y[2] * 0.5f + y[6] * 0.5f) * 0.45f;
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * clamp (params.width, 0.0f, 2.0f);
        outL = mid + side;
        outR = mid - side;
    }

    const Params& getParams() const noexcept { return params; }

private:
    float sampleRate = 44100.0f;
    Params params;
    std::array<DelayLine, N> lines;
    std::array<std::array<AllpassDelay, 4>, 2> diffusers;
    std::array<DelayLine, 2> preDelay;
    std::array<OnePole, N> damp;
    std::array<OnePole, 2> inputHp;
    std::array<LFO, N> lfos;
    std::array<float, N> lengths {}, gains {};
    std::array<float, N> feedback {};
    float modSamples = 0.0f, diffGain = 0.6f, preDelaySamples = 0.0f;
};
} // namespace msfx::dsp
