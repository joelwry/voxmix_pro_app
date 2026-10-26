// DSP tests on synthetic signals: every registered effect, EQ response, dynamics, pitch
// correction, chain ordering, bypass and latency.

#include "TestFramework.h"
#include "TestHelpers.h"
#include "voxfx/EffectChain.h"
#include "voxfx/EffectRegistry.h"
#include <algorithm>
#include <map>

using namespace voxfx;

namespace
{
constexpr double kSr = 48000.0;

std::vector<float> runEffect (Effect& fx, const std::vector<float>& inL, const std::vector<float>& inR, int block = 256, double bpm = 120)
{
    std::vector<float> L = inL, R = inR;
    ProcessInfo info;
    info.sampleRate = kSr;
    info.bpm = bpm;
    info.playing = true;
    for (size_t s = 0; s < L.size(); s += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, L.size() - s);
        info.beatPosition = double (s) / kSr * bpm / 60.0;
        fx.process (L.data() + s, R.data() + s, n, info);
    }
    std::vector<float> out (L.size() * 2);
    for (size_t i = 0; i < L.size(); ++i) { out[2 * i] = L[i]; out[2 * i + 1] = R[i]; }
    return out;
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x) || std::abs (x) > 100.0f) return false;
    return true;
}

double diffDb (const std::vector<float>& out, const std::vector<float>& in)
{
    // difference between the stereo output and the (dual-mono) input
    double d = 0, e = 0;
    const size_t frames = in.size();
    for (size_t i = (size_t) (0.25 * kSr); i < frames; ++i)
    {
        const double a = out[2 * i] - in[i], b = out[2 * i + 1] - in[i];
        d += a * a + b * b;
        e += 2.0 * in[i] * in[i];
    }
    return 10.0 * std::log10 ((d + 1e-20) / (e + 1e-20));
}
} // namespace

TEST (Registry_HasAllEffectFamilies)
{
    const auto& reg = EffectRegistry::instance();
    std::map<std::string, int> perCat;
    for (const auto& t : reg.types()) ++perCat[categoryId (t.descriptor->category)];
    CHECK (reg.find ("vocal_sweetener") != nullptr);
    CHECK (reg.find ("pitch_correction") != nullptr);
    CHECK (reg.find ("eq") && reg.find ("compressor") && reg.find ("deesser") && reg.find ("gate"));
    CHECK (reg.find ("saturation") && reg.find ("limiter") && reg.find ("reverb") && reg.find ("delay"));
    CHECK (reg.find ("chorus") && reg.find ("stereo_width") && reg.find ("mastering"));
    CHECK (reg.find ("ms_underwater") && reg.find ("ms_wah_vocal") && reg.find ("ms_choir_mist"));
    CHECK (perCat["creative"] == 40);
    // ids are unique, params have sane ranges and defaults inside them
    std::map<std::string, int> ids;
    for (const auto& t : reg.types())
    {
        CHECK_MSG (++ids[t.descriptor->typeId] == 1, t.descriptor->typeId);
        for (const auto& p : t.descriptor->params)
        {
            CHECK_MSG (p.min < p.max, t.descriptor->typeId + "." + p.id);
            CHECK_MSG (p.def >= p.min && p.def <= p.max, t.descriptor->typeId + "." + p.id);
            if (p.type == ParamType::Choice) CHECK_MSG ((int) p.choices.size() == (int) p.max + 1, t.descriptor->typeId + "." + p.id);
        }
    }
    CHECK (reg.create ("does_not_exist") == nullptr);
}

