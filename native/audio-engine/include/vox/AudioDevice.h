#pragma once
// AudioDeviceManager interface. The platform layer (Oboe on Android, a simulated device in the
// tests) implements IAudioDevice and drives the engine's AudioCallback from its real-time thread.

#include <cstdint>
#include <string>

namespace vox
{
class AudioCallback
{
public:
    virtual ~AudioCallback() = default;
    /** Real-time thread. `in` is interleaved (inChannels) or null when input is closed;
        inFrames may differ from outFrames by a few frames in full-duplex operation.
        `out` is interleaved stereo float and must be fully written. */
    virtual void onAudio (const float* in, int inChannels, int inFrames, float* out, int outFrames) noexcept = 0;
    /** Device thread (not real time): the stream stopped unexpectedly (disconnect, error). */
    virtual void onDeviceError (const std::string& message) noexcept = 0;
};

struct DeviceStatus
{
    bool running = false;
    bool inputOpen = false;
    int sampleRate = 0;
    int framesPerBurst = 0;
    int bufferSizeFrames = 0;
    int outputLatencyFrames = 0;   // measured (timestamps) or estimated
    int inputLatencyFrames = 0;
    bool latencyMeasured = false;  // true when derived from hardware timestamps
    int xruns = 0;
    std::string api;               // "AAudio", "OpenSL ES", "simulated"
    std::string outputDevice, inputDevice;
    bool exclusive = false;
    bool lowLatency = false;
};

class IAudioDevice
{
public:
    virtual ~IAudioDevice() = default;
    /** Opens and starts the streams. Control thread. */
    virtual bool start (int sampleRate, bool withInput, AudioCallback* cb, std::string& error) = 0;
    virtual void stop() = 0;
    virtual DeviceStatus status() = 0;
};

} // namespace vox
