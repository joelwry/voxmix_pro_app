#pragma once
// The project data model (control thread only). This is what is saved to project.json and what
// the UI edits through engine commands. The audio thread never touches it: the engine compiles
// it into an immutable render graph (MixGraph) whenever it changes.
//
// Time is measured in frames at the project sample rate (int64), so clip placement is exact.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vox
{
constexpr int kProjectFormatVersion = 1;
constexpr int kMaxVocalLayers = 10;
constexpr int kMaxBuses = 4;

struct EffectSlotSpec
{
    std::string id;                      // unique within the project
    std::string type;                    // effect type id from the EffectRegistry
    bool bypass = false;
    std::map<std::string, float> params; // missing params use the effect defaults
};

struct ClipSpec
{
    std::string id;
    std::string sourceId;        // audio file in the project's media pool
    int64_t start = 0;           // timeline position of the first audible frame
    int64_t offset = 0;          // first frame used from the source
    int64_t length = 0;          // frames used from the source
    float gainDb = 0.0f;
    int64_t fadeIn = 0, fadeOut = 0;  // frames
    std::string name;
    bool muted = false;
};

enum class TrackKind
{
    Beat,     // the instrumental / backing track
    Vocal     // a vocal layer (max kMaxVocalLayers)
};

struct TrackSpec
{
    std::string id;
    std::string name;
    TrackKind kind = TrackKind::Vocal;
    std::string color = "#8B5CF6";
    float volumeDb = 0.0f;
    float pan = 0.0f;            // -1 .. 1
    bool mute = false, solo = false;
    std::string output = "master";   // "master" or a bus id
    int inputChannel = -1;           // -1 = mono sum of all input channels
    std::vector<EffectSlotSpec> fx;  // Layer FX, in processing order
    std::vector<ClipSpec> clips;
};

struct BusSpec
{
    std::string id;
    std::string name;
    float volumeDb = 0.0f;
    float pan = 0.0f;
    bool mute = false;
    std::vector<EffectSlotSpec> fx;  // Bus FX
};

struct MasterSpec
{
    float volumeDb = 0.0f;
    std::vector<EffectSlotSpec> fx;  // Master FX (whole mix)
    bool masteringOn = false;
    EffectSlotSpec mastering;        // type "mastering"
    std::string masteringPreset = "default";
    float loudnessTarget = -14.0f;   // LUFS, used by loudness match
};

struct ExportSettings
{
    std::string format = "wav";      // "wav" (mp3 planned)
    int bitDepth = 24;               // 16, 24, 32 (float)
    int sampleRate = 0;              // 0 = project rate
    int channels = 2;
    int mp3Bitrate = 320;
    bool includeTail = true;
};

struct SourceSpec
{
    std::string id;
    std::string file;                // relative to the project folder (e.g. "media/rec-...wav")
    std::string originalName;
    int64_t frames = 0;
    int channels = 1;
    int sampleRate = 48000;          // always equal to the project rate after import
    bool recorded = false;
};

struct Project
{
    int formatVersion = kProjectFormatVersion;
    std::string id;
    std::string name = "Untitled Project";
    int64_t createdAt = 0, modifiedAt = 0;   // unix ms
    int sampleRate = 48000;
    double bpm = 120.0;
    int timeSigNum = 4, timeSigDen = 4;
    int64_t recordOffsetFrames = 0;          // user latency trim added to automatic compensation
    bool inputMonitoring = false;
    std::vector<SourceSpec> sources;
    std::vector<TrackSpec> tracks;
    std::vector<BusSpec> buses;
    MasterSpec master;
    ExportSettings exportSettings;
    std::map<std::string, std::string> meta;

    TrackSpec* findTrack (const std::string& id);
    const TrackSpec* findTrack (const std::string& id) const;
    BusSpec* findBus (const std::string& id);
    const SourceSpec* findSource (const std::string& id) const;
    ClipSpec* findClip (const std::string& clipId, TrackSpec** owner = nullptr);
    int vocalLayerCount() const;
    /** Last frame of any clip on the timeline. */
    int64_t contentEnd() const;
};

/** Generates a short unique id with the given prefix (e.g. "trk-3f9a1c"). */
std::string makeId (const std::string& prefix);
int64_t nowMs();

} // namespace vox
