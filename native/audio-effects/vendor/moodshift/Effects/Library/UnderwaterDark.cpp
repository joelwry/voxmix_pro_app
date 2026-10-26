// Category A — Underwater & Dark
#include "EffectBase.h"

namespace msfx::library
{
namespace
{
//==============================================================================
/** Resonant low-pass with liquid cutoff modulation, plus a short modulated
    delay that adds a "bubbly", slightly detuned wobble. Amount = submerged depth. */
class Underwater final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : lp2) f.prepare (sr);
        for (auto& d : dl) d.prepare (sr, 0.12f);
        for (auto& s : timeS) s.prepare (sr, 0.05f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : lp) { f.reset(); f.setMode (SVF::Mode::lowpass); }
        for (auto& f : lp2) f.reset();
        for (auto& d : dl) d.reset();
        for (auto& s : timeS) s.reset (0.018f * srf());
        fb = { 0.0f, 0.0f };
        clk.reset();
    }
    double getTailSeconds() const override { return 0.6; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float baseCut = p.get (AdvParam::cutoff) * std::exp2 (1.2f - 2.6f * a + 1.5f * p.tone);
        const float q = 0.7f + res01 (ctx) * 7.0f * (0.5f + 0.5f * a);
        const float depthOct = 0.25f + 0.85f * a;
        const float baseDelay = p.get (AdvParam::delayTime) * 0.001f * srf();
        const float modDelay = (0.6f + 2.4f * a) * 0.001f * srf();
        const float feedback = p.fb01() * 0.8f;
        const float stereoOff = 0.25f * p.width;
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        for (auto& s : timeS) s.setTarget (baseDelay);
        lp2[0].setCutoff (baseCut * 2.5f);
        lp2[1].setCutoff (baseCut * 2.5f);

        for (int i = 0; i < n; ++i)
        {
            const float ph = float (clk.next());
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float phc = ph + (c == 1 ? stereoOff : 0.0f);
                const float m = std::sin (kTwoPi * phc);
                const float m2 = std::sin (kTwoPi * (3.0f * phc + 0.3f));
                lp[size_t (c)].setParams (shiftOctaves (baseCut, depthOct * m, srf()), q);
                const float y = lp2[size_t (c)].lowpass (lp[size_t (c)].process (*io[c]));
                auto& d = dl[size_t (c)];
                d.push (y + fastTanh (fb[size_t (c)]) * feedback);
                const float wob = d.readLinear (timeS[size_t (c)].next() + modDelay * (0.5f + 0.5f * m2));
                fb[size_t (c)] = wob;
                *io[c] = 0.65f * y + 0.6f * wob;
            }
            applyWidth (L[i], R[i], p.width);
        }
    }

private:
    std::array<SVF, 2> lp;
    std::array<OnePole, 2> lp2;
    std::array<DelayLine, 2> dl;
    std::array<OnePoleSmoother, 2> timeS;
    std::array<float, 2> fb {};
    ModClock clk;
};

//==============================================================================
/** Steep, slowly moving low-pass feeding a large, dark, low-frequency-weighted FDN. */
class DeepSea final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : lpA) { f.prepare (sr); f.setMode (SVF::Mode::lowpass); }
        for (auto& f : lpB) { f.prepare (sr); f.setMode (SVF::Mode::lowpass); }
        rev.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : lpA) f.reset();
        for (auto& f : lpB) f.reset();
        rev.reset();
        clk.reset();
    }
    double getTailSeconds() const override { return double (rev.getParams().decaySeconds) * 1.2; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float baseCut = p.get (AdvParam::cutoff) * std::exp2 (1.0f - 2.0f * a + 1.2f * p.tone);
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay) * (0.6f + 0.8f * a);
        rp.size = 1.6f;
        rp.dampingHz = clamp (baseCut * 2.0f, 200.0f, 6000.0f);
        rp.lowCutHz = 25.0f;
        rp.diffusion = p.get (AdvParam::diffusion) * 0.01f;
        rp.modDepth = 0.5f;
        rp.modRate = 0.25f * p.movementRateScale();
        rp.preDelayMs = 25.0f;
        rp.width = p.width;
        rev.setParams (rp);
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        const float dryAmt = 1.0f - 0.45f * a, revAmt = 0.45f + 0.65f * a;

        for (int i = 0; i < n; ++i)
        {
            const float m = std::sin (kTwoPi * float (clk.next()));
            const float cut = shiftOctaves (baseCut, 0.5f * m, srf());
            float x[2] = { L[i], R[i] };
            for (int c = 0; c < 2; ++c)
            {
                lpA[size_t (c)].setParams (cut, 0.9f);
                lpB[size_t (c)].setParams (cut, 0.6f);
                x[c] = lpB[size_t (c)].process (lpA[size_t (c)].process (x[c]));
            }
            float rl, rr;
            rev.process (x[0], x[1], rl, rr);
            L[i] = x[0] * dryAmt + rl * revAmt;
            R[i] = x[1] * dryAmt + rr * revAmt;
        }
    }

