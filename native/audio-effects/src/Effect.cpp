#include "voxfx/EffectRegistry.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace voxfx
{
const char* categoryId (EffectCategory c) noexcept
{
    switch (c)
    {
        case EffectCategory::Pitch:     return "pitch";
        case EffectCategory::Dynamics:  return "dynamics";
        case EffectCategory::Space:     return "space";
        case EffectCategory::Creative:  return "creative";
        case EffectCategory::Chain:     return "chain";
        case EffectCategory::Mastering: return "mastering";
    }
    return "dynamics";
}

Effect::Effect (const EffectDescriptor& d)
    : desc (d), values (new std::atomic<float>[std::max<size_t> (1, d.params.size())])
{
    for (size_t i = 0; i < d.params.size(); ++i)
        values[i].store (d.params[i].def);
}

int Effect::paramIndex (const std::string& id) const noexcept
{
    for (size_t i = 0; i < desc.params.size(); ++i)
        if (desc.params[i].id == id)
            return (int) i;
    return -1;
}

float Effect::sanitise (const ParamInfo& info, float v) noexcept
{
    if (! std::isfinite (v))
        return info.def;
    v = std::clamp (v, info.min, info.max);
    if (info.type != ParamType::Continuous)
        v = std::round (v);
    return v;
}

void Effect::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= numParams())
        return;
    values[(size_t) index].store (sanitise (desc.params[(size_t) index], value), std::memory_order_relaxed);
}

float Effect::getParam (int index) const noexcept
{
    if (index < 0 || index >= numParams())
        return 0.0f;
    return values[(size_t) index].load (std::memory_order_relaxed);
}

std::string paramGroup (const EffectDescriptor& d, const ParamInfo& p)
{
    if (! p.group.empty()) return p.group;
    static const std::map<std::string, std::map<std::string, std::string>> table = {
        { "eq", { { "hpfOn", "Low" }, { "hpfFreq", "Low" }, { "lowFreq", "Low" }, { "lowGain", "Low" },
                  { "mid1Freq", "Mids" }, { "mid1Gain", "Mids" }, { "mid1Q", "Mids" }, { "mid2Freq", "Mids" }, { "mid2Gain", "Mids" }, { "mid2Q", "Mids" },
                  { "highFreq", "High" }, { "highGain", "High" }, { "output", "High" } } },
        { "compressor", { { "threshold", "Main" }, { "ratio", "Main" }, { "makeup", "Main" }, { "autoMakeup", "Main" }, { "mix", "Main" },
                          { "attack", "Timing" }, { "release", "Timing" }, { "knee", "Timing" } } },
        { "reverb", { { "type", "Main" }, { "decay", "Main" }, { "mix", "Main" }, { "width", "Main" },
                      { "predelay", "Tone" }, { "damping", "Tone" }, { "lowCut", "Tone" } } },
        { "delay", { { "sync", "Time" }, { "division", "Time" }, { "time", "Time" }, { "pingPong", "Time" },
                     { "feedback", "Main" }, { "mix", "Main" }, { "lowCut", "Main" }, { "highCut", "Main" } } },
        { "pitch_correction", { { "key", "Key" }, { "scale", "Key" }, { "reference", "Key" },
                                { "speed", "Tune" }, { "strength", "Tune" }, { "humanize", "Tune" }, { "mix", "Tune" } } },
        { "mastering", { { "inputGain", "Tone" }, { "eqOn", "Tone" }, { "lowGain", "Tone" }, { "midFreq", "Tone" }, { "midGain", "Tone" }, { "highGain", "Tone" },
                         { "compOn", "Glue" }, { "compThreshold", "Glue" }, { "compRatio", "Glue" }, { "compAttack", "Glue" }, { "compRelease", "Glue" },
                         { "width", "Finish" }, { "limiterOn", "Finish" }, { "ceiling", "Finish" } } },
    };
    if (auto t = table.find (d.typeId); t != table.end())
        if (auto g = t->second.find (p.id); g != t->second.end()) return g->second;
    if (d.typeId.rfind ("ms_", 0) == 0)   // MoodShift FX: 5 macros, then the effect's own controls
    {
        static const char* macros[] = { "amount", "mix", "tone", "movement", "width" };
        for (auto* m : macros) if (p.id == m) return "Main";
        return "Advanced";
    }
    return "Main";
}

//==============================================================================
EffectRegistry::EffectRegistry()
{
    registerVocalSweet (all);
    registerPitchCorrection (all);
    registerCoreEffects (all);
    registerMoodShift (all);
    registerMastering (all);
}

const EffectRegistry& EffectRegistry::instance()
{
    static const EffectRegistry registry;
    return registry;
}

const EffectType* EffectRegistry::find (const std::string& typeId) const noexcept
{
    for (const auto& t : all)
        if (t.descriptor->typeId == typeId)
            return &t;
    return nullptr;
}

EffectPtr EffectRegistry::create (const std::string& typeId) const
{
    if (const auto* t = find (typeId))
        return t->create();
    return nullptr;
}

} // namespace voxfx
