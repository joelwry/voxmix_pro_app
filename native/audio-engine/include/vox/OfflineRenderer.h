#pragma once
// OfflineRenderer + AudioExporter.
//
// The mix is rendered with fresh effect instances (same parameters) through the same
// MixRenderer code as live playback, faster than real time, on a worker thread. It is then
// written to WAV and validated by decoding the file again (duration, channel count, peak).
// The project and its recordings are never modified.

#include "GraphBuilder.h"
#include <atomic>
#include <string>

namespace vox
{
struct RenderOptions
{
    std::string path;            // output file (.wav)
    int bitDepth = 24;           // 16, 24, 32 (float)
    int sampleRate = 0;          // 0 = project rate
    int channels = 2;            // 1 = mono fold-down
    bool includeTail = true;     // render effect tails after the last clip (max 6 s)
    bool masteringOverride = false, masteringOn = false;  // force mastering on/off
    bool writeFile = true;       // false: analysis only (loudness, peaks)
    int64_t startFrame = 0, endFrame = 0;   // 0/0 = whole song
};

struct RenderProgress
{
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancel { false };
};

struct RenderReport
{
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string path;
    int64_t frames = 0;          // at the output rate
    int sampleRate = 0, channels = 0, bitDepth = 0;
    double seconds = 0.0;
    float peakDb = -144.0f, truePeakDb = -144.0f, integratedLufs = -144.0f, rmsDb = -144.0f;
    bool clipped = false;
    int64_t fileBytes = 0;
    bool validated = false;      // file decoded back and checked
    std::string validationNote;
};

RenderReport renderProject (const Project& p, const SourcePool& sources, const RenderOptions& opts,
                            RenderProgress* progress = nullptr);

} // namespace vox
