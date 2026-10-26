#pragma once
// Single source of truth for every VocalSweet parameter (ID, range, default) plus the musical
// macro mapping. Framework independent so the DSP tests, the plugin and the UI all share it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace vs
{
enum class ParamKind { Float, Bool, Choice };

struct ParamDef
{
    const char* id;
    const char* name;
    ParamKind kind;
    float min, max, def;
    float centre;       // skew centre for Float (0 = linear)
    const char* unit;
    const char* group;
};

// clang-format off
enum ParamIndex : int
{
    pInputGain, pHpfOn, pHpfFreq, pHpfSlope,
    pEqOn, pMudFreq, pMudGain, pBoxFreq, pBoxGain, pPresFreq, pPresGain, pAirFreq, pAirGain,
    pCompOn, pCompThresh, pCompRatio, pCompAttack, pCompRelease, pCompMakeup,
    pDeessOn, pDeessFreq, pDeessThresh, pDeessRange,
    pSatOn, pSatDrive,
    pExciteOn, pExciteFreq, pExciteAmount,
    pDoubleOn, pDoubleAmount, pDoubleWidth,
    pOutputGain, pLimiterOn, pCeiling, pMix, pBypass,
    pSweeten, pTone, pWarmth, pPresence, pControl,
    kNumParams
};

inline constexpr std::array<ParamDef, kNumParams> kParamDefs {{
    { "inputGain",   "Input Gain",        ParamKind::Float,  -24.0f,  24.0f,   0.0f,    0.0f, "dB", "IO" },
    { "hpfOn",       "HPF On",            ParamKind::Bool,     0.0f,   1.0f,   1.0f,    0.0f, "",   "HPF" },
    { "hpfFreq",     "HPF Frequency",     ParamKind::Float,   20.0f, 400.0f,  80.0f,  100.0f, "Hz", "HPF" },
    { "hpfSlope",    "HPF Slope",         ParamKind::Choice,   0.0f,   1.0f,   0.0f,    0.0f, "",   "HPF" },   // 0 = 12 dB/oct, 1 = 24 dB/oct
    { "eqOn",        "EQ On",             ParamKind::Bool,     0.0f,   1.0f,   1.0f,    0.0f, "",   "EQ" },
    { "mudFreq",     "Mud Frequency",     ParamKind::Float,  120.0f, 600.0f, 280.0f,  280.0f, "Hz", "EQ" },
    { "mudGain",     "Mud Gain",          ParamKind::Float,  -12.0f,   6.0f,  -1.5f,    0.0f, "dB", "EQ" },
    { "boxFreq",     "Box Frequency",     ParamKind::Float,  400.0f,1800.0f, 750.0f,  800.0f, "Hz", "EQ" },
    { "boxGain",     "Box Gain",          ParamKind::Float,  -12.0f,   6.0f,  -1.0f,    0.0f, "dB", "EQ" },
    { "presFreq",    "Presence Frequency",ParamKind::Float, 1500.0f,7000.0f,3500.0f, 3200.0f, "Hz", "EQ" },
    { "presGain",    "Presence Gain",     ParamKind::Float,   -6.0f,  12.0f,   1.0f,    0.0f, "dB", "EQ" },
    { "airFreq",     "Air Frequency",     ParamKind::Float, 6000.0f,18000.0f,11000.0f,10500.0f,"Hz","EQ" },
    { "airGain",     "Air Gain",          ParamKind::Float,   -6.0f,  12.0f,   1.5f,    0.0f, "dB", "EQ" },
    { "compOn",      "Compressor On",     ParamKind::Bool,     0.0f,   1.0f,   1.0f,    0.0f, "",   "COMP" },
    { "compThresh",  "Comp Threshold",    ParamKind::Float,  -60.0f,   0.0f, -16.0f,    0.0f, "dB", "COMP" },
    { "compRatio",   "Comp Ratio",        ParamKind::Float,    1.0f,  20.0f,   2.5f,    4.0f, ":1", "COMP" },
    { "compAttack",  "Comp Attack",       ParamKind::Float,    0.1f, 100.0f,   8.0f,   10.0f, "ms", "COMP" },
    { "compRelease", "Comp Release",      ParamKind::Float,   10.0f,1000.0f, 120.0f,  150.0f, "ms", "COMP" },
    { "compMakeup",  "Comp Makeup",       ParamKind::Float,   -6.0f,  18.0f,   0.0f,    0.0f, "dB", "COMP" },
    { "deessOn",     "De-Esser On",       ParamKind::Bool,     0.0f,   1.0f,   1.0f,    0.0f, "",   "DEESS" },
    { "deessFreq",   "De-Ess Frequency",  ParamKind::Float, 3000.0f,12000.0f,6500.0f, 6500.0f, "Hz", "DEESS" },
    { "deessThresh", "De-Ess Threshold",  ParamKind::Float,  -60.0f,   0.0f, -28.0f,    0.0f, "dB", "DEESS" },
    { "deessRange",  "De-Ess Range",      ParamKind::Float,    0.0f,  18.0f,   6.0f,    0.0f, "dB", "DEESS" },
    { "satOn",       "Saturation On",     ParamKind::Bool,     0.0f,   1.0f,   1.0f,    0.0f, "",   "SAT" },
    { "satDrive",    "Saturation Drive",  ParamKind::Float,    0.0f, 100.0f,  15.0f,    0.0f, "%",  "SAT" },
    { "exciteOn",    "Exciter On",        ParamKind::Bool,     0.0f,   1.0f,   1.0f,    0.0f, "",   "EXCITE" },
    { "exciteFreq",  "Exciter Frequency", ParamKind::Float, 2000.0f,10000.0f,4500.0f, 4500.0f, "Hz", "EXCITE" },
    { "exciteAmount","Exciter Amount",    ParamKind::Float,    0.0f, 100.0f,  10.0f,    0.0f, "%",  "EXCITE" },
    { "doubleOn",    "Doubler On",        ParamKind::Bool,     0.0f,   1.0f,   0.0f,    0.0f, "",   "DOUBLE" },
    { "doubleAmount","Doubler Amount",    ParamKind::Float,    0.0f, 100.0f,  40.0f,    0.0f, "%",  "DOUBLE" },
    { "doubleWidth", "Doubler Width",     ParamKind::Float,    0.0f, 100.0f,  70.0f,    0.0f, "%",  "DOUBLE" },
    { "outputGain",  "Output Gain",       ParamKind::Float,  -24.0f,  24.0f,   0.0f,    0.0f, "dB", "IO" },
    { "limiterOn",   "Limiter On",        ParamKind::Bool,     0.0f,   1.0f,   1.0f,    0.0f, "",   "IO" },
    { "ceiling",     "Output Ceiling",    ParamKind::Float,  -12.0f,   0.0f,  -1.0f,    0.0f, "dB", "IO" },
    { "mix",         "Dry/Wet",           ParamKind::Float,    0.0f, 100.0f, 100.0f,    0.0f, "%",  "IO" },
    { "bypass",      "Bypass",            ParamKind::Bool,     0.0f,   1.0f,   0.0f,    0.0f, "",   "IO" },
    { "sweeten",     "Sweeten",           ParamKind::Float,    0.0f, 100.0f,  40.0f,    0.0f, "%",  "MACRO" },
    { "tone",        "Tone",              ParamKind::Float, -100.0f, 100.0f,   0.0f,    0.0f, "",   "MACRO" },
    { "warmth",      "Warmth",            ParamKind::Float,    0.0f, 100.0f,  30.0f,    0.0f, "%",  "MACRO" },
    { "presence",    "Presence",          ParamKind::Float,    0.0f, 100.0f,  30.0f,    0.0f, "%",  "MACRO" },
    { "control",     "Control",           ParamKind::Float,    0.0f, 100.0f,  30.0f,    0.0f, "%",  "MACRO" },
}};
// clang-format on

inline int findParamIndex (const char* id) noexcept
{
    for (int i = 0; i < kNumParams; ++i)
        if (std::strcmp (kParamDefs[(size_t) i].id, id) == 0)
            return i;
    return -1;
}

using ParamValues = std::array<float, kNumParams>;

inline ParamValues defaultParamValues() noexcept
{
    ParamValues v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = kParamDefs[(size_t) i].def;
    return v;
}

inline float clampParam (int index, float value) noexcept
{
    const auto& d = kParamDefs[(size_t) index];
    if (! std::isfinite (value)) return d.def;
    value = std::clamp (value, d.min, d.max);
    if (d.kind != ParamKind::Float) value = std::round (value);
    return value;
}

//==============================================================================
/** Values actually used by the DSP after the macro controls have been applied. */
struct EffectiveParams
{
    float inputGainDb = 0;
    bool hpfOn = true; float hpfFreq = 80; bool hpf24 = false;
    bool eqOn = true;
    float mudFreq = 280, mudGain = 0, boxFreq = 750, boxGain = 0;
    float presFreq = 3500, presGain = 0, airFreq = 11000, airGain = 0;
    float bodyFreq = 200, bodyGain = 0;      // tone/warmth low shelf
    float tiltHiFreq = 3000, tiltHiGain = 0; // tone high shelf
    bool compOn = true; float compThresh = -16, compRatio = 2.5f, compAttack = 8, compRelease = 120, compMakeup = 0;
    bool deessOn = true; float deessFreq = 6500, deessThresh = -28, deessRange = 6;
    bool satOn = true; float satDrive = 0.15f, satBias = 0.1f;
    bool exciteOn = true; float exciteFreq = 4500, exciteAmount = 0.1f;
    bool doubleOn = false; float doubleAmount = 0.4f, doubleWidth = 0.7f;
    float outputGainDb = 0; bool limiterOn = true; float ceilingDb = -1;
    float mix = 1; bool bypass = false;
};

/** Static compressor curve (6 dB soft knee), dB of gain reduction for a given level. */
inline float staticGainReductionDb (float levelDb, float thresholdDb, float ratio) noexcept
{
    const float slope = 1.0f - 1.0f / std::max (ratio, 1.0f), knee = 6.0f, over = levelDb - thresholdDb;
    if (2.0f * over <= -knee) return 0.0f;
    if (2.0f * over >= knee) return slope * over;
    const float x = over + knee * 0.5f;
    return slope * x * x / (2.0f * knee);
}

/** The macro layer. Detailed parameters are the user's "base" settings; the macros add musically
    coordinated offsets on top of them. With all macros at 0 (Tone centred) the effective values
    equal the detailed values exactly, so the behaviour is predictable and fully automatable.

    SWEETEN  : overall polish - cleans mud/box, adds presence & air, a little glue, harmonics and
               de-essing so the extra brightness never turns harsh.
    TONE     : dark <-> bright tilt (low shelf / high shelf in opposite directions).
    WARMTH   : low-mid body, asymmetric (even-harmonic) saturation, slightly softer top.
    PRESENCE : forwardness - 3-5 kHz lift, exciter, matched extra de-essing.
    CONTROL  : levelling - lower threshold, higher ratio, extra de-essing.
    Compressor makeup is partly automatic (see below) so macro moves stay near loudness-neutral. */
inline EffectiveParams computeEffective (const ParamValues& v) noexcept
{
    auto P = [&v] (int i) { return v[(size_t) i]; };
    EffectiveParams e;

    const float s = P (pSweeten) * 0.01f;
    const float t = P (pTone) * 0.01f;
    const float w = P (pWarmth) * 0.01f;
    const float p = P (pPresence) * 0.01f;
    const float c = P (pControl) * 0.01f;

    e.inputGainDb = P (pInputGain);

    e.hpfOn = P (pHpfOn) > 0.5f;
    e.hpfFreq = std::clamp (P (pHpfFreq) * (1.0f + 0.2f * s), 20.0f, 500.0f);
    e.hpf24 = P (pHpfSlope) > 0.5f;

    e.eqOn = P (pEqOn) > 0.5f;
    e.mudFreq = P (pMudFreq);
    e.mudGain = std::clamp (P (pMudGain) - 3.0f * s, -18.0f, 12.0f);
    e.boxFreq = P (pBoxFreq);
    e.boxGain = std::clamp (P (pBoxGain) - 2.0f * s, -18.0f, 12.0f);
    e.presFreq = P (pPresFreq);
    e.presGain = std::clamp (P (pPresGain) + 2.0f * s + 4.0f * p, -12.0f, 15.0f);
    e.airFreq = P (pAirFreq);
    e.airGain = std::clamp (P (pAirGain) + 3.0f * s + 1.5f * p - 1.0f * w, -12.0f, 15.0f);
    e.bodyFreq = 200.0f;
    e.bodyGain = -3.0f * t + 2.5f * w;
    e.tiltHiFreq = 3000.0f;
    e.tiltHiGain = 4.0f * t - 0.75f * w;

    e.compOn = P (pCompOn) > 0.5f;
    const float threshOffset = 4.0f * s + 10.0f * c;
    e.compThresh = std::clamp (P (pCompThresh) - threshOffset, -70.0f, 0.0f);
    e.compRatio = std::clamp (P (pCompRatio) * (1.0f + 0.25f * s + 0.6f * c), 1.0f, 20.0f);
    e.compAttack = P (pCompAttack);
    e.compRelease = P (pCompRelease);
    // Automatic makeup: 80% of the static gain reduction a typical vocal peak (-14 dBFS) receives,
    // so presets and macro moves stay close to loudness-neutral. compMakeup adds on top of it.
    e.compMakeup = std::clamp (P (pCompMakeup) + 0.8f * staticGainReductionDb (-14.0f, e.compThresh, e.compRatio), -6.0f, 30.0f);

    e.deessOn = P (pDeessOn) > 0.5f;
    e.deessFreq = P (pDeessFreq);
    e.deessThresh = std::clamp (P (pDeessThresh) - 4.0f * s - 5.0f * p - 3.0f * c, -70.0f, 0.0f);
    e.deessRange = std::clamp (P (pDeessRange) + 3.0f * s + 3.0f * p, 0.0f, 24.0f);

    e.satOn = P (pSatOn) > 0.5f;
    e.satDrive = std::clamp ((P (pSatDrive) + 20.0f * s + 45.0f * w) * 0.01f, 0.0f, 1.0f);
    e.satBias = 0.08f + 0.27f * w;

    e.exciteOn = P (pExciteOn) > 0.5f;
    e.exciteFreq = P (pExciteFreq);
    e.exciteAmount = std::clamp ((P (pExciteAmount) + 15.0f * s + 35.0f * p) * 0.01f, 0.0f, 1.0f);

    e.doubleOn = P (pDoubleOn) > 0.5f;
    e.doubleAmount = P (pDoubleAmount) * 0.01f;
    e.doubleWidth = P (pDoubleWidth) * 0.01f;

    e.outputGainDb = P (pOutputGain);
    e.limiterOn = P (pLimiterOn) > 0.5f;
    e.ceilingDb = P (pCeiling);
    e.mix = P (pMix) * 0.01f;
    e.bypass = P (pBypass) > 0.5f;
    return e;
}

} // namespace vs
