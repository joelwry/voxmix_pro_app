#pragma once
// Immutable decoded audio held in memory for real-time playback.
//
// Samples are stored as planar int16 at the project sample rate. That halves memory against
// float (a 3-minute stereo beat is ~35 MB, a 3-minute mono take ~17 MB) and loses nothing for
// phone microphones and decoded MP3s. Sources are shared (shared_ptr<const>) between the
// control thread and render graphs and are never modified after creation.
// A future streaming source (disk read-ahead) can replace this behind the same read() call.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vox
{
class AudioSource
{
public:
    static constexpr int kPeakBucket = 256;   // frames per waveform overview bucket

    AudioSource (std::string id, int sampleRate, int channels, std::vector<std::vector<int16_t>> planar);

    const std::string& id() const noexcept { return sourceId; }
    int sampleRate() const noexcept { return rate; }
    int channels() const noexcept { return numChannels; }
    int64_t frames() const noexcept { return numFrames; }

    /** Adds `n` frames starting at `start` (clipped to the source) to outL/outR, scaled by gain.
        Mono sources feed both outputs. Real-time safe. */
    void addTo (float* outL, float* outR, int64_t start, int n, float gainStart, float gainEnd) const noexcept;

    /** Sample value of one channel at a frame (bounds checked), for tests and analysis. */
    float sample (int ch, int64_t frame) const noexcept;

    /** Max-abs overview, one value per kPeakBucket frames, 0..1. */
    const std::vector<float>& peaks() const noexcept { return peakData; }

    /** Waveform for a frame range resampled into `buckets` max-abs values (0..1). */
    std::vector<float> waveform (int64_t start, int64_t length, int buckets) const;

private:
    std::string sourceId;
    int rate, numChannels;
    int64_t numFrames;
    std::vector<std::vector<int16_t>> data;
    std::vector<float> peakData;
};

using AudioSourcePtr = std::shared_ptr<const AudioSource>;

/** Decoded file before conversion: interleaved float. */
struct DecodedAudio
{
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> interleaved;
    int64_t frames() const noexcept { return channels > 0 ? (int64_t) interleaved.size() / channels : 0; }
};

/** Decodes WAV (PCM 8/16/24/32, float, IMA/MS ADPCM) or MP3 by inspecting the file header.
    Returns false with a user-facing error for unsupported or corrupt files. */
bool decodeAudioFile (const std::string& path, DecodedAudio& out, std::string& error);

/** High-quality offline sample-rate conversion (windowed-sinc, Kaiser, 32 zero crossings).
    progress (0..1) may be null. */
std::vector<float> resampleInterleaved (const std::vector<float>& in, int channels, double fromRate, double toRate,
                                        float* progress = nullptr);

/** Converts decoded audio (any rate, 1..N channels) into a source at targetRate.
    More than two channels are folded to stereo. */
AudioSourcePtr makeSource (const std::string& id, const DecodedAudio& in, int targetRate, int maxChannels = 2,
                           float* progress = nullptr);

/** Loads a project media file (already at project rate) into a source. */
AudioSourcePtr loadSourceFile (const std::string& id, const std::string& path, int projectRate, std::string& error);

} // namespace vox
