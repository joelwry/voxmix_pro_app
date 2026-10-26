#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace msfx::dsp
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr double kPiD = 3.14159265358979323846;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -120.0f) noexcept
{
    return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb;
}

template <typename T>
inline T clamp (T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

/** Maps 0..1 onto a logarithmic range [lo, hi]. */
inline float expMap (float t, float lo, float hi) noexcept { return lo * std::pow (hi / lo, clamp (t, 0.0f, 1.0f)); }

/** Multiplies a frequency by 2^(octaves) and keeps it inside an audible, stable range. */
inline float shiftOctaves (float hz, float octaves, float sampleRate) noexcept
{
    return clamp (hz * std::exp2 (octaves), 20.0f, 0.45f * sampleRate);
}

/** Replaces NaN/Inf with zero and flushes denormals. */
inline float sanitize (float x) noexcept
{
    if (! std::isfinite (x)) return 0.0f;
    return std::abs (x) < 1.0e-20f ? 0.0f : x;
}

/** Cheap rational tanh approximation, accurate to ~1e-3 and bounded to +-1. */
inline float fastTanh (float x) noexcept
{
    x = clamp (x, -4.97f, 4.97f);
    const float x2 = x * x;
    const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return clamp (a / b, -1.0f, 1.0f);
}

/** Smooth soft clipper with unity slope at the origin; output bounded to +-ceiling. */
inline float softClip (float x, float ceiling = 1.0f) noexcept
{
    return ceiling * fastTanh (x / ceiling);
}

/** Deterministic, allocation-free PRNG (xorshift32). */
class Random
{
public:
    explicit Random (uint32_t seed = 0x9E3779B9u) noexcept { setSeed (seed); }
    void setSeed (uint32_t seed) noexcept { state = seed != 0 ? seed : 0x9E3779B9u; }
    uint32_t nextUInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return float (nextUInt() >> 8) * (1.0f / 16777216.0f); }
    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

private:
    uint32_t state;
};

/** Stateless hash used for position-locked (repeatable) random decisions. */
inline uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline float hashToUnit (uint32_t x) noexcept { return float (hash32 (x) >> 8) * (1.0f / 16777216.0f); }

/** Equal-power pan gains for p in [-1, 1]. */
inline void panGains (float p, float& l, float& r) noexcept
{
    const float a = (clamp (p, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    l = std::cos (a) * 1.41421356f;
    r = std::sin (a) * 1.41421356f;
}
} // namespace msfx::dsp
