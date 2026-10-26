#include "FactoryPresets.h"

namespace msfx
{
namespace
{
using A = AdvParam;

// Division indices (see kDivisionNames)
constexpr float d1_1 = 0, d1_2 = 1, d1_4 = 2, d1_8 = 3, d1_16 = 4, d1_4D = 7, d1_8D = 8;
constexpr float on = 1.0f, off = 0.0f;
// Shapes
constexpr float sine = 0, tri = 1, square = 2, random = 5;

std::vector<FactoryPreset> build()
{
    return {
        // id                    amount mix   tone   move  width
        // --- A: Underwater & Dark --------------------------------------------------------------
        { "underwater",          0.65f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::cutoff, 1400 }, { A::resonance, 55 }, { A::rate, 0.6f }, { A::sync, off }, { A::division, d1_4 }, { A::delayTime, 18 }, { A::feedback, 25 } } },
        { "deep_sea",            0.70f, 0.90f,  0.0f, 0.50f, 1.0f, { { A::cutoff, 900 }, { A::decay, 5.0f }, { A::diffusion, 80 }, { A::rate, 0.12f }, { A::sync, off }, { A::division, d1_1 } } },
        { "telephone_ghost",     0.60f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::cutoff, 1500 }, { A::drive, 9 }, { A::delayTime, 70 }, { A::feedback, 25 }, { A::rate, 0.4f }, { A::sync, off } } },
        { "dark_tunnel",         0.55f, 0.60f,  0.0f, 0.50f, 1.0f, { { A::delayTime, 230 }, { A::sync, off }, { A::division, d1_8 }, { A::feedback, 55 }, { A::cutoff, 2200 }, { A::decay, 3.5f }, { A::rate, 0.25f } } },
        { "drowning",            0.70f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::cutoff, 4000 }, { A::resonance, 35 }, { A::sync, on }, { A::division, d1_1 }, { A::rate, 0.25f }, { A::decay, 3.0f } } },
        // --- B: Moving & Rhythmic --------------------------------------------------------------
        { "wah_vocal",           0.70f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::rate, 1.2f }, { A::sync, on }, { A::division, d1_2 }, { A::cutoff, 900 }, { A::resonance, 50 }, { A::shape, sine } } },
        { "auto_wah",            0.70f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::attack, 6 }, { A::release, 180 }, { A::cutoff, 300 }, { A::resonance, 55 } } },
        { "talkbox_motion",      0.70f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::rate, 0.8f }, { A::sync, on }, { A::division, d1_2 }, { A::resonance, 40 }, { A::drive, 12 }, { A::shape, tri } } },
        { "liquid_phaser",       0.70f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::rate, 0.25f }, { A::sync, off }, { A::division, d1_1 }, { A::feedback, 55 }, { A::cutoff, 1000 } } },
        { "rhythm_pulse",        0.85f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::division, d1_16 }, { A::shape, square }, { A::cutoff, 900 }, { A::sync, on }, { A::rate, 4 } } },
        // --- C: Dreamy & Atmospheric -----------------------------------------------------------
        { "dreamscape",          0.60f, 0.45f,  0.0f, 0.50f, 1.0f, { { A::decay, 4.5f }, { A::diffusion, 80 }, { A::cutoff, 7000 }, { A::rate, 0.4f }, { A::delayTime, 30 } } },
        { "angel_dust",          0.60f, 0.45f,  0.0f, 0.50f, 1.0f, { { A::decay, 6.0f }, { A::pitch, 12 }, { A::feedback, 55 }, { A::cutoff, 8000 }, { A::diffusion, 85 } } },
        { "cloud_nine",          0.60f, 0.50f,  0.0f, 0.50f, 1.0f, { { A::decay, 7.0f }, { A::diffusion, 90 }, { A::cutoff, 5000 }, { A::rate, 0.07f }, { A::sync, off }, { A::division, d1_1 } } },
        { "reverse_dream",       0.55f, 0.60f,  0.0f, 0.50f, 1.0f, { { A::delayTime, 600 }, { A::sync, on }, { A::division, d1_2 }, { A::decay, 3.5f }, { A::feedback, 20 } } },
        { "frozen_echo",         0.65f, 0.45f,  0.0f, 0.50f, 1.0f, { { A::delayTime, 450 }, { A::sync, on }, { A::division, d1_4D }, { A::decay, 9.0f }, { A::diffusion, 60 }, { A::pitch, 12 }, { A::cutoff, 4500 } } },
        // --- D: Glitch & Experimental ----------------------------------------------------------
        { "glitch_machine",      0.70f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::density, 45 }, { A::division, d1_16 }, { A::sync, on }, { A::rate, 8 } } },
        { "robot_voice",         0.75f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::freq, 95 }, { A::resonance, 55 } } },
        { "alien_transmission",  0.60f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::freq, 180 }, { A::rate, 0.3f }, { A::sync, off }, { A::division, d1_1 }, { A::feedback, 35 }, { A::cutoff, 1800 }, { A::resonance, 40 }, { A::drive, 6 } } },
        { "digital_shatter",     0.65f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::cutoff, 6000 }, { A::drive, 4 }, { A::rate, 0.5f }, { A::sync, off }, { A::division, d1_1 }, { A::shape, random } } },
        { "broken_radio",        0.65f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::drive, 10 }, { A::density, 40 }, { A::rate, 0.5f }, { A::resonance, 30 }, { A::sync, off }, { A::division, d1_1 } } },
        // --- E: Vintage & Character ------------------------------------------------------------
        { "tape_melt",           0.55f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::drive, 8 }, { A::rate, 0.6f }, { A::sync, off }, { A::division, d1_1 }, { A::delayTime, 320 }, { A::feedback, 25 } } },
        { "vinyl_soul",          0.50f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::cutoff, 9000 }, { A::drive, 5 }, { A::density, 50 } } },
        { "retro_chorus",        0.55f, 0.55f,  0.0f, 0.50f, 1.0f, { { A::rate, 0.7f }, { A::voices, 3 } } },
        { "dusty_sample",        0.55f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::cutoff, 7500 }, { A::drive, 5 } } },
        { "megaphone",           0.65f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::drive, 18 } } },
        // --- F: Echo & Space -------------------------------------------------------------------
        { "slapback",            0.55f, 0.40f,  0.0f, 0.50f, 1.0f, { { A::delayTime, 95 }, { A::sync, off }, { A::division, d1_16 }, { A::feedback, 12 }, { A::drive, 3 }, { A::cutoff, 4500 } } },
        { "ping_pong_galaxy",    0.55f, 0.40f,  0.0f, 0.50f, 1.0f, { { A::delayTime, 375 }, { A::sync, on }, { A::division, d1_8D }, { A::feedback, 55 }, { A::cutoff, 4500 }, { A::rate, 0.15f } } },
        { "endless_hall",        0.60f, 0.40f,  0.0f, 0.50f, 1.0f, { { A::decay, 14.0f }, { A::cutoff, 6000 }, { A::diffusion, 85 }, { A::rate, 0.3f }, { A::delayTime, 45 } } },
        { "delay_spiral",        0.60f, 0.45f,  0.0f, 0.50f, 1.0f, { { A::delayTime, 300 }, { A::sync, on }, { A::division, d1_8 }, { A::feedback, 78 }, { A::rate, 0.2f }, { A::resonance, 40 } } },
        { "echo_chamber",        0.55f, 0.40f,  0.0f, 0.50f, 1.0f, { { A::delayTime, 70 }, { A::diffusion, 75 }, { A::decay, 1.6f }, { A::cutoff, 6500 } } },
        // --- G: Vocal & Instrument -------------------------------------------------------------
        { "octave_shadow",       0.60f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::pitch, -12 }, { A::cutoff, 2200 }, { A::detune, 8 } } },
        { "higher_self",         0.45f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::pitch, 12 }, { A::cutoff, 9000 }, { A::detune, 6 } } },
        { "ghost_double",        0.70f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::delayTime, 28 }, { A::detune, 14 }, { A::rate, 0.6f } } },
        { "formant_shift",       0.75f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::pitch, 5 }, { A::rate, 0.5f }, { A::sync, off }, { A::division, d1_2 } } },
        { "choir_mist",          0.60f, 0.70f,  0.0f, 0.50f, 1.0f, { { A::voices, 6 }, { A::detune, 18 }, { A::pitch, 0 } } },
        // --- H: Extreme & Performance ----------------------------------------------------------
        { "reverse_suck_in",     0.60f, 0.70f,  0.0f, 0.50f, 1.0f, { { A::sync, on }, { A::division, d1_2 }, { A::delayTime, 800 }, { A::decay, 3.0f } } },
        { "stutter_burst",       0.70f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::division, d1_16 }, { A::sync, on }, { A::delayTime, 60 }, { A::gate, 70 }, { A::density, 40 }, { A::rate, 2 } } },
        { "pitch_dive",          0.70f, 0.85f,  0.0f, 0.50f, 1.0f, { { A::pitch, -12 }, { A::delayTime, 700 }, { A::sync, off }, { A::division, d1_4 } } },
        { "vortex",              0.65f, 1.00f,  0.0f, 0.50f, 1.0f, { { A::rate, 0.35f }, { A::sync, off }, { A::division, d1_2 }, { A::feedback, 45 }, { A::cutoff, 3500 } } },
        { "chaos_mode",          0.60f, 0.80f,  0.0f, 0.50f, 1.0f, { { A::division, d1_2 }, { A::sync, on }, { A::rate, 1.0f }, { A::density, 70 }, { A::feedback, 40 } } },
    };
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = build();
    return presets;
}

std::array<float, kNumAdvParams> defaultAdvValues()
{
    std::array<float, kNumAdvParams> v {};
    for (int i = 0; i < kNumAdvParams; ++i) v[size_t (i)] = kAdvSpecs[size_t (i)].defaultValue;
    return v;
}

EngineParams toEngineParams (const FactoryPreset& preset)
{
    EngineParams e;
    e.effectIndex = std::max (0, findEffectIndex (preset.effectId));
    e.fx.amount = preset.amount;
    e.fx.tone = preset.tone;
    e.fx.movement = preset.movement;
    e.fx.width = preset.width;
    e.fx.adv = defaultAdvValues();
    for (const auto& [p, v] : preset.adv) e.fx.adv[size_t (p)] = v;
    e.mix = preset.mix;
    return e;
}
} // namespace msfx
