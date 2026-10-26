#pragma once

#include "../EffectRegistry.h"
#include "../Delays/DelayLine.h"
#include "../Distortion/Distortion.h"
#include "../Filters/Filters.h"
#include "../Glitch/Glitch.h"
#include "../Modulation/Modulation.h"
#include "../Pitch/FormantShifter.h"
#include "../Pitch/PitchShifter.h"
#include "../Reverb/FDNReverb.h"
#include "../Spatial/Spatial.h"
#include "../Utility/EnvelopeFollower.h"
#include "../Utility/LFO.h"
#include "../Utility/Noise.h"
#include "../Utility/Smoothing.h"

namespace msfx::library
{
using namespace dsp;

/** Convenience base: stores the sample rate and forwards to prepareFx(). */
class StereoEffect : public EffectProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize) override
    {
        sr = sampleRate;
        maxBlock = maxBlockSize;
        prepareFx();
        reset();
    }

protected:
    virtual void prepareFx() = 0;
    float srf() const noexcept { return float (sr); }
    double sr = 44100.0;
    int maxBlock = 32;
};

/** Phase source for modulation that is either free-running (Rate in Hz) or
    locked to the host's musical position (division). In sync mode the
    Movement macro selects x0.25 .. x4 in power-of-two steps so the phase stays
    locked to the bar grid; in free mode it scales the rate continuously. */
class ModClock
{
public:
    static constexpr double kGridEpsilon = 1.0e-7;

    void prepare (double sampleRate) noexcept { sr = sampleRate; }
    void reset (double p0 = 0.0) noexcept { phase = p0; }

    /** Call once per slice. cycleBeats: length of one modulation cycle in beats when synced. */
    void begin (const ProcessContext& ctx, float freeRateHz, double cycleBeats, double offset = 0.0) noexcept
    {
        sync = ctx.params.sync();
        bps = ctx.beatsPerSample();
        const float scale = ctx.params.movementRateScale();
        if (sync)
        {
            const double q = std::round (std::log2 (double (scale)));
            mult = std::exp2 (clamp (q, -2.0, 2.0)) / std::max (1.0e-3, cycleBeats);
            ppq = ctx.tempo.ppq;
            off = offset;
            i = 0;
        }
        else
        {
            inc = double (freeRateHz) * double (scale) / sr;
        }
    }
    /** Phase in [0, 1) for the next sample. */
    double next() noexcept
    {
        if (sync)
        {
            // kGridEpsilon (far below one sample) keeps grid boundaries that fall exactly
            // on a sample from flipping with floating-point rounding of the host position.
            const double p = (ppq + double (i++) * bps) * mult + off + kGridEpsilon;
            phase = p - std::floor (p);
            return phase;
        }
        const double p = phase;
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            ++freeCycles;
        }
        return p;
    }
    /** Index of the current cycle in synced mode (for position-locked random decisions). */
    int64_t cycleIndex() const noexcept
    {
        return sync ? int64_t (std::floor ((ppq + double (std::max (0, i - 1)) * bps) * mult + off + kGridEpsilon)) : freeCycles;
    }
    bool isSynced() const noexcept { return sync; }

private:
    double sr = 44100.0, phase = 0.0, inc = 0.0, ppq = 0.0, bps = 0.0, mult = 1.0, off = 0.0;
    int i = 0;
    int64_t freeCycles = 0;
    bool sync = false;
};

/** Evaluates a modulation shape at `phase`; `cycle` makes the Random shape
    deterministic and position-locked (same bar -> same value). Range -1..1. */
inline float shapeValue (LfoShape shape, double phase, int64_t cycle) noexcept
{
    const float p = float (phase - std::floor (phase));
    switch (shape)
    {
        case LfoShape::sine:     return std::sin (kTwoPi * p);
        case LfoShape::triangle: return 1.0f - 4.0f * std::abs (p - 0.5f);
        case LfoShape::sawUp:    return 2.0f * p - 1.0f;
        case LfoShape::sawDown:  return 1.0f - 2.0f * p;
        case LfoShape::square:
        {
            const float e = 0.04f;
            if (p < e)        return -std::cos (kPi * p / e);
            if (p < 0.5f)     return 1.0f;
            if (p < 0.5f + e) return std::cos (kPi * (p - 0.5f) / e);
            return -1.0f;
        }
        case LfoShape::smoothRandom:
        {
            const float a = hashToUnit (uint32_t (cycle) * 2654435761u + 17u) * 2.0f - 1.0f;
            const float b = hashToUnit (uint32_t (cycle + 1) * 2654435761u + 17u) * 2.0f - 1.0f;
            return lerp (a, b, 0.5f - 0.5f * std::cos (kPi * p));
        }
    }
    return 0.0f;
}

/** Tempo-synced or free delay time in samples (Time parameter in ms). */
inline float delayTimeSamples (const ProcessContext& ctx) noexcept
{
    const double seconds = ctx.params.sync() ? ctx.divisionSeconds() : double (ctx.params.get (AdvParam::delayTime)) * 0.001;
    return float (seconds * ctx.sampleRate);
}

inline float res01 (const ProcessContext& ctx) noexcept { return ctx.params.get (AdvParam::resonance) * 0.01f; }

/** Helper to build registry entries tersely. */
template <typename T>
std::unique_ptr<EffectProcessor> make() { return std::make_unique<T>(); }
} // namespace msfx::library
