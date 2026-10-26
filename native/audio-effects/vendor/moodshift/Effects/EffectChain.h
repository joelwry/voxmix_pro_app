#pragma once

#include "EffectRegistry.h"
#include "Utility/Smoothing.h"
#include <atomic>
#include <memory>
#include <vector>

namespace msfx
{
/** Complete set of target values for the engine (normally read from host parameters). */
struct EngineParams
{
    int effectIndex = 0;
    EffectParams fx;
    float mix = 0.6f;          // 0..1
    float inputGainDb = 0.0f;
    float outputGainDb = 0.0f;
    bool bypass = false;
};

/** Real-time processing chain.

    - All 40 effects are instantiated and prepared in prepare(), never on the audio thread.
    - Effect changes crossfade (equal power, 40 ms) between the outgoing and incoming
      effect; a change requested mid-crossfade is applied when the current one ends.
    - Continuous parameters are smoothed at control rate (32-sample slices); gains,
      mix and bypass are smoothed per sample.
    - Every slice is checked for NaN/Inf; a misbehaving effect is reset and muted.
    - A zero-latency peak limiter followed by a soft ceiling protects the output.
    - Bypass crossfades to the untouched input (no limiter, no gain) in 25 ms. */
class EffectChain
{
public:
    static constexpr int kSliceSize = 32;

    EffectChain();
    ~EffectChain();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    bool isPrepared() const noexcept { return prepared; }

    /** Sets new targets; call once per host block before process(). Real-time safe. */
    void setParameters (const EngineParams& p) noexcept { target = p; }

    /** Processes a stereo buffer in place. */
    void process (float* left, float* right, int numSamples, const TempoInfo& hostTempo) noexcept;

    int getActiveEffect() const noexcept { return current; }
    bool isCrossfading() const noexcept { return previous >= 0; }
    double getTailSeconds() const;
    /** Number of slices in which a non-finite value was caught (diagnostics/tests). */
    int getNonFiniteEvents() const noexcept { return nonFiniteEvents.load(); }
    /** Current safety-limiter gain reduction in dB (<= 0). */
    float getLimiterReductionDb() const noexcept { return limiterDb.load(); }

private:
    void startSwitchIfNeeded() noexcept;
    void smoothControls() noexcept;
    bool runEffect (int index, float* l, float* r, int n, const ProcessContext& ctx) noexcept;

    std::vector<std::unique_ptr<EffectProcessor>> effects;
    std::vector<float> trims;
    std::vector<MixLaw> laws;

    double sampleRate = 44100.0;
    bool prepared = false;

    EngineParams target, smoothed;
    EffectParams prevParams;
    int current = 0, previous = -1;
    int xfadePos = 0, xfadeLen = 1;
    int slicePhase = 0;

    // per-sample smoothers
    dsp::LinearSmoother mixS, inGainS, outGainS, bypassS, trimCurS, trimPrevS;
    bool effectSleeping = false;

    // free-running transport used when the host is stopped
    double freePpq = 0.0;

    // limiter
    float limGain = 1.0f, limRelease = 0.999f;

    std::vector<float> dryL, dryR, inL, inR, aL, aR, bL, bR;

    std::atomic<int> nonFiniteEvents { 0 };
    std::atomic<float> limiterDb { 0.0f };
    float controlCoeff = 0.9f;
};
} // namespace msfx
