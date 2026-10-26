// Category C — Dreamy & Atmospheric
#include "EffectBase.h"

namespace msfx::library
{
namespace
{
//==============================================================================
/** Gently chorused input into a rich, modulated FDN reverb. */
class Dreamscape final : public StereoEffect
{
public:
    void prepareFx() override { chorus.prepare (sr); rev.prepare (sr); for (auto& t : tilt) t.prepare (sr); }
    void reset() override { chorus.reset(); rev.reset(); for (auto& t : tilt) t.reset(); }
    double getTailSeconds() const override { return double (rev.getParams().decaySeconds) * 1.3; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        chorus.setParams (3, 0.35f * p.movementRateScale(), 2.5f, 9.0f, 0.8f);
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay) * (0.5f + a);
        rp.size = 1.3f;
        rp.dampingHz = shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf());
        rp.diffusion = p.get (AdvParam::diffusion) * 0.01f;
        rp.modDepth = clamp (0.3f + 0.6f * p.movement, 0.0f, 1.0f);
        rp.modRate = p.get (AdvParam::rate) * p.movementRateScale();
        rp.preDelayMs = p.get (AdvParam::delayTime);
        rp.width = p.width;
        rev.setParams (rp);
        for (auto& t : tilt) t.setTone (p.tone * 0.5f);
        const float chorusAmt = 0.35f * (1.0f - 0.5f * a);
        for (int i = 0; i < n; ++i)
        {
            float cl, cr, rl, rr;
            chorus.process (L[i], R[i], cl, cr);
            rev.process (L[i] + cl * 0.5f, R[i] + cr * 0.5f, rl, rr);
            L[i] = tilt[0].process (rl + cl * chorusAmt);
            R[i] = tilt[1].process (rr + cr * chorusAmt);
        }
    }

private:
    Chorus chorus;
    FDNReverb rev;
    std::array<TiltEQ, 2> tilt;
};

//==============================================================================
/** Shimmer ambience: the reverb tail is pitch-shifted (genuine granular pitch
    shifting, default +12 st) and fed back into the reverb, filtered to stay airy. */
class AngelDust final : public StereoEffect
{
public:
    void prepareFx() override
    {
        rev.prepare (sr);
        for (auto& s : shifter) { s.prepare (sr, 100.0f); s.setWindowMs (70.0f); }
        for (auto& f : shimLp) f.prepare (sr);
        for (auto& f : shimHp) f.prepare (sr);
        for (auto& f : inHp) f.prepare (sr);
    }
    void reset() override
    {
        rev.reset();
        for (auto& s : shifter) s.reset();
        for (auto& f : shimLp) f.reset();
        for (auto& f : shimHp) f.reset();
        for (auto& f : inHp) f.reset();
        ret = { 0.0f, 0.0f };
    }
    double getTailSeconds() const override { return double (rev.getParams().decaySeconds) * 2.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay);
        rp.size = 1.4f;
        rp.dampingHz = shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf());
        rp.diffusion = p.get (AdvParam::diffusion) * 0.01f;
        rp.modDepth = clamp (0.25f + 0.5f * p.movement, 0.0f, 1.0f);
        rp.modRate = 0.5f * p.movementRateScale();
        rp.preDelayMs = 20.0f;
        rp.width = p.width;
        rev.setParams (rp);
        const float ratio = std::exp2 (p.get (AdvParam::pitch) / 12.0f);
        const float shimmer = clamp (p.fb01() * (0.35f + 0.65f * a), 0.0f, 0.7f);
        for (int c = 0; c < 2; ++c)
        {
            shimLp[size_t (c)].setCutoff (shiftOctaves (7500.0f, p.tone, srf()));
            shimHp[size_t (c)].setCutoff (350.0f);
            inHp[size_t (c)].setCutoff (150.0f);
        }
        for (int i = 0; i < n; ++i)
        {
            float rl, rr;
            rev.process (inHp[0].highpass (L[i]) * 0.7f + ret[0], inHp[1].highpass (R[i]) * 0.7f + ret[1], rl, rr);
            const float sl = shimHp[0].highpass (shimLp[0].lowpass (shifter[0].process (rl, ratio)));
            const float sr2 = shimHp[1].highpass (shimLp[1].lowpass (shifter[1].process (rr, ratio)));
            ret[0] = fastTanh (sl * shimmer * 1.2f) * 0.85f;
            ret[1] = fastTanh (sr2 * shimmer * 1.2f) * 0.85f;
            L[i] = rl + sl * 0.35f * a;
            R[i] = rr + sr2 * 0.35f * a;
        }
    }