TEST (AllEffects_FiniteAndAudible)
{
    // Every effect must stay finite on silence, noise, a loud sine, an impulse and random
    // parameter extremes, and with default settings it must audibly change the signal.
    const int n = (int) (1.5 * kSr);
    const auto noiseIn = th::noise (n, 0.25f, 7);
    const auto loudSine = th::sine (220.0, kSr, n, 0.99f);
    std::vector<float> silence ((size_t) n, 0.0f), impulse ((size_t) n, 0.0f);
    impulse[1000] = 1.0f;
    std::mt19937 rng (42);

    for (const auto& t : EffectRegistry::instance().types())
    {
        const std::string id = t.descriptor->typeId;
        for (double sr : { 44100.0, 48000.0 })
        {
            auto fx = t.create();
            fx->prepare (sr, 256);
            CHECK_MSG (allFinite (runEffect (*fx, silence, silence)), id + " silence");
            CHECK_MSG (allFinite (runEffect (*fx, impulse, impulse, 97)), id + " impulse / odd block");
            CHECK_MSG (allFinite (runEffect (*fx, loudSine, loudSine, 1)), id + " loud sine / 1-sample blocks");
        }
        // random parameter combinations, including extremes
        for (int k = 0; k < 6; ++k)
        {
            auto fx = t.create();
            fx->prepare (kSr, 256);
            for (int i = 0; i < fx->numParams(); ++i)
            {
                const auto& p = fx->descriptor().params[(size_t) i];
                const int mode = (int) (rng() % 3);
                fx->setParam (i, mode == 0 ? p.min : mode == 1 ? p.max : p.min + (p.max - p.min) * float (rng() % 1000) / 1000.0f);
            }
            fx->setParam (0, NAN);   // invalid values are sanitised to the default
            CHECK_MSG (std::isfinite (fx->getParam (0)), id);
            CHECK_MSG (allFinite (runEffect (*fx, noiseIn, noiseIn)), id + " random params");
        }
        // audible with defaults (input chosen to trigger each effect)
        auto fx = t.create();
        fx->prepare (kSr, 256);
        std::vector<float> in = noiseIn;
        if (id == "limiter") { fx->setParam (fx->paramIndex ("input"), 12.0f); }
        if (id == "compressor") { fx->setParam (fx->paramIndex ("autoMakeup"), 0.0f); }
        if (id == "gate") { for (size_t i = 0; i < in.size(); ++i) in[i] *= (i / 24000) % 2 ? 1.0f : 0.001f; }
        if (id == "stereo_width")
        {
            // width needs a stereo input
            std::vector<float> l = in, r = th::noise (n, 0.25f, 8);
            auto out = runEffect (*fx, l, r);
            double d = 0;
            for (size_t i = 0; i < l.size(); ++i) d += std::abs (out[2 * i] - l[i]);
            CHECK_MSG (d > 10.0, id + " audible");
            continue;
        }
        const auto out = runEffect (*fx, in, in);
        const double dd = diffDb (out, in);
        CHECK_MSG (dd > -40.0, id + " has an audible effect (difference " + std::to_string (dd) + " dB)");
    }
}

TEST (Effect_ParamsAreClamped)
{
    auto fx = EffectRegistry::instance().create ("compressor");
    const int ratio = fx->paramIndex ("ratio");
    fx->setParam (ratio, 1000.0f);
    CHECK_NEAR (fx->getParam (ratio), 20.0f, 1e-6);
    fx->setParam (ratio, -5.0f);
    CHECK_NEAR (fx->getParam (ratio), 1.0f, 1e-6);
    auto pc = EffectRegistry::instance().create ("pitch_correction");
    const int key = pc->paramIndex ("key");
    pc->setParam (key, 4.6f);   // choices are rounded
    CHECK_NEAR (pc->getParam (key), 5.0f, 1e-6);
}

