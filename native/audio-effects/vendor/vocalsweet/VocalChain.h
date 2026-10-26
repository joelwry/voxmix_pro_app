#pragma once
// The complete VocalSweet processing chain (framework independent, real-time safe):
//
//  in -> input gain -> HPF (12/24 dB) -> mud/box cuts + tone/warmth shelves -> compressor
//     -> saturation -> presence/air EQ -> exciter -> de-esser -> doubler
//     -> dry/wet -> output gain -> look-ahead limiter (ceiling) -> bypass crossfade -> out
//
// All memory is allocated in prepare(). process() performs no allocation, locking or I/O.

#include "Dynamics.h"
#include "Params.h"
#include "Tone.h"

namespace vs::dsp
{
class VocalChain
{
public:
    struct Meters
    {
        float inPeak[kMaxChannels] {}, outPeak[kMaxChannels] {};
        float inRms[kMaxChannels] {}, outRms[kMaxChannels] {};
        float compGrDb = 0, deessGrDb = 0, limGrDb = 0;
    };

    static constexpr int kSubBlock = 16;

    void prepare (double newSampleRate, int /*maxBlockSize*/)
    {
        sampleRate = newSampleRate;
        comp.prepare (sampleRate);
        deess.prepare (sampleRate);
        sat.prepare (sampleRate);
        excite.prepare (sampleRate);
        doubler.prepare (sampleRate);
        limiter.prepare (sampleRate, 1.0);
        bypassDelay.prepare (limiter.getLatency());
        bypassDelay.setDelay (limiter.getLatency());

        for (auto& s : slow) s.prepare (sampleRate / kSubBlock, 40.0);
        for (auto& s : fast) s.prepare (sampleRate, 20.0);
        for (int f : { fBypass, fHpfOn, fHpfSlope, fCompOn, fSatOn, fExciteOn, fDeessOn })
            fast[f].prepareLinear (sampleRate, 20.0);   // on/off crossfades: exact 20 ms ramps
        reset();
        prepared = true;
    }

    void reset()
    {
        for (auto& ch : filt) for (auto& f : ch) f.reset();
        comp.reset(); deess.reset(); sat.reset(); excite.reset(); doubler.reset();
        limiter.reset(); bypassDelay.reset();
        applyTargets (true);
        updateCoefficients (true);
    }

    int getLatencySamples() const noexcept { return limiter.getLatency(); }
    double getSampleRate() const noexcept { return sampleRate; }

    /** Set new parameter targets (call once per block before process()). */
    void setParams (const EffectiveParams& p) noexcept
    {
        params = p;
        applyTargets (false);
    }

    /** Process in place. numChannels 1 or 2 (extra channels are left untouched). */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, kMaxChannels);
        if (! prepared || numChannels <= 0) return;

        Meters m;
        double inSq[kMaxChannels] {}, outSq[kMaxChannels] {};
        float compGrMax = 0.0f, deessGrMax = 0.0f;

        for (int start = 0; start < numSamples; start += kSubBlock)
        {
            const int n = std::min (kSubBlock, numSamples - start);
            for (auto& s : slow) s.skip (1);
            updateCoefficients (false);

            for (int i = start; i < start + n; ++i)
            {
                float frame[kMaxChannels], dry[kMaxChannels], byp[kMaxChannels];

                const float inGain = fast[fInGain].next();
                const float outGain = fast[fOutGain].next();
                const float makeup = fast[fMakeup].next();
                const float mix = fast[fMix].next();
                const float bypass = fast[fBypass].next();
                const float eHpf = fast[fHpfOn].next();
                const float hpfSlope = fast[fHpfSlope].next();
                const float eComp = fast[fCompOn].next();
                const float eSat = fast[fSatOn].next();
                const float eExc = fast[fExciteOn].next();
                const float eDeess = fast[fDeessOn].next();

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float x = channels[ch][i];
                    if (! std::isfinite (x)) x = 0.0f;   // never let NaN/Inf enter the filters
                    x = std::clamp (x, -64.0f, 64.0f);   // +36 dBFS absolute input guard
                    m.inPeak[ch] = std::max (m.inPeak[ch], std::abs (x));
                    inSq[ch] += (double) x * x;
                    dry[ch] = x;
                    byp[ch] = bypassDelay.process (ch, x);

                    auto& f = filt[(size_t) ch];
                    float s = x * inGain;

                    // high-pass (two cascaded 2nd-order stages, slope crossfaded)
                    const float h1 = f[kHpf1].process (cHpf, s);
                    const float h2 = f[kHpf2].process (cHpf, h1);
                    const float hp = h1 + hpfSlope * (h2 - h1);
                    s += eHpf * (hp - s);

                    // corrective EQ + tone/warmth shelves
                    s = f[kMud].process (cMud, s);
                    s = f[kBox].process (cBox, s);
                    s = f[kBody].process (cBody, s);
                    s = f[kTilt].process (cTilt, s);
                    frame[ch] = s;
                }
                bypassDelay.advance();

                // compressor (stereo linked)
                {
                    float det = 0.0f;
                    for (int ch = 0; ch < numChannels; ++ch) det = std::max (det, std::abs (frame[ch]));
                    const float g = comp.processSample (det) * makeup;
                    for (int ch = 0; ch < numChannels; ++ch) frame[ch] += eComp * (frame[ch] * g - frame[ch]);
                    compGrMax = std::max (compGrMax, comp.getGainReductionDb() * eComp);
                }

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& f = filt[(size_t) ch];
                    float s = frame[ch];
                    s += eSat * (sat.process (ch, s) - s);
                    s = f[kPres].process (cPres, s);
                    s = f[kAir].process (cAir, s);
                    s += eExc * (excite.process (ch, s) - s);
                    frame[ch] = s;
                }

