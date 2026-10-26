#pragma once
// RecordingManager: captures the input on the audio thread into a lock-free ring buffer and
// writes it to disk on a writer thread (32-bit float WAV), so the real-time callback never
// touches the file system.
//
// Timing model: the audio thread remembers the transport position of the block in which the
// first input frame was captured (startPosition). Input frame k of the take was then captured
// k frames later. When the take is placed on the timeline the engine subtracts the measured
// round-trip latency (input + output + master chain latency) and adds the user's offset:
//     clip.start = startPosition - latencyCompensation + userOffset
// so that what the singer heard and what they sang line up.

#include "RingBuffer.h"
#include "WavWriter.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace vox
{
struct TakeResult
{
    bool ok = false;
    std::string error;            // non-empty: problem (storage full, overflow...)
    std::string path;
    std::string trackId;
    int64_t startPosition = 0;    // transport position of the first captured frame
    int64_t frames = 0;           // frames safely stored on disk
    int64_t droppedFrames = 0;    // ring overflow (should be 0)
    std::vector<int16_t> samples; // mono, project rate (for immediate playback)
};

class RecordingManager
{
public:
    RecordingManager();
    ~RecordingManager();

    /** Allocates the ring buffer (control thread, while not recording). */
    void prepare (int sampleRate, double bufferSeconds = 10.0);

    /** Control thread: opens the file and starts the writer thread. Capture starts at the
        next audio block once armed. */
    bool begin (const std::string& path, const std::string& trackId, std::string& error);

    /** Audio thread. mono == nullptr captures silence (input not available). */
    void capture (const float* mono, int n, int64_t transportPosition) noexcept;
    bool isArmed() const noexcept { return armed.load (std::memory_order_acquire); }
    bool isCapturing() const noexcept { return capturing.load (std::memory_order_acquire); }

    /** Control thread: stop capturing (the audio thread acknowledges at its next block). */
    void disarm() noexcept { armed.store (false, std::memory_order_release); }

    /** Control thread, after disarm() and the audio thread's acknowledgement (or with the
        device stopped): drains the buffer, closes the file and returns the take. */
    TakeResult finish();

    // status (any thread)
    int64_t capturedFrames() const noexcept { return captured.load (std::memory_order_relaxed); }
    int64_t droppedFrames() const noexcept { return dropped.load (std::memory_order_relaxed); }
    float takeInputPeak() noexcept { return inputPeak.exchange (0.0f, std::memory_order_relaxed); }
    bool active() const noexcept { return writerRunning.load(); }

private:
    void writerLoop();

    SpscRingBuffer ring;
    WavWriter writer;
    std::thread writerThread;
    std::atomic<bool> writerRunning { false }, stopWriter { false };
    std::atomic<bool> armed { false }, capturing { false };
    std::atomic<int64_t> startPos { 0 }, captured { 0 }, dropped { 0 };
    std::atomic<float> inputPeak { 0.0f };
    int rate = 48000;
    std::string currentPath, currentTrack;
    std::string writeError;
    std::vector<int16_t> memory;   // writer thread only until finish()
};

} // namespace vox
