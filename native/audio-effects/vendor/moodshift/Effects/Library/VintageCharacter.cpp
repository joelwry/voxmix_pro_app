// Category E — Vintage & Character
#include "EffectBase.h"

namespace msfx::library
{
namespace
{
//==============================================================================
/** Tape machine character: asymmetric saturation, wow, flutter and slow pitch
    drift (modulated delay), head bump, HF loss and a filtered tape echo. */
class TapeMelt final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& d : wowLine) d.prepare (sr, 0.05f);
        for (auto& d : echo) d.prepare (sr, 2.2f);
        for (auto& f : hf) f.prepare (sr);
        for (auto& f : bump) f.prepare (sr);
        for (auto& f : echoLp) f.prepare (sr);
        for (auto& s : timeS) s.prepare (sr, 0.3f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& d : wowLine) d.reset();
        for (auto& d : echo) d.reset();
        for (auto& f : hf) f.reset();
        for (auto& f : bump) f.reset();
        for (auto& f : echoLp) f.reset();
        for (auto& s : sat) s.reset();
        for (auto& s : timeS) s.reset (0.32f * srf());
        clk.reset();
        flutterPhase = 0.0;
    }
    double getTailSeconds() const override { return 3.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        for (int c = 0; c < 2; ++c)
        {
            sat[size_t (c)].set (Saturator::Type::tape, p.get (AdvParam::drive) * (0.5f + 0.5f * a), 0.15f);
            hf[size_t (c)].set (Biquad::Type::lowpass, shiftOctaves (12000.0f, p.tone - a, srf()), 0.6f);
            bump[size_t (c)].set (Biquad::Type::peak, 90.0f, 1.0f, 2.0f);
            echoLp[size_t (c)].setCutoff (shiftOctaves (2800.0f, p.tone, srf()));
            timeS[size_t (c)].setTarget (clamp (delayTimeSamples (ctx), 32.0f, echo[0].getMaxDelaySamples() - 64.0f));
        }
        const float wowDepth = (0.4f + 3.6f * a) * 0.001f * srf();
        const float flutterDepth = 0.12f * a * 0.001f * srf();
        const float driftDepth = 1.0f * a * 0.001f * srf();
        const float fbk = clamp (p.fb01(), 0.0f, 0.85f);
        const double flutterInc = 7.0 / sr;
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            flutterPhase += flutterInc;
            if (flutterPhase >= 1.0) flutterPhase -= 1.0;
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const double off = c ? 0.17 * double (p.width) : 0.0;
                const float wow = std::sin (kTwoPi * float (ph + off));
                const float drift = shapeValue (LfoShape::smoothRandom, ph * 0.25 + off, clk.cycleIndex() / 4 + c * 977);
                const float flut = std::sin (kTwoPi * float (flutterPhase + off * 3.0));
                const float d = 2.0f + wowDepth * (1.0f + wow) + driftDepth * (1.0f + drift) + flutterDepth * (1.0f + flut);
                auto& wl = wowLine[size_t (c)];
                wl.push (bump[size_t (c)].process (sat[size_t (c)].process (*io[c])));
                const float tape = hf[size_t (c)].process (wl.readHermite (d));
                auto& e = echo[size_t (c)];
                const float rep = e.readLinear (timeS[size_t (c)].next());
                e.push (tape + echoLp[size_t (c)].lowpass (fastTanh (rep * fbk)));
                *io[c] = tape + rep * 0.35f * (0.5f + 0.5f * a);
            }
        }
    }

private:
    std::array<DelayLine, 2> wowLine, echo;
    std::array<Biquad, 2> hf, bump;
    std::array<OnePole, 2> echoLp;
    std::array<Saturator, 2> sat;
    std::array<OnePoleSmoother, 2> timeS;
    ModClock clk;
    double flutterPhase = 0.0;
};

//==============================================================================
/** Warm record playback: band-limiting, low-mid warmth, gentle saturation,
    turntable wow and synthesised crackle that only plays while there is signal. */