                // de-esser (split band, linked)
                {
                    float d[kMaxChannels];
                    for (int ch = 0; ch < numChannels; ++ch) d[ch] = frame[ch];
                    deess.processFrame (d, numChannels);
                    for (int ch = 0; ch < numChannels; ++ch) frame[ch] += eDeess * (d[ch] - frame[ch]);
                    deessGrMax = std::max (deessGrMax, deess.getGainReductionDb() * eDeess);
                }

                doubler.processFrame (frame, numChannels);

                for (int ch = 0; ch < numChannels; ++ch)
                    frame[ch] = (dry[ch] + mix * (frame[ch] - dry[ch])) * outGain;

                limiter.processFrame (frame, numChannels);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    // written so that bypass == 1 is bit-exact (latency-compensated) dry signal
                    const float y = bypass >= 1.0f ? byp[ch] : (1.0f - bypass) * frame[ch] + bypass * byp[ch];
                    channels[ch][i] = y;
                    m.outPeak[ch] = std::max (m.outPeak[ch], std::abs (y));
                    outSq[ch] += (double) y * y;
                }
            }
        }

        // stability watchdog: if anything blew up, reset and output silence for this block
        if (! isStateFinite())
        {
            reset();
            for (int ch = 0; ch < numChannels; ++ch)
                std::fill (channels[ch], channels[ch] + numSamples, 0.0f);
            ++watchdogResets;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            m.inRms[ch] = (float) std::sqrt (inSq[ch] / std::max (1, numSamples));
            m.outRms[ch] = (float) std::sqrt (outSq[ch] / std::max (1, numSamples));
        }
        if (numChannels == 1)
        {
            m.inPeak[1] = m.inPeak[0]; m.outPeak[1] = m.outPeak[0];
            m.inRms[1] = m.inRms[0];   m.outRms[1] = m.outRms[0];
        }
        m.compGrDb = compGrMax;
        m.deessGrDb = deessGrMax;
        m.limGrDb = limiter.popGainReductionDb();
        meters = m;
    }

    const Meters& getMeters() const noexcept { return meters; }
    int getWatchdogResets() const noexcept { return watchdogResets; }

    /** Static magnitude response (dB) of the linear EQ section for the *given* effective
        parameters - used by the editor's curve display so it shows what the DSP is doing. */
    static double responseDb (const EffectiveParams& p, double sr, double freq)
    {
        double db = 0.0;
        if (p.hpfOn)
        {
            const auto c = designSvf (SvfType::HighPass, sr, p.hpfFreq, 0.707);
            db += svfMagnitudeDb (c, sr, freq) * (p.hpf24 ? 2.0 : 1.0);
        }
        const double g = p.eqOn ? 1.0 : 0.0;
        db += svfMagnitudeDb (designSvf (SvfType::Bell, sr, p.mudFreq, 1.0, g * p.mudGain), sr, freq);
        db += svfMagnitudeDb (designSvf (SvfType::Bell, sr, p.boxFreq, 1.4, g * p.boxGain), sr, freq);
        db += svfMagnitudeDb (designSvf (SvfType::LowShelf, sr, p.bodyFreq, 0.707, p.bodyGain), sr, freq);
        db += svfMagnitudeDb (designSvf (SvfType::HighShelf, sr, p.tiltHiFreq, 0.707, p.tiltHiGain), sr, freq);
        db += svfMagnitudeDb (designSvf (SvfType::Bell, sr, p.presFreq, 0.9, g * p.presGain), sr, freq);
        db += svfMagnitudeDb (designSvf (SvfType::HighShelf, sr, p.airFreq, 0.707, g * p.airGain), sr, freq);
        return db;
    }