private:
    FDNReverb rev;
    std::array<GranularPitchShifter, 2> shifter;
    std::array<OnePole, 2> shimLp, shimHp, inHp;
    std::array<float, 2> ret {};
};

//==============================================================================
/** Soft, transient-free bloom: heavy input diffusion, long smooth tail and a
    slow rotation of the stereo image. */
class CloudNine final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& ch : diff) for (auto& d : ch) d.prepare (sr, 0.08f);
        for (auto& f : soft) f.prepare (sr);
        rev.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& ch : diff) for (auto& d : ch) d.reset();
        for (auto& f : soft) f.reset();
        rev.reset();
        clk.reset();
    }
    double getTailSeconds() const override { return double (rev.getParams().decaySeconds) * 1.3; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        static constexpr float diffMs[4] = { 31.0f, 23.3f, 47.1f, 17.7f };
        const auto& p = ctx.params;
        const float a = p.amount;
        const float g = 0.45f + 0.3f * p.get (AdvParam::diffusion) * 0.01f;
        for (auto& f : soft) f.setCutoff (shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf()));
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay) * (0.6f + 0.8f * a);
        rp.size = 1.8f;
        rp.dampingHz = shiftOctaves (p.get (AdvParam::cutoff) * 0.8f, p.tone, srf());
        rp.diffusion = 1.0f;
        rp.modDepth = 0.7f;
        rp.modRate = 0.3f;
        rp.preDelayMs = 60.0f;
        rp.width = clamp (p.width, 0.0f, 1.6f);
        rev.setParams (rp);
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        const float rotDepth = 0.45f * clamp (p.width, 0.0f, 2.0f) * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            float x[2] = { soft[0].lowpass (L[i]), soft[1].lowpass (R[i]) };
            for (int c = 0; c < 2; ++c)
                for (int d = 0; d < 4; ++d)
                    x[c] = diff[size_t (c)][size_t (d)].process (x[c], diffMs[d] * (c ? 1.07f : 1.0f) * 0.001f * srf(), g);
            float rl, rr;
            rev.process (x[0], x[1], rl, rr);
            const float th = rotDepth * std::sin (kTwoPi * float (clk.next()));
            const float cs = std::cos (th), sn = std::sin (th);
            const float bloom = 0.25f * (1.0f - a);
            L[i] = (rl * cs - rr * sn) + x[0] * bloom;
            R[i] = (rl * sn + rr * cs) + x[1] * bloom;
        }
    }

private:
    std::array<std::array<AllpassDelay, 4>, 2> diff;
    std::array<OnePole, 2> soft;
    FDNReverb rev;
    ModClock clk;
};

//==============================================================================
/** Genuine reverse playback (overlapping reversed chunks) washed in reverb.
    Chunks follow the tempo division or the Time parameter. */
