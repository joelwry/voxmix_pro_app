// Category D — Glitch & Experimental
#include "EffectBase.h"

namespace msfx::library
{
namespace
{
//==============================================================================
/** Grid-based buffer glitches. Each step of the grid deterministically (hash of
    the step's musical position) chooses pass-through or a glitch action:
    repeat, short repeat, reversed slice, varispeed up/down repeat or gated
    stutter. When synced, the same bar always glitches the same way. */
class GlitchMachine final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& s : rep) s.prepare (sr, 4.0f);
        for (auto& t : tilt) t.prepare (sr);
        clk.prepare (sr);
        panS[0].prepare (sr, 0.01f);
        panS[1].prepare (sr, 0.01f);
    }
    void reset() override
    {
        for (auto& s : rep) s.reset();
        for (auto& t : tilt) t.reset();
        clk.reset();
        lastStep = -1;
        panS[0].reset (1.0f);
        panS[1].reset (1.0f);
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float prob = clamp (p.get (AdvParam::density) * 0.01f * (0.3f + 0.7f * p.amount), 0.0f, 1.0f);
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        const double stepSec = p.sync() ? ctx.divisionSeconds() / std::exp2 (std::round (std::log2 (double (p.movementRateScale()))))
                                        : 1.0 / std::max (0.05, double (p.get (AdvParam::rate) * p.movementRateScale()));
        const int stepLen = int (clamp (stepSec, 0.01, 2.0) * sr);
        for (auto& t : tilt) t.setTone (p.tone);

        for (int i = 0; i < n; ++i)
        {
            clk.next();
            const int64_t step = clk.cycleIndex();
            if (step != lastStep)
            {
                lastStep = step;
                const uint32_t h = uint32_t (step) * 2654435761u + 0x51ED27u;
                if (hashToUnit (h) < prob)
                {
                    const int action = int (hashToUnit (h ^ 0xA5A5A5u) * 6.0f);
                    int slice = stepLen / 2;
                    bool reverse = false;
                    float speed = 1.0f, gate = 1.0f;
                    switch (action)
                    {
                        case 0: slice = stepLen / 2; break;
                        case 1: slice = stepLen / 4; break;
                        case 2: slice = stepLen / 2; reverse = true; break;
                        case 3: slice = stepLen / 2; speed = 2.0f; break;
                        case 4: slice = stepLen / 4; speed = 0.5f; break;
                        default: slice = stepLen / 8; gate = 0.5f; break;
                    }
                    for (auto& s : rep) s.engage (slice, reverse, speed, gate);
                    const float pan = (hashToUnit (h ^ 0x1234567u) * 2.0f - 1.0f) * 0.8f * clamp (p.width, 0.0f, 1.5f);
                    float gl, gr;
                    panGains (pan, gl, gr);
                    panS[0].setTarget (gl);
                    panS[1].setTarget (gr);
                }
                else
                {
                    for (auto& s : rep) s.release();
                    panS[0].setTarget (1.0f);
                    panS[1].setTarget (1.0f);
                }
            }
            L[i] = tilt[0].process (rep[0].process (L[i]) * panS[0].next());
            R[i] = tilt[1].process (rep[1].process (R[i]) * panS[1].next());
        }
    }

private:
    std::array<SliceRepeater, 2> rep;
    std::array<TiltEQ, 2> tilt;
    std::array<OnePoleSmoother, 2> panS;
    ModClock clk;
    int64_t lastStep = -1;
};

//==============================================================================
/** Ring modulation into a tuned metallic comb resonance. */
class RobotVoice final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& r : ring) r.prepare (sr);
        for (auto& d : comb) d.prepare (sr, 0.05f);
        for (auto& f : bp) f.prepare (sr);
        for (auto& f : dc) f.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& r : ring) r.reset();
        for (auto& d : comb) d.reset();
        for (auto& f : bp) f.reset();
        for (auto& f : dc) f.reset();
        clk.reset();
    }
    double getTailSeconds() const override { return 0.3; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float carrier = p.get (AdvParam::freq);
        const float fbk = clamp (res01 (ctx) * 0.9f * (0.5f + 0.5f * a), 0.0f, 0.92f);
        const float vib = 0.04f * p.movementDepthScale();
        for (int c = 0; c < 2; ++c)
            bp[size_t (c)].set (Biquad::Type::peak, shiftOctaves (1500.0f, p.tone, srf()), 0.8f, 5.0f * p.tone);
        clk.begin (ctx, 5.5f, 1.0);
        for (int i = 0; i < n; ++i)
        {
            const float v = 1.0f + vib * std::sin (kTwoPi * float (clk.next()));
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float detune = c ? 1.0f + 0.025f * p.width : 1.0f;
                const float f = carrier * v * detune;
                const float x = *io[c];
                const float rm = ring[size_t (c)].process (x, f, 0.35f * a);
                // comb tuned to twice the carrier for a metallic, pitched resonance
                const float period = clamp (srf() / (2.0f * f), 8.0f, comb[size_t (c)].getMaxDelaySamples() - 4.0f);
                const float y = rm + fbk * comb[size_t (c)].readLinear (period);
                comb[size_t (c)].push (fastTanh (y));
                const float wet = dc[size_t (c)].process (bp[size_t (c)].process (y * (1.0f - 0.5f * fbk)));
                *io[c] = wet * (0.4f + 0.6f * a) + x * 0.35f * (1.0f - a);
            }
        }
    }

