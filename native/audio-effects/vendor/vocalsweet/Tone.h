#pragma once
// Harmonic processors (saturation, exciter) and the vocal doubler.

#include "Svf.h"
#include <array>
#include <vector>

namespace vs::dsp
{
//==============================================================================
/** Asymmetric tanh waveshaper with first-order antiderivative anti-aliasing (ADAA).
    f(x) = (tanh(x + b) - tanh(b)) / f'(0); the bias b adds musically "warm" even harmonics.
    Only the *non-linear residual* r(x) = f(x) - x is passed through ADAA, so the clean signal
    is not low-pass filtered by the ADAA averaging and dry/wet blends do not comb-filter. */
class AdaaTanh
{
public:
    void setBias (float b) noexcept
    {
        if ((double) b == bias) return;
        bias = b;
        tb = std::tanh ((double) b);
        slope0 = 1.0 - tb * tb;
        fPrev = antiderivative (xPrev);   // keep the ADAA state consistent -> no click on change
    }

    void reset() noexcept { xPrev = 0.0; fPrev = antiderivative (0.0); }

    /** Returns the anti-aliased non-linear residual f(x) - x. */
    inline float processResidual (float xf) noexcept
    {
        const double x = (double) xf;
        const double F = antiderivative (x);
        const double dx = x - xPrev;
        double y;
        if (std::abs (dx) > 1.0e-5)
            y = (F - fPrev) / dx;
        else
            y = residual (0.5 * (x + xPrev));
        xPrev = x;
        fPrev = F;
        return (float) y;
    }

    inline double residual (double x) const noexcept { return (std::tanh (x + bias) - tb) / slope0 - x; }

private:
    static inline double logCosh (double x) noexcept
    {
        const double ax = std::abs (x);
        return ax + std::log1p (std::exp (-2.0 * ax)) - 0.69314718055994530942;
    }
    inline double antiderivative (double x) const noexcept
    {
        return (logCosh (x + bias) - x * tb) / slope0 - 0.5 * x * x;
    }

    double bias = 0.0, tb = 0.0, slope0 = 1.0, xPrev = 0.0, fPrev = 0.0;
};

//==============================================================================
class Saturator
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        dcCoeffs = designSvf (SvfType::HighPass, sr, 12.0, 0.707);
        // "warm" saturation: the shaper is fed a low-passed copy, so harmonics are generated
        // mainly from the body of the voice (less fizz on sibilants, far less aliasing).
        preCoeffs = designSvf (SvfType::LowPass, sr, std::min (4500.0, 0.4 * sr), 0.6);
        softenCoeffs = designSvf (SvfType::LowPass, sr, std::min (16000.0, 0.45 * sr), 0.6);
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : shaper) s.reset();
        for (auto& s : dc) s.reset();
        for (auto& s : soften) s.reset();
        for (auto& s : pre) s.reset();
    }

    /** drive01: 0..1 ; bias: asymmetry 0..0.5 */
    void setParams (float drive01, float biasAmount) noexcept
    {
        drive01 = std::clamp (drive01, 0.0f, 1.0f);
        preGain = dbToGain (drive01 * 18.0f);
        wet = std::sqrt (drive01);
        for (auto& s : shaper) s.setBias (biasAmount);
    }

    inline float process (int ch, float x) noexcept
    {
        // distortion products only (linear part cancels), DC-blocked and gently softened
        const float xl = pre[(size_t) ch].process (preCoeffs, x);
        float r = shaper[(size_t) ch].processResidual (xl * preGain) / preGain;
        r = dc[(size_t) ch].process (dcCoeffs, r);
        r = soften[(size_t) ch].process (softenCoeffs, r);
        return x + wet * r;
    }

    bool isFinite() const noexcept
    {
        for (auto& s : dc) if (! s.isFinite()) return false;
        for (auto& s : soften) if (! s.isFinite()) return false;
        for (auto& s : pre) if (! s.isFinite()) return false;
        return true;
    }

private:
    double sampleRate = 48000.0;
    std::array<AdaaTanh, 2> shaper;
    std::array<SvfState, 2> dc, soften, pre;
    SvfCoeffs dcCoeffs, softenCoeffs, preCoeffs;
    float preGain = 1.0f, wet = 0.0f;
};

//==============================================================================
/** Presence exciter: isolates the upper band, soft-saturates it to create new upper harmonics
    and blends it back in parallel. */
class Exciter
{
public:
    void prepare (double sr) noexcept { sampleRate = sr; reset(); }
    void reset() noexcept { for (auto& s : hp) s.reset(); for (auto& s : post) s.reset(); for (auto& s : shaper) s.reset(); }

