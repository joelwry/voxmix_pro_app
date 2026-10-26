// Foundational effects. The DSP building blocks are reused from the existing plugins:
//   VocalSweet  (vendor/vocalsweet):  TPT SVF, compressor, de-esser, saturator, look-ahead limiter
//   MoodShift FX (vendor/moodshift):  delay lines, FDN reverb, chorus
// The effect classes here only add parameter handling, smoothing and mixing.

#include "ParamHelpers.h"
#include "voxfx/EffectRegistry.h"
#include "../vendor/vocalsweet/Dynamics.h"
#include "../vendor/vocalsweet/Params.h"
#include "../vendor/vocalsweet/Tone.h"
#include "../vendor/moodshift/Effects/Delays/DelayLine.h"
#include "../vendor/moodshift/Effects/Modulation/Modulation.h"
#include "../vendor/moodshift/Effects/Reverb/FDNReverb.h"
#include "../vendor/moodshift/Parameters/ParameterIDs.h"

namespace voxfx
{
using namespace detail;
namespace vsd = vs::dsp;

namespace
{
EffectDescriptor makeDesc (const char* id, const char* name, EffectCategory cat, const char* origin,
                           const char* description, std::vector<ParamInfo> params)
{
    EffectDescriptor d;
    d.typeId = id; d.name = name; d.category = cat; d.origin = origin; d.description = description;
    d.params = std::move (params);
    return d;
}

std::vector<std::string> divisionChoices()
{
    std::vector<std::string> c;
    for (auto* n : msfx::kDivisionNames) c.emplace_back (n);
    return c;
}

//==============================================================================
// Parametric EQ: high-pass, low shelf, two bells, high shelf.
const EffectDescriptor& eqDesc()
{
    static const EffectDescriptor d = makeDesc ("eq", "Equalizer", EffectCategory::Dynamics, "VoxMix (VocalSweet SVF)",
        "Five-band vocal EQ with high-pass filter.",
        { toggle ("hpfOn", "Low Cut", true),
          cont ("hpfFreq", "Low Cut Freq", 20, 500, 80, "Hz", true),
          cont ("lowFreq", "Low Shelf Freq", 40, 600, 150, "Hz", true),
          cont ("lowGain", "Low Shelf Gain", -15, 15, 0, "dB"),
          cont ("mid1Freq", "Bell 1 Freq", 100, 3000, 350, "Hz", true),
          cont ("mid1Gain", "Bell 1 Gain", -15, 15, 0, "dB"),
          cont ("mid1Q", "Bell 1 Q", 0.3f, 6, 1.0f),
          cont ("mid2Freq", "Bell 2 Freq", 800, 9000, 3000, "Hz", true),
          cont ("mid2Gain", "Bell 2 Gain", -15, 15, 0, "dB"),
          cont ("mid2Q", "Bell 2 Q", 0.3f, 6, 1.0f),
          cont ("highFreq", "High Shelf Freq", 2000, 18000, 10000, "Hz", true),
          cont ("highGain", "High Shelf Gain", -15, 15, 0, "dB"),
          cont ("output", "Output", -18, 18, 0, "dB") });
    return d;
}

class EqEffect final : public Effect
{
public:
    EqEffect() : Effect (eqDesc()) {}
    enum { kHpfOn, kHpf, kLowF, kLowG, kM1F, kM1G, kM1Q, kM2F, kM2G, kM2Q, kHighF, kHighG, kOut };

