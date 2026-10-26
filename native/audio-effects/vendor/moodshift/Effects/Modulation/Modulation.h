#pragma once

#include "../Delays/DelayLine.h"
#include "../Filters/Filters.h"
#include "../Utility/LFO.h"
#include <array>

namespace msfx::dsp
{
//==============================================================================
/** Multi-voice stereo chorus / ensemble. Up to 8 voices, each a modulated delay
    tap with its own LFO phase, rate offset and pan position. */
class Chorus
{
public:
    static constexpr int maxVoices = 8;

    void prepare (double sr)
    {
        sampleRate = float (sr);
        for (auto& l : lines) l.prepare (sr, 0.12f);
        for (int v = 0; v < maxVoices; ++v)
        {
            lfos[size_t (v)].prepare (sr);
            lfos[size_t (v)].setSeed (0xC0FFEEu + uint32_t (v) * 7919u);
        }
        reset();
    }
    void reset()
    {
        for (auto& l : lines) l.reset();
        for (int v = 0; v < maxVoices; ++v)
            lfos[size_t (v)].reset (float (v) / float (maxVoices) + 0.13f * float (v % 2));
    }

    /** voices 1..8; rate Hz; depthMs modulation depth; baseMs centre delay; spread 0..1 stereo spread. */
    void setParams (int voices, float rateHz, float depthMs, float baseMs, float spread, LfoShape shape = LfoShape::sine)
    {
        numVoices = clamp (voices, 1, maxVoices);
        depth = std::max (0.0f, depthMs) * 0.001f * sampleRate;
        base = std::max (0.5f, baseMs) * 0.001f * sampleRate;
        for (int v = 0; v < maxVoices; ++v)
        {
            // Slightly different rates per voice give a natural, non-periodic ensemble.
            const float spreadRate = 1.0f + 0.11f * float (v) - 0.05f * float (v % 3);
            lfos[size_t (v)].setFrequency (rateHz * spreadRate);
            lfos[size_t (v)].setShape (shape);
            const float p = numVoices == 1 ? 0.0f : (float (v) / float (numVoices - 1)) * 2.0f - 1.0f;
            panGains (p * clamp (spread, 0.0f, 1.0f), panL[size_t (v)], panR[size_t (v)]);
        }
    }

    /** Processes one stereo frame; returns wet only. */
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        lines[0].push (inL);
        lines[1].push (inR);
        float l = 0.0f, r = 0.0f;
        for (int v = 0; v < numVoices; ++v)
        {
            const float m = lfos[size_t (v)].next();
            const float d = base * (1.0f + 0.07f * float (v)) + depth * (0.5f + 0.5f * m);
            // Odd voices read from the right channel to keep stereo information.
            const float s = lines[size_t (v & 1)].readLinear (d);
            l += s * panL[size_t (v)];
            r += s * panR[size_t (v)];
        }
        const float norm = 1.0f / std::sqrt (float (numVoices));
        outL = l * norm;
        outR = r * norm;
    }

private:
    float sampleRate = 44100.0f, depth = 0.0f, base = 400.0f;
    int numVoices = 2;
    std::array<DelayLine, 2> lines;
    std::array<LFO, maxVoices> lfos;
    std::array<float, maxVoices> panL {}, panR {};
};

//==============================================================================
/** Multi-stage stereo phaser (first-order allpass cascade with feedback). */
class Phaser
{
public:
    static constexpr int maxStages = 12;

    void prepare (double sr)
    {
        sampleRate = float (sr);
        lfo.prepare (sr);
        reset();
    }
    void reset()
    {
        for (auto& ch : stages) for (auto& s : ch) s.reset();
        fb = { 0.0f, 0.0f };
        lfo.reset (0.0f);
    }
    void setParams (int numStages, float rateHz, float depth01, float feedback, float minHz, float maxHz, float stereoPhase01)
    {
        nStages = clamp (numStages, 2, maxStages);
        lfo.setFrequency (rateHz);
        depth = clamp (depth01, 0.0f, 1.0f);
        feedbackAmt = clamp (feedback, -0.95f, 0.95f);
        lo = std::max (20.0f, minHz);
        hi = clamp (maxHz, lo + 1.0f, 0.45f * sampleRate);
        stereoOffset = stereoPhase01;
    }
    void setLfoPhase (float p) noexcept { lfo.setPhase (p); }

