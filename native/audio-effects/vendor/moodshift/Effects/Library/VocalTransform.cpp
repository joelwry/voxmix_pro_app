// Category G — Vocal & Instrument Transformations
#include "EffectBase.h"

namespace msfx::library
{
namespace
{
//==============================================================================
/** Pitch-shifted layer (granular/WSOLA) blended with the original.
    Shared by Octave Shadow (down) and Higher Self (up). */
class OctaveLayer : public StereoEffect
{
public:
    OctaveLayer (bool upwards) : up (upwards) {}

    void prepareFx() override
    {
        for (auto& s : shifter) { s.prepare (sr, 120.0f); s.setWindowMs (up ? 40.0f : 75.0f); }
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : hp) f.prepare (sr);
        for (auto& f : air) f.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& s : shifter) s.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : air) f.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float level = p.amount * (up ? 0.9f : 1.2f);
        const float semis = p.get (AdvParam::pitch);
        const float detuneCents = p.get (AdvParam::detune) * clamp (p.width, 0.0f, 2.0f) * 0.5f;
        const float wobbleCents = 8.0f * p.movementDepthScale();
        for (int c = 0; c < 2; ++c)
        {
            lp[size_t (c)].set (Biquad::Type::lowpass, shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf()), 0.7f);
            hp[size_t (c)].set (Biquad::Type::highpass, up ? 350.0f : 40.0f, 0.7f);
            air[size_t (c)].set (Biquad::Type::highShelf, 6000.0f, 0.7f, up ? 3.0f * p.tone + 2.0f : 0.0f);
        }
        clk.begin (ctx, 0.35f, 4.0);
        for (int i = 0; i < n; ++i)
        {
            const float w = std::sin (kTwoPi * float (clk.next()));
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float cents = semis * 100.0f + (c ? detuneCents : -detuneCents) + wobbleCents * (c ? -w : w);
                const float ratio = std::exp2 (cents / 1200.0f);
                const float x = *io[c];
                float layer = shifter[size_t (c)].process (x, ratio);
                layer = air[size_t (c)].process (lp[size_t (c)].process (hp[size_t (c)].process (layer)));
                *io[c] = x + layer * level;
            }
        }
    }

private:
    bool up;
    std::array<GranularPitchShifter, 2> shifter;
    std::array<Biquad, 2> lp, hp, air;
    ModClock clk;
};

class OctaveShadow final : public OctaveLayer { public: OctaveShadow() : OctaveLayer (false) {} };
class HigherSelf final : public OctaveLayer { public: HigherSelf() : OctaveLayer (true) {} };

//==============================================================================
/** Artificial double tracking: two delayed, independently pitch-drifting copies
    placed left and right. */
class GhostDouble final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& d : dl) d.prepare (sr, 0.15f);
        for (auto& s : shifter) { s.prepare (sr, 80.0f); s.setWindowMs (35.0f); }
        for (auto& s : timeS) s.prepare (sr, 0.1f);
        for (auto& t : tilt) t.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& s : shifter) s.reset();
        for (auto& s : timeS) s.reset (0.028f * srf());
        for (auto& t : tilt) t.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float t = p.get (AdvParam::delayTime) * 0.001f * srf();
        timeS[0].setTarget (t);
        timeS[1].setTarget (t * 1.37f);
        const float cents = p.get (AdvParam::detune);
        for (auto& t : tilt) t.setTone (p.tone);
        float pl[2], pr[2];
        panGains (-0.75f * clamp (p.width, 0.0f, 1.33f), pl[0], pr[0]);
        panGains (0.75f * clamp (p.width, 0.0f, 1.33f), pl[1], pr[1]);
        clk.begin (ctx, p.get (AdvParam::rate), 4.0);
        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            const auto cyc = clk.cycleIndex();
            const float x = 0.5f * (L[i] + R[i]);
            float outL = 0.0f, outR = 0.0f;
            for (int v = 0; v < 2; ++v)
            {
                const float drift = shapeValue (LfoShape::smoothRandom, ph + 0.37 * v, cyc + v * 7919);
                const float timing = 1.0f + 0.15f * shapeValue (LfoShape::smoothRandom, ph * 0.5 + 0.11 * v, cyc / 2 + v * 104729);
                dl[size_t (v)].push (x);
                const float delayed = dl[size_t (v)].readLinear (timeS[size_t (v)].next() * timing);
                const float ratio = std::exp2 ((v ? 1.0f : -1.0f) * cents * (0.6f + 0.4f * drift) / 1200.0f);
                const float s = tilt[size_t (v)].process (shifter[size_t (v)].process (delayed, ratio));
                outL += s * pl[v];
                outR += s * pr[v];
            }
            L[i] = L[i] + outL * 0.7f * a;
            R[i] = R[i] + outR * 0.7f * a;
        }
    }

