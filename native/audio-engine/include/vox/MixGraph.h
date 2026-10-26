#pragma once
// MultitrackMixer: the render graph and the code that renders it.
//
// A MixGraph is an immutable snapshot compiled from the Project on the control thread. It holds
// shared pointers to the audio sources, the effect chains and the per-channel controls, so the
// audio thread never allocates or frees anything. The engine publishes a new graph atomically
// when the project structure changes; continuous controls (volume, pan, mute, solo, effect
// parameters, bypass) are atomics shared between graphs and change without a rebuild.
//
// Processing order (identical in real time and offline export):
//   Track:  clips (clip gain + fades) [+ live input when monitoring] -> Layer FX chain
//           -> volume -> pan -> mute/solo -> destination (bus or master)
//   Bus:    sum -> Bus FX chain -> volume -> pan -> mute -> master
//   Master: sum -> Master FX chain -> master volume -> Mastering chain (when on) -> output
//
// Latency (plugin delay) compensation: every chain reports a constant latency. Clips are read
// ahead by their track's chain latency plus the bus latency, so every track arrives at the
// master aligned. Master latency delays everything equally and is added to the recording
// latency compensation (see RecordingManager).

#include "AudioSource.h"
#include "Metering.h"
#include "voxfx/EffectChain.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace vox
{
/** Volume/pan/mute/solo of one track, bus or the master, plus its meter. Persistent across
    graph rebuilds (looked up by id). */
struct ChannelControls
{
    std::atomic<float> gain { 1.0f };   // linear
    std::atomic<float> pan { 0.0f };    // -1 .. 1
    std::atomic<bool> mute { false }, solo { false };
    PeakMeter meter;

    // audio-thread state
    float curL = 0.0f, curR = 0.0f;
    bool primed = false;
    int64_t silentFrames = 0;
};
using ControlsPtr = std::shared_ptr<ChannelControls>;

struct ClipRT
{
    AudioSourcePtr source;
    int64_t start = 0, offset = 0, length = 0, fadeIn = 0, fadeOut = 0;
    float gain = 1.0f;
};

struct TrackRT
{
    std::string id;
    ControlsPtr controls;
    std::vector<ClipRT> clips;      // sorted by start
    voxfx::EffectChainPtr chain;
    int bus = -1;                   // index into MixGraph::buses, -1 = master
    bool stereoBalance = false;     // stereo material (beat): balance pan instead of mono pan law
    int64_t readAhead = 0;          // latency compensation in frames
    int64_t tailFrames = 0;
};

struct BusRT
{
    std::string id;
    ControlsPtr controls;
    voxfx::EffectChainPtr chain;
    int64_t latency = 0;
};

struct MixGraph
{
    double sampleRate = 48000.0;
    int maxBlock = 256;
    double bpm = 120.0;
    std::vector<TrackRT> tracks;
    std::vector<BusRT> buses;
    voxfx::EffectChainPtr masterFx;
    voxfx::EffectChainPtr mastering;
    ControlsPtr master;
    int64_t masterLatency = 0;      // master FX + mastering
    int64_t endFrame = 0;           // last clip end on the timeline
};
using MixGraphPtr = std::shared_ptr<MixGraph>;

/** Live input routed into the mix (input monitoring during recording). */
struct MonitorInput
{
    const float* mono = nullptr;
    int trackIndex = -1;
};

class MixRenderer
{
public:
    void prepare (int maxBlock, int maxBuses);

    /** Renders n <= maxBlock frames at timeline position pos. Real-time safe. */
    void render (MixGraph& g, int64_t pos, int n, bool playing, const MonitorInput& monitor,
                 float* outL, float* outR, LoudnessMeter* loudness) noexcept;

private:
    std::vector<float> tL, tR, mL, mR;
    std::vector<std::vector<float>> bL, bR;
};

} // namespace vox
