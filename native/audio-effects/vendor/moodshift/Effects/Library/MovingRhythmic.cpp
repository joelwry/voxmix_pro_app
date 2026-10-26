// Category B — Moving & Rhythmic
#include "EffectBase.h"
#include "../Filters/FormantBank.h"

namespace msfx::library
{
namespace
{
//==============================================================================
/** Classic swept resonant band-pass wah, free running or tempo synced. */
class WahVocal final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : bp) { f.prepare (sr); f.setMode (SVF::Mode::bandpassNormalized); }
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : bp) f.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float centre = p.get (AdvParam::cutoff) * std::exp2 (1.2f * p.tone);
        const float rangeOct = 0.6f + 1.6f * a;
        const float q = 1.5f + res01 (ctx) * 10.0f * (0.5f + 0.5f * a);
        const float body = 0.25f * (1.0f - a);
        const float stereoOff = 0.25f * p.width;
        const auto shape = p.lfoShape();
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());

        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            const auto cyc = clk.cycleIndex();
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float v = shapeValue (shape, ph + (c ? stereoOff : 0.0f), cyc);
                bp[size_t (c)].setParams (shiftOctaves (centre, rangeOct * v, srf()), q);
                *io[c] = bp[size_t (c)].process (*io[c]) * 1.4f + *io[c] * body;
            }
        }
    }

private:
    std::array<SVF, 2> bp;
    ModClock clk;
};

//==============================================================================
/** Envelope-driven filter: louder input opens the filter further (dynamic "quack"). */
class AutoWah final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : svf) { f.prepare (sr); f.setMode (SVF::Mode::bandpassNormalized); }
        for (auto& f : lp) { f.prepare (sr); f.setMode (SVF::Mode::lowpass); }
        env.prepare (sr);
        cutS.prepare (sr, 0.002f);
    }
    void reset() override
    {
        for (auto& f : svf) f.reset();
        for (auto& f : lp) f.reset();
        env.reset();
        cutS.reset (0.0f);
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        // Movement speeds up (or slows down) the envelope response.
        const float speed = p.movementRateScale();
        env.setTimes (p.get (AdvParam::attack) / speed, p.get (AdvParam::release) / speed);
        const float base = p.get (AdvParam::cutoff) * std::exp2 (p.tone);
        const float rangeOct = 1.5f + 3.0f * a;
        const float q = 1.2f + res01 (ctx) * 9.0f;
        const float sens = 30.0f + 25.0f * a; // dB of range mapped to full sweep
        const float stereoScale = 1.0f - 0.12f * p.width;

        for (int i = 0; i < n; ++i)
        {
            const float e = env.process (0.5f * (L[i] + R[i]));
            const float t = clamp ((gainToDb (e) + 50.0f) / sens, 0.0f, 1.0f);
            cutS.setTarget (t);
            const float tt = cutS.next();
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float f = shiftOctaves (base, rangeOct * tt * (c ? stereoScale : 1.0f), srf());
                svf[size_t (c)].setParams (f, q);
                lp[size_t (c)].setParams (f * 1.2f, 0.9f);
                const float x = *io[c];
                *io[c] = 1.1f * svf[size_t (c)].process (x) + 0.35f * lp[size_t (c)].process (x);
            }
        }
    }

private:
    std::array<SVF, 2> svf, lp;
    EnvelopeFollower env;
    OnePoleSmoother cutS;
};

//==============================================================================
/** Talkbox-like vowel motion: harmonically enriched signal through a morphing
    A-E-I-O-U formant filter bank. */
class TalkboxMotion final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& b : bank) b.prepare (sr);
        for (auto& h : hp) h.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& b : bank) b.reset();
        for (auto& h : hp) h.reset();
        for (auto& s : sat) s.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float scale = std::exp2 (0.45f * p.tone);
        const float sharp = 0.6f + 0.9f * a + res01 (ctx) * 0.8f;
        const float keep = 0.25f * (1.0f - a);
        const auto shape = p.lfoShape();
        for (int c = 0; c < 2; ++c)
        {
            sat[size_t (c)].set (Saturator::Type::tube, p.get (AdvParam::drive));
            hp[size_t (c)].setCutoff (150.0f);
        }
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());

        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            const auto cyc = clk.cycleIndex();
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                // vowel position 0..4, right channel offset by Width
                const float v = 2.0f + 2.0f * shapeValue (shape, ph + (c ? 0.15f * p.width : 0.0f), cyc);
                bank[size_t (c)].setVowel (v, scale, sharp);
                const float x = *io[c];
                const float rich = sat[size_t (c)].process (hp[size_t (c)].highpass (x));
                *io[c] = bank[size_t (c)].process (rich) * 2.2f + x * keep;
            }
        }
    }

private:
    std::array<FormantBank, 2> bank;
    std::array<Saturator, 2> sat;
    std::array<OnePole, 2> hp;
    ModClock clk;
};

