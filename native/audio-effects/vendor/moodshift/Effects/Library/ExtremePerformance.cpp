// Category H — Extreme & Performance
#include "EffectBase.h"

namespace msfx::library
{
namespace
{
//==============================================================================
/** Classic reverse-reverb "suck-in": the input and its reverb tail are captured
    in chunks (tempo grid or Time) and played back reversed, with a rising level
    and an opening high-pass so each chunk swells into the next downbeat. */
class ReverseSuckIn final : public StereoEffect
{
public:
    void prepareFx() override
    {
        rev.prepare (sr);
        for (auto& c : cap) c.prepare (sr, 8.5f);
        for (auto& f : hp) { f.prepare (sr); f.setMode (SVF::Mode::highpass); }
        clk.prepare (sr);
        fade = std::max (16, int (0.005 * sr));
    }
    void reset() override
    {
        rev.reset();
        for (auto& c : cap) c.reset();
        for (auto& f : hp) f.reset();
        clk.reset();
        counter = 0;
        playLen = 0;
        lastCycle = INT64_MIN;
        chunkStart = 0;
        sinceBoundary = 0;
    }
    double getTailSeconds() const override { return 8.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        FDNReverb::Params rp;
        rp.decaySeconds = p.get (AdvParam::decay);
        rp.size = 1.4f;
        rp.dampingHz = 7000.0f;
        rp.diffusion = 0.9f;
        rp.width = p.width;
        rev.setParams (rp);
        const double chunkSec = p.sync() ? ctx.divisionSeconds() : double (p.get (AdvParam::delayTime)) * 0.001;
        const int freeLen = int (clamp (chunkSec / double (p.movementRateScale()), 0.05, 4.0) * sr);
        const float hpStart = shiftOctaves (2500.0f, -p.tone, srf());
        const float risePow = 1.0f + 2.0f * a;
        clk.begin (ctx, 1.0f, ctx.divisionBeats());

        for (int i = 0; i < n; ++i)
        {
            clk.next();
            float rl, rr;
            rev.process (L[i], R[i], rl, rr);
            cap[0].push (L[i] * 0.35f + rl);
            cap[1].push (R[i] * 0.35f + rr);
            ++sinceBoundary;

            bool boundary;
            if (p.sync())
            {
                const auto cyc = clk.cycleIndex();
                boundary = cyc != lastCycle;
                lastCycle = cyc;
            }
            else
                boundary = sinceBoundary >= freeLen;
            if (sinceBoundary > int (4.0 * sr)) boundary = true;

            if (boundary)
            {
                chunkStart = cap[0].getWriteIndex();
                playLen = sinceBoundary;
                sinceBoundary = 0;
                counter = 0;
            }

            float out[2] = { 0.0f, 0.0f };
            if (counter < playLen && playLen > 2 * fade)
            {
                const float progress = float (counter) / float (playLen);
                const float rise = std::pow (progress, risePow);
                const float edge = clamp (float (std::min (counter, playLen - 1 - counter)) / float (fade), 0.0f, 1.0f);
                const float cut = std::exp (lerp (std::log (hpStart), std::log (30.0f), progress));
                for (int c = 0; c < 2; ++c)
                {
                    const float s = cap[size_t (c)].read (chunkStart - 1 - uint64_t (counter));
                    hp[size_t (c)].setParams (cut, 0.7f);
                    out[c] = hp[size_t (c)].process (s) * rise * edge * 1.6f;
                }
                ++counter;
            }
            L[i] = out[0];
            R[i] = out[1];
        }
    }

private:
    FDNReverb rev;
    std::array<CaptureBuffer, 2> cap;
    std::array<SVF, 2> hp;
    ModClock clk;
    int counter = 0, playLen = 0, sinceBoundary = 0, fade = 240;
    int64_t lastCycle = INT64_MIN;
    uint64_t chunkStart = 0;
};

//==============================================================================
/** Beat-locked stutter bursts: on selected beats the most recent slice repeats
    (gated) for the rest of the beat; above 50 % Movement the repeats ratchet. */
class StutterBurst final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& s : rep) s.prepare (sr, 4.0f);
        for (auto& t : tilt) t.prepare (sr);
        clk.prepare (sr);
        for (auto& g : panS) g.prepare (sr, 0.005f);
    }
    void reset() override
    {
        for (auto& s : rep) s.reset();
        for (auto& t : tilt) t.reset();
        clk.reset();
        lastBeat = INT64_MIN;
        burst = false;
        stage = 0;
        for (auto& g : panS) g.reset (1.0f);
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float density = p.get (AdvParam::density) * 0.01f;
        const float prob = clamp (density * (0.3f + 0.7f * a), 0.0f, 1.0f);
        const double sliceSec = p.sync() ? ctx.divisionSeconds() : double (p.get (AdvParam::delayTime)) * 0.001;
        const int slice = int (clamp (sliceSec, 0.008, 1.0) * sr);
        const float gate = p.get (AdvParam::gate) * 0.01f;
        const double start = density > 0.85f ? 0.0 : 0.5;
        const bool ratchet = p.movement > 0.5f;
        for (auto& t : tilt) t.setTone (p.tone);
        // One decision per beat (synced) or per Rate cycle (free).
        clk.begin (ctx, p.get (AdvParam::rate), 1.0);

        for (int i = 0; i < n; ++i)
        {
            const double ph = clk.next();
            const auto beat = clk.cycleIndex();
            if (beat != lastBeat)
            {
                lastBeat = beat;
                const uint32_t h = uint32_t (beat) * 2246822519u + 0x9E37u;
                burst = hashToUnit (h) < prob;
                stage = 0;
                const float pan = (hashToUnit (h ^ 0xFACEu) * 2.0f - 1.0f) * 0.7f * clamp (p.width, 0.0f, 1.5f);
                float gl, gr;
                panGains (burst ? pan : 0.0f, gl, gr);
                panS[0].setTarget (burst ? gl : 1.0f);
                panS[1].setTarget (burst ? gr : 1.0f);
            }
            const bool inBurst = burst && ph >= start;
            if (inBurst && stage == 0)
            {
                for (auto& s : rep) s.engage (slice, false, 1.0f, gate);
                stage = 1;
            }
            else if (inBurst && stage == 1 && ratchet && ph >= start + (1.0 - start) * 0.6)
            {
                for (auto& s : rep) s.engage (std::max (64, slice / 2), false, 1.0f, gate);
                stage = 2;
            }
            else if (! inBurst && stage != 0)
            {
                for (auto& s : rep) s.release();
                stage = 0;
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
    int64_t lastBeat = INT64_MIN;
    bool burst = false;
    int stage = 0;
};

//==============================================================================
/** Downward pitch dive (genuine granular pitch shifting) retriggered on detected
    onsets (free) or on every tempo division (synced). */
class PitchDive final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& s : shifter) { s.prepare (sr, 100.0f); s.setWindowMs (55.0f); }
        fast.prepare (sr); fast.setTimes (1.0f, 40.0f);
        slow.prepare (sr); slow.setTimes (40.0f, 500.0f);
        for (auto& t : tilt) t.prepare (sr);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& s : shifter) s.reset();
        fast.reset();
        slow.reset();
        for (auto& t : tilt) t.reset();
        clk.reset();
        since = 1 << 30;
        lastCycle = INT64_MIN;
    }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float depth = p.get (AdvParam::pitch) * (0.3f + 0.7f * a);
        const double durSec = (p.sync() ? ctx.divisionSeconds() : double (p.get (AdvParam::delayTime)) * 0.001)
                              / double (p.movementRateScale());
        const float dur = float (clamp (durSec, 0.03, 8.0) * sr);
        const int refractory = int (0.15 * sr);
        for (auto& t : tilt) t.setTone (p.tone);
        clk.begin (ctx, 1.0f, ctx.divisionBeats());
        for (int i = 0; i < n; ++i)
        {
            clk.next();
            const float mono = 0.5f * (L[i] + R[i]);
            const float ef = fast.process (mono), es = slow.process (mono);
            bool trig;
            if (p.sync())
            {
                const auto cyc = clk.cycleIndex();
                trig = cyc != lastCycle;
                lastCycle = cyc;
            }
            else
                trig = since > refractory && ef > 0.02f && ef > es * 2.0f;
            if (trig) since = 0;
            const float t = std::min (1.0f, float (since) / dur);
            if (since < (1 << 30)) ++since;
            const float semis = depth * std::pow (t, 1.6f);
            const float ratioL = std::exp2 (semis / 12.0f);
            const float ratioR = std::exp2 (semis * (1.0f + 0.04f * p.width) / 12.0f);
            L[i] = tilt[0].process (shifter[0].process (L[i], ratioL));
            R[i] = tilt[1].process (shifter[1].process (R[i], ratioR));
        }
    }

