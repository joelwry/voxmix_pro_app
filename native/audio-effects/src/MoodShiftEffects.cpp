// The 40 MoodShift FX creative effects (vendor/moodshift, reused unchanged) exposed as individual
// VoxMix effect types "ms_<id>". Instead of the plugin's EffectChain, which instantiates all 40
// effects at once, each slot instantiates only the one effect it needs (important on mobile:
// up to 10 layers x several slots). The mixing behaviour of the plugin is reproduced here:
// control-rate smoothing on a 32-sample grid, the effect's mix law and level trim, and a
// non-finite guard that resets a misbehaving effect.

#include "ParamHelpers.h"
#include "voxfx/EffectRegistry.h"
#include "../vendor/moodshift/Effects/EffectRegistry.h"
#include "../vendor/moodshift/Presets/FactoryPresets.h"

namespace voxfx
{
using namespace detail;

namespace
{
constexpr int kSlice = 32;

EffectCategory mapCategory (msfx::Category) { return EffectCategory::Creative; }

struct MsTypeData
{
    int msIndex = 0;
    EffectDescriptor desc;
    std::vector<int> advIndex;  // our param index - 5 -> msfx AdvParam index
    std::array<float, msfx::kNumAdvParams> advDefaults {};
};

const std::vector<MsTypeData>& msTypes()
{
    static const std::vector<MsTypeData> types = []
    {
        std::vector<MsTypeData> out;
        const auto& reg = msfx::effectRegistry();
        const auto& presets = msfx::factoryPresets();
        for (size_t i = 0; i < reg.size(); ++i)
        {
            const auto& info = reg[i];
            MsTypeData t;
            t.msIndex = (int) i;
            const msfx::FactoryPreset* fp = nullptr;
            for (const auto& p : presets)
                if (std::string (p.effectId) == info.id) fp = &p;
            const auto engine = fp ? msfx::toEngineParams (*fp) : msfx::EngineParams {};
            t.advDefaults = engine.fx.adv;

            auto& d = t.desc;
            d.typeId = std::string ("ms_") + info.id;
            d.name = info.name;
            d.category = mapCategory (info.category);
            d.origin = std::string ("MoodShift FX / ") + msfx::categoryName (info.category);
            d.description = info.description;
            d.params.push_back (cont ("amount", info.roles.amount ? "Amount" : "Amount", 0, 100, engine.fx.amount * 100.0f, "%"));
            d.params.push_back (cont ("mix", "Mix", 0, 100, engine.mix * 100.0f, "%"));
            d.params.push_back (cont ("tone", "Tone", -100, 100, engine.fx.tone * 100.0f, ""));
            d.params.push_back (cont ("movement", "Movement", 0, 100, engine.fx.movement * 100.0f, "%"));
            d.params.push_back (cont ("width", "Width", 0, 200, engine.fx.width * 100.0f, "%"));
            for (const auto& c : info.controls)
            {
                const auto& spec = msfx::advSpec (c.param);
                ParamInfo p;
                p.id = spec.id;
                p.name = c.label ? c.label : spec.name;
                p.min = spec.minValue; p.max = spec.maxValue;
                p.def = engine.fx.adv[(size_t) c.param];
                p.unit = spec.unit;
                p.logScale = spec.skew == msfx::Skew::log;
                switch (spec.kind)
                {
                    case msfx::ParamKind::continuous: p.type = ParamType::Continuous; break;
                    case msfx::ParamKind::integer:    p.type = ParamType::Integer; break;
                    case msfx::ParamKind::toggle:     p.type = ParamType::Toggle; break;
                    case msfx::ParamKind::choice:
                        p.type = ParamType::Choice;
                        if (c.param == msfx::AdvParam::division)
                            for (auto* n : msfx::kDivisionNames) p.choices.emplace_back (n);
                        else
                            for (auto* n : msfx::kShapeNames) p.choices.emplace_back (n);
                        break;
                }
                d.params.push_back (p);
                t.advIndex.push_back ((int) c.param);
            }
            out.push_back (std::move (t));
        }
        return out;
    }();
    return types;
}

class MoodShiftEffect final : public Effect
{
public:
    explicit MoodShiftEffect (const MsTypeData& t)
        : Effect (t.desc), type (t), info (msfx::effectRegistry()[(size_t) t.msIndex])
    {
        fx = info.create();
        trim = dbToGain (info.trimDb);
    }

    void prepare (double sr, int maxBlock) override
    {
        sampleRate = sr;
        fx->prepare (sr, std::max (maxBlock, kSlice));
        wetL.assign ((size_t) std::max (maxBlock, kSlice), 0.0f);
        wetR.assign ((size_t) std::max (maxBlock, kSlice), 0.0f);
        controlCoeff = float (std::exp (-double (kSlice) / (0.03 * sr)));
        reset();
    }