class VinylSoul final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : hp) f.prepare (sr);
        for (auto& f : warm) f.prepare (sr);
        for (auto& d : wow) d.prepare (sr, 0.02f);
        for (auto& g : noise) g.prepare (sr);
        env.prepare (sr);
        env.setTimes (10.0f, 800.0f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : warm) f.reset();
        for (auto& d : wow) d.reset();
        for (auto& s : sat) s.reset();
        noise[0].reset (0x1111u);
        noise[1].reset (0x2222u);
        env.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        for (int c = 0; c < 2; ++c)
        {
            lp[size_t (c)].set (Biquad::Type::lowpass, shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf()), 0.75f);
            hp[size_t (c)].set (Biquad::Type::highpass, 40.0f, 0.7f);
            warm[size_t (c)].set (Biquad::Type::peak, 220.0f, 0.8f, 1.0f + 2.5f * a);
            sat[size_t (c)].set (Saturator::Type::soft, p.get (AdvParam::drive) * (0.3f + 0.7f * a));
        }
        const float crackleLevel = 0.03f + 0.12f * a;
        const float density = p.get (AdvParam::density) * 0.8f;
        const float wowDepth = 0.25f * p.movementDepthScale() * 0.001f * srf();
        clk.begin (ctx, 0.55f, 4.0); // 33 1/3 rpm
        for (int i = 0; i < n; ++i)
        {
            const float w = std::sin (kTwoPi * float (clk.next()));
            const float level = std::min (1.0f, env.process (0.5f * (L[i] + R[i])) * 25.0f);
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                auto& d = wow[size_t (c)];
                d.push (*io[c]);
                float y = d.readLinear (2.0f + wowDepth * (1.0f + w));
                y = sat[size_t (c)].process (warm[size_t (c)].process (hp[size_t (c)].process (lp[size_t (c)].process (y))));
                *io[c] = y + noise[size_t (c)].crackle (density) * crackleLevel * level;
            }
            applyWidth (L[i], R[i], 0.8f * p.width);
        }
    }

private:
    std::array<Biquad, 2> lp, hp, warm;
    std::array<DelayLine, 2> wow;
    std::array<Saturator, 2> sat;
    std::array<NoiseGenerator, 2> noise;
    EnvelopeFollower env;
    ModClock clk;
};

//==============================================================================
/** Bucket-brigade style multi-voice chorus: darkened, slightly saturated voices. */
class RetroChorus final : public StereoEffect
{
public:
    void prepareFx() override
    {
        chorus.prepare (sr);
        for (auto& f : lp) f.prepare (sr);
    }
    void reset() override
    {
        chorus.reset();
        for (auto& f : lp) f.reset();
        for (auto& s : sat) s.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const int voices = clamp (p.voices(), 2, 8);
        chorus.setParams (voices, p.get (AdvParam::rate) * p.movementRateScale(), 1.0f + 6.0f * a, 7.0f, clamp (0.5f * p.width, 0.0f, 1.0f));
        for (int c = 0; c < 2; ++c)
        {
            lp[size_t (c)].setCutoff (shiftOctaves (9000.0f, p.tone, srf()));
            sat[size_t (c)].set (Saturator::Type::soft, 4.0f);
        }
        for (int i = 0; i < n; ++i)
        {
            float l, r;
            chorus.process (L[i], R[i], l, r);
            L[i] = sat[0].process (lp[0].lowpass (l));
            R[i] = sat[1].process (lp[1].lowpass (r));
            if (p.width > 1.0f) applyWidth (L[i], R[i], p.width);
        }
    }

private:
    Chorus chorus;
    std::array<OnePole, 2> lp;
    std::array<Saturator, 2> sat;
};

//==============================================================================
/** Vintage sampler grit: reduced bandwidth, 12-bit-style quantisation,
    lower sample rate, subtle saturation and a jittery sample clock. */
class DustySample final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : post) f.prepare (sr);
        for (auto& b : crush) b.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : lp) f.reset();
        for (auto& f : post) f.reset();
        for (auto& b : crush) b.reset();
        for (auto& s : sat) s.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float cut = shiftOctaves (p.get (AdvParam::cutoff), p.tone - 0.8f * a, srf());
        const float bits = 14.0f - 6.0f * a;
        const float rate = std::min (srf(), lerp (26040.0f, 11025.0f, a));
        for (int c = 0; c < 2; ++c)
        {
            lp[size_t (c)].set (Biquad::Type::lowpass, cut, 0.9f);
            post[size_t (c)].set (Biquad::Type::lowpass, std::min (cut * 1.2f, 0.45f * rate), 0.7f);
            sat[size_t (c)].set (Saturator::Type::soft, p.get (AdvParam::drive));
        }
        const float jitter = 0.06f * p.movementDepthScale();
        clk.begin (ctx, 3.0f, 1.0);
        for (int i = 0; i < n; ++i)
        {
            const float j = std::sin (kTwoPi * float (clk.next()));
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                crush[size_t (c)].set (bits, rate * (1.0f + jitter * j));
                const float y = crush[size_t (c)].process (sat[size_t (c)].process (lp[size_t (c)].process (*io[c])));
                *io[c] = post[size_t (c)].process (y);
            }
            applyWidth (L[i], R[i], 0.6f * p.width);
        }
    }

private:
    std::array<Biquad, 2> lp, post;
    std::array<BitCrusher, 2> crush;
    std::array<Saturator, 2> sat;
    ModClock clk;
};

