#include "vox/AudioSource.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_WCHAR
#include <dr_libs/dr_wav.h>
#define DR_MP3_IMPLEMENTATION
#include <dr_libs/dr_mp3.h>

namespace vox
{
namespace
{
inline int16_t toI16 (float x) noexcept
{
    if (! std::isfinite (x)) x = 0.0f;
    const float v = std::clamp (x, -1.0f, 1.0f) * 32767.0f;
    return (int16_t) std::lrint (v);
}
constexpr float kI16ToF = 1.0f / 32768.0f;
constexpr double kPiD = 3.14159265358979323846;

std::string lowerExt (const std::string& path)
{
    const auto dot = path.find_last_of ('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr (dot + 1);
    for (auto& c : e) c = (char) std::tolower ((unsigned char) c);
    return e;
}

bool readHeader (const std::string& path, unsigned char* buf, size_t n)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    const size_t r = std::fread (buf, 1, n, f);
    std::fclose (f);
    return r == n;
}

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}
} // namespace

//==============================================================================
AudioSource::AudioSource (std::string id, int sampleRate, int channels, std::vector<std::vector<int16_t>> planar)
    : sourceId (std::move (id)), rate (sampleRate), numChannels (std::clamp (channels, 1, 2)), data (std::move (planar))
{
    data.resize ((size_t) numChannels);
    numFrames = (int64_t) data[0].size();
    for (auto& ch : data) ch.resize ((size_t) numFrames, 0);

    const int64_t buckets = (numFrames + kPeakBucket - 1) / kPeakBucket;
    peakData.assign ((size_t) buckets, 0.0f);
    for (int64_t b = 0; b < buckets; ++b)
    {
        int m = 0;
        const int64_t s = b * kPeakBucket, e = std::min (numFrames, s + kPeakBucket);
        for (const auto& ch : data)
            for (int64_t i = s; i < e; ++i)
                m = std::max (m, std::abs ((int) ch[(size_t) i]));
        peakData[(size_t) b] = (float) m * kI16ToF;
    }
}

void AudioSource::addTo (float* outL, float* outR, int64_t start, int n, float g0, float g1) const noexcept
{
    if (n <= 0) return;
    int64_t s = start;
    int skip = 0;
    if (s < 0) { skip = (int) std::min<int64_t> (n, -s); s = 0; }
    if (s >= numFrames) return;
    const int count = (int) std::min<int64_t> (n - skip, numFrames - s);
    if (count <= 0) return;
    const float step = (g1 - g0) / float (n);
    float g = g0 + step * float (skip);
    const int16_t* a = data[0].data() + s;
    if (numChannels == 1)
    {
        for (int i = 0; i < count; ++i, g += step)
        {
            const float v = float (a[i]) * kI16ToF * g;
            outL[skip + i] += v;
            outR[skip + i] += v;
        }
    }
    else
    {
        const int16_t* b = data[1].data() + s;
        for (int i = 0; i < count; ++i, g += step)
        {
            outL[skip + i] += float (a[i]) * kI16ToF * g;
            outR[skip + i] += float (b[i]) * kI16ToF * g;
        }
    }
}

float AudioSource::sample (int ch, int64_t frame) const noexcept
{
    if (frame < 0 || frame >= numFrames) return 0.0f;
    ch = std::clamp (ch, 0, numChannels - 1);
    return float (data[(size_t) ch][(size_t) frame]) * kI16ToF;
}

std::vector<float> AudioSource::waveform (int64_t start, int64_t length, int buckets) const
{
    buckets = std::clamp (buckets, 1, 4096);
    std::vector<float> out ((size_t) buckets, 0.0f);
    if (length <= 0) return out;
    const double per = double (length) / buckets;
    for (int b = 0; b < buckets; ++b)
    {
        const int64_t s = start + int64_t (b * per), e = std::max (s + 1, start + int64_t ((b + 1) * per));
        float m = 0.0f;
        if (per >= kPeakBucket * 2)
        {
            for (int64_t k = s / kPeakBucket; k <= (e - 1) / kPeakBucket && k < (int64_t) peakData.size(); ++k)
                if (k >= 0) m = std::max (m, peakData[(size_t) k]);
        }
        else
        {
            for (int64_t i = std::max<int64_t> (0, s); i < std::min (e, numFrames); ++i)
                for (int c = 0; c < numChannels; ++c)
                    m = std::max (m, std::abs (float (data[(size_t) c][(size_t) i]) * kI16ToF));
        }
        out[(size_t) b] = m;
    }
    return out;
}

