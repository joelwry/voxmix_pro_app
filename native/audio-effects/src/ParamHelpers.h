#pragma once
// Small helpers shared by the effect implementations (internal header).

#include "voxfx/Effect.h"
#include <algorithm>
#include <cmath>

namespace voxfx::detail
{
inline ParamInfo cont (const char* id, const char* name, float mn, float mx, float def, const char* unit = "", bool log = false)
{
    ParamInfo p;
    p.id = id; p.name = name; p.type = ParamType::Continuous;
    p.min = mn; p.max = mx; p.def = def; p.unit = unit; p.logScale = log;
    return p;
}

inline ParamInfo integer (const char* id, const char* name, float mn, float mx, float def, const char* unit = "")
{
    ParamInfo p = cont (id, name, mn, mx, def, unit);
    p.type = ParamType::Integer;
    return p;
}

inline ParamInfo toggle (const char* id, const char* name, bool def)
{
    ParamInfo p = cont (id, name, 0.0f, 1.0f, def ? 1.0f : 0.0f);
    p.type = ParamType::Toggle;
    return p;
}

inline ParamInfo choice (const char* id, const char* name, std::vector<std::string> choices, int def)
{
    ParamInfo p = cont (id, name, 0.0f, float (choices.size() - 1), float (def));
    p.type = ParamType::Choice;
    p.choices = std::move (choices);
    return p;
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

/** One-pole parameter smoother advanced once per processing block. */
struct BlockSmoother
{
    float value = 0.0f;
    bool init = false;
    float next (float target, int numFrames, double sampleRate, float timeMs = 30.0f) noexcept
    {
        if (! init) { value = target; init = true; return value; }
        const float a = std::exp (-float (numFrames) / std::max (1.0f, timeMs * 0.001f * float (sampleRate)));
        value = target + a * (value - target);
        if (std::abs (value - target) < 1.0e-6f * (1.0f + std::abs (target))) value = target;
        return value;
    }
    void reset() noexcept { init = false; }
};

/** Linear per-sample gain ramp across one block (removes zipper noise on gain changes). */
struct GainRamp
{
    float current = 1.0f;
    bool init = false;
    template <typename Fn>
    void apply (float target, int n, Fn&& fn) noexcept
    {
        if (! init) { current = target; init = true; }
        const float step = (target - current) / float (std::max (1, n));
        for (int i = 0; i < n; ++i)
        {
            current += step;
            fn (i, current);
        }
        current = target;
    }
    void reset() noexcept { init = false; }
};

/** Equal-power dry/wet gains for mix 0..1. */
inline void equalPowerMix (float mix, float& dry, float& wet) noexcept
{
    mix = std::clamp (mix, 0.0f, 1.0f);
    dry = std::cos (mix * 1.5707963f);
    wet = std::sin (mix * 1.5707963f);
}

inline float sanitizeSample (float x) noexcept
{
    if (! std::isfinite (x)) return 0.0f;
    return std::clamp (x, -64.0f, 64.0f);
}

} // namespace voxfx::detail