    void prepare (double sr, int) override { sampleRate = sr; reset(); }
    void reset() override
    {
        for (auto& ch : st) for (auto& s : ch) s.reset();
        for (auto& s : sm) s.reset();
        rampInit = false;
    }

    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        const float hpf = sm[0].next (p (kHpf), n, sampleRate);
        const float lf = sm[1].next (p (kLowF), n, sampleRate), lg = sm[2].next (p (kLowG), n, sampleRate);
        const float m1f = sm[3].next (p (kM1F), n, sampleRate), m1g = sm[4].next (p (kM1G), n, sampleRate), m1q = sm[5].next (p (kM1Q), n, sampleRate);
        const float m2f = sm[6].next (p (kM2F), n, sampleRate), m2g = sm[7].next (p (kM2G), n, sampleRate), m2q = sm[8].next (p (kM2Q), n, sampleRate);
        const float hf = sm[9].next (p (kHighF), n, sampleRate), hg = sm[10].next (p (kHighG), n, sampleRate);
        c[0] = vsd::designSvf (vsd::SvfType::HighPass, sampleRate, hpf, 0.707);
        c[1] = vsd::designSvf (vsd::SvfType::LowShelf, sampleRate, lf, 0.707, lg);
        c[2] = vsd::designSvf (vsd::SvfType::Bell, sampleRate, m1f, m1q, m1g);
        c[3] = vsd::designSvf (vsd::SvfType::Bell, sampleRate, m2f, m2q, m2g);
        c[4] = vsd::designSvf (vsd::SvfType::HighShelf, sampleRate, hf, 0.707, hg);
        const float hpfTarget = p (kHpfOn) > 0.5f ? 1.0f : 0.0f;
        const float outTarget = dbToGain (p (kOut));
        if (! rampInit) { hpfGain = hpfTarget; outGain = outTarget; rampInit = true; }
        const float hpStep = (hpfTarget - hpfGain) / float (n), oStep = (outTarget - outGain) / float (n);
        float* chans[2] = { L, R };
        const bool dualMono = std::equal (L, L + n, R);   // mono layer: filter once, copy
        for (int ch = 0; ch < (dualMono ? 1 : 2); ++ch)
        {
            float* x = chans[ch];
            auto& s = st[(size_t) ch];
            float hpG = hpfGain, oG = outGain;
            for (int i = 0; i < n; ++i)
            {
                hpG += hpStep; oG += oStep;
                float v = sanitizeSample (x[i]);
                const float h = s[0].process (c[0], v);
                v += hpG * (h - v);
                v = s[1].process (c[1], v);
                v = s[2].process (c[2], v);
                v = s[3].process (c[3], v);
                v = s[4].process (c[4], v);
                x[i] = v * oG;
            }
        }
        if (dualMono) std::copy (L, L + n, R);
        hpfGain = hpfTarget; outGain = outTarget;
    }

private:
    double sampleRate = 48000.0;
    std::array<std::array<vsd::SvfState, 5>, 2> st;
    std::array<vsd::SvfCoeffs, 5> c;
    std::array<BlockSmoother, 11> sm;
    float hpfGain = 1.0f, outGain = 1.0f;
    bool rampInit = false;
};

//==============================================================================
const EffectDescriptor& compDesc()
{
    static const EffectDescriptor d = makeDesc ("compressor", "Compressor", EffectCategory::Dynamics, "VocalSweet",
        "Stereo-linked soft-knee feed-forward compressor.",
        { cont ("threshold", "Threshold", -60, 0, -18, "dB"),
          cont ("ratio", "Ratio", 1, 20, 3, ":1"),
          cont ("attack", "Attack", 0.1f, 100, 8, "ms", true),
          cont ("release", "Release", 10, 1000, 120, "ms", true),
          cont ("knee", "Knee", 0, 18, 6, "dB"),
          cont ("makeup", "Makeup", -6, 24, 0, "dB"),
          toggle ("autoMakeup", "Auto Makeup", true),
          cont ("mix", "Mix", 0, 100, 100, "%") });
    return d;
}

class CompressorEffect final : public Effect
{
public:
    CompressorEffect() : Effect (compDesc()) {}
    void prepare (double sr, int) override { sampleRate = sr; comp.prepare (sr); reset(); }
    void reset() override { comp.reset(); gainRamp.reset(); mixRamp.reset(); }

    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        const float thr = p (0), ratio = p (1);
        comp.setParams (thr, ratio, p (2), p (3), p (4));
        float makeupDb = p (5);
        if (p (6) > 0.5f) // ~70 % of the reduction applied to a -12 dBFS peak
            makeupDb += 0.7f * vs::staticGainReductionDb (-12.0f, thr, ratio);
        const float mk = dbToGain (makeupDb);
        float dry, wet;
        const float mix = p (7) * 0.01f;
        dry = 1.0f - mix; wet = mix;
        float grMax = 0.0f;
        gainRamp.apply (mk, n, [&] (int i, float g)
        {
            const float l = sanitizeSample (L[i]), r = sanitizeSample (R[i]);
            const float det = std::max (std::abs (l), std::abs (r));
            const float cg = comp.processSample (det) * g;
            L[i] = l * (dry + wet * cg);
            R[i] = r * (dry + wet * cg);
            grMax = std::max (grMax, comp.getGainReductionDb());
        });
        gr.store (grMax, std::memory_order_relaxed);
    }
    float gainReductionDb() const noexcept override { return gr.load (std::memory_order_relaxed); }

private:
    double sampleRate = 48000.0;
    vsd::Compressor comp;
    GainRamp gainRamp, mixRamp;
    std::atomic<float> gr { 0.0f };
};