private:
    std::array<GranularPitchShifter, 2> shifter;
    EnvelopeFollower fast, slow;
    std::array<TiltEQ, 2> tilt;
    ModClock clk;
    int since = 1 << 30;
    int64_t lastCycle = INT64_MIN;
};

//==============================================================================
/** Phaser + flanger + counter-swept filter + circular auto-pan, all driven from
    one phase-related LFO system for a spinning vortex. */
class Vortex final : public StereoEffect
{
public:
    void prepareFx() override
    {
        ph.prepare (sr);
        for (auto& d : flange) d.prepare (sr, 0.02f);
        for (auto& f : lp) { f.prepare (sr); f.setMode (SVF::Mode::lowpass); }
        clk.prepare (sr);
    }
    void reset() override
    {
        ph.reset();
        for (auto& d : flange) d.reset();
        for (auto& f : lp) f.reset();
        fb = { 0.0f, 0.0f };
        clk.reset();
    }
    double getTailSeconds() const override { return 0.3; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        ph.setParams (6, 0.0f, 0.5f + 0.5f * a, 0.3f + 0.4f * a, 150.0f, 6000.0f, 0.33f);
        const float fbk = clamp (p.fb01() * (0.4f + 0.6f * a), 0.0f, 0.9f);
        const float base = shiftOctaves (p.get (AdvParam::cutoff), p.tone, srf());
        const float rot = 0.9f * clamp (p.width, 0.0f, 2.0f) * 0.5f;
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        for (int i = 0; i < n; ++i)
        {
            const float phase = float (clk.next());
            float l = L[i], r = R[i];
            ph.processAt (l, r, phase);
            float x[2] = { l, r };
            for (int c = 0; c < 2; ++c)
            {
                const float m = std::sin (kTwoPi * (phase + 0.25f + 0.5f * float (c)));
                const float d = (1.0f + 2.5f * (1.0f + m)) * 0.001f * srf();
                auto& dl = flange[size_t (c)];
                const float e = dl.readLinear (d);
                dl.push (x[c] + fastTanh (e * fbk));
                const float y = 0.6f * (x[c] + e);
                lp[size_t (c)].setParams (shiftOctaves (base, -1.5f * a * m, srf()), 1.2f);
                x[c] = lp[size_t (c)].process (y);
            }
            // circular auto-pan: rotate the stereo pair
            const float th = rot * std::sin (kTwoPi * phase * 2.0f);
            const float cs = std::cos (th), sn = std::sin (th);
            L[i] = x[0] * cs - x[1] * sn;
            R[i] = x[0] * sn + x[1] * cs;
        }
    }

private:
    Phaser ph;
    std::array<DelayLine, 2> flange;
    std::array<SVF, 2> lp;
    std::array<float, 2> fb {};
    ModClock clk;
};

