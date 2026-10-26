#include "vox/ProjectSerializer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace vox
{
using json = nlohmann::json;

namespace
{
const char* kindName (TrackKind k) { return k == TrackKind::Beat ? "beat" : "vocal"; }

json slotJson (const EffectSlotSpec& s)
{
    json params = json::object();
    for (const auto& [k, v] : s.params) params[k] = v;
    return { { "id", s.id }, { "type", s.type }, { "bypass", s.bypass }, { "params", params } };
}

json chainJson (const std::vector<EffectSlotSpec>& fx)
{
    json a = json::array();
    for (const auto& s : fx) a.push_back (slotJson (s));
    return a;
}

template <typename T>
T get (const json& j, const char* key, T def)
{
    auto it = j.find (key);
    if (it == j.end() || it->is_null()) return def;
    try { return it->get<T>(); } catch (...) { return def; }
}

float finiteOr (float v, float def) { return std::isfinite (v) ? v : def; }

EffectSlotSpec parseSlot (const json& j)
{
    EffectSlotSpec s;
    s.id = get<std::string> (j, "id", makeId ("fx"));
    s.type = get<std::string> (j, "type", "");
    s.bypass = get<bool> (j, "bypass", false);
    if (auto it = j.find ("params"); it != j.end() && it->is_object())
        for (auto& [k, v] : it->items())
            if (v.is_number()) s.params[k] = finiteOr (v.get<float>(), 0.0f);
    return s;
}

std::vector<EffectSlotSpec> parseChain (const json& j, const char* key)
{
    std::vector<EffectSlotSpec> out;
    if (auto it = j.find (key); it != j.end() && it->is_array())
        for (const auto& s : *it)
            if (s.is_object())
            {
                auto slot = parseSlot (s);
                if (! slot.type.empty()) out.push_back (std::move (slot));
            }
    return out;
}
} // namespace

std::string effectSlotToJson (const EffectSlotSpec& s) { return slotJson (s).dump(); }

std::string projectToJson (const Project& p, bool pretty)
{
    json j;
    j["format"] = "voxmix-project";
    j["formatVersion"] = p.formatVersion;
    j["id"] = p.id;
    j["name"] = p.name;
    j["createdAt"] = p.createdAt;
    j["modifiedAt"] = p.modifiedAt;
    j["sampleRate"] = p.sampleRate;
    j["bpm"] = p.bpm;
    j["timeSignature"] = { p.timeSigNum, p.timeSigDen };
    j["recordOffsetFrames"] = p.recordOffsetFrames;
    j["inputMonitoring"] = p.inputMonitoring;

    json sources = json::array();
    for (const auto& s : p.sources)
        sources.push_back ({ { "id", s.id }, { "file", s.file }, { "originalName", s.originalName }, { "frames", s.frames },
                             { "channels", s.channels }, { "sampleRate", s.sampleRate }, { "recorded", s.recorded } });
    j["sources"] = sources;

    json tracks = json::array();
    for (const auto& t : p.tracks)
    {
        json clips = json::array();
        for (const auto& c : t.clips)
            clips.push_back ({ { "id", c.id }, { "sourceId", c.sourceId }, { "start", c.start }, { "offset", c.offset },
                               { "length", c.length }, { "gainDb", c.gainDb }, { "fadeIn", c.fadeIn }, { "fadeOut", c.fadeOut },
                               { "name", c.name }, { "muted", c.muted } });
        tracks.push_back ({ { "id", t.id }, { "name", t.name }, { "kind", kindName (t.kind) }, { "color", t.color },
                            { "volumeDb", t.volumeDb }, { "pan", t.pan }, { "mute", t.mute }, { "solo", t.solo },
                            { "output", t.output }, { "inputChannel", t.inputChannel }, { "fx", chainJson (t.fx) }, { "clips", clips } });
    }
    j["tracks"] = tracks;

    json buses = json::array();
    for (const auto& b : p.buses)
        buses.push_back ({ { "id", b.id }, { "name", b.name }, { "volumeDb", b.volumeDb }, { "pan", b.pan }, { "mute", b.mute }, { "fx", chainJson (b.fx) } });
    j["buses"] = buses;

    j["master"] = { { "volumeDb", p.master.volumeDb }, { "fx", chainJson (p.master.fx) }, { "masteringOn", p.master.masteringOn },
                    { "mastering", slotJson (p.master.mastering) }, { "masteringPreset", p.master.masteringPreset },
                    { "loudnessTarget", p.master.loudnessTarget } };
    const auto& e = p.exportSettings;
    j["export"] = { { "format", e.format }, { "bitDepth", e.bitDepth }, { "sampleRate", e.sampleRate }, { "channels", e.channels },
                    { "mp3Bitrate", e.mp3Bitrate }, { "includeTail", e.includeTail } };
    json meta = json::object();
    for (const auto& [k, v] : p.meta) meta[k] = v;
    j["meta"] = meta;
    return j.dump (pretty ? 2 : -1);
}

bool projectFromJson (const std::string& text, Project& out, std::string& error, bool* migrated)
{
    json j = json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object()) { error = "Project file is not valid JSON"; return false; }
    if (get<std::string> (j, "format", "") != "voxmix-project") { error = "Not a VoxMix project file"; return false; }
    const int version = get<int> (j, "formatVersion", 0);
    if (version > kProjectFormatVersion)
    {
        error = "This project was saved by a newer version of VoxMix Pro (format " + std::to_string (version) + ")";
        return false;
    }
    if (migrated) *migrated = version < kProjectFormatVersion;   // no older formats exist yet

    Project p;
    p.formatVersion = kProjectFormatVersion;
    p.id = get<std::string> (j, "id", makeId ("prj"));
    p.name = get<std::string> (j, "name", "Untitled Project");
    p.createdAt = get<int64_t> (j, "createdAt", 0);
    p.modifiedAt = get<int64_t> (j, "modifiedAt", 0);
    p.sampleRate = std::clamp (get<int> (j, "sampleRate", 48000), 8000, 192000);
    p.bpm = std::clamp (get<double> (j, "bpm", 120.0), 20.0, 300.0);
    if (auto it = j.find ("timeSignature"); it != j.end() && it->is_array() && it->size() == 2)
    {
        p.timeSigNum = std::clamp ((*it)[0].get<int>(), 1, 32);
        p.timeSigDen = std::clamp ((*it)[1].get<int>(), 1, 32);
    }
    p.recordOffsetFrames = get<int64_t> (j, "recordOffsetFrames", 0);
    p.inputMonitoring = get<bool> (j, "inputMonitoring", false);

    if (auto it = j.find ("sources"); it != j.end() && it->is_array())
        for (const auto& s : *it)
        {
            SourceSpec src;
            src.id = get<std::string> (s, "id", "");
            src.file = get<std::string> (s, "file", "");
            src.originalName = get<std::string> (s, "originalName", "");
            src.frames = std::max<int64_t> (0, get<int64_t> (s, "frames", 0));
            src.channels = std::clamp (get<int> (s, "channels", 1), 1, 2);
            src.sampleRate = get<int> (s, "sampleRate", p.sampleRate);
            src.recorded = get<bool> (s, "recorded", false);
            if (! src.id.empty() && ! src.file.empty()) p.sources.push_back (src);
        }

    if (auto it = j.find ("tracks"); it != j.end() && it->is_array())
        for (const auto& t : *it)
        {
            TrackSpec tr;
            tr.id = get<std::string> (t, "id", makeId ("trk"));
            tr.name = get<std::string> (t, "name", "Layer");
            tr.kind = get<std::string> (t, "kind", "vocal") == "beat" ? TrackKind::Beat : TrackKind::Vocal;
            tr.color = get<std::string> (t, "color", "#8B5CF6");
            tr.volumeDb = std::clamp (finiteOr (get<float> (t, "volumeDb", 0.0f), 0.0f), -60.0f, 12.0f);
            tr.pan = std::clamp (finiteOr (get<float> (t, "pan", 0.0f), 0.0f), -1.0f, 1.0f);
            tr.mute = get<bool> (t, "mute", false);
            tr.solo = get<bool> (t, "solo", false);
            tr.output = get<std::string> (t, "output", "master");
            tr.inputChannel = get<int> (t, "inputChannel", -1);
            tr.fx = parseChain (t, "fx");
            if (auto ci = t.find ("clips"); ci != t.end() && ci->is_array())
                for (const auto& c : *ci)
                {
                    ClipSpec cl;
                    cl.id = get<std::string> (c, "id", makeId ("clp"));
                    cl.sourceId = get<std::string> (c, "sourceId", "");
                    cl.start = std::max<int64_t> (0, get<int64_t> (c, "start", 0));
                    cl.offset = std::max<int64_t> (0, get<int64_t> (c, "offset", 0));
                    cl.length = std::max<int64_t> (0, get<int64_t> (c, "length", 0));
                    cl.gainDb = std::clamp (finiteOr (get<float> (c, "gainDb", 0.0f), 0.0f), -60.0f, 24.0f);
                    cl.fadeIn = std::max<int64_t> (0, get<int64_t> (c, "fadeIn", 0));
                    cl.fadeOut = std::max<int64_t> (0, get<int64_t> (c, "fadeOut", 0));
                    cl.name = get<std::string> (c, "name", "");
                    cl.muted = get<bool> (c, "muted", false);
                    if (! cl.sourceId.empty()) tr.clips.push_back (cl);
                }
            p.tracks.push_back (std::move (tr));
        }

    if (auto it = j.find ("buses"); it != j.end() && it->is_array())
        for (const auto& b : *it)
        {
            BusSpec bus;
            bus.id = get<std::string> (b, "id", makeId ("bus"));
            bus.name = get<std::string> (b, "name", "Bus");
            bus.volumeDb = std::clamp (finiteOr (get<float> (b, "volumeDb", 0.0f), 0.0f), -60.0f, 12.0f);
            bus.pan = std::clamp (finiteOr (get<float> (b, "pan", 0.0f), 0.0f), -1.0f, 1.0f);
            bus.mute = get<bool> (b, "mute", false);
            bus.fx = parseChain (b, "fx");
            p.buses.push_back (std::move (bus));
        }
    // tracks routed to a bus that no longer exists go to the master
    for (auto& t : p.tracks)
        if (t.output != "master" && ! p.findBus (t.output)) t.output = "master";

    if (auto it = j.find ("master"); it != j.end() && it->is_object())
    {
        const auto& m = *it;
        p.master.volumeDb = std::clamp (finiteOr (get<float> (m, "volumeDb", 0.0f), 0.0f), -60.0f, 6.0f);
        p.master.fx = parseChain (m, "fx");
        p.master.masteringOn = get<bool> (m, "masteringOn", false);
        if (auto mi = m.find ("mastering"); mi != m.end() && mi->is_object())
            p.master.mastering = parseSlot (*mi);
        p.master.masteringPreset = get<std::string> (m, "masteringPreset", "default");
        p.master.loudnessTarget = std::clamp (get<float> (m, "loudnessTarget", -14.0f), -30.0f, -5.0f);
    }
    if (p.master.mastering.type != "mastering")
    {
        p.master.mastering.type = "mastering";
        if (p.master.mastering.id.empty()) p.master.mastering.id = "mastering";
    }

    if (auto it = j.find ("export"); it != j.end() && it->is_object())
    {
        auto& e = p.exportSettings;
        e.format = get<std::string> (*it, "format", "wav");
        e.bitDepth = get<int> (*it, "bitDepth", 24);
        if (e.bitDepth != 16 && e.bitDepth != 24 && e.bitDepth != 32) e.bitDepth = 24;
        e.sampleRate = get<int> (*it, "sampleRate", 0);
        e.channels = std::clamp (get<int> (*it, "channels", 2), 1, 2);
        e.mp3Bitrate = get<int> (*it, "mp3Bitrate", 320);
        e.includeTail = get<bool> (*it, "includeTail", true);
    }
    if (auto it = j.find ("meta"); it != j.end() && it->is_object())
        for (auto& [k, v] : it->items())
            if (v.is_string()) p.meta[k] = v.get<std::string>();

    out = std::move (p);
    return true;
}

