// Category F — Echo & Space
#include "EffectBase.h"

namespace msfx::library
{
namespace
{
//==============================================================================
/** Short, warm slapback with Haas-style left/right offset. */
class Slapback final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& d : dl) d.prepare (sr, 2.2f);
        for (auto& f : lp) f.prepare (sr);
        for (auto& s : timeS) s.prepare (sr, 0.15f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& f : lp) f.reset();
        for (auto& s : sat) s.reset();
        for (auto& s : timeS) s.reset (0.095f * srf());
        clk.reset();
    }
    double getTailSeconds() const override { return 1.5; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float t = clamp (delayTimeSamples (ctx), 16.0f, dl[0].getMaxDelaySamples() - 64.0f);
        const float spread = 0.08f * clamp (p.width, 0.0f, 2.0f);
        timeS[0].setTarget (t * (1.0f - spread));
        timeS[1].setTarget (t * (1.0f + spread));
        const float fbk = clamp (p.fb01() * (0.5f + 0.5f * a), 0.0f, 0.9f);
        for (int c = 0; c < 2; ++c)
        {
            lp[size_t (c)].setCutoff (shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf()));
            sat[size_t (c)].set (Saturator::Type::soft, p.get (AdvParam::drive));
        }
        const float level = 0.6f + 0.6f * a;
        const float wob = 0.003f * p.movementDepthScale();
        clk.begin (ctx, 0.8f, 2.0);
        for (int i = 0; i < n; ++i)
        {
            const float m = 1.0f + wob * std::sin (kTwoPi * float (clk.next()));
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                auto& d = dl[size_t (c)];
                const float e = d.readLinear (timeS[size_t (c)].next() * m);
                const float shaped = lp[size_t (c)].lowpass (sat[size_t (c)].process (e));
                d.push (*io[c] + shaped * fbk);
                *io[c] = shaped * level;
            }
        }
    }

private:
    std::array<DelayLine, 2> dl;
    std::array<OnePole, 2> lp;
    std::array<Saturator, 2> sat;
    std::array<OnePoleSmoother, 2> timeS;
    ModClock clk;
};

//==============================================================================
/** Stereo ping-pong delay with filtered, smeared feedback and moving injection. */
class PingPongGalaxy final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& d : dl) d.prepare (sr, 2.2f);
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : hp) f.prepare (sr);
        for (auto& x : smear) x.prepare (sr, 0.03f);
        for (auto& s : timeS) s.prepare (sr, 0.25f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        for (auto& x : smear) x.reset();
        for (auto& s : timeS) s.reset (0.25f * srf());
        out = { 0.0f, 0.0f };
        clk.reset();
    }
    double getTailSeconds() const override { return 8.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float t = clamp (delayTimeSamples (ctx), 32.0f, dl[0].getMaxDelaySamples() - 64.0f);
        for (auto& s : timeS) s.setTarget (t);
        const float fbk = clamp (p.fb01() * (0.6f + 0.4f * a), 0.0f, 0.93f);
        const float g = 0.15f + 0.35f * a;
        for (int c = 0; c < 2; ++c)
        {
            lp[size_t (c)].setCutoff (shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf()));
            hp[size_t (c)].setCutoff (150.0f);
        }
        const float w = clamp (p.width, 0.0f, 2.0f) * 0.5f; // 0..1 ping-pong extent
        clk.begin (ctx, p.get (AdvParam::rate), 4.0);
        for (int i = 0; i < n; ++i)
        {
            const float ph = float (clk.next());
            const float mod = 1.0f + 0.003f * std::sin (kTwoPi * ph);
            const float pan = 0.5f + 0.5f * std::sin (kTwoPi * ph * 2.0f) * 0.6f; // moving injection
            const float mono = 0.5f * (L[i] + R[i]);
            const float eL = dl[0].readLinear (timeS[0].next() * mod);
            const float eR = dl[1].readLinear (timeS[1].next() / mod);
            const float fL = hp[0].highpass (lp[0].lowpass (smear[0].process (eL, 0.0071f * srf(), g)));
            const float fR = hp[1].highpass (lp[1].lowpass (smear[1].process (eR, 0.0093f * srf(), g)));
            dl[0].push (mono * (1.0f - pan) * 1.4f + fastTanh (fR * fbk));
            dl[1].push (mono * pan * 0.6f + fastTanh (fL * fbk));
            L[i] = eL * (0.5f + 0.5f * w) + eR * (0.5f - 0.5f * w);
            R[i] = eR * (0.5f + 0.5f * w) + eL * (0.5f - 0.5f * w);
        }
    }

private:
    std::array<DelayLine, 2> dl;
    std::array<OnePole, 2> lp, hp;
    std::array<AllpassDelay, 2> smear;
    std::array<OnePoleSmoother, 2> timeS;
    std::array<float, 2> out {};
    ModClock clk;
};

