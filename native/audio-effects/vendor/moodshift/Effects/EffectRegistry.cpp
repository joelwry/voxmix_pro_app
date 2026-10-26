#include "EffectRegistry.h"
#include <cstring>

namespace msfx
{
const char* categoryName (Category c) noexcept
{
    switch (c)
    {
        case Category::underwaterDark:     return "Underwater & Dark";
        case Category::movingRhythmic:     return "Moving & Rhythmic";
        case Category::dreamyAtmospheric:  return "Dreamy & Atmospheric";
        case Category::glitchExperimental: return "Glitch & Experimental";
        case Category::vintageCharacter:   return "Vintage & Character";
        case Category::echoSpace:          return "Echo & Space";
        case Category::vocalTransform:     return "Vocal & Instrument";
        case Category::extremePerformance: return "Extreme & Performance";
        case Category::count:              break;
    }
    return "";
}

static std::vector<EffectInfo> buildRegistry()
{
    std::vector<EffectInfo> r;
    r.reserve (kNumEffects);
    library::addUnderwaterDark (r);
    library::addMovingRhythmic (r);
    library::addDreamyAtmospheric (r);
    library::addGlitchExperimental (r);
    library::addVintageCharacter (r);
    library::addEchoSpace (r);
    library::addVocalTransform (r);
    library::addExtremePerformance (r);
    return r;
}

const std::vector<EffectInfo>& effectRegistry()
{
    static const std::vector<EffectInfo> registry = buildRegistry();
    return registry;
}

int findEffectIndex (const char* id) noexcept
{
    const auto& r = effectRegistry();
    for (size_t i = 0; i < r.size(); ++i)
        if (std::strcmp (r[i].id, id) == 0)
            return int (i);
    return -1;
}
} // namespace msfx