    void setParams (float freqHz, float amount01) noexcept
    {
        hpCoeffs = designSvf (SvfType::HighPass, sampleRate, freqHz, 0.707);
        postCoeffs = designSvf (SvfType::HighPass, sampleRate, freqHz * 0.8, 0.6);
        amount = std::clamp (amount01, 0.0f, 1.0f) * 0.7f;
        for (auto& s : shaper) s.setBias (0.15f);
    }

    inline float process (int ch, float x) noexcept
    {
        const float h = hp[(size_t) ch].process (hpCoeffs, x);
        const float sat = h + shaper[(size_t) ch].processResidual (h * 4.0f) * 0.25f;
        const float e = post[(size_t) ch].process (postCoeffs, sat);
        return x + amount * e;
    }

    bool isFinite() const noexcept
    {
        for (auto& s : hp) if (! s.isFinite()) return false;
        for (auto& s : post) if (! s.isFinite()) return false;
        return true;
    }

private:
    double sampleRate = 48000.0;
    SvfCoeffs hpCoeffs, postCoeffs;
    std::array<SvfState, 2> hp, post;
    std::array<AdaaTanh, 2> shaper;
    float amount = 0.0f;
};

//==============================================================================
/** Two-voice modulated-delay doubler (ADT-style). Each voice uses a slowly modulated delay
    (≈ ±5 cents of pitch drift) and is panned according to Width. */
class Doubler
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        size = (int) std::ceil (0.045 * sr) + 4;
        for (auto& b : buffer) b.assign ((size_t) size, 0.0f);
        voiceHp = designSvf (SvfType::HighPass, sr, 160.0, 0.707);
        voiceLp = designSvf (SvfType::LowPass, sr, std::min (9000.0, 0.45 * sr), 0.707);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buffer) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& s : hpS) s.reset();
        for (auto& s : lpS) s.reset();
        writePos = 0; phaseA = 0.0; phaseB = 0.37;
    }

    void setParams (float amount01, float width01) noexcept
    {
        amount = std::clamp (amount01, 0.0f, 1.0f);
        width = std::clamp (width01, 0.0f, 1.0f);
    }

    /** In-place on one frame. */
    inline void processFrame (float* frame, int numChannels) noexcept
    {
        const float mono = numChannels > 1 ? 0.5f * (frame[0] + frame[1]) : frame[0];
        buffer[0][(size_t) writePos] = mono;

        phaseA += 0.43 / sampleRate; if (phaseA >= 1.0) phaseA -= 1.0;
        phaseB += 0.31 / sampleRate; if (phaseB >= 1.0) phaseB -= 1.0;
        const double dA = (0.0135 + 0.0016 * std::sin (2.0 * kPi * phaseA)) * sampleRate;
        const double dB = (0.0215 + 0.0021 * std::sin (2.0 * kPi * phaseB)) * sampleRate;

        float vA = read (dA), vB = read (dB);
        vA = lpS[0].process (voiceLp, hpS[0].process (voiceHp, vA));
        vB = lpS[1].process (voiceLp, hpS[1].process (voiceHp, vB));

        const float g = amount * 0.6f;
        if (numChannels > 1)
        {
            // width 0 -> both voices centred, width 1 -> hard left/right
            const float side = 0.5f + 0.5f * width;
            frame[0] += g * (side * vA + (1.0f - side) * vB);
            frame[1] += g * (side * vB + (1.0f - side) * vA);
        }
        else
        {
            frame[0] += g * 0.5f * (vA + vB);
        }

        writePos = (writePos + 1) % size;
    }

    bool isFinite() const noexcept
    {
        for (auto& s : hpS) if (! s.isFinite()) return false;
        for (auto& s : lpS) if (! s.isFinite()) return false;
        return true;
    }

private:
    inline float read (double delaySamples) const noexcept
    {
        double rp = (double) writePos - delaySamples;
        while (rp < 0.0) rp += (double) size;
        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % size;
        const float frac = (float) (rp - (double) i0);
        const auto& b = buffer[0];
        return b[(size_t) i0] + frac * (b[(size_t) i1] - b[(size_t) i0]);
    }

    double sampleRate = 48000.0, phaseA = 0.0, phaseB = 0.0;
    std::array<std::vector<float>, 1> buffer;
    int size = 2048, writePos = 0;
    SvfCoeffs voiceHp, voiceLp;
    std::array<SvfState, 2> hpS, lpS;
    float amount = 0.0f, width = 0.7f;
};

} // namespace vs::dsp
