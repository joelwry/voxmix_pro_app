#pragma once
// Dynamics processors: vocal compressor, split-band de-esser and look-ahead peak limiter.
// All are allocation-free after prepare() and framework independent.

#include "Svf.h"
#include <array>
#include <vector>

namespace vs::dsp
{
constexpr int kMaxChannels = 2;

inline float timeToCoeff (double sampleRate, double ms) noexcept
{
    return (float) std::exp (-1.0 / (std::max (ms, 0.01) * 0.001 * sampleRate));
}

//==============================================================================
/** Stereo-linked feed-forward compressor with soft knee; gain smoothing in the dB domain. */
class Compressor
{
public:
    void prepare (double sr) noexcept { sampleRate = sr; reset(); }
    void reset() noexcept { grDb = 0.0f; envDb = -120.0f; }

    void setParams (float thresholdDb, float ratio, float attackMs, float releaseMs, float kneeDb = 6.0f) noexcept
    {
        threshold = thresholdDb;
        slope = 1.0f - 1.0f / std::max (ratio, 1.0f);
        knee = std::max (kneeDb, 0.01f);
        attCoeff = timeToCoeff (sampleRate, attackMs);
        relCoeff = timeToCoeff (sampleRate, releaseMs);
    }

    /** Static gain computer: returns desired gain reduction in dB (>= 0). */
    inline float computeGr (float levelDb) const noexcept
    {
        const float over = levelDb - threshold;
        if (2.0f * over <= -knee) return 0.0f;
        if (2.0f * over >= knee)  return slope * over;
        const float x = over + knee * 0.5f;
        return slope * x * x / (2.0f * knee);
    }

    /** Process one linked detector sample; returns linear gain to apply. */
    inline float processSample (float detector) noexcept
    {
        const float levelDb = gainToDbFast (detector);
        const float target = computeGr (levelDb);
        const float coeff = target > grDb ? attCoeff : relCoeff;
        grDb = target + coeff * (grDb - target);
        return dbToGain (-grDb);
    }

    float getGainReductionDb() const noexcept { return grDb; }

    static inline float gainToDbFast (float x) noexcept
    {
        return x > 1.0e-6f ? 20.0f * std::log10 (x) : -120.0f;
    }

private:
    double sampleRate = 48000.0;
    float threshold = -18.0f, slope = 0.66f, knee = 6.0f;
    float attCoeff = 0.0f, relCoeff = 0.0f;
    float grDb = 0.0f, envDb = -120.0f;
};

//==============================================================================
/** Split-band de-esser. A band-pass detector watches the sibilance region; when it exceeds the
    threshold only the high band is turned down. The split is complementary (high = x - LP(x)),
    so y = LP(x) + g * high: the body of the vocal is untouched and with no reduction the split
    reconstructs the input exactly. */
class DeEsser
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        attCoeff = timeToCoeff (sr, 0.4);
        relCoeff = timeToCoeff (sr, 60.0);
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : lpState) s.reset();
        for (auto& s : bpState) s.reset();
        env = 0.0f;
        grDb = 0.0f;
    }

    void setParams (float freqHz, float thresholdDb, float rangeDb) noexcept
    {
        lpCoeffs = designSvf (SvfType::LowPass, sampleRate, freqHz * 0.7, 0.707);
        bpCoeffs = designSvf (SvfType::BandPass, sampleRate, freqHz, 1.2);
        threshold = thresholdDb;
        range = std::max (rangeDb, 0.0f);
    }

    /** In-place process of one frame (all channels at sample index). */
    inline void processFrame (float* frame, int numChannels) noexcept
    {
        float det = 0.0f;
        float hi[kMaxChannels] {};
        for (int ch = 0; ch < numChannels; ++ch)
        {
            det = std::max (det, std::abs (bpState[(size_t) ch].process (bpCoeffs, frame[ch])));
            hi[ch] = frame[ch] - lpState[(size_t) ch].process (lpCoeffs, frame[ch]);
        }

        const float c = det > env ? attCoeff : relCoeff;
        env = det + c * (env - det);

        const float levelDb = Compressor::gainToDbFast (env);
        const float over = levelDb - threshold;
        const float target = over > 0.0f ? std::min (over * 0.75f, range) : 0.0f; // ~4:1 above threshold
        // light smoothing of the gain itself avoids zipper noise on fast sibilants
        grDb = target + 0.6f * (grDb - target);
        const float g = dbToGain (-grDb);

        for (int ch = 0; ch < numChannels; ++ch)
            frame[ch] += (g - 1.0f) * hi[ch];
    }

    float getGainReductionDb() const noexcept { return grDb; }

    bool isFinite() const noexcept
    {
        for (auto& s : lpState) if (! s.isFinite()) return false;
        for (auto& s : bpState) if (! s.isFinite()) return false;
        return std::isfinite (env);
    }

