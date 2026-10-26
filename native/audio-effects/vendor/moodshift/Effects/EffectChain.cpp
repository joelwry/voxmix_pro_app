#include "EffectChain.h"

namespace msfx
{
using namespace dsp;

namespace
{
    constexpr float kXfadeSeconds = 0.04f;
    constexpr float kLimiterThreshold = 0.944f; // -0.5 dBFS
    constexpr float kKnee = 0.95f;

    inline float ceilingClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= kKnee) return x;
        const float y = kKnee + (1.0f - kKnee) * fastTanh ((a - kKnee) / (1.0f - kKnee));
        return x < 0.0f ? -y : y;
    }

    inline void mixGains (MixLaw law, float mix, float& dryG, float& wetG) noexcept
    {
        if (law == MixLaw::linear)
        {
            dryG = 1.0f - mix;
            wetG = mix;
        }
        else
        {
            dryG = std::cos (mix * 0.5f * kPi);
            wetG = std::sin (mix * 0.5f * kPi);
        }
    }

    bool isLogParam (int i) noexcept { return kAdvSpecs[size_t (i)].skew == Skew::log; }
    bool isDiscreteParam (int i) noexcept { return kAdvSpecs[size_t (i)].kind != ParamKind::continuous; }
}

EffectChain::EffectChain()
{
    const auto& reg = effectRegistry();
    effects.reserve (reg.size());
    for (const auto& info : reg)
    {
        effects.push_back (info.create());
        trims.push_back (dbToGain (info.trimDb));
        laws.push_back (info.mixLaw);
    }
}

EffectChain::~EffectChain() = default;

void EffectChain::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    for (auto& e : effects)
        e->prepare (sampleRate, kSliceSize);

    for (auto* v : { &dryL, &dryR, &inL, &inR, &aL, &aR, &bL, &bR })
        v->assign (size_t (kSliceSize), 0.0f);

    (void) maxBlockSize; // processing is sliced, so any host block size works
    mixS.prepare (sampleRate, 0.03f);
    inGainS.prepare (sampleRate, 0.03f);
    outGainS.prepare (sampleRate, 0.03f);
    bypassS.prepare (sampleRate, 0.025f);
    trimCurS.prepare (sampleRate, 0.02f);
    trimPrevS.prepare (sampleRate, 0.02f);
    xfadeLen = std::max (1, int (kXfadeSeconds * sampleRate));
    controlCoeff = float (std::exp (-double (kSliceSize) / (0.03 * sampleRate)));
    limRelease = float (std::exp (-1.0 / (0.08 * sampleRate)));
    prepared = true;
    reset();
}

void EffectChain::reset()
{
    for (auto& e : effects) e->reset();
    current = clamp (target.effectIndex, 0, int (effects.size()) - 1);
    previous = -1;
    xfadePos = 0;
    smoothed = target;
    smoothed.effectIndex = current;
    prevParams = smoothed.fx;
    mixS.reset (target.mix);
    inGainS.reset (dbToGain (target.inputGainDb));
    outGainS.reset (dbToGain (target.outputGainDb));
    bypassS.reset (target.bypass ? 1.0f : 0.0f);
    trimCurS.reset (trims[size_t (current)]);
    trimPrevS.reset (1.0f);
    effectSleeping = target.bypass;
    limGain = 1.0f;
    freePpq = 0.0;
    slicePhase = 0;
}

double EffectChain::getTailSeconds() const
{
    return effects.empty() ? 0.0 : effects[size_t (current)]->getTailSeconds();
}

void EffectChain::startSwitchIfNeeded() noexcept
{
    const int wanted = clamp (target.effectIndex, 0, int (effects.size()) - 1);
    if (wanted == current || previous >= 0)
        return;

    if (effectSleeping)
    {
        // Nothing audible from the effect while bypassed: switch immediately.
        current = wanted;
        smoothed.fx = target.fx;
        trimCurS.reset (trims[size_t (current)]);
        return;
    }

    previous = current;
    current = wanted;
    xfadePos = 0;
    prevParams = smoothed.fx;  // outgoing effect keeps its settings while fading out
    smoothed.fx = target.fx;   // incoming effect starts directly on its own settings
    effects[size_t (current)]->reset();
    trimPrevS.reset (trimCurS.getCurrent());
    trimCurS.reset (trims[size_t (current)]);
}

void EffectChain::smoothControls() noexcept
{
    const float c = controlCoeff;
    auto& s = smoothed.fx;
    const auto& t = target.fx;
    s.amount = t.amount + c * (s.amount - t.amount);
    s.tone = t.tone + c * (s.tone - t.tone);
    s.movement = t.movement + c * (s.movement - t.movement);
    s.width = t.width + c * (s.width - t.width);
    for (int i = 0; i < kNumAdvParams; ++i)
    {
        float& v = s.adv[size_t (i)];
        const float tv = t.adv[size_t (i)];
        if (isDiscreteParam (i))
            v = tv;
        else if (isLogParam (i) && v > 0.0f && tv > 0.0f)
            v = std::exp (std::log (tv) + c * (std::log (v) - std::log (tv)));
        else
            v = tv + c * (v - tv);
    }
}

bool EffectChain::runEffect (int index, float* l, float* r, int n, const ProcessContext& ctx) noexcept
{
    effects[size_t (index)]->process (l, r, n, ctx);
    bool ok = true;
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (l[i]) || ! std::isfinite (r[i]) || std::abs (l[i]) > 1.0e6f || std::abs (r[i]) > 1.0e6f)
            ok = false;
    if (! ok)
    {
        effects[size_t (index)]->reset();
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        nonFiniteEvents.fetch_add (1);
    }
    return ok;
}

