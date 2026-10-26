// Pitch correction ("Auto-Tune" style). This is real correction, not a fixed pitch shift:
//   1. the voice is analysed with the YIN estimator (algorithm ported from MELOVAB STUDIO's
//      Analysis.cpp, de Cheveigne & Kawahara 2002) on a 2x decimated signal, every 5.3 ms;
//   2. the detected pitch is snapped to the nearest note of the selected key/scale
//      (with hysteresis so sustained notes between two scale notes do not flip);
//   3. the correction ratio is smoothed by the Retune Speed and scaled by Strength;
//   4. the voice is re-pitched by MoodShift FX's WSOLA granular pitch shifter, whose ratio can
//      change every sample. Unvoiced sounds (no confident pitch) glide back to ratio 1.
// Latency: half a grain (15 ms at the 30 ms grain); the dry path is delayed by the same amount.
// While correcting, WSOLA grain alignment can move the voice by up to ~6 ms around that nominal
// latency; when no correction is needed the effect crossfades to the delayed dry signal and is
// exactly a 15 ms delay (which the mixer compensates). Formants move with the pitch (no formant preservation yet), which
// is inaudible for typical corrections of a semitone or less.

#include "ParamHelpers.h"
#include "voxfx/EffectRegistry.h"
#include "../vendor/moodshift/Effects/Pitch/PitchShifter.h"
#include "../vendor/vocalsweet/Svf.h"

