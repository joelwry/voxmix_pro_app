#include "vox/Engine.h"
#include "vox/ProjectSerializer.h"
#include "voxfx/EffectRegistry.h"
#include "FileUtils.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace vox
{
using json = nlohmann::json;

namespace
{
const char* const kLayerNames[] = { "Lead Vocal", "Double", "Harmony 1", "Harmony 2", "Ad-libs", "Backing Vocal" };
const char* const kLayerColors[] = { "#8B5CF6", "#6366F1", "#EC4899", "#F59E0B", "#22C55E", "#06B6D4",
                                     "#3B82F6", "#EF4444", "#A855F7", "#14B8A6" };
constexpr int kMaxUndo = 60;

struct CommandError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail (const std::string& msg) { throw CommandError (msg); }

std::string okJson (json data = json::object())
{
    data["ok"] = true;
    return data.dump();
}

std::string errJson (const std::string& msg)
{
    return json { { "ok", false }, { "error", msg } }.dump();
}

template <typename T>
T arg (const json& a, const char* key, T def)
{
    auto it = a.find (key);
    if (it == a.end() || it->is_null()) return def;
    try { return it->get<T>(); } catch (...) { fail (std::string ("Invalid value for '") + key + "'"); }
}

std::string reqString (const json& a, const char* key)
{
    auto it = a.find (key);
    if (it == a.end() || ! it->is_string() || it->get<std::string>().empty())
        fail (std::string ("Missing '") + key + "'");
    return it->get<std::string>();
}

json descriptorJson (const voxfx::EffectDescriptor& d)
{
    json params = json::array();
    for (const auto& p : d.params)
    {
        const char* type = p.type == voxfx::ParamType::Continuous ? "continuous"
                         : p.type == voxfx::ParamType::Integer ? "integer"
                         : p.type == voxfx::ParamType::Toggle ? "toggle" : "choice";
        json pj = { { "id", p.id }, { "name", p.name }, { "type", type }, { "min", p.min }, { "max", p.max },
                    { "default", p.def }, { "unit", p.unit }, { "log", p.logScale } };
        if (! p.choices.empty()) pj["choices"] = p.choices;
        params.push_back (pj);
    }
    return { { "type", d.typeId }, { "name", d.name }, { "category", voxfx::categoryId (d.category) },
             { "description", d.description }, { "origin", d.origin }, { "params", params } };
}

json slotSpecJson (const EffectSlotSpec& s) { return json::parse (effectSlotToJson (s)); }

float round3 (float v) { return std::round (v * 1000.0f) / 1000.0f; }

/** Copies a slot list with fresh slot ids (presets, duplicated tracks). */
std::vector<EffectSlotSpec> withNewIds (std::vector<EffectSlotSpec> slots)
{
    for (auto& s : slots) s.id = makeId ("fx");
    return slots;
}

/** Fills missing parameters with the effect's defaults and sanitises the rest. */
void normaliseSlot (EffectSlotSpec& s)
{
    const auto* t = voxfx::EffectRegistry::instance().find (s.type);
    if (! t) fail ("Unknown effect type '" + s.type + "'");
    std::map<std::string, float> clean;
    for (const auto& p : t->descriptor->params)
    {
        auto it = s.params.find (p.id);
        clean[p.id] = voxfx::Effect::sanitise (p, it != s.params.end() ? it->second : p.def);
    }
    s.params = std::move (clean);
}

/** Non-destructive "replace" of a timeline range on a track: clips overlapping [s, e) are trimmed
    or split so that the new clip is the only audible material there. */
void clearRange (TrackSpec& t, int64_t s, int64_t e)
{
    std::vector<ClipSpec> out;
    for (const auto& c : t.clips)
    {
        const int64_t cs = c.start, ce = c.start + c.length;
        if (ce <= s || cs >= e) { out.push_back (c); continue; }
        if (cs < s)
        {
            ClipSpec left = c;
            left.length = s - cs;
            left.fadeOut = std::min (left.fadeOut, left.length);
            out.push_back (left);
        }
        if (ce > e)
        {
            ClipSpec right = c;
            right.id = cs < s ? makeId ("clp") : c.id;
            right.offset = c.offset + (e - cs);
            right.start = e;
            right.length = ce - e;
            right.fadeIn = 0;
            out.push_back (right);
        }
    }
    t.clips = std::move (out);
}
} // namespace

//==============================================================================
Engine::Engine (std::string dir, std::unique_ptr<IAudioDevice> dev)
    : dataDir (std::move (dir)), device (std::move (dev))
{
    fs::mkdirs (dataDir + "/projects");
    fs::mkdirs (dataDir + "/presets");
    fs::mkdirs (dataDir + "/exports");
    userPresets.setFile (dataDir + "/presets/user-presets.json");
    std::string e;
    if (! userPresets.load (e)) lastError = e;
    renderer.prepare (kMaxBlock, kMaxBuses);
    blockL.assign (kMaxBlock, 0.0f);
    blockR.assign (kMaxBlock, 0.0f);
    inputMono.assign (16384, 0.0f);
    calibIn.assign ((size_t) (kCalibFirst + kCalibPeriod * (kCalibClicks + 1)) * 2, 0.0f);
    loudness.prepare (48000);
    cache.sampleRate = 48000;
    cache.maxBlock = kMaxBlock;
    (void) voxfx::EffectRegistry::instance();   // build the catalogue up front
    housekeeper = std::thread ([this] { housekeeping(); });
}

Engine::~Engine()
{
    quit.store (true);
    wake.notify_all();
    if (housekeeper.joinable()) housekeeper.join();
    exportProgress.cancel.store (true);
    if (exportThread.joinable()) exportThread.join();
    {
        std::lock_guard<std::recursive_mutex> lock (control);
        if (recorder.active())
        {
            std::string r;
            stopRecordingLocked (r);
        }
        if (hasProject && dirty)
        {
            std::string e;
            saveLocked (e);
        }
    }
    if (device) device->stop();
    live.store (nullptr);
}

//==============================================================================
// Real-time thread

void Engine::onAudio (const float* in, int inChannels, int inFrames, float* out, int outFrames) noexcept
{
    const auto t0 = std::chrono::steady_clock::now();
    callbackSeq.fetch_add (1);   // odd: inside the callback
    MixGraph* g = live.load();

    const int64_t seek = pendingSeek.exchange (-1);
    if (seek >= 0) position.store (seek);
    if (loudnessReset.exchange (false)) loudness.reset();
    const bool isPlaying = playing.load();
    const int64_t pos = position.load();

    // input -> mono (selected channel handled by averaging: phone mics are mono)
    int monoFrames = 0;
    if (in && inChannels > 0 && inFrames > 0)
    {
        monoFrames = std::min (inFrames, (int) inputMono.size());
        const float scale = 1.0f / float (inChannels);
        for (int i = 0; i < monoFrames; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < inChannels; ++c) s += in[i * inChannels + c];
            inputMono[(size_t) i] = s * scale;
        }
    }

    if (calibActive.load())
    {
        // Calibration: output a click train (everything else muted) and capture the input.
        const int64_t outBase = calibOutCount.load (std::memory_order_relaxed);
        for (int i = 0; i < outFrames; ++i)
        {
            const int64_t k = outBase + i - kCalibFirst;
            float v = 0.0f;
            if (k >= 0 && (k % kCalibPeriod) < 96 && k / kCalibPeriod < kCalibClicks)
                v = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * double (k % kCalibPeriod) / 48000.0);
            out[2 * i] = out[2 * i + 1] = v;
        }
        calibOutCount.store (outBase + outFrames, std::memory_order_relaxed);
        int64_t inBase = calibInCount.load (std::memory_order_relaxed);
        for (int i = 0; i < monoFrames && inBase < (int64_t) calibIn.size(); ++i)
            calibIn[(size_t) inBase++] = inputMono[(size_t) i];
        calibInCount.store (inBase, std::memory_order_relaxed);
        callbacks.fetch_add (1, std::memory_order_relaxed);
        callbackSeq.fetch_add (1);
        return;
    }

    if (recorder.isArmed() || recorder.isCapturing())
        recorder.capture (monoFrames > 0 ? inputMono.data() : nullptr, monoFrames > 0 ? monoFrames : outFrames, pos);

    MonitorInput mon;
    const int monTrack = recordTrackIndex.load();
    const bool monitoring = monitorEnabled.load() && monoFrames > 0 && monTrack >= 0;

    for (int done = 0; done < outFrames; done += kMaxBlock)
    {
        const int m = std::min (kMaxBlock, outFrames - done);
        if (g)
        {
            MonitorInput mc;
            if (monitoring && done + m <= monoFrames)
            {
                mc.mono = inputMono.data() + done;
                mc.trackIndex = monTrack;
            }
            renderer.render (*g, pos + done, m, isPlaying, mc, blockL.data(), blockR.data(), &loudness);
        }
        else
        {
            std::fill (blockL.begin(), blockL.begin() + m, 0.0f);
            std::fill (blockR.begin(), blockR.begin() + m, 0.0f);
        }
        float* o = out + 2 * done;
        for (int i = 0; i < m; ++i)
        {
            o[2 * i] = std::clamp (blockL[(size_t) i], -1.0f, 1.0f);
            o[2 * i + 1] = std::clamp (blockR[(size_t) i], -1.0f, 1.0f);
        }
    }
    (void) mon;

    if (isPlaying)
    {
        position.store (pos + outFrames);
        // stop automatically two seconds after the end of the song (not while recording)
        if (g && g->endFrame > 0 && ! recorder.isCapturing() && pos > g->endFrame + (int64_t) (2.0 * g->sampleRate))
        {
            playing.store (false);
            autoStopped.store (true);
        }
    }

    callbacks.fetch_add (1, std::memory_order_relaxed);
    callbackFrames.store (outFrames, std::memory_order_relaxed);
    callbackSeq.fetch_add (1);   // even: outside
    if (g && outFrames > 0)
    {
        const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double budget = double (outFrames) / g->sampleRate;
        const float load = float (used / budget);
        cpuLoad.store (0.9f * cpuLoad.load (std::memory_order_relaxed) + 0.1f * load, std::memory_order_relaxed);
    }
}