//==============================================================================
/** Very long, diffuse hall (decay up to 60 s with stable gains). */
class EndlessHall final : public StereoEffect
{
public:
    void prepareFx() override { rev.prepare (sr); for (auto& f : lowCut) f.prepare (sr); }
    void reset() override { rev.reset(); for (auto& f : lowCut) f.reset(); }
    double getTailSeconds() const override { return std::min (60.0, double (rev.getParams().decaySeconds) * 1.2); }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        FDNReverb::Params rp;
        rp.decaySeconds = clamp (p.get (AdvParam::decay) * (0.4f + 1.2f * a), 0.3f, 60.0f);
        rp.size = 2.0f;
        rp.dampingHz = shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf());
        rp.diffusion = p.get (AdvParam::diffusion) * 0.01f;
        rp.modDepth = clamp (0.2f + 0.5f * p.movement, 0.0f, 1.0f);
        rp.modRate = p.get (AdvParam::rate) * p.movementRateScale();
        rp.preDelayMs = p.get (AdvParam::delayTime);
        rp.lowCutHz = 60.0f;
        rp.width = p.width;
        rev.setParams (rp);
        for (int i = 0; i < n; ++i)
        {
            float rl, rr;
            rev.process (L[i], R[i], rl, rr);
            L[i] = rl;
            R[i] = rr;
        }
    }

private:
    FDNReverb rev;
    std::array<OnePole, 2> lowCut;
};

//==============================================================================
/** Feedback delay whose repeats spiral through a moving band-pass filter and
    a slight pitch drift, so every repeat sounds different. */
class DelaySpiral final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& d : dl) d.prepare (sr, 2.2f);
        for (auto& f : bp) { f.prepare (sr); f.setMode (SVF::Mode::bandpassNormalized); }
        for (auto& f : hp) f.prepare (sr);
        for (auto& s : timeS) s.prepare (sr, 0.3f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& f : bp) f.reset();
        for (auto& f : hp) f.reset();
        for (auto& s : timeS) s.reset (0.3f * srf());
        clk.reset();
    }
    double getTailSeconds() const override { return 12.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float t = clamp (delayTimeSamples (ctx), 32.0f, dl[0].getMaxDelaySamples() - 64.0f);
        for (int c = 0; c < 2; ++c) timeS[size_t (c)].setTarget (t * (c ? 1.0f + 0.02f * p.width : 1.0f));
        const float fbk = clamp (p.fb01() * (0.7f + 0.3f * a), 0.0f, 0.95f);
        const float q = 0.7f + res01 (ctx) * 5.0f;
        const float centre = shiftOctaves (1200.0f, p.tone, srf());
        const float sweepOct = 1.0f + 2.0f * a;
        for (auto& f : hp) f.setCutoff (100.0f);
        clk.begin (ctx, p.get (AdvParam::rate), 8.0);
        for (int i = 0; i < n; ++i)
        {
            const float ph = float (clk.next());
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                const float m = std::sin (kTwoPi * (ph + (c ? 0.25f * p.width : 0.0f)));
                bp[size_t (c)].setParams (shiftOctaves (centre, sweepOct * m, srf()), q);
                const float drift = 1.0f + 0.004f * m * a;
                auto& d = dl[size_t (c)];
                const float e = d.readLinear (timeS[size_t (c)].next() * drift);
                const float loop = hp[size_t (c)].highpass (lerp (e, bp[size_t (c)].process (e) * 1.5f, 0.5f + 0.4f * a));
                d.push (*io[c] + fastTanh (loop * fbk * 1.1f) * 0.92f);
                *io[c] = e;
            }
        }
    }

private:
    std::array<DelayLine, 2> dl;
    std::array<SVF, 2> bp;
    std::array<OnePole, 2> hp;
    std::array<OnePoleSmoother, 2> timeS;
    ModClock clk;
};