private:
    double sampleRate = 48000.0;
    SvfCoeffs lpCoeffs, bpCoeffs;
    std::array<SvfState, kMaxChannels> lpState, bpState;
    float threshold = -30.0f, range = 8.0f;
    float attCoeff = 0.0f, relCoeff = 0.0f, env = 0.0f, grDb = 0.0f;
};

//==============================================================================
/** Stereo-linked look-ahead peak limiter (min-hold + box-car smoothing guarantees the gain has
    fully reached its target when the peak leaves the delay line). Adds a fixed latency. */
class Limiter
{
public:
    void prepare (double sr, double lookaheadMs = 1.0)
    {
        sampleRate = sr;
        lookahead = std::max (1, (int) std::lround (lookaheadMs * 0.001 * sr));
        for (auto& d : delay) d.assign ((size_t) lookahead + 1, 0.0f);
        gainHist.assign ((size_t) lookahead + 1, 1.0f); // window covers the delayed sample
        boxHist.assign ((size_t) lookahead, 1.0f);
        relCoeff = timeToCoeff (sr, 80.0);
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (gainHist.begin(), gainHist.end(), 1.0f);
        std::fill (boxHist.begin(), boxHist.end(), 1.0f);
        boxSum = (double) lookahead;
        pos = 0; histPos = 0; minPos = 0; smoothGain = 1.0f; grDb = 0.0f;
    }

    int getLatency() const noexcept { return lookahead; }
    void setCeiling (float ceilingDb) noexcept { ceiling = dbToGain (ceilingDb); }
    void setEnabled (bool e) noexcept { enabled = e; }

    inline void processFrame (float* frame, int numChannels) noexcept
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max (peak, std::abs (frame[ch]));

        const float required = (enabled && peak > ceiling) ? ceiling / peak : 1.0f;

        // running minimum over the look-ahead window
        gainHist[(size_t) minPos] = required;
        minPos = (minPos + 1) % (lookahead + 1);
        float minG = 1.0f;
        for (float g : gainHist) minG = std::min (minG, g);

        // instant attack, smooth release
        smoothGain = minG < smoothGain ? minG : minG + relCoeff * (smoothGain - minG);

        // box-car average of length `lookahead`
        boxSum += (double) smoothGain - (double) boxHist[(size_t) histPos];
        boxHist[(size_t) histPos] = smoothGain;
        histPos = (histPos + 1) % lookahead;
        const float g = (float) (boxSum / (double) lookahead);

        const int readPos = (pos + 1) % (lookahead + 1);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& d = delay[(size_t) ch];
            d[(size_t) pos] = frame[ch];
            float y = d[(size_t) readPos] * g;
            // absolute safety: never exceed the ceiling (only active when limiter is enabled)
            if (enabled) y = std::clamp (y, -ceiling, ceiling);
            frame[ch] = y;
        }
        pos = readPos;
        grDb = std::max (grDb, -gainToDb (g));
    }

    /** Peak gain reduction since last call (for metering). */
    float popGainReductionDb() noexcept { const float r = grDb; grDb = 0.0f; return r; }

private:
    double sampleRate = 48000.0;
    int lookahead = 48, pos = 0, histPos = 0, minPos = 0;
    std::array<std::vector<float>, kMaxChannels> delay;
    std::vector<float> gainHist, boxHist;
    double boxSum = 48.0;
    float ceiling = 0.891f, relCoeff = 0.0f, smoothGain = 1.0f, grDb = 0.0f;
    bool enabled = true;
};

//==============================================================================
/** Simple integer delay line used to latency-compensate the bypass path. */
class DelayLine
{
public:
    void prepare (int maxDelay) { for (auto& b : buf) b.assign ((size_t) maxDelay + 1, 0.0f); size = maxDelay + 1; reset(); }
    void reset() noexcept { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); pos = 0; }
    void setDelay (int d) noexcept { delay = std::clamp (d, 0, size - 1); }

    inline float process (int ch, float x) noexcept
    {
        auto& b = buf[(size_t) ch];
        b[(size_t) pos] = x;
        int r = pos - delay; if (r < 0) r += size;
        return b[(size_t) r];
    }
    void advance() noexcept { pos = (pos + 1) % size; }

private:
    std::array<std::vector<float>, kMaxChannels> buf;
    int size = 1, pos = 0, delay = 0;
};

} // namespace vs::dsp