void Engine::onDeviceError (const std::string& message) noexcept
{
    deviceFailed.store (true);
    std::lock_guard<std::mutex> l (statusMutex);
    deviceError = message;
}

//==============================================================================
// Graph publication

void Engine::publish (MixGraphPtr g)
{
    MixGraphPtr old = std::move (liveOwner);
    liveOwner = std::move (g);
    live.store (liveOwner.get());
    if (old)
        graveyard.push_back ({ std::move (old), callbackSeq.load() });
    {
        std::lock_guard<std::mutex> l (statusMutex);
        meterTable.clear();
        if (liveOwner)
            for (const auto& t : liveOwner->tracks) meterTable.push_back ({ t.id, t.controls });
        masterControls = liveOwner ? liveOwner->master : nullptr;
    }
    collectGarbage();
}

void Engine::collectGarbage()
{
    const uint64_t now = callbackSeq.load();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [now] (const Retired& r)
                                     {
                                         // even at retirement: the audio thread was not inside a
                                         // callback, so it can only see the new graph
                                         return (r.seq % 2) == 0 || now != r.seq;
                                     }),
                     graveyard.end());
}

void Engine::rebuildGraph()
{
    if (! hasProject) { publish (nullptr); return; }
    std::vector<std::string> warnings;
    auto g = buildGraph (project, sources, cache, &warnings);
    applyControls (project, cache);
    publish (std::move (g));
    pruneCache (project, cache);
}

void Engine::markDirty()
{
    dirty = true;
    lastChangeMs = nowMs();
    project.modifiedAt = lastChangeMs;
}