//==============================================================================
const EffectDescriptor& deessDesc()
{
    static const EffectDescriptor d = makeDesc ("deesser", "De-Esser", EffectCategory::Dynamics, "VocalSweet",
        "Split-band de-esser: only the sibilance band is reduced.",
        { cont ("freq", "Frequency", 3000, 12000, 6500, "Hz", true),
          cont ("threshold", "Threshold", -60, 0, -28, "dB"),
          cont ("range", "Range", 0, 18, 8, "dB") });
    return d;
}

class DeEsserEffect final : public Effect
{
public:
    DeEsserEffect() : Effect (deessDesc()) {}
    void prepare (double sr, int) override { sampleRate = sr; de.prepare (sr); reset(); }
    void reset() override { de.reset(); freqS.reset(); }
    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        de.setParams (freqS.next (p (0), n, sampleRate), p (1), p (2));
        float grMax = 0.0f;
        const bool dualMono = std::equal (L, L + n, R);
        for (int i = 0; i < n; ++i)
        {
            float f[2] = { sanitizeSample (L[i]), sanitizeSample (R[i]) };
            de.processFrame (f, dualMono ? 1 : 2);
            L[i] = f[0]; R[i] = dualMono ? f[0] : f[1];
            grMax = std::max (grMax, de.getGainReductionDb());
        }
        gr.store (grMax, std::memory_order_relaxed);
    }
    float gainReductionDb() const noexcept override { return gr.load (std::memory_order_relaxed); }

private:
    double sampleRate = 48000.0;
    vsd::DeEsser de;
    BlockSmoother freqS;
    std::atomic<float> gr { 0.0f };
};

//==============================================================================
// Noise gate / downward expander with hold and hysteresis.
const EffectDescriptor& gateDesc()
{
    static const EffectDescriptor d = makeDesc ("gate", "Gate / Expander", EffectCategory::Dynamics, "VoxMix",
        "Reduces background noise between phrases.",
        { cont ("threshold", "Threshold", -80, 0, -50, "dB"),
          cont ("range", "Range", 0, 80, 30, "dB"),
          cont ("attack", "Attack", 0.1f, 50, 1, "ms", true),
          cont ("hold", "Hold", 0, 500, 60, "ms"),
          cont ("release", "Release", 5, 1000, 150, "ms", true) });
    return d;
}

class GateEffect final : public Effect
{
public:
    GateEffect() : Effect (gateDesc()) {}
    void prepare (double sr, int) override { sampleRate = sr; reset(); }
    void reset() override { env = 0.0f; gain = 1.0f; holdLeft = 0; open = true; }
    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        const float thrOpen = dbToGain (p (0)), thrClose = dbToGain (p (0) - 4.0f); // 4 dB hysteresis
        const float floorG = dbToGain (-p (1));
        const float att = vsd::timeToCoeff (sampleRate, p (2));
        const float rel = vsd::timeToCoeff (sampleRate, p (4));
        const int hold = int (p (3) * 0.001 * sampleRate);
        const float envRel = vsd::timeToCoeff (sampleRate, 20.0);
        float grMax = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float l = sanitizeSample (L[i]), r = sanitizeSample (R[i]);
            const float det = std::max (std::abs (l), std::abs (r));
            env = det > env ? det : det + envRel * (env - det);
            if (env >= thrOpen) { open = true; holdLeft = hold; }
            else if (env < thrClose) { if (holdLeft > 0) --holdLeft; else open = false; }
            const float target = open ? 1.0f : floorG;
            gain = target + (target > gain ? att : rel) * (gain - target);
            L[i] = l * gain; R[i] = r * gain;
            grMax = std::max (grMax, -gainToDb (gain));
        }
        gr.store (grMax, std::memory_order_relaxed);
    }
    float gainReductionDb() const noexcept override { return gr.load (std::memory_order_relaxed); }

private:
    double sampleRate = 48000.0;
    float env = 0.0f, gain = 1.0f;
    int holdLeft = 0;
    bool open = true;
    std::atomic<float> gr { 0.0f };
};

//==============================================================================
const EffectDescriptor& satDesc()
{
    static const EffectDescriptor d = makeDesc ("saturation", "Saturation", EffectCategory::Dynamics, "VocalSweet",
        "Warm anti-aliased (ADAA) tanh saturation; Warmth adds even harmonics.",
        { cont ("drive", "Drive", 0, 100, 35, "%"),
          cont ("warmth", "Warmth", 0, 100, 40, "%"),
          cont ("mix", "Mix", 0, 100, 100, "%"),
          cont ("output", "Output", -18, 6, 0, "dB") });
    return d;
}

