#pragma once

#include "Filters.h"
#include <array>

namespace msfx::dsp
{
/** Parallel three-formant vowel filter (A-E-I-O-U) with continuous morphing.
    This is a formant-*inspired* filter (resonant band-passes placed on vowel
    formant frequencies), not a physical talkbox / vocal-tract model. */
class FormantBank
{
public:
    void prepare (double sr)
    {
        for (auto& f : filters) { f.prepare (sr); f.setMode (SVF::Mode::bandpassNormalized); }
        reset();
    }
    void reset() { for (auto& f : filters) f.reset(); }

    /** vowel: 0..4 (A, E, I, O, U, fractional = morph); scale multiplies all formant
        frequencies; sharpness scales the Q. */
    void setVowel (float vowel, float scale, float sharpness) noexcept
    {
        static constexpr float F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 290, 2250, 2890 }, { 520, 920, 2410 }, { 320, 800, 2240 } };
        static constexpr float G[5][3] = { { 1.0f, 0.50f, 0.25f }, { 1.0f, 0.40f, 0.28f }, { 1.0f, 0.30f, 0.25f }, { 1.0f, 0.55f, 0.15f }, { 1.0f, 0.35f, 0.10f } };
        static constexpr float BW[3] = { 90.0f, 110.0f, 160.0f };
        vowel = clamp (vowel, 0.0f, 4.0f);
        const int i0 = std::min (3, int (vowel));
        const float t = vowel - float (i0);
        for (int k = 0; k < 3; ++k)
        {
            // interpolate frequency in the log domain for a natural glide
            const float f = std::exp (lerp (std::log (F[i0][k]), std::log (F[i0 + 1][k]), t)) * scale;
            const float q = clamp (f / BW[k] * sharpness, 1.0f, 30.0f);
            filters[size_t (k)].setParams (f, q);
            gains[size_t (k)] = lerp (G[i0][k], G[i0 + 1][k], t);
        }
    }

    float process (float x) noexcept
    {
        float y = 0.0f;
        for (int k = 0; k < 3; ++k) y += filters[size_t (k)].process (x) * gains[size_t (k)];
        return y;
    }

private:
    std::array<SVF, 3> filters;
    std::array<float, 3> gains { 1.0f, 0.5f, 0.25f };
};
} // namespace msfx::dsp
