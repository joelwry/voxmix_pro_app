#include "vox/Analysis.h"
#include "Fft.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace vox
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> monoDecimated (const AudioSource& src, double targetSr, double& outSr)
{
    const int factor = std::max (1, (int) std::floor (src.sampleRate() / targetSr));
    outSr = double (src.sampleRate()) / factor;
    const int64_t outN = src.frames() / factor;
    std::vector<float> out ((size_t) outN, 0.0f);
    const float norm = 1.0f / float (factor * src.channels());
    for (int64_t i = 0; i < outN; ++i)
    {
        float acc = 0.0f;
        for (int c = 0; c < src.channels(); ++c)
            for (int k = 0; k < factor; ++k)
                acc += src.sample (c, i * factor + k);
        out[(size_t) i] = acc * norm;
    }
    return out;
}

std::vector<float> hann (int n)
{
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / n);
    return w;
}

//------------------------------------------------------------------------------------------ tempo
void onsetEnvelopes (const std::vector<float>& x, double sr, std::vector<float>& env, std::vector<float>& low, double& fps)
{
    const int order = 10, size = 1 << order, hop = 256;
    fps = sr / hop;
    Fft fft (order);
    const auto win = hann (size);
    std::vector<float> frame ((size_t) size), mag, prev ((size_t) size / 2 + 1, 0.0f);
    std::vector<std::complex<float>> work;
    const int lowBin = std::max (3, (int) (200.0 * size / sr));
    const int midBin = std::max (lowBin + 2, (int) (2000.0 * size / sr));
    const int hiBin = std::min (size / 2, (int) (8000.0 * size / sr));
    const int numFrames = (int) x.size() >= size ? ((int) x.size() - size) / hop + 1 : 0;
    std::vector<float> band[3];
    for (auto& b : band) b.assign ((size_t) numFrames, 0.0f);
    for (int f = 0; f < numFrames; ++f)
    {
        for (int i = 0; i < size; ++i) frame[(size_t) i] = x[(size_t) f * hop + (size_t) i] * win[(size_t) i];
        fft.magnitudes (frame.data(), mag, work);
        for (int b = 1; b <= hiBin; ++b)
        {
            const float m = std::log1p (240.0f * mag[(size_t) b] / (float) size);
            const float d = m - prev[(size_t) b];
            if (d > 0) band[b < lowBin ? 0 : (b < midBin ? 1 : 2)][(size_t) f] += d;
            prev[(size_t) b] = m;
        }
    }
    env.assign ((size_t) numFrames, 0.0f);
    low.assign ((size_t) numFrames, 0.0f);
    const float w[3] = { 1.3f, 1.0f, 0.5f };
    for (int b = 0; b < 3; ++b)
    {
        std::vector<float> sorted (band[b]);
        std::sort (sorted.begin(), sorted.end());
        const float ref = sorted.empty() ? 1.0f : std::max (1e-9f, sorted[(size_t) ((float) (sorted.size() - 1) * 0.98f)]);
        for (int f = 0; f < numFrames; ++f)
        {
            const float v = std::min (2.0f, band[b][(size_t) f] / ref);
            env[(size_t) f] += w[b] * v;
            if (b == 0) low[(size_t) f] = v;
        }
    }
    auto detrend = [fps] (std::vector<float>& v)
    {
        const int wl = std::max (3, (int) (0.4 * fps));
        std::vector<double> cs (v.size() + 1, 0.0);
        for (size_t i = 0; i < v.size(); ++i) cs[i + 1] = cs[i] + v[i];
        std::vector<float> out (v.size());
        for (int i = 0; i < (int) v.size(); ++i)
        {
            const int a = std::max (0, i - wl), b = std::min ((int) v.size(), i + wl + 1);
            out[(size_t) i] = std::max (0.0f, v[(size_t) i] - (float) ((cs[(size_t) b] - cs[(size_t) a]) / (b - a)));
        }
        v.swap (out);
    };
    detrend (env);
    detrend (low);
}