class SaturationEffect final : public Effect
{
public:
    SaturationEffect() : Effect (satDesc()) {}
    void prepare (double sr, int) override { sampleRate = sr; sat.prepare (sr); reset(); }
    void reset() override { sat.reset(); driveS.reset(); outRamp.reset(); }
    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        sat.setParams (driveS.next (p (0) * 0.01f, n, sampleRate), 0.05f + 0.3f * p (1) * 0.01f);
        const float mix = p (2) * 0.01f;
        // keep perceived level roughly constant as drive increases
        const float comp = dbToGain (p (3) - 2.5f * p (0) * 0.01f);
        const bool dualMono = std::equal (L, L + n, R);
        outRamp.apply (comp, n, [&] (int i, float g)
        {
            const float l = sanitizeSample (L[i]), r = sanitizeSample (R[i]);
            const float wl = sat.process (0, l), wr = dualMono ? wl : sat.process (1, r);
            L[i] = (l + mix * (wl - l)) * g;
            R[i] = (r + mix * (wr - r)) * g;
        });
    }

private:
    double sampleRate = 48000.0;
    vsd::Saturator sat;
    BlockSmoother driveS;
    GainRamp outRamp;
};

//==============================================================================
const EffectDescriptor& limDesc()
{
    static const EffectDescriptor d = makeDesc ("limiter", "Limiter", EffectCategory::Dynamics, "VocalSweet",
        "1 ms look-ahead peak limiter (adds 1 ms latency).",
        { cont ("input", "Input Gain", 0, 24, 0, "dB"),
          cont ("ceiling", "Ceiling", -12, 0, -1, "dB") });
    return d;
}

class LimiterEffect final : public Effect
{
public:
    LimiterEffect() : Effect (limDesc()) {}
    void prepare (double sr, int) override { lim.prepare (sr, 1.0); reset(); }
    void reset() override { lim.reset(); inRamp.reset(); }
    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        lim.setCeiling (p (1));
        inRamp.apply (dbToGain (p (0)), n, [&] (int i, float g)
        {
            float f[2] = { sanitizeSample (L[i]) * g, sanitizeSample (R[i]) * g };
            lim.processFrame (f, 2);
            L[i] = f[0]; R[i] = f[1];
        });
        gr.store (lim.popGainReductionDb(), std::memory_order_relaxed);
    }
    int latencySamples() const noexcept override { return lim.getLatency(); }
    float gainReductionDb() const noexcept override { return gr.load (std::memory_order_relaxed); }

private:
    vsd::Limiter lim;
    GainRamp inRamp;
    std::atomic<float> gr { 0.0f };
};

//==============================================================================
const EffectDescriptor& reverbDesc()
{
    static const EffectDescriptor d = makeDesc ("reverb", "Reverb", EffectCategory::Space, "MoodShift FX (FDN reverb)",
        "8-line feedback delay network reverb.",
        { choice ("type", "Type", { "Room", "Plate", "Hall", "Cathedral" }, 2),
          cont ("decay", "Decay", 0.2f, 15, 2.2f, "s", true),
          cont ("predelay", "Pre-Delay", 0, 200, 20, "ms"),
          cont ("damping", "Damping", 1000, 16000, 6500, "Hz", true),
          cont ("lowCut", "Low Cut", 20, 600, 150, "Hz", true),
          cont ("width", "Width", 0, 200, 100, "%"),
          cont ("mix", "Mix", 0, 100, 25, "%") });
    return d;
}