//==============================================================================
bool decodeAudioFile (const std::string& path, DecodedAudio& out, std::string& error)
{
    unsigned char hdr[12] {};
    if (! readHeader (path, hdr, sizeof (hdr)))
    {
        error = "The file could not be opened or is empty.";
        return false;
    }
    const bool isRiff = std::memcmp (hdr, "RIFF", 4) == 0 && std::memcmp (hdr + 8, "WAVE", 4) == 0;
    const bool isRf64 = std::memcmp (hdr, "RF64", 4) == 0 || std::memcmp (hdr, "BW64", 4) == 0;
    const bool isId3 = std::memcmp (hdr, "ID3", 3) == 0;
    const bool isMpegSync = hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0;
    const std::string ext = lowerExt (path);

    if (isRiff || isRf64 || (ext == "wav" && ! isId3 && ! isMpegSync))
    {
        drwav wav;
        if (! drwav_init_file (&wav, path.c_str(), nullptr))
        {
            error = "This WAV file is damaged or uses an unsupported encoding.";
            return false;
        }
        if (wav.channels == 0 || wav.sampleRate == 0 || wav.totalPCMFrameCount == 0)
        {
            drwav_uninit (&wav);
            error = "This WAV file contains no audio.";
            return false;
        }
        out.sampleRate = (int) wav.sampleRate;
        out.channels = (int) wav.channels;
        out.interleaved.resize ((size_t) (wav.totalPCMFrameCount * wav.channels));
        const auto got = drwav_read_pcm_frames_f32 (&wav, wav.totalPCMFrameCount, out.interleaved.data());
        out.interleaved.resize ((size_t) (got * wav.channels));
        drwav_uninit (&wav);
        if (got == 0) { error = "This WAV file could not be decoded."; return false; }
        return true;
    }

    if (isId3 || isMpegSync || ext == "mp3")
    {
        drmp3_config cfg {};
        drmp3_uint64 frames = 0;
        float* pcm = drmp3_open_file_and_read_pcm_frames_f32 (path.c_str(), &cfg, &frames, nullptr);
        if (! pcm || frames == 0 || cfg.channels == 0)
        {
            if (pcm) drmp3_free (pcm, nullptr);
            error = "This MP3 file is damaged or could not be decoded.";
            return false;
        }
        out.sampleRate = (int) cfg.sampleRate;
        out.channels = (int) cfg.channels;
        out.interleaved.assign (pcm, pcm + frames * cfg.channels);
        drmp3_free (pcm, nullptr);
        return true;
    }

    error = "Unsupported audio format. Please import a WAV or MP3 file.";
    return false;
}