double interp (const std::vector<float>& v, double idx)
{
    if (idx < 0 || idx >= (double) v.size() - 1) return 0.0;
    const int i = (int) idx;
    const double f = idx - i;
    return v[(size_t) i] * (1 - f) + v[(size_t) i + 1] * f;
}

double combScore (const std::vector<float>& e, double fps, double bpm, double phase)
{
    const double period = fps * 60.0 / bpm;
    double s = 0;
    int n = 0;
    for (double t = phase; t < (double) e.size() - 1; t += period) { s += interp (e, t); ++n; }
    return n > 0 ? s / n : 0.0;
}

void estimateTempo (MusicAnalysis& r, const std::vector<float>& env, double fps, double dur)
{
    const double minBpm = 60.0, maxBpm = 180.0;
    if (env.size() < 64) return;
    const int maxLag = std::min ((int) (fps * 60.0 / (minBpm * 0.5)) + 1, (int) env.size() / 2);
    std::vector<double> ac ((size_t) maxLag + 1, 0.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (size_t i = 0; i + (size_t) lag < env.size(); ++i) s += (double) env[i] * env[i + (size_t) lag];
        ac[(size_t) lag] = s / (double) (env.size() - (size_t) lag);
    }
    auto acAt = [&] (double lag)
    {
        if (lag < 1 || lag >= maxLag) return 0.0;
        const int i = (int) lag;
        const double f = lag - i;
        return ac[(size_t) i] * (1 - f) + ac[(size_t) i + 1] * f;
    };
    std::vector<std::pair<double, double>> scores;
    for (double bpm = minBpm; bpm <= maxBpm; bpm += 0.5)
    {
        const double lag = fps * 60.0 / bpm;
        double s = acAt (lag) + 0.5 * acAt (lag * 2) + 0.25 * acAt (lag * 4) + 0.3 * acAt (lag * 0.5);
        const double oct = std::log2 (bpm / 105.0);
        s *= std::exp (-0.5 * oct * oct / (0.9 * 0.9));
        scores.push_back ({ bpm, s });
    }
    std::vector<std::pair<double, double>> peaks;
    for (size_t i = 1; i + 1 < scores.size(); ++i)
        if (scores[i].second > scores[i - 1].second && scores[i].second >= scores[i + 1].second && scores[i].second > 0)
            peaks.push_back (scores[i]);
    std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.second > b.second; });
    if (peaks.empty()) return;
    double mean = 0;
    for (auto& s : scores) mean += s.second;
    mean /= (double) scores.size();
    const double top = peaks.front().second;
    const double prominence = std::clamp ((top - mean) / (top + 1e-12), 0.0, 1.0);
    const double lengthFactor = std::clamp (dur / 10.0, 0.2, 1.0);

    std::vector<double> cands;
    for (auto& p : peaks)
    {
        bool near = false;
        for (double c : cands) if (std::abs (c - p.first) / c < 0.04) near = true;
        if (! near) cands.push_back (p.first);
        if (cands.size() >= 4) break;
    }
    double bestBpm = cands.front(), bestPhase = 0, bestScore = -1, sum = 0;
    int count = 0;
    for (double bpm = cands.front() * 0.97; bpm <= cands.front() * 1.03; bpm += 0.1)
    {
        const double period = fps * 60.0 / bpm;
        for (double ph = 0; ph < period; ph += 1.0)
        {
            const double s = combScore (env, fps, bpm, ph);
            sum += s;
            ++count;
            if (s > bestScore) { bestScore = s; bestBpm = bpm; bestPhase = ph; }
        }
    }
    r.bpm = std::round (bestBpm * 10.0) / 10.0;
    // whole-number tempos are by far the most common in produced beats
    if (std::abs (r.bpm - std::round (r.bpm)) <= 0.3) r.bpm = std::round (r.bpm);
    r.beatOffsetSec = bestPhase / fps;
    const double meanComb = count ? sum / count : 0.0;
    r.bpmConfidence = (float) std::clamp ((bestScore - meanComb) / (bestScore + 1e-12) * lengthFactor * (0.5 + 0.5 * prominence), 0.0, 1.0);
    for (size_t i = 1; i < cands.size(); ++i) r.bpmAlternatives.push_back (cands[i]);
}