void Engine::pushUndo()
{
    undoStack.push_back (projectToJson (project, false));
    if ((int) undoStack.size() > kMaxUndo) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool Engine::ensureProject (std::string& error) const
{
    if (! hasProject) error = "No project is open";
    return hasProject;
}

std::string Engine::projectFolder() const { return projectFolder (project.id); }
std::string Engine::projectFolder (const std::string& id) const { return dataDir + "/projects/" + id; }
std::string Engine::mediaPath (const std::string& file) const { return projectFolder() + "/" + file; }

bool Engine::saveLocked (std::string& error)
{
    if (! hasProject) { error = "No project is open"; return false; }
    if (fs::freeSpace (projectFolder()) >= 0 && fs::freeSpace (projectFolder()) < 64 * 1024)
    {
        error = "Storage is full: free some space to save the project.";
        return false;
    }
    if (! saveProjectFile (project, projectFolder(), error)) return false;
    dirty = false;
    return true;
}

bool Engine::loadSources (std::vector<std::string>& warnings)
{
    sources.clear();
    for (const auto& s : project.sources)
    {
        std::string err;
        const std::string path = mediaPath (s.file);
        if (! fs::exists (path)) continue;   // reported by the loader as missing
        auto src = loadSourceFile (s.id, path, project.sampleRate, err);
        if (src) sources[s.id] = src;
        else warnings.push_back ("Could not load " + (s.originalName.empty() ? s.file : s.originalName) + ": " + err);
    }
    return true;
}

int64_t Engine::latencyCompensation()
{
    int64_t comp = assumedLatency;
    if (device)
    {
        const auto st = device->status();
        if (st.running) comp = st.inputLatencyFrames + st.outputLatencyFrames;
    }
    if (liveOwner) comp += liveOwner->masterLatency;
    return comp;
}

void Engine::restartDeviceLocked()
{
    if (! device || ! hasProject) return;
    device->stop();
    std::string err;
    if (! device->start (project.sampleRate, wantInput, this, err))
    {
        lastError = err;
        if (wantInput)
        {
            // fall back to playback only so the user can still listen
            std::string e2;
            if (device->start (project.sampleRate, false, this, e2)) lastError = err + " (playback only)";
        }
    }
    deviceFailed.store (false);
}

void Engine::housekeeping()
{
    std::unique_lock<std::recursive_mutex> lock (control, std::defer_lock);
    while (! quit.load())
    {
        {
            std::unique_lock<std::recursive_mutex> l (control);
            wake.wait_for (l, std::chrono::milliseconds (500));
            if (quit.load()) break;
            collectGarbage();
            if (autoStopped.exchange (false)) { /* transport already stopped by the audio thread */ }
            // autosave after 2 s without changes, never during a recording
            if (hasProject && dirty && ! recorder.active() && nowMs() - lastChangeMs > 2000)
            {
                std::string e;
                if (! saveLocked (e)) lastError = e;
            }
        }
    }
}

//==============================================================================
// Recording

void Engine::stopRecordingLocked (std::string& resultJson)
{
    recorder.disarm();
    // wait until the audio thread has acknowledged (it stops capturing at its next block)
    if (device && device->status().running)
        for (int i = 0; i < 100 && recorder.isCapturing(); ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (3));
    playing.store (false);
    monitorEnabled.store (false);
    recordTrackIndex.store (-1);
    TakeResult take = recorder.finish();
    std::remove ((take.path.substr (0, take.path.size() - 4) + ".json").c_str());   // sidecar

    if (! take.ok)
    {
        std::remove (take.path.c_str());
        resultJson = errJson (take.error);
        return;
    }
    TrackSpec* track = project.findTrack (take.trackId);
    if (! track)
    {
        resultJson = errJson ("The layer was removed during recording; the take is kept in the media folder.");
        return;
    }

    pushUndo();
    const int64_t comp = latencyCompensation();
    int64_t start = take.startPosition - comp + project.recordOffsetFrames;
    int64_t offset = 0;
    if (start < 0) { offset = -start; start = 0; }

    SourceSpec src;
    src.id = makeId ("src");
    const std::string folder = projectFolder() + "/";
    src.file = take.path.rfind (folder, 0) == 0 ? take.path.substr (folder.size()) : take.path;
    src.originalName = track->name + " take";
    src.frames = take.frames;
    src.channels = 1;
    src.sampleRate = project.sampleRate;
    src.recorded = true;
    project.sources.push_back (src);
    std::vector<std::vector<int16_t>> planar (1);
    planar[0] = std::move (take.samples);
    sources[src.id] = std::make_shared<AudioSource> (src.id, project.sampleRate, 1, std::move (planar));

    ClipSpec clip;
    clip.id = makeId ("clp");
    clip.sourceId = src.id;
    clip.start = start;
    clip.offset = offset;
    clip.length = std::max<int64_t> (0, take.frames - offset);
    clip.name = track->name;
    if (clip.length > 0)
    {
        clearRange (*track, clip.start, clip.start + clip.length);
        track->clips.push_back (clip);
        std::sort (track->clips.begin(), track->clips.end(), [] (const ClipSpec& a, const ClipSpec& b) { return a.start < b.start; });
    }
    markDirty();
    rebuildGraph();
    std::string e;
    const bool saved = saveLocked (e);

    json r;
    r["clip"] = { { "id", clip.id }, { "trackId", track->id }, { "start", clip.start }, { "length", clip.length },
                  { "latencyCompensation", comp }, { "recordedFrames", take.frames } };
    if (! take.error.empty()) r["warning"] = take.error;
    if (! saved) r["warning"] = "Recorded, but the project could not be saved: " + e;
    r["project"] = json::parse (projectToJson (project, false));
    resultJson = okJson (r);
}

//==============================================================================
// Status

std::string Engine::status()
{
    json s;
    const int sr = hasProjectAtomic.load() ? projectRate.load() : 48000;
    s["position"] = position.load();
    s["sampleRate"] = sr;
    s["playing"] = playing.load();
    s["recording"] = recorder.isArmed() || recorder.isCapturing();
    s["cpu"] = round3 (cpuLoad.load());
    s["callbacks"] = callbacks.load();
    s["bufferFrames"] = callbackFrames.load();
    {
        std::lock_guard<std::mutex> l (statusMutex);
        json tracks = json::array();
        for (auto& [id, c] : meterTable)
            tracks.push_back ({ { "id", id }, { "l", round3 (c->meter.takePeakL()) }, { "r", round3 (c->meter.takePeakR()) },
                                { "clip", c->meter.clipped() } });
        s["tracks"] = tracks;
        if (masterControls)
            s["master"] = { { "l", round3 (masterControls->meter.takePeakL()) }, { "r", round3 (masterControls->meter.takePeakR()) },
                            { "clip", masterControls->meter.clipped() } };
        if (! deviceError.empty()) s["deviceError"] = deviceError;
    }
    s["loudness"] = { { "momentary", round3 (loudness.momentary()) }, { "shortTerm", round3 (loudness.shortTerm()) },
                      { "integrated", round3 (loudness.integrated()) }, { "truePeak", round3 (loudness.truePeakDb()) } };
    if (recorder.isArmed() || recorder.isCapturing())
        s["record"] = { { "frames", recorder.capturedFrames() }, { "dropped", recorder.droppedFrames() },
                        { "inputPeak", round3 (recorder.takeInputPeak()) } };
    s["export"] = { { "running", exportRunning.load() }, { "progress", round3 (exportProgress.progress.load()) } };
    {
        std::lock_guard<std::mutex> l (exportMutex);
        if (! exportResult.empty() && ! exportRunning.load()) s["export"]["result"] = json::parse (exportResult);
    }
    return s.dump();
}

//==============================================================================
// Commands

std::string Engine::call (const std::string& method, const std::string& args)
{
    std::lock_guard<std::recursive_mutex> lock (control);
    try
    {
        auto r = dispatch (method, args.empty() ? "{}" : args);
        collectGarbage();
        return r;
    }
    catch (const CommandError& e)
    {
        return errJson (e.what());
    }
    catch (const std::exception& e)
    {
        return errJson (std::string ("Internal error: ") + e.what());
    }
}

std::string Engine::dispatch (const std::string& method, const std::string& argsText)
{
    const json a = json::parse (argsText, nullptr, false);
    if (a.is_discarded() || ! a.is_object()) fail ("Arguments must be a JSON object");
    std::string err;

    auto projectReply = [this] (json extra = json::object())
    {
        extra["project"] = json::parse (projectToJson (project, false));
        extra["canUndo"] = ! undoStack.empty();
        extra["canRedo"] = ! redoStack.empty();
        return okJson (extra);
    };
    auto needProject = [this] { std::string e; if (! ensureProject (e)) fail (e); };
    auto needIdle = [this] { if (recorder.active()) fail ("Stop the recording first"); };
    auto track = [this] (const std::string& id) -> TrackSpec&
    {
        auto* t = project.findTrack (id);
        if (! t) fail ("Layer not found");
        return *t;
    };
    auto clipRef = [this] (const std::string& id, TrackSpec** owner = nullptr) -> ClipSpec&
    {
        auto* c = project.findClip (id, owner);
        if (! c) fail ("Clip not found");
        return *c;
    };
    // effect destinations: "track:<id>", "bus:<id>", "master" (Master FX)
    auto chainOf = [this] (const std::string& target) -> std::vector<EffectSlotSpec>&
    {
        if (target == "master") return project.master.fx;
        if (target.rfind ("track:", 0) == 0)
        {
            auto* t = project.findTrack (target.substr (6));
            if (! t) fail ("Layer not found for effect destination");
            return t->fx;
        }
        if (target.rfind ("bus:", 0) == 0)
        {
            auto* b = project.findBus (target.substr (4));
            if (! b) fail ("Bus not found for effect destination");
            return b->fx;
        }
        fail ("Unknown effect destination '" + target + "'");
    };
    auto slotSpec = [&] (const std::string& target, const std::string& slotId) -> EffectSlotSpec&
    {
        if (target == "mastering") return project.master.mastering;
        for (auto& s : chainOf (target)) if (s.id == slotId) return s;
        fail ("Effect not found");
    };

    //-------------------------------------------------------------------- engine
    if (method == "getInfo")
    {
        json d = json::object();
        if (device)
        {
            const auto st = device->status();
            d = { { "running", st.running }, { "inputOpen", st.inputOpen }, { "sampleRate", st.sampleRate }, { "api", st.api },
                  { "framesPerBurst", st.framesPerBurst }, { "bufferSize", st.bufferSizeFrames },
                  { "outputLatency", st.outputLatencyFrames }, { "inputLatency", st.inputLatencyFrames },
                  { "latencyMeasured", st.latencyMeasured }, { "xruns", st.xruns }, { "lowLatency", st.lowLatency },
                  { "exclusive", st.exclusive }, { "outputDevice", st.outputDevice }, { "inputDevice", st.inputDevice } };
        }
        return okJson ({ { "version", kEngineVersion }, { "maxLayers", kMaxVocalLayers }, { "maxBuses", kMaxBuses },
                         { "device", d }, { "hasProject", hasProject }, { "projectId", hasProject ? project.id : "" },
                         { "lastError", lastError } });
    }
    if (method == "getEffectCatalog")
    {
        json list = json::array();
        for (const auto& t : voxfx::EffectRegistry::instance().types())
            if (t.descriptor->category != voxfx::EffectCategory::Mastering || a.value ("includeMastering", false))
                list.push_back (descriptorJson (*t.descriptor));
        return okJson ({ { "effects", list } });
    }

    //-------------------------------------------------------------------- device
    if (method == "startAudio")
    {
        needProject();
        wantInput = arg<bool> (a, "input", wantInput);
        restartDeviceLocked();
        if (! device) fail ("No audio device available");
        const auto st = device->status();
        if (! st.running) fail (lastError.empty() ? "The audio device could not be started" : lastError);
        return dispatch ("getInfo", "{}");
    }
    if (method == "stopAudio")
    {
        if (recorder.active()) { std::string r; stopRecordingLocked (r); }
        playing.store (false);
        if (device) device->stop();
        return okJson();
    }

    //-------------------------------------------------------------------- projects
    if (method == "listProjects")
    {
        json list = json::array();
        for (const auto& id : fs::list (dataDir + "/projects"))
        {
            Project p;
            auto r = loadProjectFile (projectFolder (id), p);
            if (! r.ok) continue;
            int layers = p.vocalLayerCount(), clips = 0;
            for (const auto& t : p.tracks) clips += (int) t.clips.size();
            std::string beat;
            for (const auto& t : p.tracks)
                if (t.kind == TrackKind::Beat && ! t.clips.empty())
                    if (const auto* s = p.findSource (t.clips.front().sourceId)) beat = s->originalName;
            list.push_back ({ { "id", p.id }, { "name", p.name }, { "modifiedAt", p.modifiedAt }, { "createdAt", p.createdAt },
                              { "seconds", double (p.contentEnd()) / p.sampleRate }, { "layers", layers }, { "clips", clips },
                              { "beat", beat }, { "bpm", p.bpm } });
        }
        std::sort (list.begin(), list.end(), [] (const json& x, const json& y) { return x["modifiedAt"].get<int64_t>() > y["modifiedAt"].get<int64_t>(); });
        return okJson ({ { "projects", list } });
    }
    if (method == "createProject" || method == "openProject")
    {
        needIdle();
        if (hasProject && dirty) { std::string e; saveLocked (e); }
        playing.store (false);
        if (device) device->stop();
        publish (nullptr);
        sources.clear();
        cache = InstanceCache {};
        undoStack.clear(); redoStack.clear();
        hasProject = false;
        hasProjectAtomic.store (false);

        LoadResult lr;
        if (method == "createProject")
        {
            Project p;
            p.id = makeId ("prj");
            p.name = arg<std::string> (a, "name", "Untitled Project");
            if (p.name.empty()) p.name = "Untitled Project";
            p.sampleRate = std::clamp (arg<int> (a, "sampleRate", 48000), 22050, 96000);
            p.bpm = std::clamp (arg<double> (a, "bpm", 120.0), 20.0, 300.0);
            p.createdAt = p.modifiedAt = nowMs();
            p.master.mastering.id = "mastering";
            p.master.mastering.type = "mastering";
            normaliseSlot (p.master.mastering);
            TrackSpec lead;
            lead.id = makeId ("trk");
            lead.name = kLayerNames[0];
            lead.color = kLayerColors[0];
            p.tracks.push_back (lead);
            if (! fs::mkdirs (projectFolder (p.id) + "/media")) fail ("Cannot create the project folder (storage full?)");
            project = std::move (p);
            lr.ok = true;
        }
        else
        {
            const std::string id = reqString (a, "id");
            lr = loadProjectFile (projectFolder (id), project);
            if (! lr.ok) fail (lr.error);
            fs::mkdirs (projectFolder() + "/media");
        }
        hasProject = true;
        projectRate.store (project.sampleRate);
        hasProjectAtomic.store (true);
        cache.sampleRate = project.sampleRate;
        cache.maxBlock = kMaxBlock;
        recorder.prepare (project.sampleRate);
        loudness.prepare (project.sampleRate);
        position.store (0);
        pendingSeek.store (-1);
        std::vector<std::string> warnings = lr.warnings;
        loadSources (warnings);

        // recover takes interrupted by a crash (sidecar .json still present)
        json recovered = json::array();
        for (const auto& f : fs::list (projectFolder() + "/media"))
        {
            if (f.size() < 5 || f.compare (f.size() - 5, 5, ".json") != 0 || f.rfind ("rec-", 0) != 0) continue;
            std::string side;
            const std::string sidePath = projectFolder() + "/media/" + f;
            readFile (sidePath, side);
            const json sj = json::parse (side, nullptr, false);
            const std::string wav = "media/" + f.substr (0, f.size() - 5) + ".wav";
            const int64_t frames = repairWavHeader (mediaPath (wav));
            std::remove (sidePath.c_str());
            if (sj.is_discarded() || frames <= 0) continue;
            auto* t = project.findTrack (sj.value ("trackId", ""));
            if (! t) continue;
            SourceSpec src;
            src.id = makeId ("src");
            src.file = wav;
            src.originalName = t->name + " (recovered take)";
            src.frames = frames; src.channels = 1; src.sampleRate = project.sampleRate; src.recorded = true;
            std::string e;
            auto loaded = loadSourceFile (src.id, mediaPath (wav), project.sampleRate, e);
            if (! loaded) continue;
            project.sources.push_back (src);
            sources[src.id] = loaded;
            ClipSpec c;
            c.id = makeId ("clp");
            c.sourceId = src.id;
            c.start = std::max<int64_t> (0, sj.value ("start", (int64_t) 0) - sj.value ("latency", (int64_t) 0));
            c.length = loaded->frames();
            c.name = src.originalName;
            t->clips.push_back (c);
            recovered.push_back ({ { "trackId", t->id }, { "frames", frames } });
            warnings.push_back ("A recording interrupted by the app closing was recovered on '" + t->name + "'.");
            dirty = true;
        }

        rebuildGraph();
        if (method == "createProject" || dirty)
            if (! saveLocked (err)) warnings.push_back (err);
        restartDeviceLocked();
        return projectReply ({ { "warnings", warnings }, { "recovered", recovered }, { "usedBackup", lr.usedBackup } });
    }
    if (method == "getProject")
    {
        needProject();
        return projectReply();
    }
    if (method == "saveProject")
    {
        needProject();
        if (! saveLocked (err)) fail (err);
        return okJson ({ { "savedAt", nowMs() } });
    }
    if (method == "closeProject")
    {
        if (! hasProject) return okJson();
        if (recorder.active()) { std::string r; stopRecordingLocked (r); }
        playing.store (false);
        if (dirty && ! saveLocked (err)) fail (err);
        if (device) device->stop();
        publish (nullptr);
        hasProject = false;
        hasProjectAtomic.store (false);
        sources.clear();
        cache = InstanceCache {};
        undoStack.clear(); redoStack.clear();
        return okJson();
    }
    if (method == "deleteProject")
    {
        const std::string id = reqString (a, "id");
        if (hasProject && project.id == id) fail ("Close the project before deleting it");
        if (id.find ('/') != std::string::npos || id.find ("..") != std::string::npos) fail ("Invalid project id");
        if (! fs::isDir (projectFolder (id)) || ! fs::removeAll (projectFolder (id))) fail ("The project could not be deleted");
        return okJson();
    }
    if (method == "updateProject")
    {
        needProject();
        pushUndo();
        if (a.contains ("name")) { auto n = arg<std::string> (a, "name", project.name); if (! n.empty()) project.name = n; }
        if (a.contains ("bpm")) project.bpm = std::clamp (arg<double> (a, "bpm", project.bpm), 20.0, 300.0);
        if (a.contains ("timeSigNum")) project.timeSigNum = std::clamp (arg<int> (a, "timeSigNum", 4), 1, 32);
        if (a.contains ("timeSigDen")) project.timeSigDen = std::clamp (arg<int> (a, "timeSigDen", 4), 1, 32);
        if (a.contains ("recordOffsetMs"))
            project.recordOffsetFrames = (int64_t) std::llround (std::clamp (arg<double> (a, "recordOffsetMs", 0.0), -500.0, 500.0) * 0.001 * project.sampleRate);
        if (a.contains ("inputMonitoring")) project.inputMonitoring = arg<bool> (a, "inputMonitoring", false);
        if (a.contains ("export") && a["export"].is_object())
        {
            const auto& e = a["export"];
            auto& x = project.exportSettings;
            x.bitDepth = arg<int> (e, "bitDepth", x.bitDepth);
            if (x.bitDepth != 16 && x.bitDepth != 24 && x.bitDepth != 32) x.bitDepth = 24;
            x.sampleRate = arg<int> (e, "sampleRate", x.sampleRate);
            x.channels = std::clamp (arg<int> (e, "channels", x.channels), 1, 2);
            x.includeTail = arg<bool> (e, "includeTail", x.includeTail);
        }
        markDirty();
        rebuildGraph();   // bpm affects tempo-synced effects
        return projectReply();
    }

    //-------------------------------------------------------------------- media
    if (method == "importAudio")
    {
        needProject();
        needIdle();
        const std::string path = reqString (a, "path");
        std::string name = arg<std::string> (a, "name", "");
        if (name.empty()) name = path.substr (path.find_last_of ('/') + 1);
        DecodedAudio dec;
        if (! decodeAudioFile (path, dec, err)) fail (err);
        const double seconds = double (dec.frames()) / std::max (1, dec.sampleRate);
        if (seconds > 20 * 60) fail ("The file is longer than 20 minutes");
        const std::string id = makeId ("src");
        auto src = makeSource (id, dec, project.sampleRate);
        if (! src) fail ("The file contains no audio");
        dec = DecodedAudio {};

        // store a normalised copy (project rate, 16-bit WAV) in the project's media folder
        SourceSpec spec;
        spec.id = id;
        spec.file = "media/" + id + ".wav";
        spec.originalName = name;
        spec.frames = src->frames();
        spec.channels = src->channels();
        spec.sampleRate = project.sampleRate;
        {
            const int64_t bytes = src->frames() * src->channels() * 2 + 44;
            const int64_t free = fs::freeSpace (projectFolder());
            if (free >= 0 && free < bytes + 1024 * 1024) fail ("Not enough storage to import this file");
            WavWriter w;
            if (! w.open (mediaPath (spec.file), project.sampleRate, src->channels(), 16, err)) fail (err);
            std::vector<float> buf (8192 * 2);
            for (int64_t f = 0; f < src->frames(); f += 8192)
            {
                const int n = (int) std::min<int64_t> (8192, src->frames() - f);
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < src->channels(); ++c)
                        buf[(size_t) (i * src->channels() + c)] = src->sample (c, f + i);
                if (! w.write (buf.data(), n)) { w.close(); std::remove (mediaPath (spec.file).c_str()); fail ("Storage is full"); }
            }
            if (! w.close()) { std::remove (mediaPath (spec.file).c_str()); fail ("The imported file could not be stored"); }
        }

        pushUndo();
        project.sources.push_back (spec);
        sources[id] = src;
        const std::string targetId = arg<std::string> (a, "trackId", "");
        TrackSpec* t = targetId.empty() ? nullptr : project.findTrack (targetId);
        if (! t)
        {
            for (auto& tr : project.tracks) if (tr.kind == TrackKind::Beat) { t = &tr; break; }
            if (! t)
            {
                TrackSpec beat;
                beat.id = makeId ("trk");
                beat.name = "Beat";
                beat.kind = TrackKind::Beat;
                beat.color = "#38BDF8";
                project.tracks.insert (project.tracks.begin(), beat);
                t = &project.tracks.front();
            }
            t->clips.clear();   // a new beat replaces the old one (its media stays for undo)
        }
        ClipSpec c;
        c.id = makeId ("clp");
        c.sourceId = id;
        c.start = t->kind == TrackKind::Beat ? 0 : std::max<int64_t> (0, position.load());
        c.length = src->frames();
        c.name = name;
        if (t->kind != TrackKind::Beat) clearRange (*t, c.start, c.start + c.length);
        t->clips.push_back (c);
        std::sort (t->clips.begin(), t->clips.end(), [] (const ClipSpec& x, const ClipSpec& y) { return x.start < y.start; });
        if (project.meta["beatName"].empty() && t->kind == TrackKind::Beat) project.meta["beatName"] = name;
        markDirty();
        rebuildGraph();
        if (! saveLocked (err)) fail ("Imported, but the project could not be saved: " + err);
        return projectReply ({ { "sourceId", id }, { "clipId", c.id }, { "trackId", t->id }, { "seconds", double (src->frames()) / project.sampleRate },
                               { "originalRate", dec.sampleRate } });
    }
    if (method == "getWaveform")
    {
        needProject();
        const int buckets = std::clamp (arg<int> (a, "buckets", 200), 1, 4096);
        std::string sourceId = arg<std::string> (a, "sourceId", "");
        int64_t start = arg<int64_t> (a, "start", 0), length = arg<int64_t> (a, "length", 0);
        if (a.contains ("clipId"))
        {
            const auto& c = clipRef (reqString (a, "clipId"));
            sourceId = c.sourceId; start = c.offset; length = c.length;
        }
        auto it = sources.find (sourceId);
        if (it == sources.end()) fail ("Audio not loaded");
        if (length <= 0) length = it->second->frames() - start;
        auto w = it->second->waveform (start, length, buckets);
        for (auto& v : w) v = round3 (v);
        return okJson ({ { "peaks", w } });
    }

    //-------------------------------------------------------------------- transport
    if (method == "play")
    {
        needProject();
        if (device && ! device->status().running) restartDeviceLocked();
        const int64_t end = liveOwner ? liveOwner->endFrame : 0;
        if (end > 0 && position.load() >= end) { pendingSeek.store (0); if (! device || ! device->status().running) position.store (0); }
        playStartPosition.store (position.load());
        loudnessReset.store (true);
        playing.store (true);
        return okJson();
    }
    if (method == "pause")
    {
        if (recorder.active()) { std::string r; stopRecordingLocked (r); return r; }
        playing.store (false);
        return okJson ({ { "position", position.load() } });
    }
    if (method == "stop")
    {
        if (recorder.active()) { std::string r; stopRecordingLocked (r); return r; }
        playing.store (false);
        const int64_t to = arg<bool> (a, "toStart", false) ? 0 : playStartPosition.load();
        if (device && device->status().running) pendingSeek.store (to); else position.store (to);
        return okJson ({ { "position", to } });
    }
    if (method == "seek")
    {
        needProject();
        if (recorder.active()) fail ("Cannot seek while recording");
        int64_t f = a.contains ("seconds") ? (int64_t) std::llround (arg<double> (a, "seconds", 0.0) * project.sampleRate)
                                           : arg<int64_t> (a, "frame", 0);
        f = std::max<int64_t> (0, f);
        if (device && device->status().running) pendingSeek.store (f); else position.store (f);
        playStartPosition.store (f);
        return okJson ({ { "position", f } });
    }

    //-------------------------------------------------------------------- recording
    if (method == "startRecording")
    {
        needProject();
        needIdle();
        auto& t = track (reqString (a, "trackId"));
        if (t.kind != TrackKind::Vocal) fail ("Recording goes on a vocal layer, not the beat track");
        if (device)
        {
            if (! wantInput || ! device->status().inputOpen)
            {
                wantInput = true;
                restartDeviceLocked();
            }
            const auto st = device->status();
            if (! st.running) fail (lastError.empty() ? "The audio device is not running" : lastError);
            if (! st.inputOpen) fail ("The microphone could not be opened. Check the microphone permission.");
        }
        const int64_t free = fs::freeSpace (projectFolder());
        if (free >= 0 && free < 20ll * 1024 * 1024) fail ("Storage is almost full: free some space before recording");

        const std::string id = makeId ("rec");
        const std::string path = projectFolder() + "/media/" + id + ".wav";
        if (! recorder.begin (path, t.id, err)) fail (err);
        int idx = 0;
        for (size_t i = 0; i < project.tracks.size(); ++i) if (project.tracks[i].id == t.id) idx = (int) i;
        recordTrackIndex.store (idx);
        recordTrackId = t.id;
        monitorEnabled.store (project.inputMonitoring);
        const int64_t startPos = position.load();
        {
            // crash-recovery sidecar
            json side = { { "trackId", t.id }, { "start", startPos }, { "latency", latencyCompensation() - project.recordOffsetFrames } };
            std::string e;
            writeFileAtomic (projectFolder() + "/media/" + id + ".json", side.dump(), e);
        }
        playStartPosition.store (startPos);
        loudnessReset.store (true);
        playing.store (true);
        return okJson ({ { "trackId", t.id }, { "startPosition", startPos }, { "latencyCompensation", latencyCompensation() } });
    }
    if (method == "stopRecording")
    {
        if (! recorder.active()) fail ("Not recording");
        std::string r;
        stopRecordingLocked (r);
        return r;
    }
    if (method == "setMonitoring")
    {
        needProject();
        project.inputMonitoring = arg<bool> (a, "enabled", false);
        monitorEnabled.store (project.inputMonitoring && recorder.active());
        markDirty();
        return projectReply();
    }

    //-------------------------------------------------------------------- tracks
    if (method == "addTrack")
    {
        needProject();
        needIdle();
        const int layers = project.vocalLayerCount();
        if (layers >= kMaxVocalLayers) fail ("A project can have up to 10 vocal layers");
        pushUndo();
        TrackSpec t;
        t.id = makeId ("trk");
        t.name = arg<std::string> (a, "name", "");
        if (t.name.empty()) t.name = layers < 6 ? kLayerNames[layers] : "Layer " + std::to_string (layers + 1);
        t.color = arg<std::string> (a, "color", kLayerColors[layers % 10]);
        project.tracks.push_back (t);
        markDirty();
        rebuildGraph();
        return projectReply ({ { "trackId", t.id } });
    }
    if (method == "removeTrack")
    {
        needProject();
        needIdle();
        const std::string id = reqString (a, "trackId");
        track (id);
        pushUndo();
        project.tracks.erase (std::remove_if (project.tracks.begin(), project.tracks.end(), [&] (const TrackSpec& t) { return t.id == id; }),
                              project.tracks.end());
        markDirty();
        rebuildGraph();
        return projectReply();
    }
    if (method == "duplicateTrack")
    {
        needProject();
        needIdle();
        auto& src = track (reqString (a, "trackId"));
        if (src.kind == TrackKind::Vocal && project.vocalLayerCount() >= kMaxVocalLayers) fail ("A project can have up to 10 vocal layers");
        pushUndo();
        TrackSpec t = src;
        t.id = makeId ("trk");
        t.name = src.name + " copy";
        t.solo = false;
        t.fx = withNewIds (t.fx);
        for (auto& c : t.clips) c.id = makeId ("clp");
        auto it = std::find_if (project.tracks.begin(), project.tracks.end(), [&] (const TrackSpec& x) { return x.id == src.id; });
        project.tracks.insert (it + 1, t);
        markDirty();
        rebuildGraph();
        return projectReply ({ { "trackId", t.id } });
    }
    if (method == "updateTrack")
    {
        needProject();
        auto& t = track (reqString (a, "trackId"));
        const bool structural = a.contains ("name") || a.contains ("color") || a.contains ("output") || a.contains ("mute")
                                || a.contains ("solo") || arg<bool> (a, "commit", false);
        if (structural) pushUndo();
        if (a.contains ("name")) { auto n = arg<std::string> (a, "name", t.name); if (! n.empty()) t.name = n.substr (0, 40); }
        if (a.contains ("color")) t.color = arg<std::string> (a, "color", t.color);
        if (a.contains ("volumeDb")) t.volumeDb = std::clamp (arg<float> (a, "volumeDb", t.volumeDb), -60.0f, 12.0f);
        if (a.contains ("pan")) t.pan = std::clamp (arg<float> (a, "pan", t.pan), -1.0f, 1.0f);
        if (a.contains ("mute")) t.mute = arg<bool> (a, "mute", t.mute);
        if (a.contains ("solo")) t.solo = arg<bool> (a, "solo", t.solo);
        bool rebuild = false;
        if (a.contains ("output"))
        {
            const auto out = arg<std::string> (a, "output", "master");
            if (out != "master" && ! project.findBus (out)) fail ("Bus not found");
            t.output = out;
            rebuild = true;
        }
        markDirty();
        if (rebuild) rebuildGraph(); else applyControls (project, cache);
        return a.value ("quiet", false) ? okJson() : projectReply();
    }
    if (method == "moveTrack")
    {
        needProject();
        needIdle();
        const std::string id = reqString (a, "trackId");
        auto it = std::find_if (project.tracks.begin(), project.tracks.end(), [&] (const TrackSpec& x) { return x.id == id; });
        if (it == project.tracks.end()) fail ("Layer not found");
        pushUndo();
        TrackSpec t = *it;
        project.tracks.erase (it);
        const int idx = std::clamp (arg<int> (a, "index", 0), 0, (int) project.tracks.size());
        project.tracks.insert (project.tracks.begin() + idx, t);
        markDirty();
        rebuildGraph();
        return projectReply();
    }

    //-------------------------------------------------------------------- clips
    if (method == "moveClip" || method == "trimClip" || method == "setClipGain" || method == "setClipFades")
    {
        needProject();
        needIdle();
        TrackSpec* owner = nullptr;
        auto& c = clipRef (reqString (a, "clipId"), &owner);
        const auto srcIt = sources.find (c.sourceId);
        const int64_t srcFrames = srcIt != sources.end() ? srcIt->second->frames() : c.offset + c.length;
        pushUndo();
        if (method == "moveClip")
        {
            c.start = std::max<int64_t> (0, arg<int64_t> (a, "start", c.start));
            const std::string to = arg<std::string> (a, "trackId", owner->id);
            if (to != owner->id)
            {
                auto& dst = track (to);
                ClipSpec moved = c;
                owner->clips.erase (std::remove_if (owner->clips.begin(), owner->clips.end(), [&] (const ClipSpec& x) { return x.id == moved.id; }), owner->clips.end());
                dst.clips.push_back (moved);
                owner = &dst;
            }
        }
        else if (method == "trimClip")
        {
            // timeline start, source offset and length; the end can be trimmed by length alone
            int64_t start = arg<int64_t> (a, "start", c.start);
            int64_t offset = arg<int64_t> (a, "offset", c.offset + (start - c.start));
            int64_t length = arg<int64_t> (a, "length", c.length - (start - c.start));
            if (offset < 0) { start -= offset; length += offset; offset = 0; }
            if (start < 0) { offset -= start; length += start; start = 0; }
            length = std::min (length, srcFrames - offset);
            if (length < 64) fail ("The clip would be too short");
            c.start = start; c.offset = offset; c.length = length;
            c.fadeIn = std::min (c.fadeIn, c.length / 2);
            c.fadeOut = std::min (c.fadeOut, c.length / 2);
        }
        else if (method == "setClipGain")
            c.gainDb = std::clamp (arg<float> (a, "gainDb", c.gainDb), -60.0f, 24.0f);
        else
        {
            c.fadeIn = std::clamp (arg<int64_t> (a, "fadeIn", c.fadeIn), (int64_t) 0, c.length / 2);
            c.fadeOut = std::clamp (arg<int64_t> (a, "fadeOut", c.fadeOut), (int64_t) 0, c.length / 2);
        }
        std::sort (owner->clips.begin(), owner->clips.end(), [] (const ClipSpec& x, const ClipSpec& y) { return x.start < y.start; });
        markDirty();
        rebuildGraph();
        return projectReply();
    }
    if (method == "splitClip")
    {
        needProject();
        needIdle();
        TrackSpec* owner = nullptr;
        auto& c = clipRef (reqString (a, "clipId"), &owner);
        const int64_t at = arg<int64_t> (a, "frame", position.load());
        if (at <= c.start + 64 || at >= c.start + c.length - 64) fail ("Place the playhead inside the clip to split it");
        pushUndo();
        ClipSpec right = c;
        right.id = makeId ("clp");
        right.start = at;
        right.offset = c.offset + (at - c.start);
        right.length = c.start + c.length - at;
        right.fadeIn = 0;
        c.length = at - c.start;
        c.fadeOut = 0;
        owner->clips.push_back (right);
        std::sort (owner->clips.begin(), owner->clips.end(), [] (const ClipSpec& x, const ClipSpec& y) { return x.start < y.start; });
        markDirty();
        rebuildGraph();
        return projectReply ({ { "clipId", right.id } });
    }
    if (method == "duplicateClip")
    {
        needProject();
        needIdle();
        TrackSpec* owner = nullptr;
        auto& c = clipRef (reqString (a, "clipId"), &owner);
        pushUndo();
        ClipSpec d = c;
        d.id = makeId ("clp");
        d.start = arg<int64_t> (a, "start", c.start + c.length);
        owner->clips.push_back (d);
        std::sort (owner->clips.begin(), owner->clips.end(), [] (const ClipSpec& x, const ClipSpec& y) { return x.start < y.start; });
        markDirty();
        rebuildGraph();
        return projectReply ({ { "clipId", d.id } });
    }
    if (method == "deleteClip")
    {
        needProject();
        needIdle();
        TrackSpec* owner = nullptr;
        const std::string id = reqString (a, "clipId");
        const std::string sourceId = clipRef (id, &owner).sourceId;
        pushUndo();
        owner->clips.erase (std::remove_if (owner->clips.begin(), owner->clips.end(), [&] (const ClipSpec& x) { return x.id == id; }), owner->clips.end());
        bool mediaDeleted = false;
        if (arg<bool> (a, "deleteMedia", false))
        {
            bool used = false;
            for (const auto& t : project.tracks) for (const auto& c : t.clips) used |= c.sourceId == sourceId;
            if (! used)
            {
                if (const auto* s = project.findSource (sourceId)) std::remove (mediaPath (s->file).c_str());
                project.sources.erase (std::remove_if (project.sources.begin(), project.sources.end(), [&] (const SourceSpec& s) { return s.id == sourceId; }), project.sources.end());
                sources.erase (sourceId);
                undoStack.clear(); redoStack.clear();   // the media cannot be brought back
                mediaDeleted = true;
            }
        }
        markDirty();
        rebuildGraph();
        return projectReply ({ { "mediaDeleted", mediaDeleted } });
    }

    //-------------------------------------------------------------------- effects
    if (method == "addEffect")
    {
        needProject();
        auto& chain = chainOf (reqString (a, "target"));
        if (chain.size() >= 12) fail ("An effect chain can hold up to 12 effects");
        EffectSlotSpec s;
        s.id = makeId ("fx");
        s.type = reqString (a, "type");
        if (const auto* t = voxfx::EffectRegistry::instance().find (s.type); t && t->descriptor->category == voxfx::EffectCategory::Mastering)
            fail ("The mastering chain lives in the Mastering workspace");
        if (a.contains ("params") && a["params"].is_object())
            for (auto& [k, v] : a["params"].items()) if (v.is_number()) s.params[k] = v.get<float>();
        normaliseSlot (s);
        pushUndo();
        const int idx = std::clamp (arg<int> (a, "index", (int) chain.size()), 0, (int) chain.size());
        chain.insert (chain.begin() + idx, s);
        markDirty();
        rebuildGraph();
        return projectReply ({ { "slotId", s.id } });
    }
    if (method == "removeEffect")
    {
        needProject();
        auto& chain = chainOf (reqString (a, "target"));
        const std::string id = reqString (a, "slotId");
        pushUndo();
        const auto before = chain.size();
        chain.erase (std::remove_if (chain.begin(), chain.end(), [&] (const EffectSlotSpec& s) { return s.id == id; }), chain.end());
        if (chain.size() == before) { undoStack.pop_back(); fail ("Effect not found"); }
        markDirty();
        rebuildGraph();
        return projectReply();
    }
    if (method == "moveEffect")
    {
        needProject();
        auto& chain = chainOf (reqString (a, "target"));
        const std::string id = reqString (a, "slotId");
        auto it = std::find_if (chain.begin(), chain.end(), [&] (const EffectSlotSpec& s) { return s.id == id; });
        if (it == chain.end()) fail ("Effect not found");
        pushUndo();
        EffectSlotSpec s = *it;
        chain.erase (it);
        const int idx = std::clamp (arg<int> (a, "index", 0), 0, (int) chain.size());
        chain.insert (chain.begin() + idx, s);
        markDirty();
        rebuildGraph();
        return projectReply();
    }
    if (method == "setChain")
    {
        needProject();
        auto& chain = chainOf (reqString (a, "target"));
        std::vector<EffectSlotSpec> slots;
        if (! a.contains ("slots") || ! a["slots"].is_array()) fail ("Missing 'slots'");
        for (const auto& sj : a["slots"])
        {
            EffectSlotSpec s;
            s.id = sj.value ("id", makeId ("fx"));
            s.type = sj.value ("type", "");
            s.bypass = sj.value ("bypass", false);
            if (sj.contains ("params") && sj["params"].is_object())
                for (auto& [k, v] : sj["params"].items()) if (v.is_number()) s.params[k] = v.get<float>();
            normaliseSlot (s);
            slots.push_back (s);
        }
        pushUndo();
        chain = std::move (slots);
        markDirty();
        rebuildGraph();
        return projectReply();
    }
    if (method == "setEffectParam" || method == "setEffectParams")
    {
        needProject();
        const std::string target = reqString (a, "target");
        auto& spec = slotSpec (target, arg<std::string> (a, "slotId", ""));
        auto cached = cache.slots.find (spec.id);
        const auto* type = voxfx::EffectRegistry::instance().find (spec.type);
        if (! type) fail ("Unknown effect type");
        std::map<std::string, float> changes;
        if (method == "setEffectParam") changes[reqString (a, "param")] = arg<float> (a, "value", 0.0f);
        else if (a.contains ("params") && a["params"].is_object())
            for (auto& [k, v] : a["params"].items()) if (v.is_number()) changes[k] = v.get<float>();
        if (arg<bool> (a, "commit", false)) pushUndo();
        json applied = json::object();
        for (const auto& [pid, value] : changes)
        {
            const auto& params = type->descriptor->params;
            auto pit = std::find_if (params.begin(), params.end(), [&] (const voxfx::ParamInfo& p) { return p.id == pid; });
            if (pit == params.end()) fail ("Unknown parameter '" + pid + "'");
            const float v = voxfx::Effect::sanitise (*pit, value);
            spec.params[pid] = v;
            if (cached != cache.slots.end()) cached->second->effect->setParam ((int) (pit - params.begin()), v);
            applied[pid] = v;
        }
        if (target == "mastering") project.master.masteringPreset = "custom";
        markDirty();
        return okJson ({ { "applied", applied } });
    }
    if (method == "setEffectBypass")
    {
        needProject();
        const std::string target = reqString (a, "target");
        auto& spec = slotSpec (target, reqString (a, "slotId"));
        pushUndo();
        spec.bypass = arg<bool> (a, "bypass", ! spec.bypass);
        if (auto it = cache.slots.find (spec.id); it != cache.slots.end()) it->second->setBypassed (spec.bypass);
        markDirty();
        return projectReply();
    }
    if (method == "getEffectState")
    {
        needProject();
        const std::string target = reqString (a, "target");
        auto& spec = slotSpec (target, arg<std::string> (a, "slotId", ""));
        auto it = cache.slots.find (spec.id);
        json values = json::object();
        float meter = 0.0f;
        int lat = 0;
        if (it != cache.slots.end())
        {
            const auto& fx = *it->second->effect;
            for (int i = 0; i < fx.numParams(); ++i) values[fx.descriptor().params[(size_t) i].id] = fx.getParam (i);
            meter = fx.gainReductionDb();
            lat = it->second->latency();
        }
        return okJson ({ { "slot", slotSpecJson (spec) }, { "values", values }, { "meter", round3 (meter) }, { "latency", lat } });
    }

    //-------------------------------------------------------------------- buses
    if (method == "addBus")
    {
        needProject();
        if ((int) project.buses.size() >= kMaxBuses) fail ("A project can have up to 4 buses");
        pushUndo();
        BusSpec b;
        b.id = makeId ("bus");
        b.name = arg<std::string> (a, "name", "Vocal Bus " + std::to_string (project.buses.size() + 1));
        project.buses.push_back (b);
        markDirty();
        rebuildGraph();
        return projectReply ({ { "busId", b.id } });
    }
    if (method == "removeBus")
    {
        needProject();
        const std::string id = reqString (a, "busId");
        if (! project.findBus (id)) fail ("Bus not found");
        pushUndo();
        for (auto& t : project.tracks) if (t.output == id) t.output = "master";
        project.buses.erase (std::remove_if (project.buses.begin(), project.buses.end(), [&] (const BusSpec& b) { return b.id == id; }), project.buses.end());
        markDirty();
        rebuildGraph();
        return projectReply();
    }
    if (method == "updateBus")
    {
        needProject();
        auto* b = project.findBus (reqString (a, "busId"));
        if (! b) fail ("Bus not found");
        if (a.contains ("name") || a.contains ("mute") || arg<bool> (a, "commit", false)) pushUndo();
        if (a.contains ("name")) b->name = arg<std::string> (a, "name", b->name);
        if (a.contains ("volumeDb")) b->volumeDb = std::clamp (arg<float> (a, "volumeDb", 0.0f), -60.0f, 12.0f);
        if (a.contains ("pan")) b->pan = std::clamp (arg<float> (a, "pan", 0.0f), -1.0f, 1.0f);
        if (a.contains ("mute")) b->mute = arg<bool> (a, "mute", false);
        markDirty();
        applyControls (project, cache);
        return a.value ("quiet", false) ? okJson() : projectReply();
    }

    //-------------------------------------------------------------------- master & mastering
    if (method == "updateMaster")
    {
        needProject();
        if (a.contains ("masteringOn") || arg<bool> (a, "commit", false)) pushUndo();
        if (a.contains ("volumeDb")) project.master.volumeDb = std::clamp (arg<float> (a, "volumeDb", 0.0f), -60.0f, 6.0f);
        if (a.contains ("masteringOn")) project.master.masteringOn = arg<bool> (a, "masteringOn", false);
        if (a.contains ("loudnessTarget")) project.master.loudnessTarget = std::clamp (arg<float> (a, "loudnessTarget", -14.0f), -30.0f, -5.0f);
        markDirty();
        applyControls (project, cache);
        return a.value ("quiet", false) ? okJson() : projectReply();
    }
    if (method == "listMasteringPresets")
    {
        json list = json::array();
        for (const auto& p : masteringPresets())
            list.push_back ({ { "id", p.id }, { "name", p.name }, { "description", p.description }, { "loudnessTarget", p.loudnessTarget }, { "params", p.params } });
        return okJson ({ { "presets", list } });
    }
    if (method == "applyMasteringPreset")
    {
        needProject();
        const auto* mp = findMasteringPreset (reqString (a, "presetId"));
        if (! mp) fail ("Mastering preset not found");
        pushUndo();
        auto& m = project.master.mastering;
        m.params.clear();
        for (const auto& [k, v] : mp->params) m.params[k] = v;
        normaliseSlot (m);
        project.master.masteringPreset = mp->id;
        project.master.loudnessTarget = mp->loudnessTarget;
        project.master.masteringOn = true;
        markDirty();
        applyControls (project, cache);
        return projectReply();
    }
    if (method == "analyzeMix" || method == "autoMaster")
    {
        needProject();
        needIdle();
        RenderOptions o;
        o.writeFile = false;
        o.includeTail = false;
        if (method == "analyzeMix")
        {
            o.masteringOverride = a.contains ("mastering");
            o.masteringOn = arg<bool> (a, "mastering", project.master.masteringOn);
            auto r = renderProject (project, sources, o);
            if (! r.ok) fail (r.error);
            return okJson ({ { "integratedLufs", round3 (r.integratedLufs) }, { "truePeakDb", round3 (r.truePeakDb) },
                             { "peakDb", round3 (r.peakDb) }, { "rmsDb", round3 (r.rmsDb) }, { "seconds", r.seconds }, { "clipped", r.clipped } });
        }
        // Loudness match: measure, set the mastering input gain, measure again and refine.
        const float target = std::clamp (arg<float> (a, "target", project.master.loudnessTarget), -30.0f, -5.0f);
        pushUndo();
        auto& m = project.master.mastering;
        normaliseSlot (m);
        project.master.masteringOn = true;
        o.masteringOverride = true;
        o.masteringOn = false;
        auto before = renderProject (project, sources, o);
        if (! before.ok) { undoStack.pop_back(); fail (before.error); }
        if (before.integratedLufs < -69.0f) { undoStack.pop_back(); fail ("The mix is silent: nothing to master yet"); }
        float gain = std::clamp (target - before.integratedLufs, -12.0f, 18.0f);
        o.masteringOn = true;
        RenderReport after;
        for (int pass = 0; pass < 2; ++pass)
        {
            m.params["inputGain"] = gain;
            after = renderProject (project, sources, o);
            if (! after.ok) break;
            const float delta = target - after.integratedLufs;
            if (std::abs (delta) < 0.3f) break;
            gain = std::clamp (gain + delta, -12.0f, 18.0f);
        }
        m.params["inputGain"] = std::round (gain * 10.0f) / 10.0f;
        project.master.loudnessTarget = target;
        markDirty();
        applyControls (project, cache);
        return projectReply ({ { "before", { { "integratedLufs", round3 (before.integratedLufs) }, { "truePeakDb", round3 (before.truePeakDb) } } },
                               { "after", { { "integratedLufs", round3 (after.integratedLufs) }, { "truePeakDb", round3 (after.truePeakDb) } } },
                               { "inputGain", m.params["inputGain"] } });
    }

    //-------------------------------------------------------------------- presets
    if (method == "listPresets")
    {
        json list = json::array();
        for (const auto& p : factoryChainPresets()) list.push_back (json::parse (presetToJson (p)));
        for (const auto& p : userPresets.presets()) list.push_back (json::parse (presetToJson (p)));
        return okJson ({ { "presets", list } });
    }
    if (method == "applyPreset")
    {
        needProject();
        const std::string pid = reqString (a, "presetId");
        const ChainPreset* p = userPresets.find (pid);
        if (! p) for (const auto& f : factoryChainPresets()) if (f.id == pid) p = &f;
        if (! p) fail ("Preset not found");
        auto& chain = chainOf (reqString (a, "target"));
        auto slots = withNewIds (p->slots);
        for (auto& s : slots) normaliseSlot (s);
        pushUndo();
        chain = std::move (slots);
        markDirty();
        rebuildGraph();
        return projectReply ({ { "presetName", p->name } });
    }
    if (method == "savePreset")
    {
        needProject();
        ChainPreset p;
        p.name = reqString (a, "name").substr (0, 40);
        p.category = "user";
        p.description = arg<std::string> (a, "description", "");
        p.slots = chainOf (reqString (a, "target"));
        if (p.slots.empty()) fail ("The effect chain is empty");
        const auto& added = userPresets.add (p);
        if (! userPresets.save (err)) { userPresets.remove (added.id); fail (err); }
        return okJson ({ { "preset", json::parse (presetToJson (userPresets.presets().back())) } });
    }
    if (method == "renamePreset")
    {
        const std::string id = reqString (a, "presetId");
        if (! userPresets.rename (id, reqString (a, "name").substr (0, 40))) fail ("Only your own presets can be renamed");
        if (! userPresets.save (err)) fail (err);
        return okJson();
    }
    if (method == "duplicatePreset")
    {
        const std::string pid = reqString (a, "presetId");
        const ChainPreset* p = userPresets.find (pid);
        if (! p) for (const auto& f : factoryChainPresets()) if (f.id == pid) p = &f;
        if (! p) fail ("Preset not found");
        ChainPreset copy = *p;
        copy.id.clear();
        copy.name = arg<std::string> (a, "name", p->name + " copy").substr (0, 40);
        copy.category = "user";
        userPresets.add (copy);
        if (! userPresets.save (err)) fail (err);
        return okJson ({ { "preset", json::parse (presetToJson (userPresets.presets().back())) } });
    }
    if (method == "deletePreset")
    {
        if (! userPresets.remove (reqString (a, "presetId"))) fail ("Factory presets cannot be deleted");
        if (! userPresets.save (err)) fail (err);
        return okJson();
    }

    //-------------------------------------------------------------------- undo / redo
    if (method == "undo" || method == "redo")
    {
        needProject();
        needIdle();
        auto& from = method == "undo" ? undoStack : redoStack;
        auto& to = method == "undo" ? redoStack : undoStack;
        if (from.empty()) fail (method == "undo" ? "Nothing to undo" : "Nothing to redo");
        to.push_back (projectToJson (project, false));
        Project restored;
        if (! projectFromJson (from.back(), restored, err)) fail (err);
        from.pop_back();
        restored.id = project.id;
        project = std::move (restored);
        markDirty();
        rebuildGraph();
        return projectReply();
    }

    //-------------------------------------------------------------------- export
    if (method == "estimateExport")
    {
        needProject();
        const int bits = arg<int> (a, "bitDepth", project.exportSettings.bitDepth);
        const int rate = arg<int> (a, "sampleRate", project.exportSettings.sampleRate > 0 ? project.exportSettings.sampleRate : project.sampleRate);
        const int ch = std::clamp (arg<int> (a, "channels", project.exportSettings.channels), 1, 2);
        const double seconds = double (liveOwner ? liveOwner->endFrame : project.contentEnd()) / project.sampleRate
                               + (project.exportSettings.includeTail ? 2.0 : 0.0);
        const double bytes = seconds * rate * ch * (bits / 8) + 44;
        return okJson ({ { "seconds", seconds }, { "bytes", (int64_t) bytes } });
    }
    if (method == "exportMix")
    {
        needProject();
        needIdle();
        if (exportRunning.load()) fail ("An export is already running");
        if (exportThread.joinable()) exportThread.join();
        RenderOptions o;
        std::string name = arg<std::string> (a, "name", project.name);
        for (auto& ch : name) if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') ch = '_';
        if (name.empty()) name = "VoxMix Export";
        o.path = arg<std::string> (a, "path", dataDir + "/exports/" + name + ".wav");
        o.bitDepth = arg<int> (a, "bitDepth", project.exportSettings.bitDepth);
        o.sampleRate = arg<int> (a, "sampleRate", project.exportSettings.sampleRate);
        o.channels = arg<int> (a, "channels", project.exportSettings.channels);
        o.includeTail = arg<bool> (a, "includeTail", project.exportSettings.includeTail);
        if (a.contains ("mastering")) { o.masteringOverride = true; o.masteringOn = arg<bool> (a, "mastering", true); }
        if (o.sampleRate != 0 && o.sampleRate != 44100 && o.sampleRate != 48000 && o.sampleRate != 88200 && o.sampleRate != 96000)
            fail ("Unsupported export sample rate");
        if (const auto f = fs::freeSpace (dataDir); f >= 0 && f < 32ll * 1024 * 1024) fail ("Not enough storage to export");
        const std::string fmt = arg<std::string> (a, "format", "wav");
        if (fmt != "wav") fail ("Only WAV export is available in this version");
        exportProgress.progress.store (0.0f);
        exportProgress.cancel.store (false);
        { std::lock_guard<std::mutex> l (exportMutex); exportResult.clear(); }
        exportRunning.store (true);
        Project copy = project;
        SourcePool pool = sources;
        exportThread = std::thread ([this, copy, pool, o]
        {
            auto r = renderProject (copy, pool, o, &exportProgress);
            json j = { { "ok", r.ok }, { "error", r.error }, { "path", r.path }, { "frames", r.frames }, { "seconds", r.seconds },
                       { "sampleRate", r.sampleRate }, { "channels", r.channels }, { "bitDepth", r.bitDepth },
                       { "peakDb", round3 (r.peakDb) }, { "truePeakDb", round3 (r.truePeakDb) }, { "integratedLufs", round3 (r.integratedLufs) },
                       { "clipped", r.clipped }, { "fileBytes", r.fileBytes }, { "validated", r.validated },
                       { "validationNote", r.validationNote }, { "cancelled", r.cancelled } };
            {
                std::lock_guard<std::mutex> l (exportMutex);
                exportResult = j.dump();
            }
            exportRunning.store (false);
        });
        return okJson ({ { "started", true }, { "path", o.path } });
    }
    if (method == "cancelExport")
    {
        exportProgress.cancel.store (true);
        return okJson();
    }
    if (method == "getExportResult")
    {
        if (exportRunning.load()) return okJson ({ { "running", true }, { "progress", exportProgress.progress.load() } });
        std::lock_guard<std::mutex> l (exportMutex);
        if (exportResult.empty()) fail ("No export has run");
        return okJson ({ { "running", false }, { "result", json::parse (exportResult) } });
    }

    //-------------------------------------------------------------------- library & app settings
    if (method == "listExports")
    {
        json list = json::array();
        const std::string dir = dataDir + "/exports";
        for (const auto& f : fs::list (dir))
        {
            if (f.size() < 5 || f.compare (f.size() - 4, 4, ".wav") != 0) continue;
            const std::string path = dir + "/" + f;
            struct stat st {};
            if (::stat (path.c_str(), &st) != 0) continue;
            list.push_back ({ { "name", f.substr (0, f.size() - 4) }, { "path", path }, { "bytes", (int64_t) st.st_size },
                              { "modifiedAt", (int64_t) st.st_mtime * 1000 } });
        }
        std::sort (list.begin(), list.end(), [] (const json& x, const json& y) { return x["modifiedAt"].get<int64_t>() > y["modifiedAt"].get<int64_t>(); });
        return okJson ({ { "exports", list } });
    }
    if (method == "deleteExport")
    {
        const std::string path = reqString (a, "path");
        if (path.rfind (dataDir + "/exports/", 0) != 0 || path.find ("..") != std::string::npos) fail ("Invalid file");
        if (std::remove (path.c_str()) != 0) fail ("The file could not be deleted");
        return okJson();
    }
    if (method == "getSettings" || method == "setSettings")
    {
        std::string text;
        json settings = json::object();
        if (readFile (dataDir + "/settings.json", text))
        {
            auto j = json::parse (text, nullptr, false);
            if (j.is_object()) settings = j;
        }
        if (method == "setSettings")
        {
            if (! a.contains ("values") || ! a["values"].is_object()) fail ("Missing 'values'");
            for (auto& [k, v] : a["values"].items()) settings[k] = v;
            if (! writeFileAtomic (dataDir + "/settings.json", settings.dump (2), err)) fail (err);
        }
        return okJson ({ { "settings", settings } });
    }

    //-------------------------------------------------------------------- latency calibration
    if (method == "startLatencyTest")
    {
        needProject();
        needIdle();
        if (device)
        {
            if (! wantInput || ! device->status().inputOpen) { wantInput = true; restartDeviceLocked(); }
            if (! device->status().inputOpen) fail ("The microphone could not be opened. Check the microphone permission.");
        }
        playing.store (false);
        std::fill (calibIn.begin(), calibIn.end(), 0.0f);
        calibInCount.store (0);
        calibOutCount.store (0);
        calibActive.store (true);
        const double seconds = double (kCalibFirst + kCalibPeriod * kCalibClicks + 9600) / 48000.0;
        return okJson ({ { "seconds", seconds } });
    }
    if (method == "finishLatencyTest")
    {
        needProject();
        calibActive.store (false);
        if (device && device->status().running)
            std::this_thread::sleep_for (std::chrono::milliseconds (30));   // let the callback leave calibration
        const int64_t captured = std::min<int64_t> (calibInCount.load(), (int64_t) calibIn.size());
        // matched filter: correlate the input with the click and take the best lag per click
        std::vector<float> tpl (96);
        for (int i = 0; i < 96; ++i) tpl[(size_t) i] = (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0);
        std::vector<int64_t> lags;
        float bestPeakTotal = 0.0f, noiseTotal = 0.0f;
        const int maxLag = 24000 - 200;   // up to 500 ms round trip
        for (int c = 0; c < kCalibClicks; ++c)
        {
            const int64_t emitted = kCalibFirst + (int64_t) c * kCalibPeriod;
            float best = 0.0f, sumAbs = 0.0f;
            int64_t bestLag = -1;
            int count = 0;
            for (int lag = 0; lag < maxLag; ++lag)
            {
                const int64_t s0 = emitted + lag;
                if (s0 + 96 >= captured) break;
                float acc = 0.0f;
                for (int i = 0; i < 96; ++i) acc += tpl[(size_t) i] * calibIn[(size_t) (s0 + i)];
                acc = std::abs (acc);
                sumAbs += acc; ++count;
                if (acc > best) { best = acc; bestLag = lag; }
            }
            if (bestLag >= 0 && count > 0)
            {
                lags.push_back (bestLag);
                bestPeakTotal += best;
                noiseTotal += sumAbs / count;
            }
        }
        if (lags.size() < 4) fail ("No clicks were detected. Turn the volume up, unplug headphones and hold the phone in a quiet room.");
        std::vector<int64_t> sorted = lags;
        std::sort (sorted.begin(), sorted.end());
        const int64_t median = sorted[sorted.size() / 2];
        int consistent = 0;
        for (auto l : lags) consistent += std::abs (l - median) <= 48;   // within 1 ms
        const float snr = noiseTotal > 0 ? bestPeakTotal / noiseTotal : 0.0f;
        const bool reliable = consistent >= (int) lags.size() - 1 && snr > 4.0f;
        // what the engine would compensate automatically (device part only)
        int64_t deviceComp = assumedLatency;
        if (device) { const auto st = device->status(); deviceComp = st.inputLatencyFrames + st.outputLatencyFrames; }
        const int64_t suggestedOffset = deviceComp - median;
        const double rate = project.sampleRate;
        if (reliable && arg<bool> (a, "apply", false))
        {
            pushUndo();
            project.recordOffsetFrames = suggestedOffset;
            markDirty();
        }
        return okJson ({ { "measuredFrames", median }, { "measuredMs", median * 1000.0 / rate },
                         { "reportedFrames", deviceComp }, { "reportedMs", deviceComp * 1000.0 / rate },
                         { "suggestedOffsetMs", suggestedOffset * 1000.0 / rate }, { "reliable", reliable },
                         { "clicksDetected", (int) lags.size() }, { "consistent", consistent }, { "snr", round3 (snr) },
                         { "applied", reliable && arg<bool> (a, "apply", false) } });
    }

    //-------------------------------------------------------------------- lifecycle & misc
    if (method == "onAppBackground")
    {
        json r = json::object();
        if (recorder.active())
        {
            std::string rec;
            stopRecordingLocked (rec);
            r["recording"] = json::parse (rec);
        }
        playing.store (false);
        calibActive.store (false);
        if (hasProject && dirty && ! saveLocked (err)) r["warning"] = err;
        if (device) device->stop();   // no audio (and no battery use) in the background
        return okJson (r);
    }
    if (method == "onAppForeground")
    {
        if (hasProject && device && ! device->status().running) restartDeviceLocked();
        return okJson();
    }
    if (method == "resetClipIndicators")
    {
        for (auto& [id, c] : cache.controls) c->meter.resetClip();
        return okJson();
    }

    fail ("Unknown command '" + method + "'");
}

} // namespace vox