private:
    std::array<RingModulator, 2> ring;
    std::array<DelayLine, 2> comb;
    std::array<Biquad, 2> bp;
    std::array<DCBlocker, 2> dc;
    ModClock clk;
};

//==============================================================================
/** Single-sideband frequency shifting (Hilbert based) with a drifting shift,
    resonant band-pass, optional drive and barber-pole feedback. */
class AlienTransmission final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : fs) f.prepare (sr);
        for (auto& f : bp) { f.prepare (sr); f.setMode (SVF::Mode::bandpassNormalized); }
        for (auto& f : dc) f.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : fs) f.reset();
        for (auto& f : bp) f.reset();
        for (auto& s : sat) s.reset();
        for (auto& f : dc) f.reset();
        fb = { 0.0f, 0.0f };
        clk.reset();
    }
    double getTailSeconds() const override { return 0.5; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float shift = p.get (AdvParam::freq) * (0.25f + 0.75f * a);
        const float fbk = clamp (p.fb01() * 0.75f, 0.0f, 0.7f);
        const float q = 0.8f + res01 (ctx) * 9.0f;
        const float centre = shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf());
        for (int c = 0; c < 2; ++c)
        {
            bp[size_t (c)].setParams (centre * (c ? 1.0f + 0.1f * p.width : 1.0f), q);
            sat[size_t (c)].set (Saturator::Type::fold, p.get (AdvParam::drive) * a);
        }
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        for (int i = 0; i < n; ++i)
        {
            const float m = std::sin (kTwoPi * float (clk.next()));
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float spread = c ? 1.0f + 0.35f * p.width : 1.0f - 0.35f * p.width;
                const float s = shift * spread * (1.0f + 0.5f * m);
                const float x = *io[c];
                const float y = fs[size_t (c)].process (x + fb[size_t (c)] * fbk, s);
                fb[size_t (c)] = fastTanh (y);
                const float res = bp[size_t (c)].process (y);
                *io[c] = dc[size_t (c)].process (sat[size_t (c)].process (0.7f * y + 1.2f * res));
            }
        }
    }

private:
    std::array<FrequencyShifter, 2> fs;
    std::array<SVF, 2> bp;
    std::array<Saturator, 2> sat;
    std::array<DCBlocker, 2> dc;
    std::array<float, 2> fb {};
    ModClock clk;
};

//==============================================================================
/** Bit-depth and sample-rate reduction with modulated rate and optional wavefolding. */
class DigitalShatter final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& b : crush) b.prepare (sr);
        for (auto& f : dc) f.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& b : crush) b.reset();
        for (auto& s : sat) s.reset();
        for (auto& f : dc) f.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float bits = 16.0f - 13.0f * a;
        const float rate = p.get (AdvParam::cutoff) * std::exp2 (p.tone);
        const float modOct = 0.8f * p.movementDepthScale();
        for (int c = 0; c < 2; ++c) sat[size_t (c)].set (Saturator::Type::fold, p.get (AdvParam::drive));
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        const auto shape = p.lfoShape();
        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            const float m = shapeValue (shape, ph, clk.cycleIndex());
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float r = rate * std::exp2 (modOct * m) * (c ? 1.0f + 0.12f * p.width : 1.0f);
                crush[size_t (c)].set (bits - (c ? 0.5f * p.width : 0.0f), r);
                *io[c] = dc[size_t (c)].process (crush[size_t (c)].process (sat[size_t (c)].process (*io[c])));
            }
        }
    }

private:
    std::array<BitCrusher, 2> crush;
    std::array<Saturator, 2> sat;
    std::array<DCBlocker, 2> dc;
    ModClock clk;
};

//==============================================================================
/** Detuned short-wave radio: band-limited, saturated, fading in and out, with
    a slight SSB mistuning and static/crackle that follows the signal level. */