TEST (EQ_BellAndHighPassResponse)
{
    auto fx = EffectRegistry::instance().create ("eq");
    fx->prepare (kSr, 256);
    fx->setParam (fx->paramIndex ("hpfOn"), 0);
    fx->setParam (fx->paramIndex ("mid1Freq"), 1000);
    fx->setParam (fx->paramIndex ("mid1Gain"), 6);
    fx->reset();
    const int n = (int) kSr;
    auto in = th::sine (1000, kSr, n, 0.25f);
    auto out = runEffect (*fx, in, in);
    std::vector<float> l (n / 2);
    for (int i = 0; i < n / 2; ++i) l[(size_t) i] = out[2 * (size_t) (i + n / 2)];
    CHECK_NEAR (th::rmsDb (l) - th::rmsDb (std::vector<float> (in.begin() + n / 2, in.end())), 6.0, 0.3);

    auto hp = EffectRegistry::instance().create ("eq");
    hp->prepare (kSr, 256);
    hp->setParam (hp->paramIndex ("hpfFreq"), 200);
    hp->reset();
    auto low = th::sine (50, kSr, n, 0.25f);
    auto lo = runEffect (*hp, low, low);
    std::vector<float> ll (n / 2);
    for (int i = 0; i < n / 2; ++i) ll[(size_t) i] = lo[2 * (size_t) (i + n / 2)];
    // 12 dB/oct, 2 octaves below the corner: about -24 dB
    CHECK (th::rmsDb (ll) - th::rmsDb (std::vector<float> (low.begin() + n / 2, low.end())) < -20.0);
}

TEST (Compressor_ReducesLoudSignals)
{
    auto fx = EffectRegistry::instance().create ("compressor");
    fx->prepare (kSr, 256);
    fx->setParam (fx->paramIndex ("threshold"), -30);
    fx->setParam (fx->paramIndex ("ratio"), 4);
    fx->setParam (fx->paramIndex ("autoMakeup"), 0);
    fx->setParam (fx->paramIndex ("knee"), 0);
    fx->setParam (fx->paramIndex ("attack"), 0.1f);     // peak-hold behaviour so the static curve is measured
    fx->setParam (fx->paramIndex ("release"), 1000);
    const int n = (int) kSr;
    auto in = th::sine (500, kSr, n, 0.5f);   // peak -6 dBFS: 24 dB over -> 18 dB reduction
    auto out = runEffect (*fx, in, in);
    std::vector<float> l (n / 2);
    for (int i = 0; i < n / 2; ++i) l[(size_t) i] = out[2 * (size_t) (i + n / 2)];
    const double change = th::rmsDb (l) - th::rmsDb (std::vector<float> (in.begin() + n / 2, in.end()));
    CHECK_NEAR (change, -18.0, 1.0);
    CHECK_NEAR (fx->gainReductionDb(), 18.0, 1.0);
}

TEST (Limiter_HoldsCeiling)
{
    auto fx = EffectRegistry::instance().create ("limiter");
    fx->prepare (kSr, 256);
    fx->setParam (fx->paramIndex ("input"), 18);
    fx->setParam (fx->paramIndex ("ceiling"), -1);
    auto in = th::noise ((int) kSr, 0.5f, 3);
    auto out = runEffect (*fx, in, in);
    CHECK (th::peak (out) <= std::pow (10.0f, -1.0f / 20.0f) + 1e-4f);
    CHECK (fx->latencySamples() == 48);
}

TEST (PitchCorrection_PullsToNearestNote)
{
    // 452 Hz is ~47 cents above A4 (440): chromatic correction must bring it to 440 Hz,
    // while C major (no A#) keeps the nearest scale note A.
    for (int scale : { 0, 1 })
    {
        auto fx = EffectRegistry::instance().create ("pitch_correction");
        fx->prepare (kSr, 256);
        fx->setParam (fx->paramIndex ("scale"), (float) scale);
        fx->setParam (fx->paramIndex ("speed"), 0);
        const int n = (int) (1.5 * kSr);
        auto in = th::sine (452.0, kSr, n, 0.4f);
        auto out = runEffect (*fx, in, in);
        std::vector<float> l (out.size() / 2);
        for (size_t i = 0; i < l.size(); ++i) l[i] = out[2 * i];
        const double f = th::estimatePitch (l, (int) (0.8 * kSr), 4096, kSr);
        CHECK_NEAR (f, 440.0, 3.0);
        const double fin = th::estimatePitch (in, (int) (0.8 * kSr), 4096, kSr);
        CHECK_NEAR (fin, 452.0, 1.0);
    }
    // strength 0 leaves the pitch alone
    auto fx = EffectRegistry::instance().create ("pitch_correction");
    fx->prepare (kSr, 256);
    fx->setParam (fx->paramIndex ("strength"), 0);
    auto in = th::sine (452.0, kSr, (int) kSr, 0.4f);
    auto out = runEffect (*fx, in, in);
    std::vector<float> l (out.size() / 2);
    for (size_t i = 0; i < l.size(); ++i) l[i] = out[2 * i];
    CHECK_NEAR (th::estimatePitch (l, (int) (0.5 * kSr), 4096, kSr), 452.0, 2.0);
}

