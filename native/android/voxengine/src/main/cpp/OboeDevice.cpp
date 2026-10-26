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
    ob.setDirection (oboe::Direction::Output)
        ->setPerformanceMode (oboe::PerformanceMode::LowLatency)
        ->setSharingMode (oboe::SharingMode::Exclusive)
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
            ->setSharingMode (oboe::SharingMode::Exclusive)
            ->setFormat (oboe::AudioFormat::Float)
            ->setChannelCount (oboe::ChannelCount::Mono)
            ->setSampleRate (requestedRate)
            ->setSampleRateConversionQuality (oboe::SampleRateConversionQuality::Medium)
            ->setInputPreset (sdkLevel() >= 29 ? oboe::InputPreset::VoicePerformance : oboe::InputPreset::VoiceRecognition)
            ->setBufferCapacityInFrames (std::max (out->getFramesPerBurst() * 8, 2048));
        r = ib.openStream (in);
        if (r != oboe::Result::OK)
        {
            LOGW ("input open failed: %s", oboe::convertToText (r));
            error = std::string ("Could not open the microphone: ") + oboe::convertToText (r);
            in.reset();
        }
    }

    // low latency: two bursts of buffering to start with
    out->setBufferSizeInFrames (out->getFramesPerBurst() * 2);
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
    int inFrames = 0;
    if (input)
    {
        if (drainInput.exchange (false))
        {
            // discard what accumulated while starting, so the input path starts with minimal latency
            while (input->read (inBuf.data(), (int32_t) inBuf.size(), 0).value() > 0) {}
        }
        const int cap = (int) inBuf.size();
        auto avail = input->getAvailableFrames();
        int want = avail ? std::min (cap, std::max (0, avail.value())) : numFrames;
        if (want <= 0) want = 0;
        if (want > 0)
        {
            auto res = input->read (inBuf.data(), want, 0);
            inFrames = res ? res.value() : 0;
        }
    }
    if (callback)
        callback->onAudio (inFrames > 0 ? inBuf.data() : nullptr, 1, inFrames, output, numFrames);
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
    if (in) s.inputDevice = std::to_string (in->getDeviceId());
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
        if (! out || ! running.load()) { outAvg = inAvg = -1.0; measured.store (false); continue; }
        const double sr = out->getSampleRate();
        bool okOut = false, okIn = ! in;
        if (out->getAudioApi() == oboe::AudioApi::AAudio)
        {
            auto ts = out->getTimestamp (CLOCK_MONOTONIC);
            if (ts)
            {
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
