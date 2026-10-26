#include "vox/Presets.h"
#include "vox/ProjectSerializer.h"
#include "voxfx/EffectRegistry.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace vox
{
using json = nlohmann::json;

namespace
{
using Params = std::map<std::string, float>;

EffectSlotSpec slot (const char* type, Params params = {}, bool bypass = false)
{
    EffectSlotSpec s;
    s.id = type;
    s.type = type;
    s.params = std::move (params);
    s.bypass = bypass;
    return s;
}

/** The Vocal Sweetener effect loaded with one of the VocalSweet plugin's factory presets,
    plus optional overrides. */
EffectSlotSpec sweetener (const char* vsPreset, Params overrides = {})
{
    EffectSlotSpec s = slot ("vocal_sweetener");
    std::vector<std::pair<std::string, float>> values;
    if (voxfx::vocalSweetPresetValues (vsPreset, values))
        for (const auto& [k, v] : values) s.params[k] = v;
    for (const auto& [k, v] : overrides) s.params[k] = v;
    return s;
}

ChainPreset preset (const char* id, const char* name, const char* cat, const char* desc, std::vector<EffectSlotSpec> slots)
{
    ChainPreset p;
    p.id = id; p.name = name; p.category = cat; p.description = desc; p.factory = true;
    p.slots = std::move (slots);
    return p;
}

// Delay division indices (MoodShift order): 0 1/1, 1 1/2, 2 1/4, 3 1/8, 4 1/16, 5 1/32,
// 6 1/2D, 7 1/4D, 8 1/8D, 9 1/16D, 10 1/4T, 11 1/8T, 12 1/16T
// Reverb types: 0 Room, 1 Plate, 2 Hall, 3 Cathedral

json slotsJson (const std::vector<EffectSlotSpec>& slots)
{
    json a = json::array();
    for (const auto& s : slots) a.push_back (json::parse (effectSlotToJson (s)));
    return a;
}
} // namespace

const std::vector<ChainPreset>& factoryChainPresets()
{
    static const std::vector<ChainPreset> list = {
        preset ("clean_lead", "Clean Lead", "vocal", "Transparent cleanup: low cut, gentle compression, de-essing and a short plate.",
            { slot ("eq", { { "hpfFreq", 90 }, { "mid1Freq", 300 }, { "mid1Gain", -2 }, { "mid2Freq", 3500 }, { "mid2Gain", 1.5f }, { "highGain", 1.5f } }),
              slot ("compressor", { { "threshold", -20 }, { "ratio", 3 }, { "attack", 6 }, { "release", 110 } }),
              slot ("deesser", { { "freq", 6500 }, { "threshold", -30 }, { "range", 6 } }),
              slot ("reverb", { { "type", 1 }, { "decay", 1.4f }, { "predelay", 20 }, { "mix", 12 } }) }),
        preset ("warm_lead", "Warm Lead", "vocal", "Full, warm lead with body and soft top end.",
            { sweetener ("Warm Intimate Vocal", { { "sweeten", 50 }, { "warmth", 60 } }),
              slot ("reverb", { { "type", 0 }, { "decay", 1.1f }, { "mix", 12 } }) }),
        preset ("bright_lead", "Bright Lead", "vocal", "Forward pop lead with presence, air and an eighth-note echo.",
            { sweetener ("Bright Pop Vocal", { { "sweeten", 65 } }),
              slot ("delay", { { "sync", 1 }, { "division", 3 }, { "feedback", 22 }, { "highCut", 5000 }, { "mix", 10 } }),
              slot ("reverb", { { "type", 1 }, { "decay", 1.6f }, { "mix", 14 } }) }),
        preset ("rap_vocal", "Rap Vocal", "vocal", "Tight, punchy and upfront: firm compression, grit and a quick slap.",
            { sweetener ("Melodic Rap", { { "control", 60 } }),
              slot ("compressor", { { "threshold", -24 }, { "ratio", 4 }, { "attack", 3 }, { "release", 80 } }),
              slot ("saturation", { { "drive", 25 }, { "warmth", 30 }, { "mix", 60 } }),
              slot ("delay", { { "sync", 0 }, { "time", 95 }, { "feedback", 10 }, { "mix", 10 } }) }),
        preset ("afrobeat_lead", "Afrobeat Lead", "vocal", "Bright, forward lead that sits on busy percussion, with light tuning.",
            { slot ("pitch_correction", { { "speed", 60 }, { "strength", 70 } }),
              sweetener ("Afrobeat Lead"),
              slot ("delay", { { "sync", 1 }, { "division", 2 }, { "feedback", 25 }, { "highCut", 4500 }, { "mix", 10 } }),
              slot ("reverb", { { "type", 1 }, { "decay", 1.8f }, { "mix", 14 } }) }),
        preset ("intimate_vocal", "Intimate Vocal", "vocal", "Close and soft: warmth, smooth dynamics and a small room.",
            { sweetener ("Warm Intimate Vocal"),
              slot ("reverb", { { "type", 0 }, { "decay", 0.8f }, { "predelay", 10 }, { "mix", 10 } }) }),
        preset ("hard_tune", "Hard Tune", "vocal", "Instant, robotic pitch correction (C major; change the key to match your beat).",
            { slot ("pitch_correction", { { "speed", 0 }, { "strength", 100 } }),
              sweetener ("Bright Pop Vocal"),
              slot ("reverb", { { "type", 1 }, { "decay", 1.5f }, { "mix", 14 } }) }),
        preset ("wide_doubles", "Wide Doubles", "space", "For double tracks: thinner low end, chorus and extra width.",
            { slot ("eq", { { "hpfFreq", 150 }, { "mid1Freq", 400 }, { "mid1Gain", -3 } }),
              slot ("compressor", { { "threshold", -22 }, { "ratio", 4 } }),
              slot ("chorus", { { "voices", 3 }, { "rate", 0.5f }, { "depth", 3 }, { "mix", 40 } }),
              slot ("stereo_width", { { "width", 170 } }) }),
        preset ("backing_vocal", "Backing Vocal", "vocal", "Smooth, darker and wider so backing vocals sit behind the lead.",
            { sweetener ("Smooth Background Vocal"),
              slot ("stereo_width", { { "width", 140 } }),
              slot ("reverb", { { "type", 2 }, { "decay", 2.4f }, { "mix", 24 } }) }),
        preset ("choir_stack", "Choir Stack", "creative", "Turns one voice into a soft choir.",
            { slot ("eq", { { "hpfFreq", 140 } }),
              slot ("ms_choir_mist"),
              slot ("reverb", { { "type", 2 }, { "decay", 3.2f }, { "mix", 30 } }) }),
        preset ("dreamy_vocal", "Dreamy Vocal", "creative", "Washy, floating vocal with shimmer and long echoes.",
            { slot ("ms_dreamscape"),
              slot ("delay", { { "sync", 1 }, { "division", 7 }, { "feedback", 40 }, { "pingPong", 1 }, { "mix", 18 } }) }),
        preset ("underwater", "Underwater", "creative", "Submerged, resonant and wobbling.",
            { slot ("ms_underwater") }),
        preset ("radio_vocal", "Radio Vocal", "creative", "Band-limited old-radio voice with drive.",
            { slot ("eq", { { "hpfFreq", 450 }, { "mid1Freq", 1500 }, { "mid1Gain", 6 }, { "mid1Q", 0.8f }, { "highFreq", 3500 }, { "highGain", -15 } }),
              slot ("saturation", { { "drive", 60 }, { "warmth", 20 } }),
              slot ("compressor", { { "threshold", -26 }, { "ratio", 6 } }) }),
        preset ("telephone", "Telephone", "creative", "Phone-line voice with ghost reflections.",
            { slot ("ms_telephone_ghost") }),
        preset ("atmospheric_adlibs", "Atmospheric Ad-libs", "space", "Far-away ad-libs: huge hall, ping-pong echoes, extra width.",
            { slot ("eq", { { "hpfFreq", 200 }, { "highGain", -3 } }),
              slot ("delay", { { "sync", 1 }, { "division", 8 }, { "feedback", 45 }, { "pingPong", 1 }, { "mix", 30 } }),
              slot ("reverb", { { "type", 3 }, { "decay", 5 }, { "mix", 35 } }),
              slot ("stereo_width", { { "width", 160 } }) }),
        preset ("delay_throws", "Delay Throws", "space", "Dotted-quarter echoes that repeat the end of phrases.",
            { slot ("delay", { { "sync", 1 }, { "division", 7 }, { "feedback", 45 }, { "lowCut", 300 }, { "highCut", 4000 }, { "mix", 35 } }),
              slot ("reverb", { { "type", 1 }, { "decay", 2 }, { "mix", 15 } }) }),
        preset ("octave_double", "Octave Double", "creative", "Adds a voice one octave below.",
            { slot ("ms_octave_shadow") }),
        preset ("slapback_vocal", "Slapback", "space", "Short single echo, classic rock'n'roll.",
            { slot ("ms_slapback") }),
    };
    return list;
}

const std::vector<MasteringPreset>& masteringPresets()
{
    static const std::vector<MasteringPreset> list = {
        { "default", "Default", "Gentle glue and a safety limiter at -1 dBFS.", -14.0f,
          { { "compThreshold", -14 }, { "compRatio", 2 }, { "width", 100 }, { "ceiling", -1 } } },
        { "streaming", "Streaming", "Balanced master for streaming platforms (about -14 LUFS).", -14.0f,
          { { "lowGain", 1 }, { "highGain", 1 }, { "compThreshold", -16 }, { "compRatio", 2 }, { "width", 110 }, { "ceiling", -1 } } },
        { "club", "Club", "Loud and dense (about -9 LUFS); more low end and harder limiting.", -9.0f,
          { { "lowGain", 2 }, { "highGain", 1.5f }, { "compThreshold", -18 }, { "compRatio", 3 }, { "compAttack", 10 }, { "width", 115 }, { "ceiling", -0.5f } } },
        { "social", "Social Media", "Loud and clear on phone speakers (about -12 LUFS).", -12.0f,
          { { "lowGain", -1 }, { "midFreq", 2500 }, { "midGain", 1.5f }, { "highGain", 1 }, { "compThreshold", -16 }, { "compRatio", 2.5f }, { "ceiling", -1 } } },
        { "podcast", "Podcast", "Speech-focused, about -16 LUFS, narrower image.", -16.0f,
          { { "lowGain", -2 }, { "midFreq", 3000 }, { "midGain", 2 }, { "compThreshold", -20 }, { "compRatio", 3 }, { "width", 80 }, { "ceiling", -1 } } },
    };
    return list;
}

const MasteringPreset* findMasteringPreset (const std::string& id)
{
    for (const auto& p : masteringPresets()) if (p.id == id) return &p;
    return nullptr;
}

//==============================================================================
std::string presetToJson (const ChainPreset& p)
{
    json j = { { "id", p.id }, { "name", p.name }, { "category", p.category }, { "description", p.description },
               { "factory", p.factory }, { "slots", slotsJson (p.slots) } };
    return j.dump();
}

void UserPresetStore::setFile (const std::string& path) { file = path; }

bool UserPresetStore::load (std::string& error)
{
    list.clear();
    std::string text;
    if (! readFile (file, text)) return true;   // no user presets yet
    json j = json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object() || ! j.contains ("presets"))
    {
        error = "User presets file is damaged";
        return false;
    }
    for (const auto& pj : j["presets"])
    {
        ChainPreset p;
        p.id = pj.value ("id", makeId ("usr"));
        p.name = pj.value ("name", "User Preset");
        p.category = pj.value ("category", "user");
        p.description = pj.value ("description", "");
        p.factory = false;
        if (pj.contains ("slots") && pj["slots"].is_array())
            for (const auto& sj : pj["slots"])
            {
                EffectSlotSpec s;
                s.id = sj.value ("id", makeId ("fx"));
                s.type = sj.value ("type", "");
                s.bypass = sj.value ("bypass", false);
                if (sj.contains ("params") && sj["params"].is_object())
                    for (auto& [k, v] : sj["params"].items())
                        if (v.is_number()) s.params[k] = v.get<float>();
                if (voxfx::EffectRegistry::instance().find (s.type)) p.slots.push_back (s);
            }
        list.push_back (std::move (p));
    }
    return true;
}

bool UserPresetStore::save (std::string& error) const
{
    json arr = json::array();
    for (const auto& p : list) arr.push_back (json::parse (presetToJson (p)));
    json j = { { "format", "voxmix-presets" }, { "formatVersion", 1 }, { "presets", arr } };
    return writeFileAtomic (file, j.dump (2), error);
}

const ChainPreset* UserPresetStore::find (const std::string& id) const
{
    for (const auto& p : list) if (p.id == id) return &p;
    return nullptr;
}

ChainPreset& UserPresetStore::add (ChainPreset p)
{
    p.factory = false;
    if (p.id.empty()) p.id = makeId ("usr");
    list.push_back (std::move (p));
    return list.back();
}

bool UserPresetStore::rename (const std::string& id, const std::string& name)
{
    for (auto& p : list) if (p.id == id) { p.name = name; return true; }
    return false;
}

bool UserPresetStore::remove (const std::string& id)
{
    const auto before = list.size();
    list.erase (std::remove_if (list.begin(), list.end(), [&] (const ChainPreset& p) { return p.id == id; }), list.end());
    return list.size() != before;
}

} // namespace vox