//==============================================================================
/** Bullhorn: tight midrange band, honky horn resonance and controlled distortion. */
class Megaphone final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : hp) f.prepare (sr);
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : pk) f.prepare (sr);
        for (auto& d : horn) d.prepare (sr, 0.01f);
        slap.prepare (sr, 0.1f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : pk) f.reset();
        for (auto& d : horn) d.reset();
        for (auto& s : sat) s.reset();
        slap.reset();
        clk.reset();
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float centre = shiftOctaves (1700.0f, p.tone, srf());
        const float narrow = 2.6f - 1.0f * a;
        for (int c = 0; c < 2; ++c)
        {
            hp[size_t (c)].set (Biquad::Type::highpass, centre / narrow, 1.1f);
            lp[size_t (c)].set (Biquad::Type::lowpass, centre * narrow, 1.1f);
            pk[size_t (c)].set (Biquad::Type::peak, centre, 1.2f, 4.0f + 4.0f * a);
            sat[size_t (c)].set (Saturator::Type::tube, p.get (AdvParam::drive) * (0.4f + 0.8f * a));
        }
        const float hornMs = 0.9f;
        const float wobble = 0.15f * p.movementDepthScale();
        const float slapAmt = 0.25f * clamp (p.width, 0.0f, 2.0f);
        clk.begin (ctx, 4.0f, 1.0);
        for (int i = 0; i < n; ++i)
        {
            const float m = 1.0f + wobble * std::sin (kTwoPi * float (clk.next()));
            float* io[2] = { L + i, R + i };
            float mono = 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                float y = sat[size_t (c)].process (pk[size_t (c)].process (hp[size_t (c)].process (*io[c])));
                y = lp[size_t (c)].process (y);
                auto& h = horn[size_t (c)];
                const float res = h.readLinear (hornMs * m * 0.001f * srf());
                h.push (y + res * 0.35f);
                *io[c] = y + res * 0.3f;
                mono += 0.5f * *io[c];
            }
            slap.push (mono);
            const float s = slap.readLinear (0.032f * srf()) * slapAmt;
            L[i] += s * 0.6f;
            R[i] += s;
        }
    }

private:
    std::array<Biquad, 2> hp, lp, pk;
    std::array<Saturator, 2> sat;
    std::array<DelayLine, 2> horn;
    DelayLine slap;
    ModClock clk;
};
} // namespace

void addVintageCharacter (std::vector<EffectInfo>& r)
{
    const auto cat = Category::vintageCharacter;
    r.push_back ({ "tape_melt", "Tape Melt", cat,
        "Warm tape saturation, wow, flutter and pitch drift with a filtered tape echo.",
        { "Wow depth, drift and saturation", "Tape brightness", "Wow speed", "Left/right wow decorrelation" },
        { { AdvParam::drive }, { AdvParam::rate, "Wow Rate" }, { AdvParam::sync }, { AdvParam::division },
          { AdvParam::delayTime, "Echo Time" }, { AdvParam::feedback, "Echo Feedback" } },
        MixLaw::linear, 0.0f, &make<TapeMelt> });

    r.push_back ({ "vinyl_soul", "Vinyl Soul", cat,
        "Warm, band-limited record playback with gentle wow and signal-following crackle.",
        { "Warmth and crackle level", "Record brightness", "Turntable wow depth", "Stereo width (vinyl is slightly narrower)" },
        { { AdvParam::cutoff }, { AdvParam::drive }, { AdvParam::density, "Crackle" } },
        MixLaw::linear, 0.0f, &make<VinylSoul> });

    r.push_back ({ "retro_chorus", "Retro Chorus", cat,
        "Lush bucket-brigade style multi-voice chorus.",
        { "Chorus depth", "Darker or brighter voices", "Chorus speed", "Voice spread" },
        { { AdvParam::rate }, { AdvParam::voices } },
        MixLaw::linear, 0.0f, &make<RetroChorus> });

    r.push_back ({ "dusty_sample", "Dusty Sample", cat,
        "Old sampler grit: reduced bandwidth, bit depth and sample rate.",
        { "Grit: fewer bits, lower sample rate, darker", "Brightness", "Sample-clock jitter", "Stereo width (narrow like an old sample)" },
        { { AdvParam::cutoff }, { AdvParam::drive } },
        MixLaw::linear, 0.0f, &make<DustySample> });

    r.push_back ({ "megaphone", "Megaphone", cat,
        "Honky midrange bullhorn with controlled distortion.",
        { "Intensity: narrower band and more distortion", "Horn pitch (band position)", "Horn wobble", "Short slap reflection on the right" },
        { { AdvParam::drive } },
        MixLaw::linear, 0.0f, &make<Megaphone> });
}
} // namespace msfx::library
