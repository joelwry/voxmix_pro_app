#include "vox/Metering.h"
#include <algorithm>
#include <cmath>

namespace vox
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
inline double energyToLufs (double e) { return e > 1e-20 ? -0.691 + 10.0 * std::log10 (e) : LoudnessMeter::kSilence; }
inline double lufsToEnergy (double l) { return std::pow (10.0, (l + 0.691) / 10.0); }
} // namespace

void PeakMeter::process (const float* L, const float* R, int n) noexcept
{
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        l = std::max (l, std::abs (L[i]));
        r = std::max (r, std::abs (R[i]));
    }
    if (l > peakL.load (std::memory_order_relaxed)) peakL.store (l, std::memory_order_relaxed);
    if (r > peakR.load (std::memory_order_relaxed)) peakR.store (r, std::memory_order_relaxed);
    if (l > 1.0f || r > 1.0f) clip.store (true, std::memory_order_relaxed);
}

//==============================================================================
void LoudnessMeter::prepare (double sr)
{
    sampleRate = sr;
    // BS.1770 K-weighting, coefficients derived for any sample rate (same design as libebur128)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = (Vh + Vb * K / Q + K * K) / a0;
        b.b1 = 2.0 * (K * K - Vh) / a0;
        b.b2 = (Vh - Vb * K / Q + K * K) / a0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        shelf = { b, b };
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (kPi * f0 / sr);
        const double den = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
        b.a1 = 2.0 * (K * K - 1.0) / den;
        b.a2 = (1.0 - K / Q + K * K) / den;
        hp = { b, b };
    }
    blockLen = std::max (1, int (std::lround (0.1 * sr)));

    // 4x interpolator: windowed sinc, 48 taps total
    for (int ph = 0; ph < 4; ++ph)
    {
        double sum = 0.0;
        for (int t = 0; t < kTaps; ++t)
        {
            const double x = double (t - kTaps / 2 + 1) - double (ph) / 4.0;
            const double s = std::abs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double w = 0.5 + 0.5 * std::cos (kPi * x / (kTaps / 2.0));
            tpCoeffs[(size_t) ph][(size_t) t] = float (s * w);
            sum += s * w;
        }
        for (auto& c : tpCoeffs[(size_t) ph]) c = float (c / sum);
    }
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& b : shelf) b.z1 = b.z2 = 0.0;
    for (auto& b : hp) b.z1 = b.z2 = 0.0;
    blockPos = 0; blockSum = 0.0; blockCount = 0; blockIndex = 0;
    blocks.fill (0.0);
    for (auto& h : hist) h.store (0, std::memory_order_relaxed);
    for (auto& h : tpHist) h.fill (0.0f);
    tpPos = 0; tpMax = 0.0f;
    mom.store (kSilence); st.store (kSilence); momMax.store (kSilence); tp.store (kSilence);
}

void LoudnessMeter::process (const float* L, const float* R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const double l = hp[0].process (shelf[0].process (L[i]));
        const double r = hp[1].process (shelf[1].process (R[i]));
        blockSum += l * l + r * r;

        // true peak
        tpHist[0][(size_t) tpPos] = L[i];
        tpHist[1][(size_t) tpPos] = R[i];
        for (int ch = 0; ch < 2; ++ch)
            for (int ph = 0; ph < 4; ++ph)
            {
                float acc = 0.0f;
                for (int t = 0; t < kTaps; ++t)
                    acc += tpCoeffs[(size_t) ph][(size_t) t] * tpHist[(size_t) ch][(size_t) ((tpPos - t + kTaps) % kTaps)];
                tpMax = std::max (tpMax, std::abs (acc));
            }
        tpPos = (tpPos + 1) % kTaps;

        if (++blockPos >= blockLen)
        {
            blocks[(size_t) blockIndex] = blockSum / double (blockLen);
            blockIndex = (blockIndex + 1) % (int) blocks.size();
            blockCount = std::min (blockCount + 1, 1 << 30);
            blockSum = 0.0; blockPos = 0;

            auto meanOf = [this] (int count)
            {
                double s = 0.0;
                const int avail = std::min (count, std::min (blockCount, (int) blocks.size()));
                for (int k = 1; k <= avail; ++k)
                    s += blocks[(size_t) ((blockIndex - k + (int) blocks.size()) % (int) blocks.size())];
                return avail > 0 ? s / double (count) : 0.0;
            };
            if (blockCount >= 4)
            {
                const double m = energyToLufs (meanOf (4));
                mom.store (float (m), std::memory_order_relaxed);
                if (m > momMax.load (std::memory_order_relaxed)) momMax.store (float (m), std::memory_order_relaxed);
                if (m > -70.0)
                {
                    const int bin = std::clamp (int ((m + 70.0) * 10.0), 0, kBins - 1);
                    hist[(size_t) bin].fetch_add (1, std::memory_order_relaxed);
                }
            }
            if (blockCount >= 30) st.store (float (energyToLufs (meanOf (30))), std::memory_order_relaxed);
            else if (blockCount >= 4) st.store (float (energyToLufs (meanOf (blockCount))), std::memory_order_relaxed);
            tp.store (tpMax > 0.0f ? 20.0f * std::log10 (tpMax) : kSilence, std::memory_order_relaxed);
        }
    }
}

float LoudnessMeter::integrated() const noexcept
{
    double sum = 0.0;
    uint64_t count = 0;
    std::array<uint32_t, kBins> h {};
    for (int b = 0; b < kBins; ++b) h[(size_t) b] = hist[(size_t) b].load (std::memory_order_relaxed);
    for (int b = 0; b < kBins; ++b)
        if (h[(size_t) b])
        {
            sum += h[(size_t) b] * lufsToEnergy (-70.0 + (b + 0.5) * 0.1);
            count += h[(size_t) b];
        }
    if (count == 0) return kSilence;
    const double relGate = energyToLufs (sum / double (count)) - 10.0;
    sum = 0.0; count = 0;
    for (int b = 0; b < kBins; ++b)
    {
        const double l = -70.0 + (b + 0.5) * 0.1;
        if (h[(size_t) b] && l >= relGate)
        {
            sum += h[(size_t) b] * lufsToEnergy (l);
            count += h[(size_t) b];
        }
    }
    return count ? float (energyToLufs (sum / double (count))) : kSilence;
}

} // namespace vox
