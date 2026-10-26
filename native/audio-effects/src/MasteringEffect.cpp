// Mastering chain for the complete mix:
//   input gain -> tone EQ (low shelf, mid bell, high shelf) -> glue compressor
//   -> stereo width (M/S, low end kept mono-compatible) -> look-ahead limiter (ceiling)
// Built from the VocalSweet DSP modules. Loudness targets are reached by the engine's
// "loudness match" step (it measures the mix offline and sets Input Gain), not by guessing.

#include "ParamHelpers.h"
#include "voxfx/EffectRegistry.h"
#include "../vendor/vocalsweet/Dynamics.h"
#include "../vendor/vocalsweet/Params.h"

namespace voxfx
{
using namespace detail;
namespace vsd = vs::dsp;

namespace
{
const EffectDescriptor& masteringDesc()
{
    static const EffectDescriptor d = []
    {
        EffectDescriptor e;
        e.typeId = "mastering";
        e.name = "Mastering Chain";
        e.category = EffectCategory::Mastering;
        e.origin = "VoxMix (VocalSweet dynamics)";
        e.description = "EQ, glue compression, stereo width and a look-ahead limiter for the final mix.";
        e.params = { cont ("inputGain", "Input Gain", -12, 18, 0, "dB"),
                     toggle ("eqOn", "EQ", true),
                     cont ("lowGain", "Low (100 Hz)", -8, 8, 0, "dB"),
                     cont ("midFreq", "Mid Freq", 300, 6000, 1500, "Hz", true),
                     cont ("midGain", "Mid", -8, 8, 0, "dB"),
                     cont ("highGain", "High (10 kHz)", -8, 8, 0, "dB"),
                     toggle ("compOn", "Glue Compressor", true),
                     cont ("compThreshold", "Comp Threshold", -40, 0, -14, "dB"),
                     cont ("compRatio", "Comp Ratio", 1, 8, 2, ":1"),
                     cont ("compAttack", "Comp Attack", 1, 100, 20, "ms", true),
                     cont ("compRelease", "Comp Release", 30, 1000, 200, "ms", true),
                     cont ("width", "Stereo Width", 0, 200, 100, "%"),
                     toggle ("limiterOn", "Limiter", true),
                     cont ("ceiling", "Ceiling", -6, 0, -1, "dB") };
        return e;
    }();
    return d;
}

class MasteringEffect final : public Effect
{
public:
    MasteringEffect() : Effect (masteringDesc()) {}
    enum { kIn, kEqOn, kLow, kMidF, kMid, kHigh, kCompOn, kThr, kRatio, kAtt, kRel, kWidth, kLimOn, kCeil };

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        comp.prepare (sr);
        lim.prepare (sr, 1.5);
        delay.prepare (lim.getLatency());
        delay.setDelay (lim.getLatency());
        reset();
    }

    void reset() override
    {
        comp.reset(); lim.reset(); delay.reset();
        for (auto& ch : eq) for (auto& s : ch) s.reset();
        lowSide.reset();
        for (auto& s : sm) s.reset();
        inG = -1.0f; widthG = -1.0f; compMix = -1.0f; eqMix = -1.0f; limMix = -1.0f;
    }

    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        c[0] = vsd::designSvf (vsd::SvfType::LowShelf, sampleRate, 100.0, 0.707, sm[0].next (p (kLow), n, sampleRate));
        c[1] = vsd::designSvf (vsd::SvfType::Bell, sampleRate, sm[1].next (p (kMidF), n, sampleRate), 0.7, sm[2].next (p (kMid), n, sampleRate));
        c[2] = vsd::designSvf (vsd::SvfType::HighShelf, sampleRate, 10000.0, 0.707, sm[3].next (p (kHigh), n, sampleRate));
        const auto cSide = vsd::designSvf (vsd::SvfType::LowPass, sampleRate, 150.0, 0.707);
        comp.setParams (p (kThr), p (kRatio), p (kAtt), p (kRel), 6.0f);
        const float makeup = dbToGain (0.5f * vs::staticGainReductionDb (-8.0f, p (kThr), p (kRatio)));
        lim.setCeiling (p (kCeil));
        lim.setEnabled (true);

        auto ramp = [n] (float& cur, float target, float& step)
        {
            if (cur < 0.0f) cur = target;
            step = (target - cur) / float (n);
        };
        float sIn, sW, sC, sE, sL;
        ramp (inG, dbToGain (p (kIn)), sIn);
        ramp (widthG, p (kWidth) * 0.01f, sW);
        ramp (compMix, p (kCompOn) > 0.5f ? 1.0f : 0.0f, sC);
        ramp (eqMix, p (kEqOn) > 0.5f ? 1.0f : 0.0f, sE);
        ramp (limMix, p (kLimOn) > 0.5f ? 1.0f : 0.0f, sL);

        float grMax = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            inG += sIn; widthG += sW; compMix += sC; eqMix += sE; limMix += sL;
            float f[2] = { sanitizeSample (L[i]) * inG, sanitizeSample (R[i]) * inG };
            for (int ch = 0; ch < 2; ++ch)
            {
                float v = f[ch];
                v = eq[(size_t) ch][0].process (c[0], v);
                v = eq[(size_t) ch][1].process (c[1], v);
                v = eq[(size_t) ch][2].process (c[2], v);
                f[ch] += eqMix * (v - f[ch]);
            }
            const float det = std::max (std::abs (f[0]), std::abs (f[1]));
            const float g = comp.processSample (det) * makeup;
            const float cg = 1.0f + compMix * (g - 1.0f);
            f[0] *= cg; f[1] *= cg;
            grMax = std::max (grMax, comp.getGainReductionDb() * compMix);

            const float mid = 0.5f * (f[0] + f[1]);
            float side = 0.5f * (f[0] - f[1]);
            const float low = lowSide.process (cSide, side);
            side = low + (side - low) * widthG;
            f[0] = mid + side; f[1] = mid - side;

            // limiter and its latency-matched bypass are crossfaded by limMix
            const float d0 = delay.process (0, f[0]), d1 = delay.process (1, f[1]);
            delay.advance();
            lim.processFrame (f, 2);
            L[i] = d0 + limMix * (f[0] - d0);
            R[i] = d1 + limMix * (f[1] - d1);
        }
        gr.store (grMax + lim.popGainReductionDb(), std::memory_order_relaxed);
    }

    int latencySamples() const noexcept override { return lim.getLatency(); }
    float gainReductionDb() const noexcept override { return gr.load (std::memory_order_relaxed); }

private:
    double sampleRate = 48000.0;
    vsd::Compressor comp;
    vsd::Limiter lim;
    vsd::DelayLine delay;
    std::array<std::array<vsd::SvfState, 3>, 2> eq;
    std::array<vsd::SvfCoeffs, 3> c;
    vsd::SvfState lowSide;
    std::array<BlockSmoother, 4> sm;
    float inG = -1.0f, widthG = -1.0f, compMix = -1.0f, eqMix = -1.0f, limMix = -1.0f;
    std::atomic<float> gr { 0.0f };
};
} // namespace

void registerMastering (std::vector<EffectType>& out)
{
    out.push_back ({ &masteringDesc(), [] { return std::make_shared<MasteringEffect>(); } });
}

} // namespace voxfx
