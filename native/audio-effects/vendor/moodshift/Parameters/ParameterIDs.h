#pragma once

#include <array>
#include <cstddef>

// Plain C++ parameter descriptors shared by the DSP engine, the JUCE parameter
// layout, the presets and the tests. Parameter IDs are part of the saved state
// and of host automation: never rename or renumber them.

namespace msfx
{
enum class AdvParam
{
    decay,
    feedback,
    cutoff,
    resonance,
    rate,
    delayTime,
    pitch,
    detune,
    diffusion,
    drive,
    density,
    voices,
    division,
    sync,
    shape,
    attack,
    release,
    freq,
    gate,
    count
};

constexpr int kNumAdvParams = int (AdvParam::count);

enum class ParamKind { continuous, integer, choice, toggle };
enum class Skew { linear, log };

struct ParamSpec
{
    const char* id;
    const char* name;
    const char* unit;      // shown after the value
    ParamKind kind;
    float minValue, maxValue, defaultValue;
    Skew skew;
    int decimals;
};

// --- Global / macro parameters ---------------------------------------------
namespace ids
{
    inline constexpr const char* effect = "effect";
    inline constexpr const char* amount = "amount";
    inline constexpr const char* mix = "mix";
    inline constexpr const char* tone = "tone";
    inline constexpr const char* movement = "movement";
    inline constexpr const char* width = "width";
    inline constexpr const char* inputGain = "inputGain";
    inline constexpr const char* outputGain = "outputGain";
    inline constexpr const char* bypass = "bypass";
}

/** Macro ranges. Amount/Mix/Movement in 0..1 (shown as %), Tone -1..1, Width 0..2 (shown 0..200 %). */
inline constexpr ParamSpec kAmountSpec   { ids::amount,   "Amount",   "%",  ParamKind::continuous, 0.0f, 1.0f, 0.7f, Skew::linear, 0 };
inline constexpr ParamSpec kMixSpec      { ids::mix,      "Mix",      "%",  ParamKind::continuous, 0.0f, 1.0f, 0.6f, Skew::linear, 0 };
inline constexpr ParamSpec kToneSpec     { ids::tone,     "Tone",     "",   ParamKind::continuous, -1.0f, 1.0f, 0.0f, Skew::linear, 0 };
inline constexpr ParamSpec kMovementSpec { ids::movement, "Movement", "%",  ParamKind::continuous, 0.0f, 1.0f, 0.5f, Skew::linear, 0 };
inline constexpr ParamSpec kWidthSpec    { ids::width,    "Width",    "%",  ParamKind::continuous, 0.0f, 2.0f, 1.0f, Skew::linear, 0 };
inline constexpr ParamSpec kInputSpec    { ids::inputGain,  "Input",  "dB", ParamKind::continuous, -24.0f, 12.0f, 0.0f, Skew::linear, 1 };
inline constexpr ParamSpec kOutputSpec   { ids::outputGain, "Output", "dB", ParamKind::continuous, -24.0f, 12.0f, 0.0f, Skew::linear, 1 };

// --- Advanced (effect-specific) parameters, absolute units ------------------
inline constexpr std::array<ParamSpec, kNumAdvParams> kAdvSpecs {{
    { "decay",     "Decay",      "s",   ParamKind::continuous, 0.1f,  20.0f,   2.5f,    Skew::log,    2 },
    { "feedback",  "Feedback",   "%",   ParamKind::continuous, 0.0f,  95.0f,   35.0f,   Skew::linear, 0 },
    { "cutoff",    "Cutoff",     "Hz",  ParamKind::continuous, 40.0f, 18000.0f, 2000.0f, Skew::log,   0 },
    { "resonance", "Resonance",  "%",   ParamKind::continuous, 0.0f,  100.0f,  30.0f,   Skew::linear, 0 },
    { "rate",      "Rate",       "Hz",  ParamKind::continuous, 0.02f, 20.0f,   1.0f,    Skew::log,    2 },
    { "time",      "Time",       "ms",  ParamKind::continuous, 1.0f,  2000.0f, 250.0f,  Skew::log,    0 },
    { "pitch",     "Pitch",      "st",  ParamKind::continuous, -24.0f, 24.0f,  0.0f,    Skew::linear, 1 },
    { "detune",    "Detune",     "ct",  ParamKind::continuous, 0.0f,  100.0f,  12.0f,   Skew::linear, 0 },
    { "diffusion", "Diffusion",  "%",   ParamKind::continuous, 0.0f,  100.0f,  60.0f,   Skew::linear, 0 },
    { "drive",     "Drive",      "dB",  ParamKind::continuous, 0.0f,  36.0f,   6.0f,    Skew::linear, 1 },
    { "density",   "Density",    "%",   ParamKind::continuous, 0.0f,  100.0f,  50.0f,   Skew::linear, 0 },
    { "voices",    "Voices",     "",    ParamKind::integer,    1.0f,  8.0f,    4.0f,    Skew::linear, 0 },
    { "division",  "Division",   "",    ParamKind::choice,     0.0f,  12.0f,   2.0f,    Skew::linear, 0 },
    { "sync",      "Tempo Sync", "",    ParamKind::toggle,     0.0f,  1.0f,    1.0f,    Skew::linear, 0 },
    { "shape",     "Shape",      "",    ParamKind::choice,     0.0f,  5.0f,    0.0f,    Skew::linear, 0 },
    { "attack",    "Attack",     "ms",  ParamKind::continuous, 0.1f,  200.0f,  10.0f,   Skew::log,    1 },
    { "release",   "Release",    "ms",  ParamKind::continuous, 10.0f, 2000.0f, 200.0f,  Skew::log,    0 },
    { "freq",      "Frequency",  "Hz",  ParamKind::continuous, 1.0f,  5000.0f, 120.0f,  Skew::log,    1 },
    { "gate",      "Gate",       "%",   ParamKind::continuous, 5.0f,  100.0f,  60.0f,   Skew::linear, 0 },
}};

inline constexpr const ParamSpec& advSpec (AdvParam p) { return kAdvSpecs[std::size_t (p)]; }

/** Note divisions for tempo sync, and their length in quarter-note beats. */
inline constexpr std::array<const char*, 13> kDivisionNames {
    "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/2 D", "1/4 D", "1/8 D", "1/16 D", "1/4 T", "1/8 T", "1/16 T" };
inline constexpr std::array<double, 13> kDivisionBeats {
    4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 3.0, 1.5, 0.75, 0.375, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };

inline constexpr std::array<const char*, 6> kShapeNames { "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Random" };
} // namespace msfx