class ReverseDream final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& r : rev) r.prepare (sr, 4.2f);
        reverb.prepare (sr);
        chorus.prepare (sr);
        for (auto& t : tilt) t.prepare (sr);
    }
    void reset() override
    {
        for (auto& r : rev) r.reset();
        reverb.reset();
        chorus.reset();
        for (auto& t : tilt) t.reset();
        fb = { 0.0f, 0.0f };
    }
    double getTailSeconds() const override { return 8.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const double sec = (p.sync() ? ctx.divisionSeconds() : double (p.get (AdvParam::delayTime)) * 0.001)
                           * std::exp2 ((double (p.movement) - 0.5) * 2.0);
        const int chunk = int (clamp (sec, 0.05, 2.0) * sr);
        rev[0].setChunkSamples (chunk);
        rev[1].setChunkSamples (int (float (chunk) * (1.0f + 0.05f * p.width)));
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay);
        rp.size = 1.2f;
        rp.dampingHz = shiftOctaves (6000.0f, p.tone, srf());
        rp.modDepth = 0.5f;
        rp.width = p.width;
        reverb.setParams (rp);
        chorus.setParams (2, 0.4f, 2.0f, 8.0f, 0.6f);
        for (auto& t : tilt) t.setTone (p.tone);
        const float fbk = clamp (p.fb01() * 0.65f, 0.0f, 0.6f);
        for (int i = 0; i < n; ++i)
        {
            const float l = rev[0].process (L[i] + fb[0] * fbk);
            const float r = rev[1].process (R[i] + fb[1] * fbk);
            fb = { fastTanh (l), fastTanh (r) };
            float cl, cr, rl, rr;
            chorus.process (l, r, cl, cr);
            reverb.process (l + cl * 0.3f, r + cr * 0.3f, rl, rr);
            L[i] = tilt[0].process (l * (1.0f - 0.45f * a) + rl * (0.3f + 0.7f * a));
            R[i] = tilt[1].process (r * (1.0f - 0.45f * a) + rr * (0.3f + 0.7f * a));
        }
    }

private:
    std::array<ReverseDelay, 2> rev;
    FDNReverb reverb;
    Chorus chorus;
    std::array<TiltEQ, 2> tilt;
    std::array<float, 2> fb {};
};

//==============================================================================
/** Long, frozen-feeling echoes: diffused, filtered feedback delay with optional
    pitch colouring (granular pitch shift inside the loop). Decay sets the tail time. */
class FrozenEcho final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& d : dl) d.prepare (sr, 2.2f);
        for (auto& ch : ap) for (auto& x : ch) x.prepare (sr, 0.05f);
        for (auto& f : lp) f.prepare (sr);
        for (auto& f : hp) f.prepare (sr);
        for (auto& s : shifter) { s.prepare (sr, 100.0f); s.setWindowMs (60.0f); }
        for (auto& s : timeS) s.prepare (sr, 0.3f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& ch : ap) for (auto& x : ch) x.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        for (auto& s : shifter) s.reset();
        for (auto& s : timeS) s.reset (0.45f * srf());
        clk.reset();
    }
    double getTailSeconds() const override { return 20.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float t = clamp (delayTimeSamples (ctx), 32.0f, dl[0].getMaxDelaySamples() - 64.0f);
        for (int c = 0; c < 2; ++c) timeS[size_t (c)].setTarget (t * (c ? 1.0f + 0.04f * p.width : 1.0f));
        const float decay = p.get (AdvParam::decay) * (0.4f + 1.2f * a);
        const float fbk = clamp (std::pow (10.0f, -3.0f * (t / srf()) / std::max (0.1f, decay)), 0.0f, 0.97f);
        const float g = 0.25f + 0.45f * p.get (AdvParam::diffusion) * 0.01f;
        const float pitchSt = p.get (AdvParam::pitch);
        const float ratio = std::exp2 (pitchSt / 12.0f);
        const float pitchBlend = clamp (std::abs (pitchSt), 0.0f, 1.0f) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            lp[size_t (c)].setCutoff (shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf()));
            hp[size_t (c)].setCutoff (120.0f);
        }
        clk.begin (ctx, 0.3f, 8.0);
        const float wow = 0.002f * p.movementDepthScale();
        for (int i = 0; i < n; ++i)
        {
            const float m = 1.0f + wow * std::sin (kTwoPi * float (clk.next()));
            float x[2] = { L[i], R[i] };
            for (int c = 0; c < 2; ++c)
            {
                const float e = dl[size_t (c)].readLinear (timeS[size_t (c)].next() * m);
                float loop = hp[size_t (c)].highpass (lp[size_t (c)].lowpass (e));
                loop = ap[size_t (c)][0].process (loop, (11.3f + 2.0f * float (c)) * 0.001f * srf(), g);
                loop = ap[size_t (c)][1].process (loop, (29.1f + 3.0f * float (c)) * 0.001f * srf(), g);
                if (pitchBlend > 0.0f)
                    loop = lerp (loop, shifter[size_t (c)].process (loop, ratio), pitchBlend);
                dl[size_t (c)].push (x[c] + fastTanh (loop * fbk));
                x[c] = e;
            }
            L[i] = x[0];
            R[i] = x[1];
        }
    }

