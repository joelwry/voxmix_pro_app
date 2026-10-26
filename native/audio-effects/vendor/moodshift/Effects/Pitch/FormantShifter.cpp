#include "FormantShifter.h"

namespace msfx::dsp
{
namespace
{
    constexpr float kPreEmphasis = 0.9f;
    constexpr float kMaxReflection = 0.995f;
}

void FormantShifter::prepare (double sr)
{
    sampleRate = float (sr);
    // Enough poles to resolve formants at any sample rate (~ one pole pair per 2.2 kHz).
    order = clamp (int (std::lround (sr / 1100.0)), 16, maxOrder);
    windowSize = clamp (int (0.03 * sr), 256, 8192);
    hop = std::max (32, windowSize / 4);
    history.assign (size_t (windowSize), 0.0f);
    frame.assign (size_t (windowSize), 0.0f);
    window.resize (size_t (windowSize));
    for (int i = 0; i < windowSize; ++i)
        window[size_t (i)] = 0.5f - 0.5f * std::cos (kTwoPi * (float (i) + 0.5f) / float (windowSize));

    // cos(k * w_m) for the envelope -> autocorrelation transform
    cosTable.resize (size_t ((maxOrder + 1) * (envBins + 1)));
    for (int k = 0; k <= maxOrder; ++k)
        for (int m = 0; m <= envBins; ++m)
            cosTable[size_t (k * (envBins + 1) + m)] = std::cos (float (k) * kPi * float (m) / float (envBins));
    reset();
}

void FormantShifter::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    writePos = 0;
    hopCounter = 0;
    kaCur.fill (0.0f); kaTarget.fill (0.0f); kaStep.fill (0.0f);
    kbCur.fill (0.0f); kbTarget.fill (0.0f); kbStep.fill (0.0f);
    gainCur = gainTarget = 1.0f;
    gainStep = 0.0f;
    gA.fill (0.0f);
    gB.fill (0.0f);
    preState = deState = 0.0f;
}

bool FormantShifter::levinson (const float* r, float* k, float& err, int order) noexcept
{
    std::array<double, maxOrder + 1> a {}, tmp {};
    a[0] = 1.0;
    double e = double (r[0]);
    if (e <= 1.0e-12) return false;

    for (int i = 1; i <= order; ++i)
    {
        double acc = double (r[i]);
        for (int j = 1; j < i; ++j) acc += a[size_t (j)] * double (r[i - j]);
        double ki = -acc / e;
        ki = clamp (ki, double (-kMaxReflection), double (kMaxReflection));
        k[i] = float (ki);
        tmp = a;
        for (int j = 1; j < i; ++j) a[size_t (j)] = tmp[size_t (j)] + ki * tmp[size_t (i - j)];
        a[size_t (i)] = ki;
        e *= (1.0 - ki * ki);
        if (e <= 1.0e-15) { for (int j = i + 1; j <= order; ++j) k[j] = 0.0f; break; }
    }
    err = float (e);
    return true;
}

