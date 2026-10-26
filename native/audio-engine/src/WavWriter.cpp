#include "vox/WavWriter.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vox
{
namespace
{
void put16 (uint8_t* p, uint16_t v) { p[0] = uint8_t (v); p[1] = uint8_t (v >> 8); }
void put32 (uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t (v >> (8 * i)); }
uint32_t get32 (const uint8_t* p) { return uint32_t (p[0]) | uint32_t (p[1]) << 8 | uint32_t (p[2]) << 16 | uint32_t (p[3]) << 24; }
uint16_t get16 (const uint8_t* p) { return uint16_t (p[0] | p[1] << 8); }
constexpr int kHeaderSize = 44;
} // namespace

WavWriter::~WavWriter() { close(); }

bool WavWriter::open (const std::string& path, int sampleRate, int channels, int bitDepth, std::string& error)
{
    close();
    rate = sampleRate; numChannels = std::clamp (channels, 1, 8);
    bits = (bitDepth == 16 || bitDepth == 24 || bitDepth == 32) ? bitDepth : 24;
    frames = 0; failed = false;
    file = std::fopen (path.c_str(), "wb");
    if (! file) { error = "Cannot create " + path; return false; }
    if (! writeHeader()) { error = "Cannot write to " + path; std::fclose (file); file = nullptr; return false; }
    return true;
}

bool WavWriter::writeHeader()
{
    uint8_t h[kHeaderSize] {};
    const uint32_t bytesPerSample = uint32_t (bits / 8);
    const uint64_t dataBytes = uint64_t (frames) * numChannels * bytesPerSample;
    const uint32_t data32 = dataBytes > 0xFFFFFFF0ull ? 0xFFFFFFF0u : uint32_t (dataBytes);
    std::memcpy (h, "RIFF", 4);
    put32 (h + 4, 36 + data32);
    std::memcpy (h + 8, "WAVEfmt ", 8);
    put32 (h + 16, 16);
    put16 (h + 20, bits == 32 ? 3 : 1);       // 3 = IEEE float, 1 = PCM
    put16 (h + 22, uint16_t (numChannels));
    put32 (h + 24, uint32_t (rate));
    put32 (h + 28, uint32_t (rate) * numChannels * bytesPerSample);
    put16 (h + 32, uint16_t (numChannels * bytesPerSample));
    put16 (h + 34, uint16_t (bits));
    std::memcpy (h + 36, "data", 4);
    put32 (h + 40, data32);
    return std::fwrite (h, 1, kHeaderSize, file) == kHeaderSize;
}

bool WavWriter::write (const float* x, int64_t n)
{
    if (! file || failed || n <= 0) return ! failed;
    const size_t samples = size_t (n) * numChannels;
    const size_t bps = size_t (bits / 8);
    scratch.resize (samples * bps);
    uint8_t* o = scratch.data();
    for (size_t i = 0; i < samples; ++i)
    {
        float v = std::isfinite (x[i]) ? x[i] : 0.0f;
        if (bits == 32)
        {
            std::memcpy (o, &v, 4);
            o += 4;
            continue;
        }
        v = std::clamp (v, -1.0f, 1.0f);
        if (bits == 16)
        {
            const int s = (int) std::lrint (v * 32767.0f);
            put16 (o, uint16_t (int16_t (s)));
            o += 2;
        }
        else
        {
            const int s = (int) std::lrint (v * 8388607.0f);
            o[0] = uint8_t (s); o[1] = uint8_t (s >> 8); o[2] = uint8_t (s >> 16);
            o += 3;
        }
    }
    if (std::fwrite (scratch.data(), 1, scratch.size(), file) != scratch.size())
    {
        failed = true;
        return false;
    }
    frames += n;
    return true;
}

bool WavWriter::close()
{
    if (! file) return ! failed;
    bool ok = std::fflush (file) == 0 && ! failed;
    if (ok && std::fseek (file, 0, SEEK_SET) == 0) ok = writeHeader();
    ok = std::fclose (file) == 0 && ok;
    file = nullptr;
    if (! ok) failed = true;
    return ok;
}

int64_t repairWavHeader (const std::string& path)
{
    FILE* f = std::fopen (path.c_str(), "r+b");
    if (! f) return -1;
    uint8_t h[kHeaderSize] {};
    if (std::fread (h, 1, kHeaderSize, f) != kHeaderSize || std::memcmp (h, "RIFF", 4) != 0 || std::memcmp (h + 36, "data", 4) != 0)
    {
        std::fclose (f);
        return -1;
    }
    std::fseek (f, 0, SEEK_END);
    const long size = std::ftell (f);
    const uint16_t blockAlign = get16 (h + 32);
    if (size < kHeaderSize || blockAlign == 0) { std::fclose (f); return -1; }
    uint64_t dataBytes = uint64_t (size - kHeaderSize);
    dataBytes -= dataBytes % blockAlign;
    if (get32 (h + 40) != uint32_t (dataBytes))
    {
        put32 (h + 4, uint32_t (36 + dataBytes));
        put32 (h + 40, uint32_t (dataBytes));
        std::fseek (f, 0, SEEK_SET);
        std::fwrite (h, 1, kHeaderSize, f);
    }
    std::fclose (f);
    return int64_t (dataBytes / blockAlign);
}

} // namespace vox