private:
    std::array<DelayLine, 2> dl;
    std::array<GranularPitchShifter, 2> shifter;
    std::array<OnePoleSmoother, 2> timeS;
    std::array<TiltEQ, 2> tilt;
    ModClock clk;
};

//==============================================================================
/** LPC formant shifting (spectral envelope warping) with optional movement. */
class FormantShift final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : shifter) f.prepare (sr);
        for (auto& t : tilt) t.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : shifter) f.reset();
        for (auto& t : tilt) t.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float st = p.get (AdvParam::pitch) * (0.2f + 0.8f * p.amount);
        const float wobble = 1.5f * clamp (p.movement - 0.5f, 0.0f, 0.5f) * 2.0f; // only above 50 %
        const float spread = 1.5f * clamp (p.width - 1.0f, -1.0f, 1.0f);
        for (auto& t : tilt) t.setTone (p.tone);
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        for (int i = 0; i < n; ++i)
        {
            const float m = std::sin (kTwoPi * float (clk.next()));
            shifter[0].setFormantRatio (std::exp2 ((st + wobble * m - spread * 0.5f) / 12.0f));
            shifter[1].setFormantRatio (std::exp2 ((st + wobble * m + spread * 0.5f) / 12.0f));
            L[i] = tilt[0].process (shifter[0].process (L[i]));
            R[i] = tilt[1].process (shifter[1].process (R[i]));
        }
    }

private:
    std::array<FormantShifter, 2> shifter;
    std::array<TiltEQ, 2> tilt;
    ModClock clk;
};

//==============================================================================
/** Ensemble from a single voice: up to eight pitch-shifted, detuned, delayed and
    panned copies with slowly drifting, slightly imperfect timing, plus a mist
    of reverb. An optional interval voice (Pitch) adds harmony. */
class ChoirMist final : public StereoEffect
{
public:
    static constexpr int maxVoices = 8;

    void prepareFx() override
    {
        for (auto& d : dl) d.prepare (sr, 0.12f);
        for (auto& s : shifter) { s.prepare (sr, 80.0f); s.setWindowMs (45.0f); }
        rev.prepare (sr);
        clk.prepare (sr);
        for (auto& t : tilt) t.prepare (sr);
    }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& s : shifter) s.reset();
        rev.reset();
        clk.reset();
        for (auto& t : tilt) t.reset();
    }
    double getTailSeconds() const override { return 3.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        static constexpr float baseDelayMs[maxVoices] = { 12.0f, 27.0f, 19.0f, 35.0f, 23.0f, 41.0f, 15.0f, 31.0f };
        const auto& p = ctx.params;
        const float a = p.amount;
        const int nv = clamp (p.voices(), 2, maxVoices);
        const float detune = p.get (AdvParam::detune);
        const float interval = p.get (AdvParam::pitch);
        const float spread = clamp (p.width, 0.0f, 2.0f) * 0.5f;
        float gl[maxVoices], gr[maxVoices], cents[maxVoices];
        for (int v = 0; v < nv; ++v)
        {
            const float pos = float (v) / float (nv - 1) * 2.0f - 1.0f;      // -1..1
            const float pan = (v % 2 ? pos : -pos) * spread;
            panGains (pan, gl[v], gr[v]);
            cents[v] = pos * detune + ((v % 3 == 2 && std::abs (interval) > 0.01f) ? interval * 100.0f : 0.0f);
        }
        FDNReverb::Params rp;
        rp.decaySeconds = 2.2f;
        rp.size = 1.1f;
        rp.dampingHz = shiftOctaves (7000.0f, p.tone, srf());
        rp.width = p.width;
        rev.setParams (rp);
        for (auto& t : tilt) t.setTone (p.tone);
        const float norm = 1.0f / std::sqrt (float (nv));
        const float mist = 0.15f + 0.35f * a;
        const float imperfect = 3.0f * p.movementDepthScale(); // ms of timing drift
        clk.begin (ctx, 0.25f, 8.0);
        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            const auto cyc = clk.cycleIndex();
            const float x = 0.5f * (L[i] + R[i]);
            float outL = 0.0f, outR = 0.0f;
            for (int v = 0; v < nv; ++v)
            {
                const float r1 = shapeValue (LfoShape::smoothRandom, ph * (1.0 + 0.13 * v) + 0.29 * v, cyc + v * 7919);
                const float d = (baseDelayMs[v] + imperfect * r1) * 0.001f * srf();
                dl[size_t (v)].push (x);
                const float delayed = dl[size_t (v)].readLinear (std::max (2.0f, d));
                const float ratio = std::exp2 ((cents[v] + 4.0f * r1 * a) / 1200.0f);
                const float s = shifter[size_t (v)].process (delayed, ratio);
                outL += s * gl[v];
                outR += s * gr[v];
            }
            outL *= norm;
            outR *= norm;
            float rl, rr;
            rev.process (outL, outR, rl, rr);
            L[i] = tilt[0].process (outL + rl * mist);
            R[i] = tilt[1].process (outR + rr * mist);
        }
    }

