#pragma once

#include "EffectProcessor.h"
#include <array>
#include <memory>
#include <vector>

namespace msfx
{
enum class Category
{
    underwaterDark,
    movingRhythmic,
    dreamyAtmospheric,
    glitchExperimental,
    vintageCharacter,
    echoSpace,
    vocalTransform,
    extremePerformance,
    count
};

constexpr int kNumCategories = int (Category::count);
const char* categoryName (Category c) noexcept;

enum class MixLaw
{
    linear,      // wet is correlated with dry (filters, phasers, saturation, layers)
    equalPower   // wet is largely decorrelated (reverbs, long delays, reverse)
};

/** One advanced control exposed for an effect, with an optional effect-specific label. */
struct AdvControl
{
    AdvParam param;
    const char* label = nullptr; // nullptr = use the parameter's generic name
};

/** What each macro does for a given effect (shown as tooltips / captions).
    A null `width` means the effect has no meaningful stereo parameter. */
struct MacroRoles
{
    const char* amount;
    const char* tone;
    const char* movement;
    const char* width;
};

struct EffectInfo
{
    const char* id;            // stable identifier, saved in presets
    const char* name;
    Category category;
    const char* description;
    MacroRoles roles;
    std::vector<AdvControl> controls;
    MixLaw mixLaw;
    float trimDb;              // static output trim used to roughly level-match effects
    std::unique_ptr<EffectProcessor> (*create)();
};

constexpr int kNumEffects = 40;

/** The 40 factory effects in catalogue order (index == effect parameter value). */
const std::vector<EffectInfo>& effectRegistry();
int findEffectIndex (const char* id) noexcept;

// Category builders (implemented in Library/*.cpp)
namespace library
{
    void addUnderwaterDark (std::vector<EffectInfo>&);
    void addMovingRhythmic (std::vector<EffectInfo>&);
    void addDreamyAtmospheric (std::vector<EffectInfo>&);
    void addGlitchExperimental (std::vector<EffectInfo>&);
    void addVintageCharacter (std::vector<EffectInfo>&);
    void addEchoSpace (std::vector<EffectInfo>&);
    void addVocalTransform (std::vector<EffectInfo>&);
    void addExtremePerformance (std::vector<EffectInfo>&);
}
} // namespace msfx
