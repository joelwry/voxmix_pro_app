#pragma once
// Factory presets. Each preset is the default parameter set plus explicit overrides, so every
// preset is a complete, deterministic parameter state. Framework independent.

#include "Params.h"
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace vs
{
struct Preset
{
    std::string name;
    ParamValues values;
};

inline Preset makePreset (const char* name, std::initializer_list<std::pair<ParamIndex, float>> overrides)
{
    Preset p { name, defaultParamValues() };
    for (const auto& [index, value] : overrides)
        p.values[(size_t) index] = clampParam (index, value);
    return p;
}

inline const std::vector<Preset>& factoryPresets()
{
    // clang-format off
    static const std::vector<Preset> presets {
        makePreset ("Default", {}),

        // Forward, bright and punchy lead that sits on top of busy percussion.
        makePreset ("Afrobeat Lead", {
            { pHpfFreq, 90 }, { pMudFreq, 300 }, { pMudGain, -2.5f }, { pBoxFreq, 800 }, { pBoxGain, -1.5f },
            { pPresFreq, 3800 }, { pPresGain, 2.0f }, { pAirFreq, 12000 }, { pAirGain, 2.5f },
            { pCompThresh, -18 }, { pCompRatio, 3.0f }, { pCompAttack, 6 }, { pCompRelease, 90 },
            { pDeessFreq, 7000 }, { pDeessThresh, -30 }, { pDeessRange, 7 },
            { pSatDrive, 20 }, { pExciteFreq, 5000 }, { pExciteAmount, 15 },
            { pSweeten, 55 }, { pTone, 15 }, { pWarmth, 25 }, { pPresence, 55 }, { pControl, 45 } }),

        // Smoother and warmer with an open, airy top - laid-back but present.
        makePreset ("Afrofusion Lead", {
            { pHpfFreq, 85 }, { pMudFreq, 260 }, { pMudGain, -2.0f }, { pBoxFreq, 700 }, { pBoxGain, -1.0f },
            { pPresFreq, 3200 }, { pPresGain, 1.0f }, { pAirFreq, 13000 }, { pAirGain, 3.0f },
            { pCompThresh, -20 }, { pCompRatio, 2.8f }, { pCompAttack, 10 }, { pCompRelease, 140 },
            { pDeessFreq, 6500 }, { pDeessThresh, -30 }, { pDeessRange, 8 },
            { pSatDrive, 25 }, { pExciteFreq, 4500 }, { pExciteAmount, 10 },
            { pSweeten, 50 }, { pTone, 5 }, { pWarmth, 45 }, { pPresence, 40 }, { pControl, 40 } }),

        // Close, soft, rounded - for ballads and quiet verses.
        makePreset ("Warm Intimate Vocal", {
            { pHpfFreq, 70 }, { pMudFreq, 250 }, { pMudGain, -1.0f }, { pBoxFreq, 900 }, { pBoxGain, -2.0f },
            { pPresFreq, 3000 }, { pPresGain, 0.0f }, { pAirFreq, 10000 }, { pAirGain, 1.0f },
            { pCompThresh, -22 }, { pCompRatio, 2.2f }, { pCompAttack, 15 }, { pCompRelease, 180 },
            { pDeessFreq, 6000 }, { pDeessThresh, -32 }, { pDeessRange, 6 },
            { pSatDrive, 30 }, { pExciteAmount, 5 },
            { pSweeten, 35 }, { pTone, -25 }, { pWarmth, 70 }, { pPresence, 20 }, { pControl, 35 } }),

        // Tight low end, glossy top, firm levelling.
        makePreset ("Bright Pop Vocal", {
            { pHpfFreq, 100 }, { pHpfSlope, 1 }, { pMudFreq, 320 }, { pMudGain, -3.0f }, { pBoxFreq, 850 }, { pBoxGain, -2.0f },
            { pPresFreq, 4000 }, { pPresGain, 3.0f }, { pAirFreq, 12000 }, { pAirGain, 4.0f },
            { pCompThresh, -18 }, { pCompRatio, 3.5f }, { pCompAttack, 5 }, { pCompRelease, 80 },
            { pDeessFreq, 7500 }, { pDeessThresh, -34 }, { pDeessRange, 9 },
            { pSatDrive, 15 }, { pExciteFreq, 6000 }, { pExciteAmount, 25 },
            { pSweeten, 65 }, { pTone, 35 }, { pWarmth, 15 }, { pPresence, 65 }, { pControl, 60 } }),

        // Dense, upfront and controlled with some grit for rhythmic delivery.
        makePreset ("Melodic Rap", {
            { pHpfFreq, 95 }, { pMudFreq, 300 }, { pMudGain, -2.5f }, { pBoxFreq, 1000 }, { pBoxGain, -2.5f },
            { pPresFreq, 3000 }, { pPresGain, 2.5f }, { pAirFreq, 10000 }, { pAirGain, 1.5f },
            { pCompThresh, -18 }, { pCompRatio, 4.0f }, { pCompAttack, 3 }, { pCompRelease, 70 },
            { pDeessFreq, 6500 }, { pDeessThresh, -30 }, { pDeessRange, 7 },
            { pSatDrive, 35 }, { pExciteFreq, 4000 }, { pExciteAmount, 15 },
            { pSweeten, 55 }, { pTone, 10 }, { pWarmth, 40 }, { pPresence, 50 }, { pControl, 70 } }),

        // Thin, soft, wide and heavily controlled so stacks sit behind the lead.
        makePreset ("Smooth Background Vocal", {
            { pHpfFreq, 130 }, { pHpfSlope, 1 }, { pMudFreq, 300 }, { pMudGain, -4.0f }, { pBoxFreq, 800 }, { pBoxGain, -2.0f },
            { pPresFreq, 3500 }, { pPresGain, -2.0f }, { pAirFreq, 11000 }, { pAirGain, 2.0f },
            { pCompThresh, -22 }, { pCompRatio, 3.5f }, { pCompAttack, 12 }, { pCompRelease, 200 },
            { pDeessFreq, 6000 }, { pDeessThresh, -36 }, { pDeessRange, 10 },
            { pSatDrive, 20 }, { pExciteAmount, 5 },
            { pDoubleOn, 1 }, { pDoubleAmount, 35 }, { pDoubleWidth, 85 },
            { pOutputGain, -2 },
            { pSweeten, 40 }, { pTone, -15 }, { pWarmth, 40 }, { pPresence, 10 }, { pControl, 65 } }),

        // Two detuned, delayed voices spread left/right for an instant double-track.
        makePreset ("Vocal Double", {
            { pHpfFreq, 120 }, { pMudFreq, 280 }, { pMudGain, -3.0f }, { pBoxFreq, 750 }, { pBoxGain, -1.5f },
            { pPresGain, 1.0f }, { pAirGain, 2.0f },
            { pCompThresh, -20 }, { pCompRatio, 3.0f }, { pCompAttack, 8 }, { pCompRelease, 120 },
            { pDeessThresh, -32 }, { pDeessRange, 8 },
            { pSatDrive, 20 }, { pExciteAmount, 10 },
            { pDoubleOn, 1 }, { pDoubleAmount, 70 }, { pDoubleWidth, 100 },
            { pOutputGain, -1.5f },
            { pSweeten, 45 }, { pTone, 0 }, { pWarmth, 35 }, { pPresence, 35 }, { pControl, 50 } }),

        // Gentle clean-up only: rumble removal, light levelling, light de-essing.
        makePreset ("Natural Clean Vocal", {
            { pHpfFreq, 75 }, { pMudFreq, 280 }, { pMudGain, -1.0f }, { pBoxGain, -0.5f },
            { pPresGain, 0.5f }, { pAirGain, 1.0f },
            { pCompThresh, -14 }, { pCompRatio, 2.0f }, { pCompAttack, 12 }, { pCompRelease, 150 },
            { pDeessThresh, -26 }, { pDeessRange, 5 },
            { pSatDrive, 0 }, { pExciteAmount, 0 },
            { pSweeten, 20 }, { pTone, 0 }, { pWarmth, 10 }, { pPresence, 15 }, { pControl, 20 } }),
    };
    // clang-format on
    return presets;
}

} // namespace vs