bool readFile (const std::string& path, std::string& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileAtomic (const std::string& path, const std::string& contents, std::string& error)
{
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen (tmp.c_str(), "wb");
    if (! f) { error = "Cannot write " + tmp; return false; }
    const size_t written = std::fwrite (contents.data(), 1, contents.size(), f);
    bool ok = written == contents.size() && std::fflush (f) == 0;
#if ! defined(_WIN32)
    if (ok) ok = ::fsync (fileno (f)) == 0;
#endif
    ok = (std::fclose (f) == 0) && ok;
    if (! ok)
    {
        std::remove (tmp.c_str());
        error = "Storage full or write error while saving";
        return false;
    }
    if (std::rename (tmp.c_str(), path.c_str()) != 0)
    {
        std::remove (tmp.c_str());
        error = "Cannot replace " + path;
        return false;
    }
    return true;
}

bool saveProjectFile (const Project& p, const std::string& folder, std::string& error)
{
    const std::string main = folder + "/project.json";
    const std::string bak = folder + "/project.json.bak";
    struct stat st {};
    if (::stat (main.c_str(), &st) == 0)
    {
        // keep the last good version as a backup (copy, so main stays valid until the rename)
        std::string previous;
        if (readFile (main, previous))
        {
            Project check;
            std::string e;
            if (projectFromJson (previous, check, e))
            {
                std::string be;
                writeFileAtomic (bak, previous, be);
            }
        }
    }
    return writeFileAtomic (main, projectToJson (p), error);
}

LoadResult loadProjectFile (const std::string& folder, Project& out)
{
    LoadResult r;
    std::string text, err;
    if (readFile (folder + "/project.json", text) && projectFromJson (text, out, err, &r.migrated))
    {
        r.ok = true;
    }
    else
    {
        std::string bakText, bakErr;
        if (readFile (folder + "/project.json.bak", bakText) && projectFromJson (bakText, out, bakErr, &r.migrated))
        {
            r.ok = true;
            r.usedBackup = true;
            r.warnings.push_back ("The project file was damaged; the last backup was restored.");
        }
        else
        {
            r.error = err.empty() ? "Project file not found" : err;
            return r;
        }
    }
    for (const auto& s : out.sources)
    {
        struct stat st {};
        if (::stat ((folder + "/" + s.file).c_str(), &st) != 0)
            r.warnings.push_back ("Missing audio file: " + (s.originalName.empty() ? s.file : s.originalName));
    }
    return r;
}

} // namespace vox