namespace voxfx
{
using namespace detail;

namespace
{
const char* const kKeyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

struct ScaleDef { const char* name; int mask; };   // bit n = semitone n above the key is allowed
constexpr ScaleDef kScales[] = {
    { "Chromatic",        0xFFF },
    { "Major",            (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11) },
    { "Minor",            (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10) },
    { "Harmonic Minor",   (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11) },
    { "Major Pentatonic", (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9) },
    { "Minor Pentatonic", (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10) },
    { "Blues",            (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10) },
    { "Dorian",           (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10) },
};

const EffectDescriptor& pitchDesc()
{
    static const EffectDescriptor d = []
    {
        EffectDescriptor e;
        e.typeId = "pitch_correction";
        e.name = "Pitch Correction";
        e.category = EffectCategory::Pitch;
        e.origin = "VoxMix (YIN from MELOVAB STUDIO, granular shifter from MoodShift FX)";
        e.description = "Detects the sung pitch and pulls it to the nearest note of the chosen key and scale.";
        std::vector<std::string> keys (std::begin (kKeyNames), std::end (kKeyNames));
        std::vector<std::string> scales;
        for (const auto& s : kScales) scales.emplace_back (s.name);
        e.params = { choice ("key", "Key", keys, 0),
                     choice ("scale", "Scale", scales, 1),
                     cont ("speed", "Retune Speed", 0, 400, 25, "ms"),
                     cont ("strength", "Strength", 0, 100, 100, "%"),
                     cont ("reference", "Reference A4", 430, 450, 440, "Hz"),
                     cont ("humanize", "Humanize", 0, 100, 0, "%"),
                     cont ("mix", "Mix", 0, 100, 100, "%") };
        return e;
    }();
    return d;
}

/** YIN fundamental estimator on a contiguous buffer. Returns Hz, or 0 when unvoiced. */
double yin (const float* x, int window, int tauMin, int tauMax, double sr, float threshold, float& conf, float* d)
{
    for (int tau = 0; tau <= tauMax; ++tau)
    {
        // 8 independent accumulators: lets the compiler vectorise without -ffast-math
        float acc[8] {};
        const float* y = x + tau;
        int i = 0;
        for (; i + 8 <= window; i += 8)
            for (int k = 0; k < 8; ++k)
            {
                const float diff = x[i + k] - y[i + k];
                acc[k] += diff * diff;
            }
        float sum = (acc[0] + acc[1]) + (acc[2] + acc[3]) + (acc[4] + acc[5]) + (acc[6] + acc[7]);
        for (; i < window; ++i)
        {
            const float diff = x[i] - y[i];
            sum += diff * diff;
        }
        d[tau] = sum;
    }
    float running = 0.0f;
    d[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[tau];
        d[tau] = running > 1e-12f ? d[tau] * (float) tau / running : 1.0f;
    }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (d[tau] < threshold)
        {
            while (tau + 1 < tauMax && d[tau + 1] < d[tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) { conf = 0.0f; return 0.0; }
    double refined = best;
    if (best > 1 && best < tauMax)
    {
        const double a = d[best - 1], b = d[best], c = d[best + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-9) refined = best + 0.5 * (a - c) / den;
    }
    conf = std::clamp (1.0f - d[best], 0.0f, 1.0f);
    return sr / refined;
}

class PitchCorrectionEffect final : public Effect
{
public:
    PitchCorrectionEffect() : Effect (pitchDesc()) {}

    static constexpr float kGrainMs = 30.0f;
    static constexpr double kMinHz = 70.0, kMaxHz = 1100.0;

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        anaRate = sr / 2.0;
        window = std::max (64, int (std::round (0.0107 * anaRate)));        // ~10.7 ms
        tauMax = int (std::ceil (anaRate / kMinHz));
        tauMin = std::max (2, int (std::floor (anaRate / kMaxHz)));
        hop = std::max (32, int (std::round (0.0053 * anaRate)));            // ~5.3 ms
        anaSize = window + tauMax + 1;
        ana.assign ((size_t) anaSize * 2, 0.0f);
        diff.assign ((size_t) tauMax + 2, 0.0f);
        shifter.prepare (sr, 60.0f);
        shifter.setWindowMs (kGrainMs);
        latency = int (std::round (kGrainMs * 0.5f * 0.001f * float (sr)));
        dryDelay.assign ((size_t) std::max (1, latency), 0.0f);   // read-before-write: size == delay
        decim = vs::dsp::designSvf (vs::dsp::SvfType::LowPass, sr, std::min (5000.0, 0.2 * sr), 0.707);
        engageStep = 1.0f / float (0.015 * sr);
        engageHoldFrames = int (0.15 * sr);
        reset();
    }

    void reset() override
    {
        shifter.reset();
        std::fill (ana.begin(), ana.end(), 0.0f);
        std::fill (dryDelay.begin(), dryDelay.end(), 0.0f);
        anaPos = 0; anaCount = 0; hopCount = 0; dryPos = 0; decimPhase = false;
        lpState.reset();
        currentNote = -1000.0; ratioLog = 0.0f; targetLog = 0.0f;
        engage = 0.0f; engageHold = 0;
        detectedHz.store (0.0f);
    }

    void process (float* L, float* R, int n, const ProcessInfo&) noexcept override
    {
        const int key = int (p (0));
        const int mask = kScales[std::clamp (int (p (1)), 0, int (std::size (kScales)) - 1)].mask;
        const float speedMs = p (2);
        const float strength = p (3) * 0.01f;
        const double ref = p (4);
        const float humanize = p (5) * 0.01f;
        const float mix = p (6) * 0.01f;
        // one-pole smoothing of the log ratio; 0 ms = instant ("hard tune")
        const float coeff = speedMs < 0.5f ? 0.0f : std::exp (-1.0f / (speedMs * 0.001f * float (sampleRate)));

        for (int i = 0; i < n; ++i)
        {
            const float l = sanitizeSample (L[i]), r = sanitizeSample (R[i]);
            const float mono = 0.5f * (l + r);

            // analysis path: low-pass + 2x decimation into a linearised ring (double-written)
            const float lp = lpState.process (decim, mono);
            decimPhase = ! decimPhase;
            if (decimPhase)
            {
                ana[(size_t) anaPos] = lp;
                ana[(size_t) (anaPos + anaSize)] = lp;
                anaPos = (anaPos + 1) % anaSize;
                anaCount = std::min (anaCount + 1, anaSize);
                if (++hopCount >= hop && anaCount >= anaSize)
                {
                    hopCount = 0;
                    analyse (key, mask, ref, strength, humanize);
                }
            }

            ratioLog = targetLog + coeff * (ratioLog - targetLog);
            const float ratio = std::exp2 (ratioLog / 12.0f);
            const float shifted = shifter.process (mono, ratio);

            // When no correction is needed the shifter is crossfaded out (15 ms) and the
            // latency-matched dry signal is used, so the effect is transparent (a pure delay).
            if (std::abs (targetLog) > 0.005f || std::abs (ratioLog) > 0.005f) engageHold = engageHoldFrames;
            else if (engageHold > 0) --engageHold;
            const float engageTarget = engageHold > 0 ? 1.0f : 0.0f;
            engage += engageTarget > engage ? std::min (engageStep, engageTarget - engage)
                                            : -std::min (engageStep, engage - engageTarget);

            const float dry = dryDelay[(size_t) dryPos];
            dryDelay[(size_t) dryPos] = mono;
            dryPos = (dryPos + 1) % (int) dryDelay.size();

            // Pitch correction is monophonic: it works on the mono sum (vocal layers are mono
            // recordings duplicated to both channels, so nothing is lost there).
            const float wet = dry + engage * (shifted - dry);
            const float out = dry + mix * (wet - dry);
            L[i] = out;
            R[i] = out;
        }
    }

    int latencySamples() const noexcept override { return latency; }

    /** Last detected pitch in Hz (0 = unvoiced) for the tuner display. */
    float gainReductionDb() const noexcept override { return detectedHz.load (std::memory_order_relaxed); }

private:
    void analyse (int key, int mask, double ref, float strength, float humanize) noexcept
    {
        const float* frame = ana.data() + anaPos;   // oldest sample of the linearised window
        float conf = 0.0f;
        const double hz = yin (frame, window, tauMin, tauMax, anaRate, 0.15f, conf, diff.data());
        if (hz <= 0.0 || conf < 0.6f)
        {
            detectedHz.store (0.0f, std::memory_order_relaxed);
            targetLog = 0.0f;           // unvoiced: no correction
            currentNote = -1000.0;
            return;
        }
        detectedHz.store ((float) hz, std::memory_order_relaxed);
        const double midi = 69.0 + 12.0 * std::log2 (hz / ref);

        // nearest allowed note, with hysteresis around the current note
        double bestNote = midi, bestDist = 1e9;
        const int base = int (std::floor (midi)) - 7;
        for (int m = base; m <= base + 14; ++m)
        {
            const int pc = ((m - key) % 12 + 12) % 12;
            if (! (mask & (1 << pc))) continue;
            double dist = std::abs (midi - m);
            if (std::abs (m - currentNote) < 0.01) dist -= 0.15;   // stick to the current note
            if (dist < bestDist) { bestDist = dist; bestNote = m; }
        }
        currentNote = bestNote;
        float correction = float (bestNote - midi);                 // semitones
        // humanize: leave small deviations on sustained notes partially uncorrected
        if (humanize > 0.0f && std::abs (correction) < 0.25f)
            correction *= 1.0f - humanize;
        targetLog = std::clamp (correction * strength, -6.0f, 6.0f);
    }

    double sampleRate = 48000.0, anaRate = 24000.0;
    int window = 256, tauMin = 20, tauMax = 343, hop = 128, anaSize = 600;
    std::vector<float> ana, diff, dryDelay;
    int anaPos = 0, anaCount = 0, hopCount = 0, dryPos = 0, latency = 0;
    bool decimPhase = false;
    vs::dsp::SvfCoeffs decim;
    vs::dsp::SvfState lpState;
    msfx::dsp::GranularPitchShifter shifter;
    double currentNote = -1000.0;
    float ratioLog = 0.0f, targetLog = 0.0f;
    float engage = 0.0f, engageStep = 0.001f;
    int engageHold = 0, engageHoldFrames = 7200;
    std::atomic<float> detectedHz { 0.0f };
};
} // namespace

void registerPitchCorrection (std::vector<EffectType>& out)
{
    out.push_back ({ &pitchDesc(), [] { return std::make_shared<PitchCorrectionEffect>(); } });
}

} // namespace voxfx