private:
    std::array<SVF, 2> lpA, lpB;
    FDNReverb rev;
    ModClock clk;
};

//==============================================================================
/** Narrow telephone band with gentle saturation and faint, distant reflections. */
class TelephoneGhost final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : hp) f.prepare (sr);
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : pk) f.prepare (sr);
        for (auto& f : reflLp) f.prepare (sr);
        refl.prepare (sr, 0.6f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : pk) f.reset();
        for (auto& f : reflLp) { f.reset(); }
        for (auto& s : sat) s.reset();
        refl.reset();
        clk.reset();
        fbState = 0.0f;
        counter = 0;
    }
    double getTailSeconds() const override { return 1.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        const float spread = 2.7f - 1.3f * a;
        const float driveDb = p.get (AdvParam::drive) * (0.4f + 0.6f * a);
        auto updateBand = [&] (float flutter)
        {
            const float centre = shiftOctaves (p.get (AdvParam::cutoff), p.tone + flutter, srf());
            for (int c = 0; c < 2; ++c)
            {
                hp[size_t (c)].set (Biquad::Type::highpass, centre / spread, 0.9f);
                lp[size_t (c)].set (Biquad::Type::lowpass, centre * spread, 0.9f);
                pk[size_t (c)].set (Biquad::Type::peak, centre, 1.4f, 3.0f + 4.0f * a);
                sat[size_t (c)].set (Saturator::Type::soft, driveDb);
                reflLp[size_t (c)].setCutoff (centre * 1.3f);
            }
        };
        const float t = p.get (AdvParam::delayTime) * 0.001f * srf();
        const float taps[3] = { t, t * 1.618f, t * 2.414f };
        const float tg[3] = { 0.42f, 0.28f, 0.18f };
        const float reflAmt = 0.35f + 0.65f * a;
        const float fbk = p.fb01() * 0.6f;
        float pl[3], pr[3];
        for (int k = 0; k < 3; ++k)
        {
            const float pan = (k == 1 ? 0.0f : (k == 0 ? -0.7f : 0.7f)) * clamp (p.width, 0.0f, 1.5f);
            panGains (pan, pl[k], pr[k]);
        }

        for (int i = 0; i < n; ++i)
        {
            const float flutter = std::sin (kTwoPi * float (clk.next())) * 0.12f;
            if ((counter++ & 31) == 0) updateBand (flutter); // coefficient updates on a fixed grid
            float x[2] = { L[i], R[i] };
            for (int c = 0; c < 2; ++c)
                x[c] = sat[size_t (c)].process (pk[size_t (c)].process (lp[size_t (c)].process (hp[size_t (c)].process (x[c]))));
            const float mono = 0.5f * (x[0] + x[1]);
            refl.push (mono + fbState * fbk);
            float rl = 0.0f, rr = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float s = refl.readLinear (taps[k]) * tg[k];
                rl += s * pl[k];
                rr += s * pr[k];
            }
            fbState = fastTanh (refl.readLinear (taps[2]));
            rl = reflLp[0].lowpass (rl);
            rr = reflLp[1].lowpass (rr);
            L[i] = x[0] + rl * reflAmt;
            R[i] = x[1] + rr * reflAmt;
        }
    }

private:
    std::array<Biquad, 2> hp, lp, pk;
    std::array<Saturator, 2> sat;
    std::array<OnePole, 2> reflLp;
    DelayLine refl;
    ModClock clk;
    float fbState = 0.0f;
    uint32_t counter = 0;
};

