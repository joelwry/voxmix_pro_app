// Engine-level tests: command API, project persistence and recovery, clip placement, gain/pan,
// effect destinations, recording synchronisation with a simulated loopback device, export
// validation, undo/redo, loudness metering and a long-session performance check.

#include "TestFramework.h"
#include "TestHelpers.h"
#include "vox/Engine.h"
#include "vox/Metering.h"
#include "vox/ProjectSerializer.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

using json = nlohmann::json;
using vox::Engine;

namespace
{
constexpr int kSr = 48000;

json call (Engine& e, const std::string& m, const json& a = json::object())
{
    return json::parse (e.call (m, a.dump()));
}

json mustCall (Engine& e, const std::string& m, const json& a = json::object())
{
    auto r = call (e, m, a);
    CHECK_MSG (r.value ("ok", false), m + ": " + r.value ("error", std::string()));
    return r;
}

std::string trackIdByName (const json& project, const std::string& name)
{
    for (const auto& t : project["tracks"]) if (t["name"] == name) return t["id"];
    return {};
}

std::string beatTrack (const json& project)
{
    for (const auto& t : project["tracks"]) if (t["kind"] == "beat") return t["id"];
    return {};
}

/** Exports synchronously and returns the decoded interleaved stereo audio. */
vox::DecodedAudio exportAndDecode (Engine& e, const std::string& path, json extra = json::object())
{
    extra["path"] = path;
    extra["bitDepth"] = 32;
    extra["includeTail"] = false;
    mustCall (e, "exportMix", extra);
    json r;
    for (int i = 0; i < 60000; ++i)
    {
        r = call (e, "getExportResult");
        if (r.value ("ok", false) && ! r.value ("running", true)) break;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    CHECK_MSG (r["result"].value ("ok", false), r["result"].value ("error", std::string ("export failed")));
    CHECK_MSG (r["result"].value ("validated", false), r["result"].value ("validationNote", std::string()));
    vox::DecodedAudio d;
    std::string err;
    CHECK (vox::decodeAudioFile (path, d, err));
    return d;
}

std::vector<float> channel (const vox::DecodedAudio& d, int ch)
{
    std::vector<float> v ((size_t) d.frames());
    for (int64_t i = 0; i < d.frames(); ++i) v[(size_t) i] = d.interleaved[(size_t) (i * d.channels + ch)];
    return v;
}

/** Drives the engine like a full-duplex device whose microphone hears the speaker output
    `latency` frames later (acoustic loopback with a known round trip). */
struct LoopbackDriver
{
    Engine& engine;
    int latency;
    int block = 192;
    std::vector<float> history;   // everything the engine has output (mono L)
    int64_t frame = 0;

    void run (double seconds, float micGain = 1.0f)
    {
        const int64_t total = (int64_t) (seconds * kSr);
        std::vector<float> in ((size_t) block), out ((size_t) block * 2);
        for (int64_t done = 0; done < total; done += block)
        {
            for (int i = 0; i < block; ++i)
            {
                const int64_t src = frame + i - latency;
                in[(size_t) i] = src >= 0 && src < (int64_t) history.size() ? micGain * history[(size_t) src] : 0.0f;
            }
            engine.onAudio (in.data(), 1, block, out.data(), block);
            for (int i = 0; i < block; ++i) history.push_back (out[(size_t) (2 * i)]);
            frame += block;
        }
    }
};
} // namespace

TEST (Engine_ProjectLifecycleAndRoundTrip)
{
    const auto dir = th::tempDir ("lifecycle");
    std::string projectId, savedJson;
    {
        Engine e (dir, nullptr);
        auto r = mustCall (e, "createProject", { { "name", "Blessings (Remix)" }, { "bpm", 96 } });
        projectId = r["project"]["id"];
        CHECK (r["project"]["tracks"].size() == 1);
        CHECK (r["project"]["tracks"][0]["name"] == "Lead Vocal");
        for (int i = 0; i < 9; ++i) mustCall (e, "addTrack");
        auto full = call (e, "addTrack");
        CHECK (! full["ok"].get<bool>());
        CHECK (full["error"].get<std::string>().find ("10") != std::string::npos);
        r = mustCall (e, "getProject");
        const std::string harmony = trackIdByName (r["project"], "Harmony 1");
        CHECK (! harmony.empty());
        r = mustCall (e, "addEffect", { { "target", "track:" + harmony }, { "type", "reverb" } });
        const std::string slot = r["slotId"];
        mustCall (e, "setEffectParam", { { "target", "track:" + harmony }, { "slotId", slot }, { "param", "decay" }, { "value", 3.5 } });
        // invalid values never reach the DSP
        r = mustCall (e, "setEffectParam", { { "target", "track:" + harmony }, { "slotId", slot }, { "param", "mix" }, { "value", 500 } });
        CHECK_NEAR (r["applied"]["mix"].get<float>(), 100.0f, 1e-4);
        mustCall (e, "updateTrack", { { "trackId", harmony }, { "volumeDb", -4.5 }, { "pan", -0.4 }, { "mute", true } });
        mustCall (e, "addEffect", { { "target", "master" }, { "type", "eq" } });
        mustCall (e, "applyMasteringPreset", { { "presetId", "club" } });
        mustCall (e, "saveProject");
        savedJson = mustCall (e, "getProject")["project"].dump();
        mustCall (e, "closeProject");
    }
    {
        Engine e (dir, nullptr);
        auto list = mustCall (e, "listProjects");
        CHECK (list["projects"].size() == 1);
        CHECK (list["projects"][0]["name"] == "Blessings (Remix)");
        auto r = mustCall (e, "openProject", { { "id", projectId } });
        CHECK (r["project"].dump() == savedJson);
        const std::string harmony = trackIdByName (r["project"], "Harmony 1");
        for (const auto& t : r["project"]["tracks"])
            if (t["id"] == harmony)
            {
                CHECK_NEAR (t["volumeDb"].get<float>(), -4.5f, 1e-5);
                CHECK (t["mute"].get<bool>());
                CHECK (t["fx"].size() == 1);
                CHECK_NEAR (t["fx"][0]["params"]["decay"].get<float>(), 3.5f, 1e-5);
            }
        CHECK (r["project"]["master"]["masteringOn"].get<bool>());
        CHECK_NEAR (r["project"]["master"]["mastering"]["params"]["ceiling"].get<float>(), -0.5f, 1e-5);
    }
}

TEST (Engine_CorruptProjectFallsBackToBackup)
{
    const auto dir = th::tempDir ("corrupt");
    std::string id;
    {
        Engine e (dir, nullptr);
        id = mustCall (e, "createProject", { { "name", "Safe" } })["project"]["id"];
        mustCall (e, "addTrack", { { "name", "Kept Layer" } });
        mustCall (e, "saveProject");   // second save: the first version becomes project.json.bak
        mustCall (e, "addTrack", { { "name", "Newest" } });
        mustCall (e, "saveProject");
        mustCall (e, "closeProject");
    }
    {
        FILE* f = std::fopen ((dir + "/projects/" + id + "/project.json").c_str(), "wb");
        std::fputs ("{ \"format\": \"voxmix-project\", truncated", f);
        std::fclose (f);
        Engine e (dir, nullptr);
        auto r = mustCall (e, "openProject", { { "id", id } });
        CHECK (r["usedBackup"].get<bool>());
        CHECK (! trackIdByName (r["project"], "Kept Layer").empty());
        CHECK (r["warnings"].size() >= 1);
    }
    {
        Engine e (dir, nullptr);
        auto bad = call (e, "openProject", { { "id", "prj-missing" } });
        CHECK (! bad["ok"].get<bool>());
    }
}

TEST (Engine_ImportValidation_AndMissingMedia)
{
    const auto dir = th::tempDir ("import");
    Engine e (dir, nullptr);
    const std::string id = mustCall (e, "createProject")["project"]["id"];
    // garbage and truncated files are rejected with a message
    {
        FILE* f = std::fopen ((dir + "/garbage.mp3").c_str(), "wb");
        for (int i = 0; i < 1000; ++i) std::fputc (i * 7 % 256, f);
        std::fclose (f);
        auto r = call (e, "importAudio", { { "path", dir + "/garbage.mp3" } });
        CHECK (! r["ok"].get<bool>());
        CHECK (! r["error"].get<std::string>().empty());
        auto r2 = call (e, "importAudio", { { "path", dir + "/does-not-exist.wav" } });
        CHECK (! r2["ok"].get<bool>());
        FILE* t = std::fopen ((dir + "/trunc.wav").c_str(), "wb");
        std::fputs ("RIFF\x10\x00\x00\x00WAVEfmt ", t);
        std::fclose (t);
        CHECK (! call (e, "importAudio", { { "path", dir + "/trunc.wav" } })["ok"].get<bool>());
    }
    // a 44.1 kHz stereo beat is resampled to the project rate
    auto beat = th::sine (441.0, 44100, 44100 * 2, 0.5f);
    std::vector<float> st (beat.size() * 2);
    for (size_t i = 0; i < beat.size(); ++i) { st[2 * i] = beat[i]; st[2 * i + 1] = beat[i]; }
    CHECK (th::writeWav (dir + "/beat44.wav", st, 2, 44100));
    auto r = mustCall (e, "importAudio", { { "path", dir + "/beat44.wav" }, { "name", "Afro Vibes.wav" } });
    CHECK_NEAR (r["seconds"].get<double>(), 2.0, 0.001);
    const std::string bt = beatTrack (r["project"]);
    CHECK (! bt.empty());
    CHECK (r["project"]["sources"][0]["frames"].get<int64_t>() == 96000);
    auto wf = mustCall (e, "getWaveform", { { "clipId", r["clipId"] }, { "buckets", 50 } });
    CHECK (wf["peaks"].size() == 50);
    CHECK_NEAR (wf["peaks"][25].get<float>(), 0.5f, 0.02);
    mustCall (e, "closeProject");

    // delete the media: the project still opens and reports it
    std::remove ((dir + "/projects/" + id + "/" + r["project"]["sources"][0]["file"].get<std::string>()).c_str());
    auto o = mustCall (e, "openProject", { { "id", id } });
    bool reported = false;
    for (const auto& w : o["warnings"]) reported |= w.get<std::string>().find ("Missing audio") != std::string::npos;
    CHECK (reported);
}

TEST (Mixer_ClipPlacementGainPanAndLayerIsolation)
{
    const auto dir = th::tempDir ("mixer");
    Engine e (dir, nullptr);
    mustCall (e, "createProject");
    // mono "vocal" with an impulse at frame 0 followed by a constant tone
    std::vector<float> v (48000, 0.0f);
    v[0] = 0.9f;
    for (int i = 4800; i < 48000; ++i) v[(size_t) i] = 0.5f * (float) std::sin (2.0 * th::kPi * 300.0 * i / kSr);
    CHECK (th::writeWav (dir + "/vox.wav", v, 1, kSr, 32));
    auto p = mustCall (e, "getProject")["project"];
    const std::string lead = trackIdByName (p, "Lead Vocal");
    auto r = mustCall (e, "importAudio", { { "path", dir + "/vox.wav" }, { "trackId", lead } });
    const std::string clip = r["clipId"];
    mustCall (e, "moveClip", { { "clipId", clip }, { "start", 12345 } });

    auto d = exportAndDecode (e, dir + "/place.wav");
    auto L = channel (d, 0), R = channel (d, 1);
    CHECK (th::onset (L, 0, 0.5f) == 12345);              // frame-exact placement
    CHECK_NEAR (L[12345], 0.9f, 1e-4);
    CHECK_NEAR (R[12345], 0.9f, 1e-4);                     // mono layer centred: unity both sides

    mustCall (e, "updateTrack", { { "trackId", lead }, { "volumeDb", -6.0206 } });
    d = exportAndDecode (e, dir + "/gain.wav");
    CHECK_NEAR (channel (d, 0)[12345], 0.45f, 1e-3);

    mustCall (e, "updateTrack", { { "trackId", lead }, { "volumeDb", 0 }, { "pan", -1 } });
    d = exportAndDecode (e, dir + "/pan.wav");
    CHECK_NEAR (channel (d, 0)[12345], 0.9f * 1.41421356f, 2e-3);
    CHECK_NEAR (channel (d, 1)[12345], 0.0f, 1e-5);
    mustCall (e, "updateTrack", { { "trackId", lead }, { "pan", 0 } });

    // a second layer with the same audio and a reverb: the reverb must not leak into the lead
    auto dup = mustCall (e, "duplicateTrack", { { "trackId", lead } });
    const std::string layer2 = dup["trackId"];
    mustCall (e, "addEffect", { { "target", "track:" + layer2 }, { "type", "reverb" }, { "params", { { "mix", 100 } } } });
    mustCall (e, "updateTrack", { { "trackId", lead }, { "solo", true } });
    d = exportAndDecode (e, dir + "/solo.wav");
    auto soloL = channel (d, 0);
    CHECK_NEAR (soloL[12345], 0.9f, 1e-4);
    // after the impulse and before the tone the lead is silent: no reverb tail
    float tail = 0.0f;
    for (int i = 12345 + 10; i < 12345 + 4700; ++i) tail = std::max (tail, std::abs (soloL[(size_t) i]));
    CHECK (tail < 1e-6f);
    mustCall (e, "updateTrack", { { "trackId", lead }, { "solo", false } });
    mustCall (e, "updateTrack", { { "trackId", lead }, { "mute", true } });
    d = exportAndDecode (e, dir + "/wet.wav");
    auto wet = channel (d, 0);
    tail = 0.0f;
    for (int i = 12345 + 2000; i < 12345 + 4700; ++i) tail = std::max (tail, std::abs (wet[(size_t) i]));
    CHECK (tail > 1e-3f);   // the reverb is on layer 2

    // Master FX processes the whole mix
    mustCall (e, "updateTrack", { { "trackId", lead }, { "mute", false } });
    mustCall (e, "updateTrack", { { "trackId", layer2 }, { "mute", true } });
    auto m = mustCall (e, "addEffect", { { "target", "master" }, { "type", "eq" }, { "params", { { "hpfOn", 0 }, { "output", -6.0206 } } } });
    d = exportAndDecode (e, dir + "/master.wav");
    CHECK_NEAR (channel (d, 0)[12345], 0.45f, 2e-3);
}

TEST (Mixer_DelayCompensationKeepsTracksAligned)
{
    const auto dir = th::tempDir ("pdc");
    Engine e (dir, nullptr);
    mustCall (e, "createProject");
    std::vector<float> v (48000, 0.0f);
    v[1000] = 0.8f;
    CHECK (th::writeWav (dir + "/imp.wav", v, 1, kSr, 32));
    auto p = mustCall (e, "getProject")["project"];
    const std::string lead = trackIdByName (p, "Lead Vocal");
    mustCall (e, "importAudio", { { "path", dir + "/imp.wav" }, { "trackId", lead } });
    // limiter (1 ms look-ahead) on the layer + mastering chain on the master
    mustCall (e, "addEffect", { { "target", "track:" + lead }, { "type", "limiter" } });
    mustCall (e, "addEffect", { { "target", "track:" + lead }, { "type", "eq" }, { "params", { { "hpfOn", 0 } } } });
    mustCall (e, "updateMaster", { { "masteringOn", true } });
    mustCall (e, "applyMasteringPreset", { { "presetId", "default" } });
    auto d = exportAndDecode (e, dir + "/pdc.wav");
    auto L = channel (d, 0);
    const int at = th::onset (L, 0, 0.05f);
    CHECK_MSG (at == 1000, "impulse at " + std::to_string (at));
}

TEST (Recording_SyncWithSimulatedLoopback)
{
    // The "microphone" hears the speaker 1000 frames (~21 ms) late. A take recorded while the
    // beat plays must land on the timeline so its clicks line up with the beat's clicks.
    const auto dir = th::tempDir ("rec");
    Engine e (dir, nullptr);
    e.setAssumedRoundTripLatency (1000);
    mustCall (e, "createProject");
    auto clicksMono = th::clicks (kSr * 5, 4800, 24000);
    std::vector<float> st (clicksMono.size() * 2);
    for (size_t i = 0; i < clicksMono.size(); ++i) st[2 * i] = st[2 * i + 1] = clicksMono[i];
    CHECK (th::writeWav (dir + "/clicks.wav", st, 2, kSr, 32));
    auto r = mustCall (e, "importAudio", { { "path", dir + "/clicks.wav" } });
    const std::string beat = beatTrack (r["project"]);
    const std::string lead = trackIdByName (r["project"], "Lead Vocal");
    // latency-adding effects everywhere: compensation must still hold
    mustCall (e, "addEffect", { { "target", "track:" + beat }, { "type", "pitch_correction" }, { "params", { { "strength", 0 } } } });
    mustCall (e, "addEffect", { { "target", "track:" + lead }, { "type", "limiter" } });

    LoopbackDriver drv { e, 1000 };
    drv.run (0.1);   // device running, transport stopped
    auto s = mustCall (e, "startRecording", { { "trackId", lead } });
    CHECK (s["startPosition"].get<int64_t>() == 0);
    drv.run (4.0);
    auto status = json::parse (e.status());
    CHECK (status["recording"].get<bool>());
    CHECK (status["record"]["dropped"].get<int64_t>() == 0);
    auto stop = mustCall (e, "stopRecording");
    // compensation = simulated device round trip + the master chain's constant latency
    const int64_t comp = stop["clip"]["latencyCompensation"].get<int64_t>();
    CHECK_MSG (comp == 1000 + 72, stop.dump());
    CHECK (stop["clip"]["recordedFrames"].get<int64_t>() >= 4 * kSr);

    // Record a second take starting at 2.0 s on another layer (punch-in from a later position)
    auto added = mustCall (e, "addTrack", { { "name", "Harmony" } });
    const std::string harm = added["trackId"];
    mustCall (e, "seek", { { "frame", 2 * kSr } });
    drv.run (0.05);
    mustCall (e, "startRecording", { { "trackId", harm } });
    drv.run (2.5);
    auto stop2 = mustCall (e, "stopRecording");
    // compensation = simulated device round trip (1000) + master chain latency (mastering slot)
    const int64_t comp2 = stop2["clip"]["latencyCompensation"].get<int64_t>();
    CHECK (comp2 == comp);
    CHECK (stop2["clip"]["start"].get<int64_t>() == 2 * kSr - comp2);

    // Render the vocal layers alone and compare click onsets with the beat's
    mustCall (e, "updateTrack", { { "trackId", beat }, { "mute", true } });
    mustCall (e, "updateTrack", { { "trackId", harm }, { "mute", true } });
    auto d = exportAndDecode (e, dir + "/vocal.wav");
    auto L = channel (d, 0);
    int matched = 0;
    for (int expected = 4800; expected < 4 * kSr - 24000; expected += 24000)
    {
        const int ref = th::onset (clicksMono, expected - 5, 0.05f);   // where the beat's click is detected
        const int got = th::onset (L, expected - 2000, 0.05f);
        CHECK_MSG (std::abs (got - ref) <= 1, "take 1 click expected " + std::to_string (ref) + " got " + std::to_string (got));
        matched += std::abs (got - ref) <= 1;
    }
    CHECK (matched >= 7);
    mustCall (e, "updateTrack", { { "trackId", lead }, { "mute", true } });
    mustCall (e, "updateTrack", { { "trackId", harm }, { "mute", false } });
    d = exportAndDecode (e, dir + "/harm.wav");
    auto H = channel (d, 0);
    for (int expected = 2 * kSr + 4800; expected < 4 * kSr; expected += 24000)
    {
        const int ref = th::onset (clicksMono, expected - 5, 0.05f);
        const int got = th::onset (H, expected - 2000, 0.05f);
        CHECK_MSG (std::abs (got - ref) <= 1, "take 2 click expected " + std::to_string (ref) + " got " + std::to_string (got));
    }

    // the takes survive a reload
    const std::string id = mustCall (e, "getProject")["project"]["id"];
    mustCall (e, "closeProject");
    auto o = mustCall (e, "openProject", { { "id", id } });
    int clips = 0;
    for (const auto& t : o["project"]["tracks"]) if (t["kind"] == "vocal") clips += (int) t["clips"].size();
    CHECK (clips == 2);
}

TEST (Recording_ReplacesOverlappedTakeNonDestructively)
{
    const auto dir = th::tempDir ("retake");
    Engine e (dir, nullptr);
    mustCall (e, "createProject");
    auto p = mustCall (e, "getProject")["project"];
    const std::string lead = trackIdByName (p, "Lead Vocal");
    LoopbackDriver drv { e, 500 };
    e.setAssumedRoundTripLatency (500);
    drv.run (0.05);
    mustCall (e, "startRecording", { { "trackId", lead } });
    drv.run (3.0);
    mustCall (e, "stopRecording");
    mustCall (e, "seek", { { "frame", kSr } });
    drv.run (0.05);
    mustCall (e, "startRecording", { { "trackId", lead } });
    drv.run (1.0);
    auto r = mustCall (e, "stopRecording");
    // first take split around the new take: left part, new take, right part
    for (const auto& t : r["project"]["tracks"])
        if (t["id"] == lead)
        {
            CHECK (t["clips"].size() == 3);
            CHECK (r["project"]["sources"].size() == 2);   // both recordings are kept
        }
    mustCall (e, "undo");
    auto u = mustCall (e, "getProject");
    for (const auto& t : u["project"]["tracks"])
        if (t["id"] == lead) CHECK (t["clips"].size() == 1);
}

TEST (Recording_CrashRecovery)
{
    const auto dir = th::tempDir ("recover");
    std::string id, lead;
    {
        Engine e (dir, nullptr);
        auto r = mustCall (e, "createProject");
        id = r["project"]["id"];
        lead = trackIdByName (r["project"], "Lead Vocal");
        mustCall (e, "closeProject");
    }
    // simulate a take whose writer never closed: placeholder sizes + sidecar
    const std::string media = dir + "/projects/" + id + "/media/";
    {
        vox::WavWriter w;
        std::string err;
        w.open (media + "rec-crash.wav", kSr, 1, 32, err);
        auto s = th::sine (200, kSr, kSr, 0.3f);
        w.write (s.data(), kSr);
        FILE* f = std::fopen ((media + "rec-crash.wav").c_str(), "rb");
        CHECK (f != nullptr);
        if (f) std::fclose (f);
        // do not close the writer normally: copy the file before close() patches the header
        std::string cmd = "cp '" + media + "rec-crash.wav' '" + media + "rec-crash.tmp'";
        CHECK (std::system (cmd.c_str()) == 0);
        w.close();
        cmd = "mv '" + media + "rec-crash.tmp' '" + media + "rec-crash.wav'";
        CHECK (std::system (cmd.c_str()) == 0);
        FILE* sj = std::fopen ((media + "rec-crash.json").c_str(), "wb");
        std::fprintf (sj, "{\"trackId\":\"%s\",\"start\":24000,\"latency\":0}", lead.c_str());
        std::fclose (sj);
    }
    Engine e (dir, nullptr);
    auto r = mustCall (e, "openProject", { { "id", id } });
    CHECK (r["recovered"].size() == 1);
    for (const auto& t : r["project"]["tracks"])
        if (t["id"] == lead)
        {
            CHECK (t["clips"].size() == 1);
            if (! t["clips"].empty()) CHECK (t["clips"][0]["start"].get<int64_t>() == 24000);
        }
}

TEST (Engine_UndoRedoAndClipEditing)
{
    const auto dir = th::tempDir ("undo");
    Engine e (dir, nullptr);
    mustCall (e, "createProject");
    auto s = th::sine (220, kSr, kSr * 2, 0.3f);
    CHECK (th::writeWav (dir + "/v.wav", s, 1, kSr));
    auto p = mustCall (e, "getProject")["project"];
    const std::string lead = trackIdByName (p, "Lead Vocal");
    auto r = mustCall (e, "importAudio", { { "path", dir + "/v.wav" }, { "trackId", lead } });
    const std::string clip = r["clipId"];
    mustCall (e, "moveClip", { { "clipId", clip }, { "start", 5000 } });
    mustCall (e, "trimClip", { { "clipId", clip }, { "start", 6000 } });
    auto t = mustCall (e, "getProject")["project"];
    auto findClip = [&] (const json& pr) { for (const auto& tr : pr["tracks"]) for (const auto& c : tr["clips"]) if (c["id"] == clip) return c; return json(); };
    CHECK (findClip (t)["start"] == 6000);
    CHECK (findClip (t)["offset"] == 1000);
    CHECK (findClip (t)["length"] == 2 * kSr - 1000);
    mustCall (e, "undo");
    CHECK (findClip (mustCall (e, "getProject")["project"])["start"] == 5000);
    mustCall (e, "undo");
    CHECK (findClip (mustCall (e, "getProject")["project"])["start"] == 0);
    mustCall (e, "redo");
    CHECK (findClip (mustCall (e, "getProject")["project"])["start"] == 5000);
    auto sp = mustCall (e, "splitClip", { { "clipId", clip }, { "frame", 5000 + kSr } });
    int clips = 0;
    for (const auto& tr : sp["project"]["tracks"]) if (tr["id"] == lead) clips = (int) tr["clips"].size();
    CHECK (clips == 2);
    mustCall (e, "setClipGain", { { "clipId", clip }, { "gainDb", -3 } });
    mustCall (e, "deleteClip", { { "clipId", sp["clipId"] } });
    // the source is kept unless deleteMedia is requested
    CHECK (mustCall (e, "getProject")["project"]["sources"].size() == 1);
    CHECK (! call (e, "moveClip", { { "clipId", "nope" }, { "start", 1 } })["ok"].get<bool>());
}

TEST (Presets_ApplySaveRenameDelete)
{
    const auto dir = th::tempDir ("presets");
    Engine e (dir, nullptr);
    mustCall (e, "createProject");
    auto list = mustCall (e, "listPresets")["presets"];
    CHECK (list.size() >= 14);
    for (const auto& want : { "Clean Lead", "Warm Lead", "Bright Lead", "Rap Vocal", "Afrobeat Lead", "Intimate Vocal", "Wide Doubles",
                              "Backing Vocal", "Choir Stack", "Dreamy Vocal", "Underwater", "Radio Vocal", "Atmospheric Ad-libs", "Delay Throws" })
    {
        bool found = false;
        for (const auto& p : list) found |= p["name"] == want;
        CHECK_MSG (found, want);
    }
    auto p = mustCall (e, "getProject")["project"];
    const std::string lead = trackIdByName (p, "Lead Vocal");
    auto r = mustCall (e, "applyPreset", { { "target", "track:" + lead }, { "presetId", "afrobeat_lead" } });
    json chain;
    for (const auto& t : r["project"]["tracks"]) if (t["id"] == lead) chain = t["fx"];
    CHECK (chain.size() == 4);
    CHECK (chain[0]["type"] == "pitch_correction");
    CHECK (chain[1]["type"] == "vocal_sweetener");
    CHECK_NEAR (chain[1]["params"]["presFreq"].get<float>(), 3800.0f, 1e-3);   // VocalSweet "Afrobeat Lead" value
    mustCall (e, "setEffectParam", { { "target", "track:" + lead }, { "slotId", chain[3]["id"] }, { "param", "decay" }, { "value", 4 } });
    auto saved = mustCall (e, "savePreset", { { "target", "track:" + lead }, { "name", "My Afro" } });
    const std::string uid = saved["preset"]["id"];
    mustCall (e, "renamePreset", { { "presetId", uid }, { "name", "My Afro 2" } });
    CHECK (! call (e, "deletePreset", { { "presetId", "clean_lead" } })["ok"].get<bool>());   // factory is read-only
    {
        Engine e2 (dir, nullptr);   // user presets persist
        bool found = false;
        const auto listed = mustCall (e2, "listPresets");
        for (const auto& pr : listed["presets"])
            if (pr["id"] == uid)
            {
                found = pr["name"] == "My Afro 2";
                CHECK_NEAR (pr["slots"][3]["params"]["decay"].get<float>(), 4.0f, 1e-4);
            }
        CHECK (found);
    }
    mustCall (e, "deletePreset", { { "presetId", uid } });
}

TEST (Export_WavFormatsAndValidation)
{
    const auto dir = th::tempDir ("export");
    Engine e (dir, nullptr);
    mustCall (e, "createProject", { { "name", "My/Song" } });
    {
        // empty project: the export reports that there is nothing to export
        mustCall (e, "exportMix", { { "path", dir + "/empty.wav" } });
        json r;
        for (int i = 0; i < 400 && (r = call (e, "getExportResult")).value ("running", true); ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        CHECK (! r["result"]["ok"].get<bool>());
    }
    auto s = th::sine (440, kSr, kSr * 3, 0.5f);
    std::vector<float> st (s.size() * 2);
    for (size_t i = 0; i < s.size(); ++i) st[2 * i] = st[2 * i + 1] = s[i];
    CHECK (th::writeWav (dir + "/b.wav", st, 2, kSr));
    mustCall (e, "importAudio", { { "path", dir + "/b.wav" } });
    for (int bits : { 16, 24, 32 })
        for (int rate : { 0, 44100 })
        {
            const std::string path = dir + "/out-" + std::to_string (bits) + "-" + std::to_string (rate) + ".wav";
            mustCall (e, "exportMix", { { "path", path }, { "bitDepth", bits }, { "sampleRate", rate }, { "includeTail", false } });
            json r;
            for (int i = 0; i < 4000; ++i)
            {
                r = call (e, "getExportResult");
                if (! r.value ("running", true)) break;
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
            const auto& res = r["result"];
            CHECK_MSG (res["ok"].get<bool>() && res["validated"].get<bool>(), res.dump());
            const int outRate = rate ? rate : kSr;
            CHECK_NEAR (res["seconds"].get<double>(), 3.0, 0.001);
            CHECK (res["sampleRate"] == outRate);
            CHECK (res["channels"] == 2);
            CHECK_NEAR (res["peakDb"].get<float>(), -6.02f, 0.1);
            CHECK (res["fileBytes"].get<int64_t>() == 44 + (int64_t) (3 * outRate) * 2 * (bits / 8));
        }
    // mono export
    auto d = exportAndDecode (e, dir + "/mono.wav", { { "channels", 1 } });
    CHECK (d.channels == 1);
    // estimate
    auto est = mustCall (e, "estimateExport", { { "bitDepth", 16 }, { "sampleRate", 44100 } });
    CHECK (est["bytes"].get<int64_t>() > 0);
}

TEST (Mastering_LoudnessMatchReachesTarget)
{
    const auto dir = th::tempDir ("master");
    Engine e (dir, nullptr);
    mustCall (e, "createProject");
    auto n = th::noise (kSr * 6, 0.05f, 11);
    std::vector<float> st (n.size() * 2);
    for (size_t i = 0; i < n.size(); ++i) { st[2 * i] = n[i]; st[2 * i + 1] = n[(i + 17) % n.size()]; }
    CHECK (th::writeWav (dir + "/quiet.wav", st, 2, kSr));
    mustCall (e, "importAudio", { { "path", dir + "/quiet.wav" } });
    auto before = mustCall (e, "analyzeMix", { { "mastering", false } });
    CHECK (before["integratedLufs"].get<float>() < -20.0f);
    auto r = mustCall (e, "autoMaster", { { "target", -14 } });
    CHECK_NEAR (r["after"]["integratedLufs"].get<float>(), -14.0f, 1.0f);
    CHECK (r["after"]["truePeakDb"].get<float>() <= 0.0f);
    CHECK (r["project"]["master"]["masteringOn"].get<bool>());
}

TEST (Metering_LoudnessOfReferenceSine)
{
    // EBU Tech 3341: a stereo 1 kHz sine at -23 dBFS reads -23 LUFS
    vox::LoudnessMeter m;
    m.prepare (kSr);
    const float amp = std::pow (10.0f, -23.0f / 20.0f);
    auto s = th::sine (1000, kSr, kSr * 10, amp);
    for (size_t i = 0; i < s.size(); i += 480) m.process (s.data() + i, s.data() + i, 480);
    CHECK_NEAR (m.integrated(), -23.0f, 0.2f);
    CHECK_NEAR (m.momentary(), -23.0f, 0.2f);
    CHECK_NEAR (m.truePeakDb(), -23.0f, 0.3f);
}

TEST (Performance_TenLayersLongSession)
{
    // 10 layers x (Vocal Sweetener + pitch correction + reverb + delay) + mastering, 60 s
    const auto dir = th::tempDir ("perf");
    Engine e (dir, nullptr);
    mustCall (e, "createProject");
    auto v = th::noise (kSr * 60, 0.1f, 5);
    CHECK (th::writeWav (dir + "/v.wav", v, 1, kSr));
    auto p = mustCall (e, "getProject")["project"];
    for (int i = 0; i < 9; ++i) mustCall (e, "addTrack");
    p = mustCall (e, "getProject")["project"];
    for (const auto& t : p["tracks"])
    {
        mustCall (e, "importAudio", { { "path", dir + "/v.wav" }, { "trackId", t["id"] } });
        for (const char* fx : { "pitch_correction", "vocal_sweetener", "reverb", "delay" })
            mustCall (e, "addEffect", { { "target", "track:" + t["id"].get<std::string>() }, { "type", fx } });
    }
    mustCall (e, "updateMaster", { { "masteringOn", true } });
    const auto t0 = std::chrono::steady_clock::now();
    auto d = exportAndDecode (e, dir + "/long.wav");
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    CHECK_NEAR (double (d.frames()) / kSr, 60.0, 0.01);
    bool finite = true;
    for (float x : d.interleaved) finite &= std::isfinite (x);
    CHECK (finite);
    std::printf ("    perf: 60 s, 10 layers x 4 effects + mastering rendered in %.2f s (%.1fx real time, host CPU)\n", sec, 60.0 / sec);
    CHECK (sec < 60.0);

    // real-time path: drive onAudio for 10 s of 192-frame callbacks and measure load
    mustCall (e, "seek", { { "frame", 0 } });
    mustCall (e, "play");
    LoopbackDriver drv { e, 0 };
    const auto t1 = std::chrono::steady_clock::now();
    drv.run (10.0, 0.0f);
    const double rt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
    std::printf ("    perf: real-time callback path at %.1f%% of one host core\n", 100.0 * rt / 10.0);
    auto st = json::parse (e.status());
    CHECK (st["playing"].get<bool>());
    CHECK (st["position"].get<int64_t>() >= 10 * kSr - 192);
}

TEST (Calibration_MeasuresLoopbackRoundTrip)
{
    const auto dir = th::tempDir ("calib");
    Engine e (dir, nullptr);
    e.setAssumedRoundTripLatency (600);   // what the "device" claims
    mustCall (e, "createProject");
    LoopbackDriver drv { e, 777 };        // what really happens
    drv.run (0.1);
    auto s = mustCall (e, "startLatencyTest");
    drv.run (s["seconds"].get<double>() + 0.1, 0.3f);
    auto r = mustCall (e, "finishLatencyTest", { { "apply", true } });
    CHECK_MSG (std::abs (r["measuredFrames"].get<int64_t>() - 777) <= 1, r.dump());
    CHECK (r["reliable"].get<bool>());
    CHECK (r["applied"].get<bool>());
    CHECK_NEAR (r["suggestedOffsetMs"].get<double>(), (600 - 777) / 48.0, 0.05);
    CHECK (r["project"].is_null());   // returns measurements only
    CHECK (mustCall (e, "getProject")["project"]["recordOffsetFrames"].get<int64_t>() == 600 - 777);

    // with the offset applied, a recording lands exactly on the beat
    auto clicksMono = th::clicks (kSr * 3, 4800, 24000);
    std::vector<float> st (clicksMono.size() * 2);
    for (size_t i = 0; i < clicksMono.size(); ++i) st[2 * i] = st[2 * i + 1] = clicksMono[i];
    CHECK (th::writeWav (dir + "/c.wav", st, 2, kSr, 32));
    auto imp = mustCall (e, "importAudio", { { "path", dir + "/c.wav" } });
    const std::string lead = trackIdByName (imp["project"], "Lead Vocal");
    const std::string beat = beatTrack (imp["project"]);
    mustCall (e, "seek", { { "frame", 0 } });
    mustCall (e, "startRecording", { { "trackId", lead } });
    drv.run (2.5);
    mustCall (e, "stopRecording");
    mustCall (e, "updateTrack", { { "trackId", beat }, { "mute", true } });
    auto d = exportAndDecode (e, dir + "/v.wav");
    auto L = channel (d, 0);
    const int ref = th::onset (clicksMono, 28800 - 5, 0.05f);
    CHECK_MSG (std::abs (th::onset (L, 28800 - 2000, 0.05f) - ref) <= 1, "aligned after calibration");

    // silence: the test reports failure instead of a wrong number
    mustCall (e, "startLatencyTest");
    drv.run (4.5, 0.0f);
    CHECK (! call (e, "finishLatencyTest")["ok"].get<bool>());
}