private:
    std::array<DelayLine, maxVoices> dl;
    std::array<GranularPitchShifter, maxVoices> shifter;
    FDNReverb rev;
    std::array<TiltEQ, 2> tilt;
    ModClock clk;
};
} // namespace

void addVocalTransform (std::vector<EffectInfo>& r)
{
    const auto cat = Category::vocalTransform;
    r.push_back ({ "octave_shadow", "Octave Shadow", cat,
        "A darker, lower-octave layer under the original (genuine pitch shifting).",
        { "Level of the shadow layer", "Brightness of the layer", "Slow drift of the layer's pitch", "Left/right detune of the layer" },
        { { AdvParam::pitch, "Interval" }, { AdvParam::cutoff, "Layer Tone" }, { AdvParam::detune } },
        MixLaw::linear, 0.0f, &make<OctaveShadow> });

    r.push_back ({ "higher_self", "Higher Self", cat,
        "An airy upper-octave layer above the original (genuine pitch shifting).",
        { "Level of the upper layer", "Air and brightness of the layer", "Slow drift of the layer's pitch", "Left/right detune of the layer" },
        { { AdvParam::pitch, "Interval" }, { AdvParam::cutoff, "Layer Tone" }, { AdvParam::detune } },
        MixLaw::linear, 0.0f, &make<HigherSelf> });

    r.push_back ({ "ghost_double", "Ghost Double", cat,
        "Two loosely timed, pitch-drifting doubles spread left and right.",
        { "Level of the doubles", "Darker or brighter doubles", "How fast the doubles drift", "Stereo spread of the doubles" },
        { { AdvParam::delayTime, "Offset" }, { AdvParam::detune }, { AdvParam::rate, "Drift Rate" } },
        MixLaw::linear, 0.0f, &make<GhostDouble> });

    r.push_back ({ "formant_shift", "Formant Shift", cat,
        "LPC spectral-envelope warping: changes vocal character/size without changing pitch.",
        { "How much of the formant shift is applied", "Darker or brighter tilt", "Above 50 %: formant wobble", "Left/right formant offset (above/below 100 %)" },
        { { AdvParam::pitch, "Formant" }, { AdvParam::rate, "Wobble Rate" }, { AdvParam::sync }, { AdvParam::division } },
        MixLaw::linear, 0.0f, &make<FormantShift> });

    r.push_back ({ "choir_mist", "Choir Mist", cat,
        "A misty ensemble of detuned, delayed and panned voices from a single recording.",
        { "Ensemble density and reverb mist", "Darker or brighter choir", "Timing looseness of the singers", "Stereo placement of the voices" },
        { { AdvParam::voices }, { AdvParam::detune }, { AdvParam::pitch, "Harmony Interval" } },
        MixLaw::linear, 0.0f, &make<ChoirMist> });
}
} // namespace msfx::library
