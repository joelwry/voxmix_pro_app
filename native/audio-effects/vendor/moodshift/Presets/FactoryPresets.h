#pragma once

#include "../Effects/EffectChain.h"
#include <utility>
#include <vector>

namespace msfx
{
/** One factory preset: an effect plus a complete parameter configuration.
    Advanced parameters not listed keep their documented defaults. */
struct FactoryPreset
{
    const char* effectId;
    float amount, mix, tone, movement, width;
    std::vector<std::pair<AdvParam, float>> adv;
};

/** The 40 factory presets, in the same order as effectRegistry(). */
const std::vector<FactoryPreset>& factoryPresets();

/** Expands a factory preset into a full engine parameter set (gains 0 dB, not bypassed). */
EngineParams toEngineParams (const FactoryPreset& preset);

/** Default value of every advanced parameter. */
std::array<float, kNumAdvParams> defaultAdvValues();
} // namespace msfx
