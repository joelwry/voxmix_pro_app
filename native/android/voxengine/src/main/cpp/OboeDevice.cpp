#include "OboeDevice.h"
#include <android/log.h>
#include <sys/system_properties.h>
#include <algorithm>
#include <chrono>
#include <ctime>

#define LOGI(...) __android_log_print (ANDROID_LOG_INFO, "VoxEngine", __VA_ARGS__)
#define LOGW(...) __android_log_print (ANDROID_LOG_WARN, "VoxEngine", __VA_ARGS__)

namespace vox
{
namespace
{
int sdkLevel()
{
    char v[PROP_VALUE_MAX] {};
    __system_property_get ("ro.build.version.sdk", v);
    return std::atoi (v);
}

oboe::InputPreset inputPreset (const std::string& mode)
{
    // raw: no AGC / noise suppression (best for singing); voice: light processing;
    // auto: the phone's automatic gain; clean: echo cancellation + noise suppression
    if (mode == "voice") return oboe::InputPreset::VoiceRecognition;
    if (mode == "auto") return oboe::InputPreset::Generic;
    if (mode == "clean") return oboe::InputPreset::VoiceCommunication;
    return sdkLevel() >= 29 ? oboe::InputPreset::VoicePerformance : oboe::InputPreset::Unprocessed;
}

int64_t nowNanos()
{
    timespec ts {};
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return int64_t (ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}
} // namespace

OboeDevice::OboeDevice()
{
    inBuf.assign (16384, 0.0f);
    monitor = std::thread ([this] { monitorLoop(); });
}

OboeDevice::~OboeDevice()
{
    monitorQuit.store (true);
    if (monitor.joinable()) monitor.join();
    stop();
}

void OboeDevice::setOptions (const DeviceOptions& o)
{
    std::lock_guard<std::mutex> l (lock);
    options = o;
}

int OboeDevice::bufferBursts() const
{
    // more cushion while the microphone is open: a missed output deadline also loses input
    const bool rec = wantInput;
    if (options.latencyMode == "lowest") return 2;
    if (options.latencyMode == "safe") return rec ? 8 : 6;
    return rec ? 4 : 3;   // balanced
}

bool OboeDevice::roundTripLatency (const StreamClock& c, double& frames)
{
    if (! c.valid) return false;
    std::lock_guard<std::mutex> l (tsLock);
    if (! tsValid || requestedRate <= 0) return false;
    const double sr = requestedRate;
    // capture time of the first input frame of that callback, then the output frame presented at
    // that instant: the take is placed so these two line up
    const double tCapture = double (tsInTime) * 1e-9 + double (c.inFramesRead - tsInFrame) / sr;
    const double presented = double (tsOutFrame) + (tCapture - double (tsOutTime) * 1e-9) * sr;
    frames = double (c.outFramesWritten) - presented;
    return frames > 0.0 && frames < sr;
}

bool OboeDevice::start (int sampleRate, bool withInput, AudioCallback* cb, std::string& error)
{
    std::lock_guard<std::mutex> l (lock);
    closeLocked();
    requestedRate = sampleRate;
    wantInput = withInput;
    callback = cb;
    return openLocked (error);
}

bool OboeDevice::openLocked (std::string& error)
{
    oboe::AudioStreamBuilder ob;
    if (options.latencyMode == "safe") ob.setSharingMode (oboe::SharingMode::Shared);
    ob.setDirection (oboe::Direction::Output)
        ->setPerformanceMode (oboe::PerformanceMode::LowLatency)
        ->setSharingMode (options.latencyMode == "safe" ? oboe::SharingMode::Shared : oboe::SharingMode::Exclusive)
        ->setFormat (oboe::AudioFormat::Float)
        ->setChannelCount (oboe::ChannelCount::Stereo)
        ->setSampleRate (requestedRate)
        ->setSampleRateConversionQuality (oboe::SampleRateConversionQuality::Medium)
        ->setUsage (oboe::Usage::Media)
        ->setContentType (oboe::ContentType::Music)
        ->setDataCallback (this)
        ->setErrorCallback (this);
    auto r = ob.openStream (out);
    if (r != oboe::Result::OK)
    {
        error = std::string ("Could not open audio output: ") + oboe::convertToText (r);
        out.reset();
        return false;
    }

    if (wantInput)
    {
        oboe::AudioStreamBuilder ib;
        ib.setDirection (oboe::Direction::Input)
            ->setPerformanceMode (oboe::PerformanceMode::LowLatency)
            ->setSharingMode (options.latencyMode == "safe" ? oboe::SharingMode::Shared : oboe::SharingMode::Exclusive)
            ->setFormat (oboe::AudioFormat::Float)
            ->setChannelCount (oboe::ChannelCount::Mono)
            ->setSampleRate (requestedRate)
            ->setSampleRateConversionQuality (oboe::SampleRateConversionQuality::Medium)
            ->setInputPreset (inputPreset (options.micMode))
            ->setBufferCapacityInFrames (std::max (out->getFramesPerBurst() * 16, requestedRate / 5));   // >= 200 ms of slack
        if (options.latencyMode == "safe") ib.setSharingMode (oboe::SharingMode::Shared);
        r = ib.openStream (in);
        if (r != oboe::Result::OK)
        {
            LOGW ("input open failed: %s", oboe::convertToText (r));
            error = std::string ("Could not open the microphone: ") + oboe::convertToText (r);
            in.reset();
        }
    }

    out->setBufferSizeInFrames (std::min (out->getBufferCapacityInFrames(), out->getFramesPerBurst() * bufferBursts()));
    lastXruns.store (0);
    drainInput.store (true);
    if (in && in->requestStart() != oboe::Result::OK)
    {
        error = "Could not start the microphone";
        in->close();
        in.reset();
    }
    r = out->requestStart();
    if (r != oboe::Result::OK)
    {
        error = std::string ("Could not start audio output: ") + oboe::convertToText (r);
        closeLocked();
        return false;
    }
    running.store (true);
    LOGI ("started: %d Hz, burst %d, api %s, %s, input %s", out->getSampleRate(), out->getFramesPerBurst(),
          out->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSLES",
          out->getSharingMode() == oboe::SharingMode::Exclusive ? "exclusive" : "shared", in ? "on" : "off");
    return ! wantInput || in != nullptr || error.empty();
}

void OboeDevice::closeLocked()
{
    running.store (false);
    if (out) { out->stop(); out->close(); out.reset(); }
    if (in) { in->stop(); in->close(); in.reset(); }
}

void OboeDevice::stop()
{
    std::lock_guard<std::mutex> l (lock);
    closeLocked();
}

oboe::DataCallbackResult OboeDevice::onAudioReady (oboe::AudioStream* stream, void* audioData, int32_t numFrames)
{
    auto* output = static_cast<float*> (audioData);
    oboe::AudioStream* input = in.get();
    StreamClock clock;
    clock.outFramesWritten = stream->getFramesWritten();
    int inFrames = 0;
    if (input)
    {
        if (drainInput.exchange (false))
        {
            // discard what accumulated while starting, so the input path starts with minimal latency
            while (input->read (inBuf.data(), (int32_t) inBuf.size(), 0).value() > 0) {}
        }
        clock.inFramesRead = input->getFramesRead();
        clock.valid = true;
        // read everything available (never block, never skip): the engine records frames exactly
        // as they arrive, so a callback that brings 0 frames is followed by one that brings more
        const int cap = (int) inBuf.size();
        auto avail = input->getAvailableFrames();
        const int want = avail ? std::clamp ((int) avail.value(), 0, cap) : std::min (numFrames, cap);
        if (want > 0)
        {
            auto res = input->read (inBuf.data(), want, 0);
            inFrames = res ? res.value() : 0;
        }
    }
    if (callback)
        callback->onAudio (input ? inBuf.data() : nullptr, input ? 1 : 0, inFrames, output, numFrames, clock);
    else
        std::fill (output, output + numFrames * 2, 0.0f);

    // simple latency tuner: one more burst of buffering after each new underrun
    auto x = stream->getXRunCount();
    if (x && x.value() > lastXruns.load (std::memory_order_relaxed))
    {
        lastXruns.store (x.value(), std::memory_order_relaxed);
        const int burst = stream->getFramesPerBurst();
        const int size = stream->getBufferSizeInFrames();
        if (size + burst <= stream->getBufferCapacityInFrames()) stream->setBufferSizeInFrames (size + burst);
    }
    return oboe::DataCallbackResult::Continue;
}

void OboeDevice::onErrorAfterClose (oboe::AudioStream*, oboe::Result error)
{
    LOGW ("stream closed: %s, reopening", oboe::convertToText (error));
    if (callback) callback->onDeviceError (std::string ("Audio device changed: ") + oboe::convertToText (error));
    if (error != oboe::Result::ErrorDisconnected) return;
    std::thread ([this]
    {
        std::lock_guard<std::mutex> l (lock);
        if (! running.load() && ! out) return;
        closeLocked();
        std::string e;
        openLocked (e);
    }).detach();
}

DeviceStatus OboeDevice::status()
{
    std::lock_guard<std::mutex> l (lock);
    DeviceStatus s;
    s.running = running.load() && out != nullptr;
    s.inputOpen = in != nullptr;
    if (! out) return s;
    s.sampleRate = out->getSampleRate();
    s.framesPerBurst = out->getFramesPerBurst();
    s.bufferSizeFrames = out->getBufferSizeInFrames();
    s.api = out->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSL ES";
    s.exclusive = out->getSharingMode() == oboe::SharingMode::Exclusive;
    s.lowLatency = out->getPerformanceMode() == oboe::PerformanceMode::LowLatency;
    s.outputDevice = std::to_string (out->getDeviceId());
    if (in)
    {
        s.inputDevice = std::to_string (in->getDeviceId());
        auto ix = in->getXRunCount();
        s.inputXruns = ix ? ix.value() : 0;
    }
    auto x = out->getXRunCount();
    s.xruns = x ? x.value() : 0;
    s.latencyMeasured = measured.load();
    s.outputLatencyFrames = outLatency.load();
    s.inputLatencyFrames = in ? inLatency.load() : 0;
    if (! s.latencyMeasured)
    {
        // estimates without timestamps: the buffer plus one burst each way
        s.outputLatencyFrames = s.bufferSizeFrames + s.framesPerBurst;
        s.inputLatencyFrames = in ? 2 * s.framesPerBurst : 0;
    }
    return s;
}

void OboeDevice::monitorLoop()
{
    // Averages hardware-timestamp latency over time (4 measurements per second).
    double outAvg = -1.0, inAvg = -1.0;
    while (! monitorQuit.load())
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (250));
        std::lock_guard<std::mutex> l (lock);
        if (! out || ! running.load())
        {
            outAvg = inAvg = -1.0;
            measured.store (false);
            std::lock_guard<std::mutex> tl (tsLock);
            tsValid = false;
            continue;
        }
        const double sr = out->getSampleRate();
        bool okOut = false, okIn = ! in;
        if (out->getAudioApi() == oboe::AudioApi::AAudio)
        {
            auto ts = out->getTimestamp (CLOCK_MONOTONIC);
            if (ts)
            {
                {
                    std::lock_guard<std::mutex> tl (tsLock);
                    tsOutFrame = ts.value().position;
                    tsOutTime = ts.value().timestamp;
                }
                const int64_t written = out->getFramesWritten();
                const double presentedNow = double (ts.value().position) + double (nowNanos() - ts.value().timestamp) * 1e-9 * sr;
                const double lat = double (written) - presentedNow;
                if (lat > 0 && lat < sr)
                {
                    outAvg = outAvg < 0 ? lat : 0.8 * outAvg + 0.2 * lat;
                    okOut = true;
                }
            }
            if (in)
            {
                auto ti = in->getTimestamp (CLOCK_MONOTONIC);
                if (ti)
                {
                    {
                        std::lock_guard<std::mutex> tl (tsLock);
                        tsInFrame = ti.value().position;
                        tsInTime = ti.value().timestamp;
                        tsValid = static_cast<bool> (ts);
                    }
                    const int64_t read = in->getFramesRead();
                    const double capturedNow = double (ti.value().position) + double (nowNanos() - ti.value().timestamp) * 1e-9 * sr;
                    const double lat = capturedNow - double (read);
                    if (lat > 0 && lat < sr)
                    {
                        inAvg = inAvg < 0 ? lat : 0.8 * inAvg + 0.2 * lat;
                        okIn = true;
                    }
                }
            }
        }
        if (okOut) outLatency.store ((int) std::lround (outAvg));
        if (okIn && in) inLatency.store ((int) std::lround (inAvg));
        measured.store (okOut && okIn);
    }
}

} // namespace vox