//==============================================================================
/** Controlled chaos. Each grid step derives a "scene" from a hash of its
    musical position (so playback is repeatable): filter position, delay time,
    feedback, ring-mod depth, drive and pan. Density blends from a calm base
    scene towards the random scene; every target glides (no jumps) and the
    signal path is soft-limited. */
class ChaosMode final : public StereoEffect
{
public:
    void prepareFx() override
    {
        for (auto& f : svf) { f.prepare (sr); f.setMode (SVF::Mode::lowpass); }
        for (auto& d : dl) d.prepare (sr, 1.5f);
        for (auto& r : ring) r.prepare (sr);
        for (auto& s : smooth) s.prepare (sr, 0.06f);
        clk.prepare (sr);
    }
    void reset() override
    {
        for (auto& f : svf) f.reset();
        for (auto& d : dl) d.reset();
        for (auto& r : ring) r.reset();
        for (auto& s : sat) s.reset();
        for (auto& s : smooth) s.reset (0.0f);
        smooth[0].reset (std::log (3000.0f));
        smooth[1].reset (0.25f * srf());
        clk.reset();
        lastStep = INT64_MIN;
        counter = 0;
        smoothDrive = 0.0f;
    }
    double getTailSeconds() const override { return 3.0; }

    void process (float* L, float* R, int n, const ProcessContext& ctx) override
    {
        const auto& p = ctx.params;
        const float a = p.amount;
        const float density = p.get (AdvParam::density) * 0.01f;
        const double beatSec = 60.0 / ctx.tempo.bpm;
        clk.begin (ctx, p.get (AdvParam::rate), ctx.divisionBeats());
        const float baseFb = p.fb01();

        for (int i = 0; i < n; ++i)
        {
            clk.next();
            const auto step = clk.cycleIndex();
            if (step != lastStep)
            {
                lastStep = step;
                const uint32_t h = uint32_t (step) * 0x9E3779B1u + 0xC4A05u;
                auto rnd = [h] (uint32_t salt) { return hashToUnit (h ^ (salt * 0x85EBCA6Bu)); };
                const float k = density * a;
                static constexpr double delayBeats[4] = { 0.25, 0.5, 0.75, 1.0 };
                const float cutoff = std::exp (lerp (std::log (3000.0f), std::log (300.0f + rnd (1) * 11000.0f), k));
                const float delay = float (delayBeats[int (rnd (2) * 3.99f)] * beatSec * sr);
                const float feedback = lerp (baseFb * 0.5f, baseFb * (0.3f + 0.7f * rnd (3)), k);
                const float ringDepth = rnd (4) > 0.6f ? 0.6f * k : 0.0f;
                const float drive = 18.0f * k * rnd (5);
                const float pan = (rnd (6) * 2.0f - 1.0f) * k * clamp (p.width, 0.0f, 1.5f) * 0.8f;
                const float ringHz = 60.0f + rnd (7) * 700.0f;
                const float shift = p.tone;
                smooth[0].setTarget (std::log (cutoff) + shift * 0.7f);
                smooth[1].setTarget (clamp (delay, 64.0f, dl[0].getMaxDelaySamples() - 64.0f));
                smooth[2].setTarget (clamp (feedback, 0.0f, 0.85f));
                smooth[3].setTarget (ringDepth);
                smooth[4].setTarget (drive);
                smooth[5].setTarget (pan);
                smooth[6].setTarget (ringHz);
            }
            const float cut = clamp (std::exp (smooth[0].next()), 60.0f, 0.45f * srf());
            const float dt = smooth[1].next();
            const float fbk = smooth[2].next();
            const float rd = smooth[3].next();
            smoothDrive = smooth[4].next();
            if ((counter++ & 31) == 0)
                for (auto& st : sat) st.set (Saturator::Type::soft, smoothDrive);
            const float pan = smooth[5].next();
            const float rhz = smooth[6].next();
            float gl, gr;
            panGains (pan, gl, gr);
            float* io[2] = { L + i, R + i };
            for (int c = 0; c < 2; ++c)
            {
                float x = *io[c];
                x = lerp (x, ring[size_t (c)].process (x, rhz * (c ? 1.01f : 1.0f), 0.2f), rd);
                svf[size_t (c)].setParams (cut, 1.5f + 2.0f * a);
                x = svf[size_t (c)].process (x);
                x = sat[size_t (c)].process (x);
                auto& d = dl[size_t (c)];
                const float e = d.readLinear (dt * (c ? 1.02f : 1.0f));
                d.push (x + fastTanh (e * fbk));
                *io[c] = softClip ((x + e * 0.7f) * (c ? gr : gl), 1.2f);
            }
        }
    }

private:
    std::array<SVF, 2> svf;
    std::array<DelayLine, 2> dl;
    std::array<RingModulator, 2> ring;
    std::array<Saturator, 2> sat;
    std::array<OnePoleSmoother, 7> smooth;
    ModClock clk;
    int64_t lastStep = INT64_MIN;
    float smoothDrive = 0.0f;
    uint32_t counter = 0;
};
} // namespace