    void reset() override
    {
        fx->reset();
        readTargets (target);
        smoothed = target;
        mixSmoothed = p (1) * 0.01f;
    }

    void process (float* L, float* R, int n, const ProcessInfo& pi) noexcept override
    {
        readTargets (target);
        const double bpm = (pi.bpm >= 20.0 && pi.bpm <= 999.0) ? pi.bpm : 120.0;
        const double beatsPerSample = bpm / (60.0 * sampleRate);
        const float mixTarget = p (1) * 0.01f;

        for (int start = 0; start < n; start += kSlice)
        {
            const int m = std::min (kSlice, n - start);
            smooth();
            msfx::TempoInfo tempo;
            tempo.bpm = bpm;
            tempo.isPlaying = pi.playing;
            tempo.ppq = (pi.playing ? pi.beatPosition : freePpq) + double (start) * beatsPerSample;
            const msfx::ProcessContext ctx { smoothed, tempo, sampleRate };

            float* l = L + start;
            float* r = R + start;
            for (int i = 0; i < m; ++i) { wetL[(size_t) i] = sanitizeSample (l[i]); wetR[(size_t) i] = sanitizeSample (r[i]); }
            fx->process (wetL.data(), wetR.data(), m, ctx);
            bool ok = true;
            for (int i = 0; i < m; ++i)
                if (! std::isfinite (wetL[(size_t) i]) || ! std::isfinite (wetR[(size_t) i])
                    || std::abs (wetL[(size_t) i]) > 1.0e6f || std::abs (wetR[(size_t) i]) > 1.0e6f)
                    ok = false;
            if (! ok)
            {
                fx->reset();
                std::fill (wetL.begin(), wetL.begin() + m, 0.0f);
                std::fill (wetR.begin(), wetR.begin() + m, 0.0f);
            }

            const float mixStep = (mixTarget - mixSmoothed) / float (m);
            for (int i = 0; i < m; ++i)
            {
                mixSmoothed += mixStep;
                float dg, wg;
                if (info.mixLaw == msfx::MixLaw::linear) { dg = 1.0f - mixSmoothed; wg = mixSmoothed; }
                else equalPowerMix (mixSmoothed, dg, wg);
                l[i] = sanitizeSample (l[i]) * dg + wetL[(size_t) i] * trim * wg;
                r[i] = sanitizeSample (r[i]) * dg + wetR[(size_t) i] * trim * wg;
            }
        }
        freePpq = (pi.playing ? pi.beatPosition : freePpq) + double (n) * beatsPerSample;
    }

    double tailSeconds() const noexcept override { return fx->getTailSeconds(); }

private:
    void readTargets (msfx::EffectParams& t) const noexcept
    {
        t.amount = p (0) * 0.01f;
        t.tone = p (2) * 0.01f;
        t.movement = p (3) * 0.01f;
        t.width = p (4) * 0.01f;
        t.adv = type.advDefaults;
        for (size_t k = 0; k < type.advIndex.size(); ++k)
            t.adv[(size_t) type.advIndex[k]] = p (int (k) + 5);
    }

    void smooth() noexcept
    {
        const float c = controlCoeff;
        smoothed.amount = target.amount + c * (smoothed.amount - target.amount);
        smoothed.tone = target.tone + c * (smoothed.tone - target.tone);
        smoothed.movement = target.movement + c * (smoothed.movement - target.movement);
        smoothed.width = target.width + c * (smoothed.width - target.width);
        for (int i = 0; i < msfx::kNumAdvParams; ++i)
        {
            const auto& spec = msfx::kAdvSpecs[(size_t) i];
            float& v = smoothed.adv[(size_t) i];
            const float tv = target.adv[(size_t) i];
            if (spec.kind != msfx::ParamKind::continuous)
                v = tv;
            else if (spec.skew == msfx::Skew::log && v > 0.0f && tv > 0.0f)
                v = std::exp (std::log (tv) + c * (std::log (v) - std::log (tv)));
            else
                v = tv + c * (v - tv);
        }
    }

    const MsTypeData& type;
    const msfx::EffectInfo& info;
    std::unique_ptr<msfx::EffectProcessor> fx;
    double sampleRate = 48000.0, freePpq = 0.0;
    float trim = 1.0f, controlCoeff = 0.9f, mixSmoothed = 1.0f;
    msfx::EffectParams target, smoothed;
    std::vector<float> wetL, wetR;
};
} // namespace

void registerMoodShift (std::vector<EffectType>& out)
{
    for (const auto& t : msTypes())
    {
        const MsTypeData* tp = &t;
        out.push_back ({ &t.desc, [tp] { return std::make_shared<MoodShiftEffect> (*tp); } });
    }
}

} // namespace voxfx
