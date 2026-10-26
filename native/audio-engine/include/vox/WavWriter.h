#pragma once
// Streaming WAV writer (PCM 16/24-bit or IEEE float 32-bit). The header is written with
// placeholder sizes and patched in close(), so an interrupted recording still leaves a file
// whose data can be recovered (repairWavHeader fixes the sizes from the file length).
// Not real-time safe: used by the recording writer thread and the offline exporter.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vox
{
class WavWriter
{
public:
    ~WavWriter();
    bool open (const std::string& path, int sampleRate, int channels, int bitDepth, std::string& error);
    /** Interleaved float frames. Returns false on a write error (e.g. storage full). */
    bool write (const float* interleaved, int64_t frames);
    bool close();
    bool isOpen() const noexcept { return file != nullptr; }
    int64_t framesWritten() const noexcept { return frames; }
    bool hadError() const noexcept { return failed; }

private:
    bool writeHeader();
    FILE* file = nullptr;
    int rate = 48000, numChannels = 2, bits = 16;
    int64_t frames = 0;
    bool failed = false;
    std::vector<uint8_t> scratch;
};

/** Rewrites the RIFF/data sizes of a WAV file whose writer did not close (crash recovery).
    Returns the number of audio frames found, or -1 if the file is not a recoverable WAV. */
int64_t repairWavHeader (const std::string& path);

} // namespace vox