void FormantShifter::analyse() noexcept
{
    // Windowed autocorrelation of the most recent `windowSize` pre-emphasised samples.
    std::array<float, maxOrder + 1> r {};
    const int n = windowSize;
    // Unroll the circular buffer (oldest first) into a windowed linear frame.
    for (int i = 0; i < n; ++i)
    {
        const int idx = writePos + i < n ? writePos + i : writePos + i - n;
        frame[size_t (i)] = history[size_t (idx)] * window[size_t (i)];
    }
    for (int lag = 0; lag <= order; ++lag)
    {
        double acc = 0.0;
        const float* f0 = frame.data() + lag;
        const float* f1 = frame.data();
        float part = 0.0f;
        for (int i = 0; i < n - lag; ++i)
        {
            part += f0[i] * f1[i];
            if ((i & 63) == 63) { acc += double (part); part = 0.0f; }
        }
        r[size_t (lag)] = float (acc + double (part));
    }

    if (r[0] < 1.0e-9f * float (n))
    {
        // Near silence: fade towards a neutral (pass-through) lattice.
        kaTarget.fill (0.0f);
        kbTarget.fill (0.0f);
        gainTarget = 1.0f;
    }
    else
    {
        r[0] *= 1.0001f;                                   // white-noise correction
        for (int lag = 1; lag <= order; ++lag)             // lag window (~ 50 Hz bandwidth expansion)
        {
            const float g = 2.0f * kPi * 60.0f * float (lag) / sampleRate;
            r[size_t (lag)] *= std::exp (-0.5f * g * g);
        }

        std::array<float, maxOrder + 1> ka {};
        float errA = 0.0f;
        if (! levinson (r.data(), ka.data(), errA, order)) return;

        // Convert reflection coefficients to direct form A(z) to evaluate the envelope.
        std::array<double, maxOrder + 1> a {}, tmp {};
        a[0] = 1.0;
        for (int i = 1; i <= order; ++i)
        {
            tmp = a;
            for (int j = 1; j < i; ++j) a[size_t (j)] = tmp[size_t (j)] + double (ka[size_t (i)]) * tmp[size_t (i - j)];
            a[size_t (i)] = double (ka[size_t (i)]);
        }

        // Warped power envelope P'(w) = errA / |A(e^{j w / ratio})|^2 sampled on [0, pi]
        std::array<float, envBins + 1> env {};
        for (int m = 0; m <= envBins; ++m)
        {
            const double wq = kPiD * double (m) / double (envBins) / double (ratio);
            const double w = std::min (kPiD, wq);
            // Beyond Nyquist (downward shifts) the envelope is extrapolated with a steep roll-off.
            const double beyond = wq > kPiD ? std::pow (kPiD / wq, 4.0) : 1.0;
            // Horner evaluation of A(z) at z^-1 = e^{-jw}
            const double cr = std::cos (w), ci = -std::sin (w);
            double re = a[size_t (order)], im = 0.0;
            for (int j = order - 1; j >= 0; --j)
            {
                const double nr = re * cr - im * ci + a[size_t (j)];
                const double ni = re * ci + im * cr;
                re = nr; im = ni;
            }
            const double mag2 = std::max (1.0e-9, re * re + im * im);
            // Warp the *true* envelope, not the pre-emphasis tilt: |E(w)|^2 / |E(w/ratio)|^2
            const double wt = kPiD * double (m) / double (envBins);
            const double pe = double (kPreEmphasis);
            const double tilt = (1.0 + pe * pe - 2.0 * pe * std::cos (wt)) / (1.0 + pe * pe - 2.0 * pe * std::cos (w));
            env[size_t (m)] = float (std::min (1.0e9, tilt * beyond * double (errA) / mag2));
        }

        // Inverse "DTFT" of the even power spectrum -> autocorrelation r'
        std::array<float, maxOrder + 1> rb {};
        for (int k = 0; k <= order; ++k)
        {
            const float* c = &cosTable[size_t (k * (envBins + 1))];
            double acc = 0.5 * (double (env[0]) * c[0] + double (env[envBins]) * c[envBins]);
            for (int m = 1; m < envBins; ++m) acc += double (env[size_t (m)]) * double (c[m]);
            rb[size_t (k)] = float (acc / double (envBins));
        }
        rb[0] *= 1.0001f;

        std::array<float, maxOrder + 1> kb {};
        float errB = 0.0f;
        if (! levinson (rb.data(), kb.data(), errB, order)) return;

        kaTarget = ka;
        kbTarget = kb;
        gainTarget = clamp (std::sqrt (std::max (0.0f, errB) / std::max (1.0e-12f, errA)), 0.0f, 8.0f);
    }

    const float inv = 1.0f / float (hop);
    for (int i = 1; i <= order; ++i)
    {
        kaStep[size_t (i)] = (kaTarget[size_t (i)] - kaCur[size_t (i)]) * inv;
        kbStep[size_t (i)] = (kbTarget[size_t (i)] - kbCur[size_t (i)]) * inv;
    }
    gainStep = (gainTarget - gainCur) * inv;
}

float FormantShifter::process (float x) noexcept
{
    // Pre-emphasis
    const float xp = x - kPreEmphasis * preState;
    preState = x;

    history[size_t (writePos)] = xp;
    writePos = (writePos + 1) % windowSize;
    if (++hopCounter >= hop)
    {
        hopCounter = 0;
        analyse();
    }

    for (int i = 1; i <= order; ++i)
    {
        kaCur[size_t (i)] += kaStep[size_t (i)];
        kbCur[size_t (i)] += kbStep[size_t (i)];
    }
    gainCur += gainStep;

    // Analysis lattice (FIR): f_i = f_{i-1} + k_i g_{i-1}[n-1];  g_i = k_i f_{i-1} + g_{i-1}[n-1]
    float f = xp;
    float gPrev = xp; // g_0[n]
    for (int i = 1; i <= order; ++i)
    {
        const float k = kaCur[size_t (i)];
        const float fNew = f + k * gA[size_t (i - 1)];
        const float gNew = k * f + gA[size_t (i - 1)];
        gA[size_t (i - 1)] = gPrev;
        gPrev = gNew;
        f = fNew;
    }
    gA[size_t (order)] = gPrev;
    const float residual = f * gainCur;

    // Synthesis lattice (all-pole): f_{i-1} = f_i - k_i g_{i-1}[n-1];  g_i = k_i f_{i-1} + g_{i-1}[n-1]
    float fs = residual;
    for (int i = order; i >= 1; --i)
    {
        const float k = kbCur[size_t (i)];
        fs = fs - k * gB[size_t (i - 1)];
        gB[size_t (i)] = sanitize (k * fs + gB[size_t (i - 1)]);
    }
    gB[0] = sanitize (fs);
    const float yp = clamp (fs, -8.0f, 8.0f);

    // De-emphasis
    deState = sanitize (yp + kPreEmphasis * deState);
    return deState;
}
} // namespace msfx::dsp