class ReverbEffect final : public Effect
{
public:
    ReverbEffect() : Effect (reverbDesc()) {}
    void prepare (double sr, int) override { sampleRate = sr; fdn.prepare (sr); reset(); }
    void reset() override { fdn.reset(); mixS.reset(); lastHash = -1.0f; }
    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        const int type = int (p (0));
        static constexpr float sizes[] = { 0.45f, 0.8f, 1.2f, 2.0f };
        static constexpr float diff[] = { 0.6f, 0.85f, 0.7f, 0.75f };
        msfx::dsp::FDNReverb::Params rp;
        rp.size = sizes[std::clamp (type, 0, 3)];
        rp.diffusion = diff[std::clamp (type, 0, 3)];
        rp.decaySeconds = p (1);
        rp.preDelayMs = p (2);
        rp.dampingHz = p (3);
        rp.lowCutHz = p (4);
        rp.width = p (5) * 0.01f;
        rp.modDepth = 0.3f; rp.modRate = 0.4f;
        const float h = rp.size * 7.0f + rp.decaySeconds * 13.0f + rp.preDelayMs * 0.37f + rp.dampingHz * 0.001f + rp.lowCutHz * 0.01f + rp.width;
        if (h != lastHash) { fdn.setParams (rp); lastHash = h; }
        float dry, wet;
        equalPowerMix (mixS.next (p (6) * 0.01f, n, sampleRate), dry, wet);
        for (int i = 0; i < n; ++i)
        {
            const float l = sanitizeSample (L[i]), r = sanitizeSample (R[i]);
            float ol, orr;
            fdn.process (l, r, ol, orr);
            L[i] = dry * l + wet * ol;
            R[i] = dry * r + wet * orr;
        }
    }
    double tailSeconds() const noexcept override { return std::max (1.0, (double) p (1) * 1.5); }

private:
    double sampleRate = 48000.0;
    msfx::dsp::FDNReverb fdn;
    BlockSmoother mixS;
    float lastHash = -1.0f;
};

//==============================================================================
const EffectDescriptor& delayDesc()
{
    static const EffectDescriptor d = makeDesc ("delay", "Delay / Echo", EffectCategory::Space, "VoxMix (MoodShift delay lines)",
        "Stereo delay with tempo sync, ping-pong and filtered feedback.",
        { toggle ("sync", "Tempo Sync", true),
          choice ("division", "Division", divisionChoices(), 3),
          cont ("time", "Time", 10, 2000, 350, "ms", true),
          cont ("feedback", "Feedback", 0, 92, 35, "%"),
          toggle ("pingPong", "Ping-Pong", false),
          cont ("lowCut", "Low Cut", 20, 2000, 200, "Hz", true),
          cont ("highCut", "High Cut", 1000, 18000, 6000, "Hz", true),
          cont ("mix", "Mix", 0, 100, 25, "%") });
    return d;
}

class DelayEffect final : public Effect
{
public:
    DelayEffect() : Effect (delayDesc()) {}
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        for (auto& l : lines) l.prepare (sr, 4.1f);
        reset();
    }
    void reset() override
    {
        for (auto& l : lines) l.reset();
        for (auto& s : hp) s.reset();
        for (auto& s : lp) s.reset();
        delaySmooth = -1.0f; mixS.reset();
    }
    void process (float* L, float* R, int n, const ProcessInfo& info) noexcept override
    {
        double seconds = p (2) * 0.001;
        if (p (0) > 0.5f)
        {
            const int div = std::clamp (int (p (1)), 0, int (msfx::kDivisionBeats.size()) - 1);
            seconds = msfx::kDivisionBeats[(size_t) div] * 60.0 / std::clamp (info.bpm, 30.0, 300.0);
        }
        seconds = std::clamp (seconds, 0.001, 4.0);
        const float target = float (seconds * sampleRate);
        if (delaySmooth < 0.0f) delaySmooth = target;
        const float fb = p (3) * 0.01f;
        const bool ping = p (4) > 0.5f;
        cHp = vsd::designSvf (vsd::SvfType::HighPass, sampleRate, p (5), 0.707);
        cLp = vsd::designSvf (vsd::SvfType::LowPass, sampleRate, p (6), 0.707);
        float dry, wet;
        equalPowerMix (mixS.next (p (7) * 0.01f, n, sampleRate), dry, wet);
        dry = std::max (dry, 0.5f); // echoes sit on top of the dry voice
        const float a = std::exp (-1.0f / float (0.05 * sampleRate)); // 50 ms glide on time changes
        for (int i = 0; i < n; ++i)
        {
            delaySmooth = target + a * (delaySmooth - target);
            const float l = sanitizeSample (L[i]), r = sanitizeSample (R[i]);
            float dl = lines[0].readLinear (delaySmooth - 1.0f);
            float dr = lines[1].readLinear (delaySmooth - 1.0f);
            dl = lp[0].process (cLp, hp[0].process (cHp, dl));
            dr = lp[1].process (cLp, hp[1].process (cHp, dr));
            if (ping)
            {
                const float mono = 0.5f * (l + r);
                lines[0].push (mono + fb * dr);
                lines[1].push (fb * dl);
            }
            else
            {
                lines[0].push (l + fb * dl);
                lines[1].push (r + fb * dr);
            }
            L[i] = dry * l + wet * dl;
            R[i] = dry * r + wet * dr;
        }
    }
    double tailSeconds() const noexcept override { return 6.0; }

