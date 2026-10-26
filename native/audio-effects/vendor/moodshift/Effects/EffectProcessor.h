#pragma once

#include "../Parameters/ParameterIDs.h"
#include "Utility/DspMath.h"
#include "Utility/LFO.h"
#include <cmath>

namespace msfx
{
/** Host transport information for one processing slice. */
struct TempoInfo
{
    double bpm = 120.0;
    double ppq = 0.0;        // quarter-note position at the first sample of the slice
    bool isPlaying = false;
};

/** Smoothed, de-normalised parameter values handed to effects for one slice. */
struct EffectParams
{
    float amount = 0.7f;     // 0..1
    float tone = 0.0f;       // -1..1
    float movement = 0.5f;   // 0..1 (0.5 = nominal)
    float width = 1.0f;      // 0..2
    std::array<float, kNumAdvParams> adv {};

    float get (AdvParam p) const noexcept { return adv[size_t (p)]; }
    int division() const noexcept { return dsp::clamp (int (std::lround (get (AdvParam::division))), 0, int (kDivisionBeats.size()) - 1); }
    bool sync() const noexcept { return get (AdvParam::sync) >= 0.5f; }
    int voices() const noexcept { return dsp::clamp (int (std::lround (get (AdvParam::voices))), 1, 8); }
    dsp::LfoShape lfoShape() const noexcept
    {
        switch (int (std::lround (get (AdvParam::shape))))
        {
            case 1:  return dsp::LfoShape::triangle;
            case 2:  return dsp::LfoShape::square;
            case 3:  return dsp::LfoShape::sawUp;
            case 4:  return dsp::LfoShape::sawDown;
            case 5:  return dsp::LfoShape::smoothRandom;
            default: return dsp::LfoShape::sine;
        }
    }
    float fb01() const noexcept { return get (AdvParam::feedback) * 0.01f; }

    /** Movement macro as a rate multiplier: x0.25 .. x4 around the nominal rate. */
    float movementRateScale() const noexcept { return std::exp2 ((movement - 0.5f) * 4.0f); }
    /** Movement macro as a depth multiplier: 0 .. 2. */
    float movementDepthScale() const noexcept { return movement * 2.0f; }
};

/** Everything an effect needs to process one slice. */
struct ProcessContext
{
    const EffectParams& params;
    TempoInfo tempo;
    double sampleRate;

    double beatsPerSample() const noexcept { return tempo.bpm / (60.0 * sampleRate); }
    double divisionBeats() const noexcept { return kDivisionBeats[size_t (params.division())]; }
    /** Seconds per selected note division at the current tempo. */
    double divisionSeconds() const noexcept { return divisionBeats() * 60.0 / tempo.bpm; }
    /** Modulation rate in Hz: tempo-synced division (scaled by Movement) or the free Rate parameter. */
    float modRateHz() const noexcept
    {
        const float base = params.sync() ? float (1.0 / divisionSeconds()) : params.get (AdvParam::rate);
        return base * params.movementRateScale();
    }
};

/** Common interface for every factory effect.
    process() works in place on a stereo pair and must produce the *wet* signal
    only; the engine handles dry/wet mixing, crossfades, gain and safety. All
    allocation happens in prepare(); process() and reset() are real-time safe. */
class EffectProcessor
{
public:
    virtual ~EffectProcessor() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void process (float* left, float* right, int numSamples, const ProcessContext& ctx) = 0;
    /** How long the effect may keep ringing after input stops. */
    virtual double getTailSeconds() const { return 0.0; }
};
} // namespace msfx