//==============================================================================
std::vector<float> resampleInterleaved (const std::vector<float>& in, int channels, double fromRate, double toRate, float* progress)
{
    if (channels <= 0 || in.empty()) return {};
    if (std::abs (fromRate - toRate) < 1e-9) { if (progress) *progress = 1.0f; return in; }

    const int64_t inFrames = (int64_t) in.size() / channels;
    const double ratio = toRate / fromRate;
    const int64_t outFrames = (int64_t) std::floor (double (inFrames) * ratio);
    std::vector<float> out ((size_t) (outFrames * channels), 0.0f);

    constexpr int kZeros = 32;               // zero crossings each side
    constexpr int kPhases = 512;
    constexpr double kBeta = 9.0;
    const double cutoff = 0.97 * std::min (1.0, ratio);   // relative to the input Nyquist
    const double scale = 1.0 / std::min (1.0, ratio);      // filter support stretched when downsampling
    const int halfTaps = (int) std::ceil (kZeros * scale);
    const int taps = 2 * halfTaps;

    // polyphase table: table[phase][tap], linearly interpolated between phases
    std::vector<float> table ((size_t) (kPhases + 1) * (size_t) taps);
    const double i0b = besselI0 (kBeta);
    for (int ph = 0; ph <= kPhases; ++ph)
    {
        const double frac = double (ph) / kPhases;
        double sum = 0.0;
        for (int t = 0; t < taps; ++t)
        {
            const double x = double (t - halfTaps + 1) - frac;    // distance in input samples
            const double arg = x * cutoff;
            const double sinc = std::abs (arg) < 1e-12 ? 1.0 : std::sin (kPiD * arg) / (kPiD * arg);
            const double r = x / double (halfTaps);
            const double win = std::abs (r) >= 1.0 ? 0.0 : besselI0 (kBeta * std::sqrt (1.0 - r * r)) / i0b;
            const double v = cutoff * sinc * win;
            table[(size_t) ph * taps + t] = (float) v;
            sum += v;
        }
        for (int t = 0; t < taps; ++t) table[(size_t) ph * taps + t] = (float) (table[(size_t) ph * taps + t] / sum);
    }

    std::vector<float> acc ((size_t) channels);
    for (int64_t o = 0; o < outFrames; ++o)
    {
        const double pos = double (o) / ratio;
        const int64_t base = (int64_t) std::floor (pos);
        const double frac = pos - double (base);
        const double fp = frac * kPhases;
        const int ph = std::min (kPhases - 1, (int) fp);
        const float mu = (float) (fp - ph);
        const float* t0 = &table[(size_t) ph * taps];
        const float* t1 = &table[(size_t) (ph + 1) * taps];
        std::fill (acc.begin(), acc.end(), 0.0f);
        for (int t = 0; t < taps; ++t)
        {
            const int64_t idx = base + t - halfTaps + 1;
            if (idx < 0 || idx >= inFrames) continue;
            const float w = t0[t] + mu * (t1[t] - t0[t]);
            const float* src = &in[(size_t) (idx * channels)];
            for (int c = 0; c < channels; ++c) acc[(size_t) c] += w * src[c];
        }
        for (int c = 0; c < channels; ++c) out[(size_t) (o * channels + c)] = acc[(size_t) c];
        if (progress && (o & 65535) == 0) *progress = float (o) / float (std::max<int64_t> (1, outFrames));
    }
    if (progress) *progress = 1.0f;
    return out;
}

AudioSourcePtr makeSource (const std::string& id, const DecodedAudio& in, int targetRate, int maxChannels, float* progress)
{
    if (in.channels <= 0 || in.interleaved.empty()) return nullptr;
    const int outCh = std::clamp (std::min (in.channels, maxChannels), 1, 2);

    // fold to the output channel count first (cheaper resampling)
    std::vector<float> folded;
    const int64_t frames = in.frames();
    if (in.channels == outCh)
        folded = in.interleaved;
    else
    {
        folded.assign ((size_t) (frames * outCh), 0.0f);
        for (int64_t f = 0; f < frames; ++f)
            for (int c = 0; c < in.channels; ++c)
            {
                const float v = in.interleaved[(size_t) (f * in.channels + c)];
                if (outCh == 1) folded[(size_t) f] += v / float (in.channels);
                else folded[(size_t) (f * 2 + (c % 2))] += v / float ((in.channels + 1 - (c % 2)) / 2);
            }
    }
    const auto rs = resampleInterleaved (folded, outCh, in.sampleRate, targetRate, progress);
    const int64_t outFrames = (int64_t) rs.size() / outCh;
    std::vector<std::vector<int16_t>> planar ((size_t) outCh, std::vector<int16_t> ((size_t) outFrames));
    for (int64_t f = 0; f < outFrames; ++f)
        for (int c = 0; c < outCh; ++c)
            planar[(size_t) c][(size_t) f] = toI16 (rs[(size_t) (f * outCh + c)]);
    return std::make_shared<AudioSource> (id, targetRate, outCh, std::move (planar));
}

AudioSourcePtr loadSourceFile (const std::string& id, const std::string& path, int projectRate, std::string& error)
{
    DecodedAudio d;
    if (! decodeAudioFile (path, d, error)) return nullptr;
    return makeSource (id, d, projectRate);
}

} // namespace vox
