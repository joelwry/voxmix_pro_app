#pragma once
// Catalogue of every effect type the engine can instantiate.
// Adding an effect = one descriptor + one factory function registered in EffectRegistry.cpp.

#include "Effect.h"
#include <functional>

namespace voxfx
{
struct EffectType
{
    const EffectDescriptor* descriptor;
    std::function<EffectPtr()> create;
};

class EffectRegistry
{
public:
    static const EffectRegistry& instance();

    const std::vector<EffectType>& types() const noexcept { return all; }
    const EffectType* find (const std::string& typeId) const noexcept;

    /** Creates an unprepared instance, or nullptr for an unknown type id. Control thread only. */
    EffectPtr create (const std::string& typeId) const;

private:
    EffectRegistry();
    std::vector<EffectType> all;
};

// Registration hooks implemented by the effect modules.
void registerCoreEffects (std::vector<EffectType>&);       // EQ, dynamics, space (VocalSweet / MoodShift modules)
void registerVocalSweet (std::vector<EffectType>&);        // complete VocalSweet chain
void registerPitchCorrection (std::vector<EffectType>&);   // pitch correction
void registerMoodShift (std::vector<EffectType>&);         // 40 MoodShift FX creative effects
void registerMastering (std::vector<EffectType>&);         // mastering chain

/** Parameter values of a VocalSweet factory preset ("Afrobeat Lead", ...) for the
    "vocal_sweetener" effect. Returns false for an unknown name. */
bool vocalSweetPresetValues (const std::string& presetName, std::vector<std::pair<std::string, float>>& out);

} // namespace voxfx