//==============================================================================
/** Dark cross-fed feedback delay inside a reverberant tunnel. Amount = distance. */
class DarkTunnel final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& d : dl) d.prepare (sr, 2.2f);
        for (auto& f : loopLp) f.prepare (sr);
        for (auto& f : loopHp) f.prepare (sr);
        for (auto& f : inLp) f.prepare (sr);
        for (auto& s : timeS) s.prepare (sr, 0.25f);
        rev.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& f : loopLp) f.reset();
        for (auto& f : loopHp) f.reset();
        for (auto& f : inLp) f.reset();
        for (auto& s : timeS) s.reset (0.22f * srf());
        fb = { 0.0f, 0.0f };
        rev.reset();
        clk.reset();
    }
    double getTailSeconds() const override { return 6.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float time = clamp (delayTimeSamples (ctx), 16.0f, dl[0].getMaxDelaySamples() - 64.0f);
        for (auto& s : timeS) s.setTarget (time);
        const float fbk = clamp (p.fb01(), 0.0f, 0.92f);
        const float cut = shiftOctaves (p.get (AdvParam::cutoff), 1.5f * p.tone - a, srf());
        for (int c = 0; c < 2; ++c)
        {
            loopLp[size_t (c)].setCutoff (cut);
            loopHp[size_t (c)].setCutoff (90.0f);
            inLp[size_t (c)].setCutoff (cut * 1.5f);
        }
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay) * (0.5f + a);
        rp.size = 1.2f;
        rp.dampingHz = clamp (cut * 1.2f, 300.0f, 9000.0f);
        rp.diffusion = 0.75f;
        rp.preDelayMs = 10.0f + 50.0f * a;
        rp.width = p.width;
        rp.modDepth = 0.3f;
        rev.setParams (rp);
        const float direct = 1.0f - 0.7f * a, send = 0.25f + 0.75f * a;
        const float cross = 0.25f * clamp (p.width, 0.0f, 2.0f);
        clk.begin (ctx, p.get (AdvParam::rate), 4.0);

        for (int i = 0; i < n; ++i)
        {
            const float wow = 1.0f + 0.004f * std::sin (kTwoPi * float (clk.next()));
            float x[2] = { inLp[0].lowpass (L[i]), inLp[1].lowpass (R[i]) };
            float e[2];
            for (int c = 0; c < 2; ++c)
                e[c] = dl[size_t (c)].readLinear (timeS[size_t (c)].next() * wow * (c == 0 ? 1.0f : 1.013f));
            for (int c = 0; c < 2; ++c)
            {
                const float mixFb = (1.0f - cross) * e[c] + cross * e[1 - c];
                const float f = loopHp[size_t (c)].highpass (loopLp[size_t (c)].lowpass (mixFb));
                dl[size_t (c)].push (x[c] + fastTanh (f * fbk));
            }
            float rl, rr;
            rev.process (x[0] * 0.5f + e[0], x[1] * 0.5f + e[1], rl, rr);
            L[i] = x[0] * direct + e[0] * 0.8f + rl * send;
            R[i] = x[1] * direct + e[1] * 0.8f + rr * send;
        }
    }

private:
    std::array<DelayLine, 2> dl;
    std::array<OnePole, 2> loopLp, loopHp, inLp;
    std::array<OnePoleSmoother, 2> timeS;
    std::array<float, 2> fb {};
    FDNReverb rev;
    ModClock clk;
};

//==============================================================================
/** Repeating "sinking" filter sweep: the sound sinks into darkness, gains a wet,
    wobbling tail as it goes down, then resurfaces smoothly. */
class Drowning final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : lpA) { f.prepare (sr); f.setMode (SVF::Mode::lowpass); }
        for (auto& f : lpB) { f.prepare (sr); f.setMode (SVF::Mode::lowpass); }
        for (auto& d : wob) d.prepare (sr, 0.05f);
        rev.prepare (sr);
        clk.prepare (sr);
        sinkS.prepare (sr, 0.06f);
    }
    void reset() override
    {
        for (auto& f : lpA) f.reset();
        for (auto& f : lpB) f.reset();
        for (auto& d : wob) d.reset();
        rev.reset();
        clk.reset();
        sinkS.reset (0.0f);
        wobPhase = 0.0f;
    }
    double getTailSeconds() const override { return double (rev.getParams().decaySeconds) * 1.2; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float start = shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf());
        const float depthOct = 1.0f + 4.0f * a;
        const float q = 0.7f + res01 (ctx) * 5.0f;
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay);
        rp.size = 1.3f;
        rp.dampingHz = clamp (start, 400.0f, 8000.0f);
        rp.diffusion = 0.8f;
        rp.width = p.width;
        rp.preDelayMs = 20.0f;
        rev.setParams (rp);
        const float wobInc = 2.3f / srf();
        const float wobDepth = 0.0015f * srf();

        for (int i = 0; i < n; ++i)
        {
            const float ph = float (clk.next());
            float s;
            if (ph < 0.85f) s = std::pow (ph / 0.85f, 0.8f);
            else { const float u = (ph - 0.85f) / 0.15f; s = 1.0f - u * u * (3.0f - 2.0f * u); }
            sinkS.setTarget (s);
            const float sink = sinkS.next();
            const float cut = shiftOctaves (start, -depthOct * sink, srf());
            wobPhase += wobInc;
            if (wobPhase >= 1.0f) wobPhase -= 1.0f;
            const float level = 1.0f - 0.35f * sink * a;
            float x[2] = { L[i], R[i] };
            for (int c = 0; c < 2; ++c)
            {
                lpA[size_t (c)].setParams (cut, q);
                lpB[size_t (c)].setParams (cut * 1.3f, 0.6f);
                const float y = lpB[size_t (c)].process (lpA[size_t (c)].process (x[c]));
                wob[size_t (c)].push (y);
                const float m = std::sin (kTwoPi * (wobPhase + 0.25f * float (c) * p.width));
                x[c] = wob[size_t (c)].readLinear (2.0f + wobDepth * sink * (1.0f + m)) * level;
            }
            float rl, rr;
            rev.process (x[0], x[1], rl, rr);
            const float send = 0.2f + 0.6f * sink * a;
            L[i] = x[0] + rl * send;
            R[i] = x[1] + rr * send;
        }
    }

