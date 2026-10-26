#include "voxfx/EffectRegistry.h"
#include <algorithm>
#include <cmath>

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