void EffectChain::process (float* left, float* right, int numSamples, const TempoInfo& hostTempo) noexcept
{
    if (! prepared || effects.empty() || numSamples <= 0)
        return;

    TempoInfo tempo = hostTempo;
    if (! (tempo.bpm >= 20.0 && tempo.bpm <= 999.0)) tempo.bpm = 120.0;
    const double beatsPerSample = tempo.bpm / (60.0 * sampleRate);
    const double basePpq = tempo.isPlaying ? tempo.ppq : freePpq;

    mixS.setTarget (clamp (target.mix, 0.0f, 1.0f));
    inGainS.setTarget (dbToGain (clamp (target.inputGainDb, -60.0f, 24.0f)));
    outGainS.setTarget (dbToGain (clamp (target.outputGainDb, -60.0f, 24.0f)));
    bypassS.setTarget (target.bypass ? 1.0f : 0.0f);

    float minLimGain = 1.0f;

    for (int pos = 0; pos < numSamples;)
    {
        // Control-rate updates happen on a fixed 32-sample grid that is
        // independent of the host's block boundaries.
        const int n = std::min (kSliceSize - slicePhase, numSamples - pos);
        float* L = left + pos;
        float* R = right + pos;
        const int slicePos = pos;
        pos += n;
        if (slicePhase == 0)
        {
            startSwitchIfNeeded();
            smoothControls();
        }
        slicePhase = (slicePhase + n) % kSliceSize;

        for (int i = 0; i < n; ++i)
        {
            dryL[size_t (i)] = sanitize (L[i]);
            dryR[size_t (i)] = sanitize (R[i]);
        }

        // Fully bypassed and settled: pass the input through untouched.
        const bool bypassSettled = target.bypass && ! bypassS.isSmoothing() && bypassS.getCurrent() >= 1.0f;
        if (bypassSettled)
        {
            effectSleeping = true;
            previous = -1;
            for (int i = 0; i < n; ++i)
            {
                L[i] = dryL[size_t (i)];
                R[i] = dryR[size_t (i)];
                mixS.next(); inGainS.next(); outGainS.next(); bypassS.next(); trimCurS.next();
            }
            continue;
        }
        if (effectSleeping)
        {
            effectSleeping = false;
            effects[size_t (current)]->reset();
            limGain = 1.0f;
        }

        for (int i = 0; i < n; ++i)
        {
            const float g = inGainS.next();
            inL[size_t (i)] = dryL[size_t (i)] * g;
            inR[size_t (i)] = dryR[size_t (i)] * g;
        }

        TempoInfo sliceTempo = tempo;
        sliceTempo.ppq = basePpq + double (slicePos) * beatsPerSample;

        std::copy (inL.begin(), inL.begin() + n, aL.begin());
        std::copy (inR.begin(), inR.begin() + n, aR.begin());
        const ProcessContext ctxCur { smoothed.fx, sliceTempo, sampleRate };
        runEffect (current, aL.data(), aR.data(), n, ctxCur);

        const bool fading = previous >= 0;
        if (fading)
        {
            std::copy (inL.begin(), inL.begin() + n, bL.begin());
            std::copy (inR.begin(), inR.begin() + n, bR.begin());
            const ProcessContext ctxPrev { prevParams, sliceTempo, sampleRate };
            runEffect (previous, bL.data(), bR.data(), n, ctxPrev);
        }

        const MixLaw lawCur = laws[size_t (current)];
        const MixLaw lawPrev = fading ? laws[size_t (previous)] : lawCur;

        for (int i = 0; i < n; ++i)
        {
            const float mix = mixS.next();
            const float tc = trimCurS.next();
            float dg, wg;
            mixGains (lawCur, mix, dg, wg);
            float outL = inL[size_t (i)] * dg + aL[size_t (i)] * tc * wg;
            float outR = inR[size_t (i)] * dg + aR[size_t (i)] * tc * wg;

            if (fading)
            {
                const float tp = trimPrevS.next();
                float dg2, wg2;
                mixGains (lawPrev, mix, dg2, wg2);
                const float pl = inL[size_t (i)] * dg2 + bL[size_t (i)] * tp * wg2;
                const float pr = inR[size_t (i)] * dg2 + bR[size_t (i)] * tp * wg2;
                const float x = float (std::min (xfadePos, xfadeLen)) / float (xfadeLen);
                const float gIn = 0.5f - 0.5f * std::cos (kPi * x); // smooth S-curve, sums to 1
                outL = pl + (outL - pl) * gIn;
                outR = pr + (outR - pr) * gIn;
                if (xfadePos < xfadeLen) ++xfadePos;
            }

            const float og = outGainS.next();
            outL *= og;
            outR *= og;

            // Zero-latency peak limiter (instant attack, smooth release)
            const float peak = std::max (std::abs (outL), std::abs (outR));
            limGain = limGain * limRelease + (1.0f - limRelease);
            if (peak * limGain > kLimiterThreshold)
                limGain = kLimiterThreshold / peak;
            minLimGain = std::min (minLimGain, limGain);
            outL = ceilingClip (outL * limGain);
            outR = ceilingClip (outR * limGain);

            const float bp = bypassS.next();
            L[i] = outL + (dryL[size_t (i)] - outL) * bp;
            R[i] = outR + (dryR[size_t (i)] - outR) * bp;
        }

        if (fading && xfadePos >= xfadeLen)
            previous = -1;
    }

    limiterDb.store (gainToDb (minLimGain));
    const double advance = double (numSamples) * beatsPerSample;
    freePpq = (tempo.isPlaying ? tempo.ppq : freePpq) + advance;
}
} // namespace msfx