private:
    std::array<SVF, 2> lpA, lpB;
    std::array<DelayLine, 2> wob;
    FDNReverb rev;
    ModClock clk;
    OnePoleSmoother sinkS;
    float wobPhase = 0.0f;
};
} // namespace

void addUnderwaterDark (std::vector<EffectInfo>& r)
{
    const auto cat = Category::underwaterDark;
    r.push_back ({ "underwater", "Underwater", cat,
        "Submerged, resonant low-pass with liquid wobble and a bubbly modulated echo.",
        { "Submerged depth: lower, more resonant filter and deeper wobble",
          "Shifts the water line darker or brighter",
          "Speed of the liquid wobble",
          "Left/right wobble offset and stereo spread" },
        { { AdvParam::cutoff, "Surface Cutoff" }, { AdvParam::resonance }, { AdvParam::rate, "Wobble Rate" },
          { AdvParam::sync }, { AdvParam::division }, { AdvParam::delayTime, "Bubble Delay" }, { AdvParam::feedback } },
        MixLaw::linear, 0.0f, &make<Underwater> });

    r.push_back ({ "deep_sea", "Deep Sea", cat,
        "Very dark, slowly drifting filter into a vast low-frequency ambience.",
        { "Darkness and amount of deep ambience",
          "Moves the darkness up or down",
          "Speed of the slow drift and tail modulation",
          "Width of the ambience" },
        { { AdvParam::cutoff }, { AdvParam::decay }, { AdvParam::diffusion }, { AdvParam::rate, "Drift Rate" },
          { AdvParam::sync }, { AdvParam::division } },
        MixLaw::equalPower, 0.0f, &make<DeepSea> });

    r.push_back ({ "telephone_ghost", "Telephone Ghost", cat,
        "Narrow phone-line band, gentle grit and faint distant reflections.",
        { "Narrower band, more grit and louder ghost reflections",
          "Moves the band centre down or up",
          "Speed of a subtle line flutter",
          "Spread of the ghost reflections" },
        { { AdvParam::cutoff, "Band Centre" }, { AdvParam::drive }, { AdvParam::delayTime, "Reflection Time" },
          { AdvParam::feedback }, { AdvParam::rate, "Flutter Rate" } },
        MixLaw::linear, 0.0f, &make<TelephoneGhost> });

    r.push_back ({ "dark_tunnel", "Dark Tunnel", cat,
        "Darkened cross-fed echoes travelling down a reverberant tunnel.",
        { "Distance: further away, darker and more reverberant",
          "Brightness of the echoes",
          "Speed of a slow tape-like wander in the echoes",
          "Cross-feed between channels and tunnel width" },
        { { AdvParam::delayTime }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::feedback },
          { AdvParam::cutoff, "Echo Tone" }, { AdvParam::decay, "Tunnel Decay" } },
        MixLaw::equalPower, 0.0f, &make<DarkTunnel> });

    r.push_back ({ "drowning", "Drowning", cat,
        "A repeating sinking filter sweep with progressive darkness and a wet, wobbling tail.",
        { "How deep it sinks and how wet it gets",
          "Starting brightness above the surface",
          "Speed of the sinking cycle",
          "Width of the tail and wobble" },
        { { AdvParam::cutoff, "Start Cutoff" }, { AdvParam::resonance }, { AdvParam::sync }, { AdvParam::division, "Sink Length" },
          { AdvParam::rate, "Sink Rate" }, { AdvParam::decay } },
        MixLaw::linear, 0.0f, &make<Drowning> });
}
} // namespace msfx::library