//==============================================================================
/** Dense room: 12-tap early reflection pattern, diffusion and a short FDN tail. */
class EchoChamber final : public StereoEffect
{
public:
    void prepareFx() override
    {
        er.prepare (sr, 0.5f);
        for (auto& ch : diff) for (auto& d : ch) d.prepare (sr, 0.05f);
        rev.prepare (sr);
        for (auto& f : damp) f.prepare (sr);
    }
    void reset() override
    {
        er.reset();
        for (auto& ch : diff) for (auto& d : ch) d.reset();
        rev.reset();
        for (auto& f : damp) f.reset();
    }
    double getTailSeconds() const override { return double (rev.getParams().decaySeconds) * 1.2 + 0.4; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        static constexpr float tapPos[12] = { 0.11f, 0.17f, 0.23f, 0.31f, 0.37f, 0.43f, 0.52f, 0.61f, 0.69f, 0.78f, 0.88f, 1.0f };
        static constexpr float tapPan[12] = { -0.8f, 0.7f, -0.3f, 0.9f, -0.6f, 0.2f, -0.9f, 0.5f, -0.1f, 0.8f, -0.5f, 0.4f };
        static constexpr float diffMs[3] = { 3.7f, 7.1f, 11.3f };
        const auto& p = ctx.params;
        const float a = p.amount;
        const float roomSamples = p.get (AdvParam::delayTime) * 0.001f * srf();
        const float g = 0.4f + 0.35f * p.get (AdvParam::diffusion) * 0.01f;
        const float w = clamp (p.width, 0.0f, 2.0f) * 0.5f;
        float tl[12], tr[12];
        for (int k = 0; k < 12; ++k)
        {
            float gl, gr;
            panGains (tapPan[k] * w, gl, gr);
            const float gain = 0.55f * std::pow (0.82f, float (k));
            tl[k] = gain * gl;
            tr[k] = gain * gr;
        }
        for (auto& f : damp) f.setCutoff (shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf()));
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay) * (0.5f + a);
        rp.size = 0.7f;
        rp.dampingHz = shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf());
        rp.diffusion = p.get (AdvParam::diffusion) * 0.01f;
        rp.modDepth = 0.15f + 0.3f * p.movement;
        rp.modRate = 0.6f * p.movementRateScale();
        rp.preDelayMs = 5.0f;
        rp.width = p.width;
        rev.setParams (rp);
        const float late = 0.35f + 0.55f * a;
        for (int i = 0; i < n; ++i)
        {
            er.push (0.5f * (L[i] + R[i]));
            float el = 0.0f, err = 0.0f;
            for (int k = 0; k < 12; ++k)
            {
                const float s = er.readLinear (tapPos[k] * roomSamples);
                el += s * tl[k];
                err += s * tr[k];
            }
            el = damp[0].lowpass (el);
            err = damp[1].lowpass (err);
            for (int d = 0; d < 3; ++d)
            {
                el = diff[0][size_t (d)].process (el, diffMs[d] * 0.001f * srf(), g);
                err = diff[1][size_t (d)].process (err, diffMs[d] * 1.09f * 0.001f * srf(), g);
            }
            float rl, rr;
            rev.process (el + L[i] * 0.3f, err + R[i] * 0.3f, rl, rr);
            L[i] = el * 0.8f + rl * late;
            R[i] = err * 0.8f + rr * late;
        }
    }

private:
    DelayLine er;
    std::array<std::array<AllpassDelay, 3>, 2> diff;
    std::array<OnePole, 2> damp;
    FDNReverb rev;
};
} // namespace

void addEchoSpace (std::vector<EffectInfo>& r)
{
    const auto cat = Category::echoSpace;
    r.push_back ({ "slapback", "Slapback", cat,
        "Short, warm rockabilly-style slap echo.",
        { "Slap level and repeats", "Echo brightness", "Subtle tape wobble in the slap", "Left/right time offset" },
        { { AdvParam::delayTime }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::feedback },
          { AdvParam::drive }, { AdvParam::cutoff, "Echo Tone" } },
        MixLaw::equalPower, 0.0f, &make<Slapback> });

    r.push_back ({ "ping_pong_galaxy", "Ping-Pong Galaxy", cat,
        "Wide ping-pong echoes with filtered, smeared, shimmering feedback.",
        { "More feedback and smear", "Echo brightness", "Speed of the stereo motion", "Ping-pong extent" },
        { { AdvParam::delayTime }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::feedback },
          { AdvParam::cutoff, "Echo Tone" }, { AdvParam::rate, "Motion Rate" } },
        MixLaw::equalPower, 0.0f, &make<PingPongGalaxy> });

    r.push_back ({ "endless_hall", "Endless Hall", cat,
        "Huge, diffuse hall with very long, stable tails.",
        { "Longer decay (up to 60 s)", "Darker or brighter hall", "Tail modulation", "Hall width" },
        { { AdvParam::decay }, { AdvParam::cutoff, "Damping" }, { AdvParam::diffusion }, { AdvParam::rate, "Mod Rate" },
          { AdvParam::delayTime, "Pre-Delay" } },
        MixLaw::equalPower, 0.0f, &make<EndlessHall> });

    r.push_back ({ "delay_spiral", "Delay Spiral", cat,
        "High-feedback delay whose repeats spiral through a moving filter.",
        { "Feedback and sweep depth", "Centre of the spiral filter", "Spiral speed", "Left/right spiral offset" },
        { { AdvParam::delayTime }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::feedback },
          { AdvParam::rate, "Spiral Rate" }, { AdvParam::resonance } },
        MixLaw::equalPower, 0.0f, &make<DelaySpiral> });

    r.push_back ({ "echo_chamber", "Echo Chamber", cat,
        "Dense multi-tap reflections and a short diffuse room tail.",
        { "More late reverberation", "Darker or brighter walls", "Tail modulation", "Reflection spread" },
        { { AdvParam::delayTime, "Room Size" }, { AdvParam::diffusion }, { AdvParam::decay }, { AdvParam::cutoff, "Damping" } },
        MixLaw::equalPower, 0.0f, &make<EchoChamber> });
}
} // namespace msfx::library