//==============================================================================
/** Eight-stage stereo phaser with feedback; output is the classic notch sum. */
class LiquidPhaser final : public StereoEffect
{
public:
    void prepareFx() override { ph.prepare (sr); clk.prepare (sr); }
    void reset() override { ph.reset(); clk.reset(); }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float centre = p.get (AdvParam::cutoff) * std::exp2 (p.tone);
        ph.setParams (8, 0.0f, 0.35f + 0.65f * a, clamp (p.fb01() * (0.4f + 0.6f * a), 0.0f, 0.93f),
                      centre / 5.0f, centre * 5.0f, 0.25f * p.width);
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            ph.processAt (l, r, float (clk.next()));
            L[i] = 0.6f * (L[i] + l);
            R[i] = 0.6f * (R[i] + r);
        }
    }

private:
    Phaser ph;
    ModClock clk;
};

//==============================================================================
/** Tempo-synced amplitude gate / tremolo that also pulses a low-pass filter. */
class RhythmPulse final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : lp) { f.prepare (sr); f.setMode (SVF::Mode::lowpass); }
        for (auto& g : gainS) g.prepare (sr, 0.0015f);
        for (auto& t : tilt) t.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : lp) f.reset();
        for (auto& g : gainS) g.reset (1.0f);
        for (auto& t : tilt) t.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float depth = p.amount;
        const float closed = p.get (AdvParam::cutoff);
        const auto shape = p.lfoShape();
        const float off = 0.5f * clamp (p.width - 1.0f, 0.0f, 1.0f); // >100 % width: alternating L/R pulses
        for (auto& t : tilt) t.setTone (p.tone);
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());

        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            const auto cyc = clk.cycleIndex();
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float v = 0.5f + 0.5f * shapeValue (shape, ph + (c ? off : 0.0f), cyc); // 0..1
                gainS[size_t (c)].setTarget (1.0f - depth * (1.0f - v));
                const float g = gainS[size_t (c)].next();
                const float cut = std::exp (lerp (std::log (closed), std::log (18000.0f), g));
                lp[size_t (c)].setParams (std::min (cut, 0.45f * srf()), 0.8f);
                *io[c] = tilt[size_t (c)].process (lp[size_t (c)].process (*io[c]) * g);
            }
            if (p.width < 1.0f) applyWidth (L[i], R[i], p.width);
        }
    }

private:
    std::array<SVF, 2> lp;
    std::array<OnePoleSmoother, 2> gainS;
    std::array<TiltEQ, 2> tilt;
    ModClock clk;
};
} // namespace

void addMovingRhythmic (std::vector<EffectInfo>& r)
{
    const auto cat = Category::movingRhythmic;
    r.push_back ({ "wah_vocal", "Wah Vocal", cat,
        "Sweeping resonant band-pass wah, free running or locked to tempo.",
        { "Sweep range and resonance", "Centre of the sweep", "Wah speed (tempo steps when synced)", "Left/right sweep offset" },
        { { AdvParam::rate }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::cutoff, "Centre" },
          { AdvParam::resonance }, { AdvParam::shape } },
        MixLaw::linear, 0.0f, &make<WahVocal> });

    r.push_back ({ "auto_wah", "Auto-Wah", cat,
        "Envelope-following filter that opens as you play or sing harder.",
        { "Sensitivity and sweep range", "Resting filter position", "Envelope speed (faster or lazier response)", "Slight left/right range difference" },
        { { AdvParam::attack }, { AdvParam::release }, { AdvParam::cutoff, "Base Cutoff" }, { AdvParam::resonance } },
        MixLaw::linear, 0.0f, &make<AutoWah> });

    r.push_back ({ "talkbox_motion", "Talkbox Motion", cat,
        "Formant-inspired vowel filter morphing through A-E-I-O-U (not a physical talkbox model).",
        { "Vowel focus and resonance", "Formant size (smaller/larger mouth)", "Vowel movement speed", "Left/right vowel offset" },
        { { AdvParam::rate, "Vowel Rate" }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::resonance },
          { AdvParam::drive, "Buzz" }, { AdvParam::shape } },
        MixLaw::linear, 0.0f, &make<TalkboxMotion> });

    r.push_back ({ "liquid_phaser", "Liquid Phaser", cat,
        "Eight-stage stereo phaser with resonant feedback.",
        { "Sweep depth and feedback intensity", "Centre of the sweep", "Phasing speed", "Stereo phase offset" },
        { { AdvParam::rate }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::feedback }, { AdvParam::cutoff, "Centre" } },
        MixLaw::linear, 0.0f, &make<LiquidPhaser> });

    r.push_back ({ "rhythm_pulse", "Rhythm Pulse", cat,
        "Tempo-locked volume and filter pulses with selectable divisions and waveforms.",
        { "Pulse depth", "Overall darker / brighter tilt", "Rhythmic activity (halves or doubles the rate)", "Above 100 %: alternating left/right pulses" },
        { { AdvParam::division }, { AdvParam::shape }, { AdvParam::cutoff, "Closed Cutoff" }, { AdvParam::sync }, { AdvParam::rate } },
        MixLaw::linear, 0.0f, &make<RhythmPulse> });
}
} // namespace msfx::library