private:
    double sampleRate = 48000.0;
    std::array<msfx::dsp::DelayLine, 2> lines;
    std::array<vsd::SvfState, 2> hp, lp;
    vsd::SvfCoeffs cHp, cLp;
    float delaySmooth = -1.0f;
    BlockSmoother mixS;
};

//==============================================================================
const EffectDescriptor& chorusDesc()
{
    static const EffectDescriptor d = makeDesc ("chorus", "Chorus", EffectCategory::Space, "MoodShift FX",
        "Multi-voice modulated chorus for width and thickness.",
        { integer ("voices", "Voices", 1, 8, 3),
          cont ("rate", "Rate", 0.05f, 5, 0.6f, "Hz", true),
          cont ("depth", "Depth", 0, 10, 3, "ms"),
          cont ("delay", "Delay", 3, 30, 12, "ms"),
          cont ("spread", "Spread", 0, 100, 80, "%"),
          cont ("mix", "Mix", 0, 100, 35, "%") });
    return d;
}

class ChorusEffect final : public Effect
{
public:
    ChorusEffect() : Effect (chorusDesc()) {}
    void prepare (double sr, int) override { sampleRate = sr; ch.prepare (sr); reset(); }
    void reset() override { ch.reset(); mixS.reset(); }
    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        ch.setParams (int (p (0)), p (1), p (2), p (3), p (4) * 0.01f);
        float dry, wet;
        equalPowerMix (mixS.next (p (5) * 0.01f, n, sampleRate), dry, wet);
        for (int i = 0; i < n; ++i)
        {
            const float l = sanitizeSample (L[i]), r = sanitizeSample (R[i]);
            float ol, orr;
            ch.process (l, r, ol, orr);
            L[i] = dry * l + wet * ol;
            R[i] = dry * r + wet * orr;
        }
    }

private:
    double sampleRate = 48000.0;
    msfx::dsp::Chorus ch;
    BlockSmoother mixS;
};

//==============================================================================
const EffectDescriptor& widthDesc()
{
    static const EffectDescriptor d = makeDesc ("stereo_width", "Stereo Width", EffectCategory::Space, "VoxMix",
        "Mid/side width control with mono-compatible low end.",
        { cont ("width", "Width", 0, 200, 130, "%"),
          cont ("monoBelow", "Mono Below", 20, 400, 120, "Hz", true) });
    return d;
}

class WidthEffect final : public Effect
{
public:
    WidthEffect() : Effect (widthDesc()) {}
    void prepare (double sr, int) override { sampleRate = sr; reset(); }
    void reset() override { lpS.reset(); widthS.reset(); }
    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        const float w = widthS.next (p (0) * 0.01f, n, sampleRate);
        const auto c = vsd::designSvf (vsd::SvfType::LowPass, sampleRate, p (1), 0.707);
        for (int i = 0; i < n; ++i)
        {
            const float l = sanitizeSample (L[i]), r = sanitizeSample (R[i]);
            const float mid = 0.5f * (l + r);
            float side = 0.5f * (l - r);
            const float lowSide = lpS.process (c, side);
            side = lowSide + (side - lowSide) * w; // low frequencies keep their original width
            L[i] = mid + side;
            R[i] = mid - side;
        }
    }

private:
    double sampleRate = 48000.0;
    vsd::SvfState lpS;
    BlockSmoother widthS;
};

template <typename T>
EffectType make (const EffectDescriptor& d)
{
    return { &d, [] { return std::make_shared<T>(); } };
}
} // namespace

void registerCoreEffects (std::vector<EffectType>& out)
{
    out.push_back (make<EqEffect> (eqDesc()));
    out.push_back (make<CompressorEffect> (compDesc()));
    out.push_back (make<DeEsserEffect> (deessDesc()));
    out.push_back (make<GateEffect> (gateDesc()));
    out.push_back (make<SaturationEffect> (satDesc()));
    out.push_back (make<LimiterEffect> (limDesc()));
    out.push_back (make<ReverbEffect> (reverbDesc()));
    out.push_back (make<DelayEffect> (delayDesc()));
    out.push_back (make<ChorusEffect> (chorusDesc()));
    out.push_back (make<WidthEffect> (widthDesc()));
}

} // namespace voxfx
