#pragma once
#include "vox/AudioSource.h"
#include "vox/WavWriter.h"
#include <cmath>
#include <cstdlib>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

namespace th
{
constexpr double kPi = 3.14159265358979323846;

inline std::string tempDir (const std::string& name)
{
    const char* base = std::getenv ("VOXMIX_TEST_DIR");
    std::string d = std::string (base ? base : "/tmp") + "/voxmix-test-" + name + "-" + std::to_string (::getpid());
    std::string cmd = "rm -rf '" + d + "' && mkdir -p '" + d + "'";
    if (std::system (cmd.c_str()) != 0) return "/tmp";
    return d;
}

inline std::vector<float> sine (double freq, double sr, int n, float amp = 0.5f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2.0 * kPi * freq * i / sr);
    return v;
}

inline std::vector<float> noise (int n, float amp = 0.25f, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> v ((size_t) n);
    for (auto& x : v) x = amp * d (rng);
    return v;
}

inline double rms (const float* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

inline double rmsDb (const std::vector<float>& x, int from = 0)
{
    const double r = rms (x.data() + from, (int) x.size() - from);
    return r > 0 ? 20.0 * std::log10 (r) : -200.0;
}

inline float peak (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x) p = std::max (p, std::abs (v));
    return p;
}

/** Fundamental frequency estimate by normalised autocorrelation (for pitch tests). */
inline double estimatePitch (const std::vector<float>& x, int from, int len, double sr, double minHz = 60, double maxHz = 1200)
{
    const int tMin = (int) (sr / maxHz), tMax = (int) (sr / minHz);
    double best = -1, bestTau = 0;
    std::vector<double> c ((size_t) tMax + 2, 0.0);
    for (int tau = tMin; tau <= tMax + 1; ++tau)
    {
        double num = 0, e1 = 0, e2 = 0;
        for (int i = 0; i < len; ++i)
        {
            const double a = x[(size_t) (from + i)], b = x[(size_t) (from + i + tau)];
            num += a * b; e1 += a * a; e2 += b * b;
        }
        c[(size_t) tau] = num / std::sqrt (e1 * e2 + 1e-20);
    }
    for (int tau = tMin + 1; tau <= tMax; ++tau)
        if (c[(size_t) tau] > best && c[(size_t) tau] >= c[(size_t) tau - 1] && c[(size_t) tau] >= c[(size_t) tau + 1])
        {
            best = c[(size_t) tau];
            bestTau = tau;
            if (best > 0.9) break;   // first strong peak = fundamental
        }
    if (bestTau <= 0) return 0.0;
    const int t = (int) bestTau;
    const double a = c[(size_t) t - 1], b = c[(size_t) t], d = c[(size_t) t + 1];
    const double den = a - 2 * b + d;
    const double refined = std::abs (den) > 1e-12 ? t + 0.5 * (a - d) / den : t;
    return sr / refined;
}

inline bool writeWav (const std::string& path, const std::vector<float>& interleaved, int channels, int sr, int bits = 16)
{
    vox::WavWriter w;
    std::string e;
    if (! w.open (path, sr, channels, bits, e)) return false;
    w.write (interleaved.data(), (int64_t) interleaved.size() / channels);
    return w.close();
}

/** Click track: 1 ms bursts of a 2 kHz tone every `period` frames, first at `first`. */
inline std::vector<float> clicks (int n, int first, int period, float amp = 0.8f, double sr = 48000)
{
    std::vector<float> v ((size_t) n, 0.0f);
    const int len = (int) (0.001 * sr);
    for (int s = first; s < n; s += period)
        for (int i = 0; i < len && s + i < n; ++i)
            v[(size_t) (s + i)] = amp * (float) std::sin (2.0 * kPi * 2000.0 * i / sr);
    return v;
}

/** Index of the first sample whose magnitude exceeds thr, starting at from. */
inline int onset (const std::vector<float>& x, int from, float thr)
{
    for (int i = std::max (0, from); i < (int) x.size(); ++i)
        if (std::abs (x[(size_t) i]) > thr) return i;
    return -1;
}
} // namespace th
