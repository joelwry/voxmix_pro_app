#pragma once
// AudioDeviceManager implementation for Android, built on Google Oboe (AAudio where available,
// OpenSL ES otherwise).
//
//  - Output stream: low-latency, exclusive when the device allows it, float stereo, at the
//    project sample rate (Oboe resamples if the hardware runs at another rate).
//  - Input stream (optional): mono float, VoicePerformance preset on Android 10+ (no AGC/noise
//    suppression), read without blocking from the output callback so input and output share
//    one callback and one clock. All frames available are read each callback so no input frame
//    is ever skipped (recording sample count stays exact).
//  - Latency is measured from hardware timestamps on a monitor thread (output: frames written
//    vs presented, input: frames captured vs read). OpenSL ES has no timestamps: estimates are
//    reported and flagged as such.
//  - Underruns: the buffer grows by one burst when xruns increase (simple latency tuner).
//  - Disconnects (headphones unplugged, USB device removed): Oboe closes the stream and
//    onErrorAfterClose reopens it on the new default device.

#include "vox/AudioDevice.h"
#include <oboe/Oboe.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace vox
{
class OboeDevice final : public IAudioDevice,
                         public oboe::AudioStreamDataCallback,
                         public oboe::AudioStreamErrorCallback
{
public:
    OboeDevice();
    ~OboeDevice() override;

    bool start (int sampleRate, bool withInput, AudioCallback* cb, std::string& error) override;
    void stop() override;
    DeviceStatus status() override;

    oboe::DataCallbackResult onAudioReady (oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose (oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool openLocked (std::string& error);
    void closeLocked();
    void monitorLoop();

    std::mutex lock;
    std::shared_ptr<oboe::AudioStream> out, in;
    AudioCallback* callback = nullptr;
    int requestedRate = 48000;
    bool wantInput = false;
    std::atomic<bool> running { false };

    std::vector<float> inBuf;
    std::atomic<bool> drainInput { false };
    std::atomic<int> outLatency { 0 }, inLatency { 0 };
    std::atomic<bool> measured { false };
    std::atomic<int> lastXruns { 0 };

    std::thread monitor;
    std::atomic<bool> monitorQuit { false };
};

} // namespace vox