class BrokenRadio final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : hp) f.prepare (sr);
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : fs) f.prepare (sr);
        for (auto& g : noise) g.prepare (sr);
        env.prepare (sr);
        env.setTimes (5.0f, 400.0f);
        fadeS.prepare (sr, 0.05f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : fs) f.reset();
        for (auto& s : sat) s.reset();
        noise[0].reset (0xBEEFu);
        noise[1].reset (0xCAFEu);
        env.reset();
        fadeS.reset (1.0f);
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float centre = shiftOctaves (1100.0f, p.tone, srf());
        const float q = 0.9f + res01 (ctx) * 2.5f;
        for (int c = 0; c < 2; ++c)
        {
            hp[size_t (c)].set (Biquad::Type::highpass, centre / 2.6f, q);
            lp[size_t (c)].set (Biquad::Type::lowpass, centre * 2.8f, q);
            sat[size_t (c)].set (Saturator::Type::tube, p.get (AdvParam::drive) * (0.5f + 0.5f * a));
        }
        const float noiseAmt = 0.06f * a;
        const float crackles = p.get (AdvParam::density) * 0.6f;
        const float detuneHz = 4.0f + 14.0f * a;
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            const float wander = shapeValue (LfoShape::smoothRandom, ph, clk.cycleIndex());
            fadeS.setTarget (1.0f - 0.6f * a * (0.5f + 0.5f * wander));
            const float fade = fadeS.next();
            const float level = std::min (1.0f, env.process (0.5f * (L[i] + R[i])) * 30.0f); // noise follows signal
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float tuning = detuneHz * (0.6f + 0.4f * wander) * (c ? 1.0f + 0.2f * p.width : 1.0f);
                float y = fs[size_t (c)].process (*io[c], tuning);
                y = sat[size_t (c)].process (lp[size_t (c)].process (hp[size_t (c)].process (y)));
                const float stat = (noise[size_t (c)].pink() * noiseAmt + noise[size_t (c)].crackle (crackles) * 0.25f * a) * level;
                *io[c] = y * fade + stat;
            }
        }
    }

private:
    std::array<Biquad, 2> hp, lp;
    std::array<FrequencyShifter, 2> fs;
    std::array<Saturator, 2> sat;
    std::array<NoiseGenerator, 2> noise;
    EnvelopeFollower env;
    OnePoleSmoother fadeS;
    ModClock clk;
};
} // namespace

void addGlitchExperimental (std::vector<EffectInfo>& r)
{
    const auto cat = Category::glitchExperimental;
    r.push_back ({ "glitch_machine", "Glitch Machine", cat,
        "Tempo-grid buffer repeats, reversed slices and varispeed stutters - repeatable per bar.",
        { "Glitch probability", "Darker or brighter", "Grid speed (halves or doubles the step)", "Random left/right placement of glitches" },
        { { AdvParam::density, "Probability" }, { AdvParam::division, "Grid" }, { AdvParam::sync }, { AdvParam::rate, "Step Rate" } },
        MixLaw::linear, 0.0f, &make<GlitchMachine> });

    r.push_back ({ "robot_voice", "Robot Voice", cat,
        "Ring modulator into a tuned metallic resonance.",
        { "Robotic intensity and resonance", "Metallic formant tilt", "Carrier vibrato", "Left/right carrier detune" },
        { { AdvParam::freq, "Carrier" }, { AdvParam::resonance, "Metal" } },
        MixLaw::linear, 0.0f, &make<RobotVoice> });

    r.push_back ({ "alien_transmission", "Alien Transmission", cat,
        "True frequency shifting with drifting offset, resonant filter and barber-pole feedback.",
        { "Amount of frequency shift and fold distortion", "Resonant band position", "Drift speed of the shift", "Opposite left/right shift spread" },
        { { AdvParam::freq, "Shift" }, { AdvParam::rate, "Drift Rate" }, { AdvParam::sync }, { AdvParam::division },
          { AdvParam::feedback }, { AdvParam::cutoff, "Band" }, { AdvParam::resonance }, { AdvParam::drive } },
        MixLaw::linear, 0.0f, &make<AlienTransmission> });

    r.push_back ({ "digital_shatter", "Digital Shatter", cat,
        "Bit-depth and sample-rate destruction with modulated aliasing.",
        { "Bit reduction (16 down to 3 bits)", "Sample-rate offset", "Amount of sample-rate modulation", "Left/right rate and depth offset" },
        { { AdvParam::cutoff, "Sample Rate" }, { AdvParam::drive, "Fold" }, { AdvParam::rate, "Mod Rate" }, { AdvParam::sync },
          { AdvParam::division }, { AdvParam::shape } },
        MixLaw::linear, 0.0f, &make<DigitalShatter> });

    r.push_back ({ "broken_radio", "Broken Radio", cat,
        "Detuned, fading, band-limited radio with static and crackle that follow the signal.",
        { "Degradation: fading, detune, static and grit", "Band position", "Fading speed", "Decorrelated left/right reception" },
        { { AdvParam::drive }, { AdvParam::density, "Crackle" }, { AdvParam::rate, "Fade Rate" }, { AdvParam::resonance },
          { AdvParam::sync }, { AdvParam::division } },
        MixLaw::linear, 0.0f, &make<BrokenRadio> });
}
} // namespace msfx::library
