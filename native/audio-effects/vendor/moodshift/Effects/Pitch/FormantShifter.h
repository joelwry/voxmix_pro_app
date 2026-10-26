#pragma once

#include "../Utility/DspMath.h"
#include <array>
#include <vector>

namespace msfx::dsp
{
/** LPC-based formant shifter (zero added latency).

    Every hop the spectral envelope of the recent past is estimated with linear
    prediction (autocorrelation method + Levinson-Durbin). The signal is
    whitened with the analysis lattice A(z), and re-synthesised through an
    all-pole lattice 1/B(z) whose envelope is the original envelope warped along
    the frequency axis by `formantRatio`:  |1/B(w)|^2 ~ |1/A(w / ratio)|^2.
    Reflection coefficients are interpolated across each hop, which keeps the
    time-varying lattice stable (|k| < 1).

    Limitations: the envelope estimate lags the input by up to one analysis
    window (~25 ms); strongly noisy or polyphonic material yields a less
    defined envelope, so the effect becomes a gentler spectral tilt. */
class FormantShifter
{
public:
    static constexpr int maxOrder = 48;
    static constexpr int envBins = 320;

    void prepare (double sr);
    void reset();
    void setFormantRatio (float r) noexcept { ratio = clamp (r, 0.4f, 2.5f); }
    float process (float x) noexcept;

private:
    void analyse() noexcept;
    static bool levinson (const float* r, float* k, float& err, int order) noexcept;

    float sampleRate = 44100.0f, ratio = 1.0f;
    int windowSize = 1024, hop = 256, hopCounter = 0, writePos = 0;
    std::vector<float> history, window, frame;
    std::vector<float> cosTable;                         // [(maxOrder+1) * (envBins+1)]
    int order = 18;
    std::array<float, maxOrder + 1> kaCur {}, kaTarget {}, kaStep {};
    std::array<float, maxOrder + 1> kbCur {}, kbTarget {}, kbStep {};
    float gainCur = 1.0f, gainTarget = 1.0f, gainStep = 0.0f;
    std::array<float, maxOrder + 1> gA {}, gB {};           // lattice backward states
    float preState = 0.0f, deState = 0.0f;
};
} // namespace msfx::dsp
