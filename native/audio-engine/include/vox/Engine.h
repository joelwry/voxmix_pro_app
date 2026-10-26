#pragma once
// The VoxMix audio engine facade.
//
// Commands arrive as (method, JSON arguments) and return JSON ({"ok":true,...} or
// {"ok":false,"error":"..."}). This keeps the native bridge (JNI) to two functions and lets the
// whole command surface be tested on a desktop. Commands are serialised by a control mutex that
// the audio thread never takes.
//
// Real-time thread: onAudio() reads the published MixGraph through an atomic pointer, renders
// it, captures input for recording and updates the transport (AudioTransport / AudioClock).
// Graph swaps use an epoch counter: an old graph is destroyed on the control side only once the
// audio thread provably no longer uses it.

#include "AudioDevice.h"
#include "GraphBuilder.h"
#include "OfflineRenderer.h"
#include "Presets.h"
#include "RecordingManager.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vox
{
constexpr const char* kEngineVersion = "0.1.0";

class Engine final : public AudioCallback
{
public:
    /** dataDir: writable app folder (projects/, presets/, exports/). device may be null
        (offline use, tests that drive onAudio() directly). */
    Engine (std::string dataDir, std::unique_ptr<IAudioDevice> device);
    ~Engine() override;

    /** Executes one command. Thread safe. */
    std::string call (const std::string& method, const std::string& argsJson);

    /** Compact real-time status snapshot (transport, meters, recording, export). Cheap. */
    std::string status();

    // AudioCallback
    void onAudio (const float* in, int inChannels, int inFrames, float* out, int outFrames) noexcept override;
    void onDeviceError (const std::string& message) noexcept override;

    /** For tests: latency the engine assumes when no device is attached (frames). */
    void setAssumedRoundTripLatency (int frames) { assumedLatency = frames; }
    /** For tests and diagnostics: render-graph publication without a running device. */
    int64_t transportPosition() const noexcept { return position.load(); }

    static constexpr int kMaxBlock = 256;

private:
    // command implementations (control mutex held)
    std::string dispatch (const std::string& method, const std::string& args);

    // helpers (control mutex held)
    void rebuildGraph();
    void publish (MixGraphPtr g);
    void collectGarbage();
    void markDirty();
    void pushUndo();
    bool ensureProject (std::string& error) const;
    std::string projectFolder() const;
    std::string projectFolder (const std::string& id) const;
    std::string mediaPath (const std::string& file) const;
    bool loadSources (std::vector<std::string>& warnings);
    int64_t latencyCompensation();
    void stopRecordingLocked (std::string& resultJson);
    void housekeeping();
    bool saveLocked (std::string& error);
    void restartDeviceLocked();

    const std::string dataDir;
    std::unique_ptr<IAudioDevice> device;
    std::recursive_mutex control;

    // project state (control thread)
    bool hasProject = false;
    Project project;
    SourcePool sources;
    InstanceCache cache;
    std::vector<std::string> undoStack, redoStack;
    bool dirty = false;
    int64_t lastChangeMs = 0;
    std::string lastError;
    UserPresetStore userPresets;
    bool wantInput = false;
    int assumedLatency = 0;

    // graph publication
    struct Retired
    {
        MixGraphPtr graph;
        uint64_t seq;   // callbackSeq when it was replaced
    };
    MixGraphPtr liveOwner;
    std::vector<Retired> graveyard;
    std::atomic<MixGraph*> live { nullptr };
    std::atomic<uint64_t> callbackSeq { 0 };   // odd while the audio thread is inside onAudio

    // transport (AudioTransport / AudioClock: the audio frame counter is the only clock)
    std::atomic<int64_t> position { 0 };
    std::atomic<bool> playing { false };
    std::atomic<int64_t> pendingSeek { -1 };
    std::atomic<bool> autoStopped { false };
    std::atomic<int64_t> playStartPosition { 0 };

    // recording
    RecordingManager recorder;
    std::atomic<int> recordTrackIndex { -1 };
    std::atomic<bool> monitorEnabled { false };
    std::string recordTrackId;
    std::vector<float> inputMono;

    // audio thread
    MixRenderer renderer;
    LoudnessMeter loudness;
    std::vector<float> blockL, blockR;
    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<int> callbackFrames { 0 };
    std::atomic<int64_t> callbacks { 0 };
    std::atomic<bool> loudnessReset { false };
    std::atomic<bool> deviceFailed { false };

    // status snapshot (read without the control mutex)
    std::mutex statusMutex;
    std::vector<std::pair<std::string, ControlsPtr>> meterTable;
    ControlsPtr masterControls;
    std::string deviceError;
    std::atomic<bool> hasProjectAtomic { false };
    std::atomic<int> projectRate { 48000 };

    // export job
    std::thread exportThread;
    RenderProgress exportProgress;
    std::atomic<bool> exportRunning { false };
    std::mutex exportMutex;
    std::string exportResult;

    // housekeeping (autosave, garbage collection)
    std::thread housekeeper;
    std::atomic<bool> quit { false };
    std::condition_variable_any wake;
};

} // namespace vox