void addExtremePerformance (std::vector<EffectInfo>& r)
{
    const auto cat = Category::extremePerformance;
    r.push_back ({ "reverse_suck_in", "Reverse Suck-In", cat,
        "Reversed reverb swells that rise and open into every downbeat - great for transitions.",
        { "Steeper, more dramatic rise", "Lower or higher swell start", "Swell length: shorter, faster swells or longer ones", "Width of the swell" },
        { { AdvParam::sync }, { AdvParam::division, "Swell Length" }, { AdvParam::delayTime, "Swell Time" }, { AdvParam::decay } },
        MixLaw::equalPower, 0.0f, &make<ReverseSuckIn> });

    r.push_back ({ "stutter_burst", "Stutter Burst", cat,
        "Beat-locked bursts of short gated repeats with optional ratcheting.",
        { "Burst probability", "Darker or brighter", "Above 50 %: repeats ratchet faster", "Random left/right burst placement" },
        { { AdvParam::division, "Repeat" }, { AdvParam::sync }, { AdvParam::delayTime, "Repeat Time" }, { AdvParam::gate },
          { AdvParam::density }, { AdvParam::rate, "Burst Rate" } },
        MixLaw::linear, 0.0f, &make<StutterBurst> });

    r.push_back ({ "pitch_dive", "Pitch Dive", cat,
        "Genuine pitch shifting that dives downward after each phrase onset or tempo step.",
        { "Dive depth", "Darker or brighter", "Dive speed", "Left/right depth difference" },
        { { AdvParam::pitch, "Dive Depth" }, { AdvParam::delayTime, "Dive Time" }, { AdvParam::sync }, { AdvParam::division } },
        MixLaw::linear, 0.0f, &make<PitchDive> });

    r.push_back ({ "vortex", "Vortex", cat,
        "Phasing, flanging, counter-swept filtering and a rotating stereo image.",
        { "Intensity of every moving part", "Filter position", "Rotation speed", "Rotation width" },
        { { AdvParam::rate }, { AdvParam::sync }, { AdvParam::division }, { AdvParam::feedback }, { AdvParam::cutoff } },
        MixLaw::linear, 0.0f, &make<Vortex> });

    r.push_back ({ "chaos_mode", "Chaos Mode", cat,
        "Repeatable, position-locked scene changes of filter, delay, ring mod, drive and pan.",
        { "How far scenes stray from calm", "Filter offset", "Scene change speed", "Pan range of the scenes" },
        { { AdvParam::division, "Scene Length" }, { AdvParam::sync }, { AdvParam::rate, "Scene Rate" },
          { AdvParam::density, "Chaos" }, { AdvParam::feedback } },
        MixLaw::linear, 0.0f, &make<ChaosMode> });
}
} // namespace msfx::library
