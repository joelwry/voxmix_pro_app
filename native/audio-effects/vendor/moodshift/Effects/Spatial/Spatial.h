#pragma once

#include "../Utility/DspMath.h"

namespace msfx::dsp
{
/** Mid/side width: 0 = mono, 1 = unchanged, 2 = extra wide. */
inline void applyWidth (float& l, float& r, float width) noexcept
{
    const float m = 0.5f * (l + r);
    const float s = 0.5f * (l - r) * clamp (width, 0.0f, 2.0f);
    l = m + s;
    r = m - s;
}

/** Places a mono signal in the stereo field (equal power). */
inline void panMono (float x, float pan, float& l, float& r) noexcept
{
    float gl, gr;
    panGains (pan, gl, gr);
    l = x * gl * 0.7071f;
    r = x * gr * 0.7071f;
}
} // namespace msfx::dsp
