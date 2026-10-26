// "Vocal Sweetener": the complete VocalSweet chain (reused unchanged from the VocalSweet VST3
// repository, vendor/vocalsweet) exposed as one VoxMix effect. The plugin's host bypass
// parameter is dropped: bypass is handled per slot by the VoxMix effect chain.

#include "voxfx/EffectRegistry.h"
#include "../vendor/vocalsweet/VocalChain.h"
#include "../vendor/vocalsweet/Presets.h"
#include <algorithm>

namespace voxfx
{
namespace
{
const EffectDescriptor& vocalSweetDescriptor()
{
    static const EffectDescriptor d = []
    {
        EffectDescriptor e;
        e.typeId = "vocal_sweetener";
        e.name = "Vocal Sweetener";
        e.category = EffectCategory::Chain;
        e.origin = "VocalSweet";
        e.description = "One-knob vocal polish: HPF, corrective EQ, compressor, warm saturation, "
                        "presence/air, exciter, de-esser, doubler and output limiter.";
        for (int i = 0; i < vs::kNumParams; ++i)
        {
            const auto& def = vs::kParamDefs[(size_t) i];
            if (i == vs::pBypass)
                continue;
            ParamInfo p;
            p.id = def.id;
            p.name = def.name;
            p.min = def.min;
            p.max = def.max;
            p.def = def.def;
            p.unit = def.unit;
            p.logScale = def.centre > 0.0f && def.unit == std::string ("Hz");
            switch (def.kind)
            {
                case vs::ParamKind::Float:  p.type = ParamType::Continuous; break;
                case vs::ParamKind::Bool:   p.type = ParamType::Toggle; break;
                case vs::ParamKind::Choice: p.type = ParamType::Choice; p.choices = { "12 dB/oct", "24 dB/oct" }; break;
            }
            e.params.push_back (p);
        }
        return e;
    }();
    return d;
}

class VocalSweetEffect final : public Effect
{
public:
    VocalSweetEffect() : Effect (vocalSweetDescriptor())
    {
        // map our parameter order back to VocalSweet's ParamIndex
        for (int i = 0; i < numParams(); ++i)
            map[(size_t) i] = vs::findParamIndex (descriptor().params[(size_t) i].id.c_str());
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        pull();
        chain.prepare (sampleRate, maxBlockSize);
        chain.setParams (vs::computeEffective (values));
        chain.reset();
    }

    void reset() override { chain.reset(); }

    void process (float* left, float* right, int n, const ProcessInfo&) noexcept override
    {
        pull();
        chain.setParams (vs::computeEffective (values));
        // Mono vocal layers arrive as identical channels: process one and copy (half the CPU).
        // Not with the doubler, which creates a stereo image from the mono input.
        const bool dualMono = values[vs::pDoubleOn] < 0.5f && std::equal (left, left + n, right);
        float* ch[2] = { left, right };
        chain.process (ch, dualMono ? 1 : 2, n);
        if (dualMono) std::copy (left, left + n, right);
    }

    int latencySamples() const noexcept override { return chain.getLatencySamples(); }
    double tailSeconds() const noexcept override { return 0.1; }
    float gainReductionDb() const noexcept override { return chain.getMeters().compGrDb; }

private:
    void pull() noexcept
    {
        values = vs::defaultParamValues();
        for (int i = 0; i < numParams(); ++i)
            if (map[(size_t) i] >= 0)
                values[(size_t) map[(size_t) i]] = p (i);
        values[vs::pBypass] = 0.0f;
    }

    vs::dsp::VocalChain chain;
    vs::ParamValues values {};
    std::array<int, vs::kNumParams> map {};
};
} // namespace

void registerVocalSweet (std::vector<EffectType>& out)
{
    out.push_back ({ &vocalSweetDescriptor(), [] { return std::make_shared<VocalSweetEffect>(); } });
}

/** Factory presets of the VocalSweet plugin, exposed so preset chains can reuse them. */
bool vocalSweetPresetValues (const std::string& name, std::vector<std::pair<std::string, float>>& out)
{
    for (const auto& preset : vs::factoryPresets())
    {
        if (preset.name != name)
            continue;
        out.clear();
        for (int i = 0; i < vs::kNumParams; ++i)
            if (i != vs::pBypass)
                out.emplace_back (vs::kParamDefs[(size_t) i].id, preset.values[(size_t) i]);
        return true;
    }
    return false;
}

} // namespace voxfx