//------------------------------------------------------------------------------------------ key
void estimateKey (MusicAnalysis& r, const std::vector<float>& x, double sr)
{
    const int order = 13, size = 1 << order, hop = size / 4;
    Fft fft (order);
    const auto win = hann (size);
    const int binLo = std::max (1, (int) (80.0 * size / sr)), binHi = std::min (size / 2 - 1, (int) (2500.0 * size / sr));
    std::vector<int> pc ((size_t) size / 2 + 1, -1);
    std::vector<float> wb ((size_t) size / 2 + 1, 0.0f);
    for (int b = binLo; b <= binHi; ++b)
    {
        const double f = b * sr / size;
        const double p = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double rr = std::round (p), dist = std::abs (p - rr);
        if (dist < 0.4)
        {
            pc[(size_t) b] = (((int) rr % 12) + 12) % 12;
            wb[(size_t) b] = (float) std::cos (dist / 0.4 * kPi * 0.5);
        }
    }
    const int numFrames = (int) x.size() >= size ? ((int) x.size() - size) / hop + 1 : 0;
    std::vector<std::array<float, 12>> frames;
    std::vector<float> energy;
    std::vector<float> frame ((size_t) size), mag;
    std::vector<std::complex<float>> work;
    for (int fi = 0; fi < numFrames; ++fi)
    {
        const float* src = x.data() + (size_t) fi * hop;
        float e = 0.0f;
        for (int i = 0; i < size; ++i) { frame[(size_t) i] = src[i] * win[(size_t) i]; e += src[i] * src[i]; }
        fft.magnitudes (frame.data(), mag, work);
        std::array<float, 12> c {};
        for (int b = binLo; b <= binHi; ++b)
            if (pc[(size_t) b] >= 0) c[(size_t) pc[(size_t) b]] += wb[(size_t) b] * std::pow (mag[(size_t) b], 0.8f);
        frames.push_back (c);
        energy.push_back (std::sqrt (e / size));
    }
    if (frames.empty()) return;
    float maxE = 0;
    for (float e : energy) maxE = std::max (maxE, e);
    if (maxE < 1e-5f) return;
    std::array<double, 12> total {};
    for (size_t i = 0; i < frames.size(); ++i)
    {
        if (energy[i] < maxE * 0.05f) continue;
        float s = 0;
        for (float v : frames[i]) s += v;
        if (s <= 0) continue;
        for (int k = 0; k < 12; ++k) total[(size_t) k] += frames[i][(size_t) k] / s;
    }
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto pearson = [&] (const double* prof)
    {
        double ma = 0, mb = 0;
        for (int i = 0; i < 12; ++i) { ma += total[(size_t) i]; mb += prof[i]; }
        ma /= 12; mb /= 12;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < 12; ++i)
        {
            num += (total[(size_t) i] - ma) * (prof[i] - mb);
            da += (total[(size_t) i] - ma) * (total[(size_t) i] - ma);
            db += (prof[i] - mb) * (prof[i] - mb);
        }
        return num / std::sqrt (da * db + 1e-20);
    };
    struct K { int root; bool minor; double score; };
    std::vector<K> keys;
    for (int root = 0; root < 12; ++root)
        for (int m = 0; m < 2; ++m)
        {
            double rot[12];
            for (int i = 0; i < 12; ++i) rot[(i + root) % 12] = (m ? mnr : maj)[i];
            keys.push_back ({ root, m == 1, pearson (rot) });
        }
    std::sort (keys.begin(), keys.end(), [] (const K& a, const K& b) { return a.score > b.score; });
    r.keyRoot = keys[0].root;
    r.keyMinor = keys[0].minor;
    r.altKeyRoot = keys[1].root;
    r.altKeyMinor = keys[1].minor;
    r.keyConfidence = (float) std::clamp ((keys[0].score - keys[1].score) * 4.0 + (keys[0].score - 0.45), 0.0, 0.95);
}
} // namespace

