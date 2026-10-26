#pragma once
// MeteringEngine building blocks.
//  - PeakMeter: per-track / master sample peak and clip detection (audio thread writes,
//    UI thread reads-and-resets through atomics).
//  - LoudnessMeter: ITU-R BS.1770-4 / EBU R128 loudness (K-weighting, momentary 400 ms,
//    short-term 3 s, gated integrated) plus true peak via 4x oversampling.
// Everything is allocated in prepare(); process() is real-time safe.

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vox
{
class PeakMeter
{
public:
    void process (const float* L, const float* R, int n) noexcept;
    /** Peak since the last call (linear), and whether a sample exceeded 0 dBFS. */
    float takePeakL() noexcept { return peakL.exchange (0.0f, std::memory_order_relaxed); }
    float takePeakR() noexcept { return peakR.exchange (0.0f, std::memory_order_relaxed); }
    bool clipped() const noexcept { return clip.load (std::memory_order_relaxed); }
    void resetClip() noexcept { clip.store (false, std::memory_order_relaxed); }

private:
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<bool> clip { false };
};

class LoudnessMeter
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void process (const float* L, const float* R, int n) noexcept;

    static constexpr float kSilence = -144.0f;
    float momentary() const noexcept { return mom.load (std::memory_order_relaxed); }
    float shortTerm() const noexcept { return st.load (std::memory_order_relaxed); }
    float maxMomentary() const noexcept { return momMax.load (std::memory_order_relaxed); }
    /** Gated integrated loudness since reset() (any thread). */
    float integrated() const noexcept;
    float truePeakDb() const noexcept { return tp.load (std::memory_order_relaxed); }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    double sampleRate = 48000.0;
    std::array<Biquad, 2> shelf, hp;
    int blockLen = 4800, blockPos = 0;
    double blockSum = 0.0;
    std::array<double, 30> blocks {};    // 100 ms sub-block energies (3 s)
    int blockCount = 0, blockIndex = 0;
    static constexpr int kBins = 1000;   // 0.1 LU bins from -70 to +30 LUFS
    std::array<std::atomic<uint32_t>, kBins> hist {};   // relaxed atomics: readable from the UI thread
    std::atomic<float> mom { kSilence }, st { kSilence }, momMax { kSilence }, tp { kSilence };

    // true peak (4x polyphase interpolation)
    static constexpr int kTaps = 12;
    std::array<std::array<float, kTaps>, 4> tpCoeffs {};
    std::array<std::array<float, kTaps>, 2> tpHist {};
    int tpPos = 0;
    float tpMax = 0.0f;
};

} // namespace vox