private:
    enum FilterSlot { kHpf1, kHpf2, kMud, kBox, kBody, kTilt, kPres, kAir, kNumFilters };
    enum Slow
    {
        sHpfFreq, sMudFreq, sMudGain, sBoxFreq, sBoxGain, sBodyGain, sTiltGain,
        sPresFreq, sPresGain, sAirFreq, sAirGain,
        sCompThresh, sCompRatio, sCompAttack, sCompRelease,
        sDeessFreq, sDeessThresh, sDeessRange,
        sSatDrive, sSatBias, sExcFreq, sExcAmount, sDblAmount, sDblWidth, sCeiling,
        kNumSlow
    };
    enum Fast { fInGain, fOutGain, fMakeup, fMix, fBypass, fHpfOn, fHpfSlope, fCompOn, fSatOn, fExciteOn, fDeessOn, kNumFast };

    void applyTargets (bool snap) noexcept
    {
        const auto& p = params;
        const float eq = p.eqOn ? 1.0f : 0.0f;
        const float slowT[kNumSlow] = {
            p.hpfFreq, p.mudFreq, eq * p.mudGain, p.boxFreq, eq * p.boxGain, p.bodyGain, p.tiltHiGain,
            p.presFreq, eq * p.presGain, p.airFreq, eq * p.airGain,
            p.compThresh, p.compRatio, p.compAttack, p.compRelease,
            p.deessFreq, p.deessThresh, p.deessRange,
            p.satDrive, p.satBias, p.exciteFreq, p.exciteAmount,
            p.doubleOn ? p.doubleAmount : 0.0f, p.doubleWidth, p.ceilingDb };
        const float fastT[kNumFast] = {
            dbToGain (p.inputGainDb), dbToGain (p.outputGainDb), dbToGain (p.compMakeup), p.mix,
            p.bypass ? 1.0f : 0.0f, p.hpfOn ? 1.0f : 0.0f, p.hpf24 ? 1.0f : 0.0f,
            p.compOn ? 1.0f : 0.0f, p.satOn ? 1.0f : 0.0f, p.exciteOn ? 1.0f : 0.0f, p.deessOn ? 1.0f : 0.0f };

        for (int i = 0; i < kNumSlow; ++i) snap ? slow[i].snap (slowT[i]) : slow[i].setTarget (slowT[i]);
        for (int i = 0; i < kNumFast; ++i) snap ? fast[i].snap (fastT[i]) : fast[i].setTarget (fastT[i]);
        limiter.setEnabled (p.limiterOn);
    }

    void updateCoefficients (bool force) noexcept
    {
        float v[kNumSlow];
        bool changed[kNumSlow];
        for (int i = 0; i < kNumSlow; ++i)
        {
            v[i] = slow[i].current;
            changed[i] = force || v[i] != lastSlow[i];
            lastSlow[i] = v[i];
        }
        auto any = [&] (std::initializer_list<int> ids) { for (int id : ids) if (changed[id]) return true; return false; };
        const double sr = sampleRate;

        if (any ({ sHpfFreq }))             cHpf  = designSvf (SvfType::HighPass, sr, v[sHpfFreq], 0.707);
        if (any ({ sMudFreq, sMudGain }))   cMud  = designSvf (SvfType::Bell, sr, v[sMudFreq], 1.0, v[sMudGain]);
        if (any ({ sBoxFreq, sBoxGain }))   cBox  = designSvf (SvfType::Bell, sr, v[sBoxFreq], 1.4, v[sBoxGain]);
        if (any ({ sBodyGain }))            cBody = designSvf (SvfType::LowShelf, sr, 200.0, 0.707, v[sBodyGain]);
        if (any ({ sTiltGain }))            cTilt = designSvf (SvfType::HighShelf, sr, 3000.0, 0.707, v[sTiltGain]);
        if (any ({ sPresFreq, sPresGain })) cPres = designSvf (SvfType::Bell, sr, v[sPresFreq], 0.9, v[sPresGain]);
        if (any ({ sAirFreq, sAirGain }))   cAir  = designSvf (SvfType::HighShelf, sr, v[sAirFreq], 0.707, v[sAirGain]);
        if (any ({ sCompThresh, sCompRatio, sCompAttack, sCompRelease }))
            comp.setParams (v[sCompThresh], v[sCompRatio], v[sCompAttack], v[sCompRelease]);
        if (any ({ sDeessFreq, sDeessThresh, sDeessRange }))
            deess.setParams (v[sDeessFreq], v[sDeessThresh], v[sDeessRange]);
        if (any ({ sSatDrive, sSatBias }))  sat.setParams (v[sSatDrive], v[sSatBias]);
        if (any ({ sExcFreq, sExcAmount })) excite.setParams (v[sExcFreq], v[sExcAmount]);
        if (any ({ sDblAmount, sDblWidth })) doubler.setParams (v[sDblAmount], v[sDblWidth]);
        if (any ({ sCeiling }))             limiter.setCeiling (v[sCeiling]);
    }

    bool isStateFinite() const noexcept
    {
        for (auto& ch : filt) for (auto& f : ch) if (! f.isFinite()) return false;
        return deess.isFinite() && sat.isFinite() && excite.isFinite() && doubler.isFinite()
               && std::isfinite (comp.getGainReductionDb());
    }

    double sampleRate = 48000.0;
    bool prepared = false;
    int watchdogResets = 0;
    EffectiveParams params;

    std::array<std::array<SvfState, kNumFilters>, kMaxChannels> filt;
    SvfCoeffs cHpf, cMud, cBox, cBody, cTilt, cPres, cAir;
    Compressor comp;
    DeEsser deess;
    Saturator sat;
    Exciter excite;
    Doubler doubler;
    Limiter limiter;
    DelayLine bypassDelay;

    std::array<Smoother, kNumSlow> slow;
    std::array<Smoother, kNumFast> fast;
    float lastSlow[kNumSlow] {};
    Meters meters;
};

} // namespace vs::dsp