TEST (Chain_OrderMatters_AndBypassIsClean)
{
    auto make = [] (const char* type) { auto fx = EffectRegistry::instance().create (type); fx->prepare (kSr, 256); return fx; };
    auto sat = make ("saturation");
    sat->setParam (sat->paramIndex ("drive"), 100);
    auto eq = make ("eq");
    eq->setParam (eq->paramIndex ("highFreq"), 2000);
    eq->setParam (eq->paramIndex ("highGain"), -15);
    auto sat2 = make ("saturation");
    sat2->setParam (sat2->paramIndex ("drive"), 100);
    auto eq2 = make ("eq");
    eq2->setParam (eq2->paramIndex ("highFreq"), 2000);
    eq2->setParam (eq2->paramIndex ("highGain"), -15);

    EffectChain a ({ std::make_shared<EffectSlot> ("s", sat), std::make_shared<EffectSlot> ("e", eq) }, kSr, 256);
    EffectChain b ({ std::make_shared<EffectSlot> ("e", eq2), std::make_shared<EffectSlot> ("s", sat2) }, kSr, 256);
    const int n = 256 * 187;
    auto in = th::sine (300, kSr, n, 0.9f);
    std::vector<float> aL = in, aR = in, bL = in, bR = in;
    ProcessInfo info;
    for (int s = 0; s < n; s += 256)
    {
        a.process (aL.data() + s, aR.data() + s, 256, info);
        b.process (bL.data() + s, bR.data() + s, 256, info);
    }
    double d = 0;
    for (int i = n / 2; i < n; ++i) d += std::abs (aL[(size_t) i] - bL[(size_t) i]);
    CHECK_MSG (d / (n / 2) > 1e-3, "saturation->EQ differs from EQ->saturation");

    // a bypassed slot passes audio delayed by exactly its latency (constant chain latency)
    auto lim = make ("limiter");
    auto slot = std::make_shared<EffectSlot> ("l", lim);
    slot->setBypassed (true);
    EffectChain c ({ slot }, kSr, 256);
    std::vector<float> L ((size_t) 1024, 0.0f), R ((size_t) 1024, 0.0f);
    L[100] = 0.5f;
    c.process (L.data(), R.data(), 256, info);
    c.process (L.data() + 256, R.data() + 256, 256, info);
    CHECK (c.latencySamples() == 48);
    CHECK_NEAR (L[148], 0.5f, 1e-6);
    CHECK_NEAR (L[100], 0.0f, 1e-6);
}

TEST (VocalSweetener_MatchesPluginPresets)
{
    std::vector<std::pair<std::string, float>> values;
    CHECK (vocalSweetPresetValues ("Afrobeat Lead", values));
    CHECK (values.size() == 40);
    CHECK (! vocalSweetPresetValues ("Nope", values));
    auto fx = EffectRegistry::instance().create ("vocal_sweetener");
    CHECK (fx->numParams() == 40);
    fx->prepare (kSr, 256);
    CHECK (fx->latencySamples() == 48);   // VocalSweet's 1 ms look-ahead limiter
}