    void process (float& l, float& r) noexcept
    {
        const float ph = lfo.getPhase();
        lfo.next();
        processAt (l, r, ph);
    }
    /** Processes with an externally supplied LFO phase (e.g. tempo locked). */
    void processAt (float& l, float& r, float phase) noexcept
    {
        processChannel (0, l, phase);
        processChannel (1, r, phase + stereoOffset);
    }

private:
    void processChannel (int ch, float& x, float phase) noexcept
    {
        const float m = 0.5f + 0.5f * std::sin (kTwoPi * phase); // 0..1
        const float centre = std::sqrt (lo * hi);
        const float sweep = std::log2 (hi / lo) * 0.5f;
        const float f = centre * std::exp2 ((m * 2.0f - 1.0f) * sweep * depth);
        const float a = FirstOrderAllpass::coefficientFor (f, sampleRate);
        float y = x + fastTanh (fb[size_t (ch)]) * feedbackAmt;
        for (int s = 0; s < nStages; ++s)
            y = stages[size_t (ch)][size_t (s)].process (y, a);
        fb[size_t (ch)] = y;
        x = y;
    }

    float sampleRate = 44100.0f, depth = 1.0f, feedbackAmt = 0.0f, lo = 200.0f, hi = 4000.0f, stereoOffset = 0.25f;
    int nStages = 6;
    std::array<std::array<FirstOrderAllpass, maxStages>, 2> stages;
    std::array<float, 2> fb {};
    LFO lfo;
};

//==============================================================================
/** 90-degree phase-difference network (pair of 4th-order allpass chains in z^-2).
    Produces an analytic signal (re, im) for frequency shifting. Valid roughly
    from 0.002 to 0.998 of Nyquist with < 0.7 degree phase error. */
class HilbertTransformer
{
public:
    void reset() noexcept
    {
        for (auto& s : st) s = {};
        delayed = 0.0f;
    }
    void process (float x, float& re, float& im) noexcept
    {
        static constexpr std::array<float, 4> a1 { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static constexpr std::array<float, 4> a2 { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        float y1 = x, y2 = x;
        for (size_t i = 0; i < 4; ++i)
        {
            y1 = section (st[i], y1, a1[i] * a1[i]);
            y2 = section (st[i + 4], y2, a2[i] * a2[i]);
        }
        re = delayed;  // first path is delayed by one sample
        delayed = y1;
        im = y2;
    }

private:
    struct State { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static float section (State& s, float x, float c) noexcept
    {
        const float y = c * (x + s.y2) - s.x2;
        s.x2 = s.x1; s.x1 = x;
        s.y2 = s.y1; s.y1 = sanitize (y);
        return s.y1;
    }
    std::array<State, 8> st {};
    float delayed = 0.0f;
};

/** Single-sideband frequency shifter (genuine frequency shifting, not pitch shifting). */
class FrequencyShifter
{
public:
    void prepare (double sr) { sampleRate = float (sr); reset(); }
    void reset() { hilbert.reset(); phase = 0.0; }
    /** Process one sample, shifting all components by shiftHz (may be negative). */
    float process (float x, float shiftHz) noexcept
    {
        float re, im;
        hilbert.process (x, re, im);
        const float c = std::cos (float (kPiD * 2.0 * phase));
        const float s = std::sin (float (kPiD * 2.0 * phase));
        phase += double (shiftHz) / double (sampleRate);
        phase -= std::floor (phase);
        return re * c + im * s;
    }

private:
    HilbertTransformer hilbert;
    float sampleRate = 44100.0f;
    double phase = 0.0;
};

//==============================================================================
/** Ring modulator with sine/blend carrier; returns wet. */
class RingModulator
{
public:
    void prepare (double sr) { sampleRate = float (sr); phase = 0.0; }
    void reset() { phase = 0.0; }
    /** squareMix 0..1 blends a softened square into the sine carrier for a harsher metallic tone. */
    float process (float x, float carrierHz, float squareMix) noexcept
    {
        const float s = std::sin (float (2.0 * kPiD * phase));
        const float sq = fastTanh (s * 6.0f);
        phase += double (carrierHz) / double (sampleRate);
        phase -= std::floor (phase);
        return x * lerp (s, sq, squareMix);
    }

private:
    float sampleRate = 44100.0f;
    double phase = 0.0;
};
} // namespace msfx::dsp