std::string keyName (int root, bool minor)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (root < 0 || root > 11) return "Unknown";
    return std::string (names[root]) + (minor ? " minor" : " major");
}

MusicAnalysis analyseMusic (const AudioSource& src, float* progress)
{
    MusicAnalysis r;
    const double dur = double (src.frames()) / src.sampleRate();
    if (dur < 3.0) return r;
    double sr = 0;
    // analyse at most the first 3 minutes: enough for a stable estimate, bounded time
    auto x = monoDecimated (src, 22050.0, sr);
    if (x.size() > (size_t) (sr * 180)) x.resize ((size_t) (sr * 180));
    if (progress) *progress = 0.1f;
    std::vector<float> env, low;
    double fps = 0;
    onsetEnvelopes (x, sr, env, low, fps);
    if (progress) *progress = 0.4f;
    estimateTempo (r, env, fps, std::min (dur, 180.0));
    if (progress) *progress = 0.6f;
    estimateKey (r, x, sr);
    if (progress) *progress = 1.0f;
    r.valid = r.bpm > 0 || r.keyRoot >= 0;
    return r;
}

//------------------------------------------------------------------------------------------ noise reduction
std::vector<std::vector<float>> reduceNoise (const std::vector<std::vector<float>>& planar, double sr, float reductionDb, float* progress)
{
    std::vector<std::vector<float>> out = planar;
    if (planar.empty() || planar[0].empty()) return out;
    const int order = sr > 64000 ? 12 : 11;   // ~43 ms frames
    const int size = 1 << order, hop = size / 4, bins = size / 2 + 1;
    const int64_t n = (int64_t) planar[0].size();
    if (n < size * 2) return out;
    Fft fft (order);
    const auto win = hann (size);
    const float floorGain = std::pow (10.0f, -std::clamp (reductionDb, 0.0f, 40.0f) / 20.0f);
    const int numFrames = (int) ((n + hop - 1) / hop) + 1;

    std::vector<std::complex<float>> work ((size_t) size);
    auto framePower = [&] (int f, std::vector<float>& pw)
    {
        pw.assign ((size_t) bins, 0.0f);
        const int64_t start = (int64_t) f * hop - size / 2;
        for (const auto& ch : planar)
        {
            for (int i = 0; i < size; ++i)
            {
                const int64_t idx = start + i;
                work[(size_t) i] = { idx >= 0 && idx < n ? ch[(size_t) idx] * win[(size_t) i] : 0.0f, 0.0f };
            }
            fft.transform (work);
            for (int b = 0; b < bins; ++b) pw[(size_t) b] += std::norm (work[(size_t) b]);
        }
    };

    // pass 1: frame energies (time domain, cheap)
    std::vector<double> energy ((size_t) numFrames, 0.0);
    for (int f = 0; f < numFrames; ++f)
    {
        const int64_t start = (int64_t) f * hop - size / 2;
        double e = 0;
        for (const auto& ch : planar)
            for (int i = 0; i < size; ++i)
            {
                const int64_t idx = start + i;
                if (idx >= 0 && idx < n) { const double v = ch[(size_t) idx] * win[(size_t) i]; e += v * v; }
            }
        energy[(size_t) f] = e;
    }

    // noise profile: per-bin median power of the quietest 15 % of frames (pauses, breaths, room
    // tone); frames that are digital silence (e.g. before the take starts) are skipped
    std::vector<int> order_;
    for (int f = 0; f < numFrames; ++f) if (energy[(size_t) f] > 1e-12) order_.push_back (f);
    if ((int) order_.size() < 8) return out;
    std::sort (order_.begin(), order_.end(), [&] (int a, int b) { return energy[(size_t) a] < energy[(size_t) b]; });
    const int quiet = std::max (8, (int) order_.size() * 15 / 100);
    std::vector<std::vector<float>> quietPow ((size_t) quiet);
    for (int k = 0; k < quiet; ++k) framePower (order_[(size_t) k], quietPow[(size_t) k]);
    if (progress) *progress = 0.2f;
    std::vector<float> noise ((size_t) bins, 0.0f), tmp ((size_t) quiet);
    for (int b = 0; b < bins; ++b)
    {
        for (int k = 0; k < quiet; ++k) tmp[(size_t) k] = quietPow[(size_t) k][(size_t) b];
        std::nth_element (tmp.begin(), tmp.begin() + quiet / 2, tmp.end());
        noise[(size_t) b] = tmp[(size_t) quiet / 2];
    }
    quietPow.clear();
    // smooth the profile across frequency
    std::vector<float> ns ((size_t) bins);
    for (int b = 0; b < bins; ++b)
    {
        float s = 0;
        int c = 0;
        for (int k = std::max (0, b - 2); k <= std::min (bins - 1, b + 2); ++k) { s += noise[(size_t) k]; ++c; }
        ns[(size_t) b] = s / c;
    }

    // pass 2: gains (Wiener-style with over-subtraction), smoothed in time (fast attack, slow
    // release) and frequency, then overlap-add resynthesis
    const float over = 3.0f;   // over-subtraction factor (in power)
    std::vector<float> gain ((size_t) bins, 1.0f), raw ((size_t) bins), smoothF ((size_t) bins), psm ((size_t) bins, 0.0f);
    const float release = 0.6f;
    for (auto& ch : out) std::fill (ch.begin(), ch.end(), 0.0f);
    std::vector<double> norm ((size_t) n, 0.0);
    std::vector<float> pw;
    for (int f = 0; f < numFrames; ++f)
    {
        framePower (f, pw);
        for (int b = 0; b < bins; ++b)
        {
            // recursive smoothing of the power estimate reduces random noise peaks ("musical noise")
            psm[(size_t) b] = f == 0 ? pw[(size_t) b] : 0.5f * psm[(size_t) b] + 0.5f * pw[(size_t) b];
            const float p = std::max (psm[(size_t) b], pw[(size_t) b] * 0.5f);
            const float snr = p > 0 ? std::max (0.0f, p - over * ns[(size_t) b]) / p : 0.0f;
            raw[(size_t) b] = std::max (floorGain, std::sqrt (snr));
        }
        for (int b = 0; b < bins; ++b)
        {
            float s = 0;
            int c = 0;
            for (int k = std::max (0, b - 1); k <= std::min (bins - 1, b + 1); ++k) { s += raw[(size_t) k]; ++c; }
            smoothF[(size_t) b] = s / c;
        }
        for (int b = 0; b < bins; ++b)
        {
            const float t = smoothF[(size_t) b];
            gain[(size_t) b] = t > gain[(size_t) b] ? t : t + release * (gain[(size_t) b] - t);
        }
        const int64_t start = (int64_t) f * hop - size / 2;
        for (size_t c = 0; c < planar.size(); ++c)
        {
            for (int i = 0; i < size; ++i)
            {
                const int64_t idx = start + i;
                work[(size_t) i] = { idx >= 0 && idx < n ? planar[c][(size_t) idx] * win[(size_t) i] : 0.0f, 0.0f };
            }
            fft.transform (work);
            for (int b = 0; b < bins; ++b)
            {
                work[(size_t) b] *= gain[(size_t) b];
                if (b > 0 && b < size / 2) work[(size_t) (size - b)] = std::conj (work[(size_t) b]);
            }
            fft.transform (work, true);
            for (int i = 0; i < size; ++i)
            {
                const int64_t idx = start + i;
                if (idx < 0 || idx >= n) continue;
                out[c][(size_t) idx] += work[(size_t) i].real() / size * win[(size_t) i];
                if (c == 0) norm[(size_t) idx] += (double) win[(size_t) i] * win[(size_t) i];
            }
        }
        if (progress && (f & 63) == 0) *progress = 0.2f + 0.8f * f / numFrames;
    }
    for (auto& ch : out)
        for (int64_t i = 0; i < n; ++i)
            ch[(size_t) i] = norm[(size_t) i] > 1e-6 ? (float) (ch[(size_t) i] / norm[(size_t) i]) : 0.0f;
    if (progress) *progress = 1.0f;
    return out;
}

} // namespace vox