private:
    std::array<DelayLine, 2> dl;
    std::array<std::array<AllpassDelay, 2>, 2> ap;
    std::array<OnePole, 2> lp, hp;
    std::array<GranularPitchShifter, 2> shifter;
    std::array<OnePoleSmoother, 2> timeS;
    ModClock clk;
};
} // namespace

void addDreamyAtmospheric (std::vector<EffectInfo>& r)
{
    const auto cat = Category::dreamyAtmospheric;
    r.push_back ({ "dreamscape", "Dreamscape", cat,
        "Rich, softly chorused reverb with a diffuse, modulated tail.",
        { "Longer decay and deeper immersion", "Darker or brighter tail", "Depth and speed of tail modulation", "Width of the reverb" },
        { { AdvParam::decay }, { AdvParam::diffusion }, { AdvParam::cutoff, "Damping" }, { AdvParam::rate, "Mod Rate" },
          { AdvParam::delayTime, "Pre-Delay" } },
        MixLaw::equalPower, 0.0f, &make<Dreamscape> });

    r.push_back ({ "angel_dust", "Angel Dust", cat,
        "Airy shimmer: an octave-up, pitch-shifted halo blooming out of the reverb.",
        { "Shimmer intensity", "Brightness of the halo (kept smooth)", "Tail modulation", "Width of the halo" },
        { { AdvParam::decay }, { AdvParam::pitch, "Shimmer Pitch" }, { AdvParam::feedback, "Shimmer" },
          { AdvParam::cutoff, "Damping" }, { AdvParam::diffusion } },
        MixLaw::equalPower, 0.0f, &make<AngelDust> });

    r.push_back ({ "cloud_nine", "Cloud Nine", cat,
        "Soft, transient-free bloom with long smooth tails and slow stereo drift.",
        { "Longer, denser cloud", "Softer or airier cloud", "Speed of the slow stereo drift", "Amount of stereo drift and width" },
        { { AdvParam::decay }, { AdvParam::diffusion }, { AdvParam::cutoff, "Softness" }, { AdvParam::rate, "Drift Rate" },
          { AdvParam::sync }, { AdvParam::division } },
        MixLaw::equalPower, 0.0f, &make<CloudNine> });

    r.push_back ({ "reverse_dream", "Reverse Dream", cat,
        "Phrases played backwards in overlapping chunks and washed in reverb.",
        { "More reverb wash around the reversed audio", "Darker or brighter", "Chunk length (shorter or longer reversed pieces)", "Left/right chunk offset and reverb width" },
        { { AdvParam::delayTime, "Chunk Time" }, { AdvParam::sync }, { AdvParam::division, "Chunk" }, { AdvParam::decay },
          { AdvParam::feedback } },
        MixLaw::equalPower, 0.0f, &make<ReverseDream> });

    r.push_back ({ "frozen_echo", "Frozen Echo", cat,
        "Long, diffused feedback echoes with optional pitch colouring in the loop.",
        { "Longer decay", "Echo brightness", "Gentle wow in the repeats", "Left/right time offset" },
        { { AdvParam::delayTime }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::decay },
          { AdvParam::diffusion, "Density" }, { AdvParam::pitch, "Pitch Colour" }, { AdvParam::cutoff, "Echo Tone" } },
        MixLaw::equalPower, 0.0f, &make<FrozenEcho> });
}
} // namespace msfx::library
