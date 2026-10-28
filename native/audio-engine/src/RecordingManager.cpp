#include "vox/RecordingManager.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace vox
{
RecordingManager::RecordingManager() { prepare (48000); }

RecordingManager::~RecordingManager()
{
    disarm();
    stopWriter.store (true);
    if (writerThread.joinable()) writerThread.join();
    writer.close();
}

void RecordingManager::prepare (int sampleRate, double bufferSeconds)
{
    if (writerRunning.load()) return;
    rate = sampleRate;
    ring.reset ((size_t) std::ceil (bufferSeconds * sampleRate));
}

bool RecordingManager::begin (const std::string& path, const std::string& trackId, std::string& error)
{
    if (writerRunning.load()) { error = "A recording is already in progress"; return false; }
    if (writerThread.joinable()) writerThread.join();
    if (! writer.open (path, rate, 1, 32, error)) return false;
    currentPath = path;
    currentTrack = trackId;
    writeError.clear();
    memory.clear();
    memory.reserve ((size_t) rate * 60);
    ring.reset (ring.capacity());
    captured.store (0); dropped.store (0); startPos.store (0);
    capturing.store (false);
    stopWriter.store (false);
    writerRunning.store (true);
    writerThread = std::thread ([this] { writerLoop(); });
    armed.store (true, std::memory_order_release);
    return true;
}

void RecordingManager::capture (const float* mono, int n, int64_t pos) noexcept
{
    if (! armed.load (std::memory_order_acquire))
    {
        capturing.store (false, std::memory_order_release);
        return;
    }
    if (n <= 0) return;   // nothing delivered this callback: start (or continue) with the next frames
    if (! capturing.load (std::memory_order_relaxed))
    {
        startPos.store (pos, std::memory_order_relaxed);
        capturing.store (true, std::memory_order_release);
    }
    float peak = 0.0f;
    static const float silence[1024] {};
    int done = 0;
    while (done < n)
    {
        const int m = std::min (n - done, 1024);
        const float* src = mono ? mono + done : silence;
        if (mono) for (int i = 0; i < m; ++i) peak = std::max (peak, std::abs (src[i]));
        const size_t w = ring.push (src, (size_t) m);
        if ((int) w < m) dropped.fetch_add (m - (int) w, std::memory_order_relaxed);
        done += m;
    }
    captured.fetch_add (n, std::memory_order_relaxed);
    if (peak > inputPeak.load (std::memory_order_relaxed)) inputPeak.store (peak, std::memory_order_relaxed);
}

void RecordingManager::writerLoop()
{
    std::vector<float> chunk (8192);
    for (;;)
    {
        const bool stopping = stopWriter.load();
        const size_t got = ring.pop (chunk.data(), chunk.size());
        if (got > 0)
        {
            if (writeError.empty() && ! writer.write (chunk.data(), (int64_t) got))
                writeError = "Storage is full: the recording was saved up to this point.";
            if (writeError.empty())
                for (size_t i = 0; i < got; ++i)
                {
                    const float v = std::clamp (chunk[i], -1.0f, 1.0f);
                    memory.push_back ((int16_t) std::lrint (v * 32767.0f));
                }
            continue;
        }
        if (stopping) break;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}

TakeResult RecordingManager::finish()
{
    TakeResult r;
    disarm();
    stopWriter.store (true);
    if (writerThread.joinable()) writerThread.join();
    const bool closed = writer.close();
    writerRunning.store (false);
    capturing.store (false);

    r.path = currentPath;
    r.trackId = currentTrack;
    r.startPosition = startPos.load();
    r.droppedFrames = dropped.load();
    r.frames = writer.framesWritten();
    r.samples = std::move (memory);
    r.samples.resize ((size_t) r.frames);
    memory = {};
    if (! writeError.empty()) r.error = writeError;
    else if (! closed) r.error = "The recording file could not be finalised.";
    else if (r.droppedFrames > 0)
        r.error = "The device could not keep up: " + std::to_string (r.droppedFrames) + " frames were lost.";
    r.ok = r.frames > 0;
    if (r.frames == 0 && r.error.empty()) r.error = "Nothing was recorded.";
    return r;
}

} // namespace vox
